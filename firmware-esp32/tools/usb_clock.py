"""Automatically synchronize one GL30 board with this Windows PC over USB.

No DTR/RTS reset, no motor commands. Each transaction releases the serial port.
The Startup shortcut runs `watch`; `stop` releases it for firmware debugging.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import time

import serial
from serial.tools import list_ports


MIN_UNIX = 946684800
MAX_UNIX = 4102444800
MUTEX_NAME = r'Local\GL30UsbClock'
EVENT_NAME = r'Local\GL30UsbClockStop'
POLL_SECONDS = 5
RESYNC_SECONDS = 300


def find_device(device_serial):
    return next((p.device for p in list_ports.comports()
                 if (p.vid, p.pid) == (0x303A, 0x1001)
                 and (p.serial_number or '').upper() == device_serial.upper()), None)


def read_status(connection):
    connection.write(b'STATUS\n')
    deadline = time.monotonic() + 3
    partial = bytearray()
    while time.monotonic() < deadline:
        partial.extend(connection.read(connection.in_waiting or 1))
        while b'\n' in partial:
            line, _, remaining = partial.partition(b'\n')
            partial[:] = remaining
            if b'GL30_STATUS ' not in line:
                continue
            state = json.loads(line.split(b'GL30_STATUS ', 1)[1].strip())
            ui = state.get('ui', {})
            if type(ui.get('clock_valid')) is not bool or type(ui.get('clock_unix')) is not int:
                raise RuntimeError('GL30 firmware has no clock status; update firmware first')
            return state
    raise RuntimeError('GL30 STATUS timeout (booting, disconnected, or other firmware)')


def synchronize(port, force=False):
    """Return verified clock state; never reset or change application state."""
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
    connection.port = port
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        connection.reset_input_buffer()
        before = read_status(connection)
        ui = before['ui']
        epoch = int(time.time())
        if not MIN_UNIX <= epoch <= MAX_UNIX:
            raise RuntimeError('PC clock is outside the supported 2000..2100 range')
        needed = force or not ui['clock_valid'] or abs(ui['clock_unix'] - epoch) > 2
        if needed:
            connection.write(f'TIME {epoch}\n'.encode('ascii'))
            expected = f'clock set to {epoch}'.encode('ascii')
            acknowledgement = bytearray()
            deadline = time.monotonic() + 3
            while expected not in acknowledgement and time.monotonic() < deadline:
                acknowledgement.extend(connection.read(connection.in_waiting or 1))
            if expected not in acknowledgement:
                raise RuntimeError('GL30 did not acknowledge clock synchronization')
            after = read_status(connection)
            ui = after['ui']
        else:
            after = before
        error_seconds = ui['clock_unix'] - int(time.time())
        if not ui['clock_valid'] or abs(error_seconds) > 2:
            raise RuntimeError('GL30 clock readback does not match PC time')
        return {'port': port, 'synced': needed, 'clock_unix': ui['clock_unix'],
                'error_seconds': error_seconds, 'uptime_ms': after['uptime_ms']}
    finally:
        connection.close()


def windows_api():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    api.CreateMutexW.restype = wintypes.HANDLE
    api.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
    api.CreateEventW.restype = wintypes.HANDLE
    api.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
    api.OpenEventW.restype = wintypes.HANDLE
    api.SetEvent.argtypes = [wintypes.HANDLE]
    api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    api.WaitForSingleObject.restype = wintypes.DWORD
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    return api


def watch(device_serial):
    api = windows_api()
    mutex = api.CreateMutexW(None, False, MUTEX_NAME)
    if not mutex:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
        api.CloseHandle(mutex)
        logging.info('already_running')
        return
    event = api.CreateEventW(None, True, False, EVENT_NAME)
    if not event:
        api.CloseHandle(mutex)
        raise ctypes.WinError(ctypes.get_last_error())
    last_sync = 0
    last_state = None
    logging.info('started serial=%s poll_seconds=%s resync_seconds=%s',
                 device_serial, POLL_SECONDS, RESYNC_SECONDS)
    try:
        while api.WaitForSingleObject(event, 0) != 0:
            try:
                port = find_device(device_serial)
                state = 'disconnected'
                if port:
                    result = synchronize(port, time.monotonic() - last_sync >= RESYNC_SECONDS)
                    state = 'connected ' + port
                    if result['synced']:
                        last_sync = time.monotonic()
                        logging.info('sync %s', json.dumps(result))
                else:
                    last_sync = 0
            except (serial.SerialException, OSError, RuntimeError, ValueError) as error:
                # Flashing/debuggers may own the port. Retry without disturbing them.
                state = 'waiting ' + str(error)
            if state != last_state:
                logging.info('%s', state)
                last_state = state
            api.WaitForSingleObject(event, POLL_SECONDS * 1000)
    finally:
        api.CloseHandle(event)
        api.CloseHandle(mutex)
        logging.info('stopped')


def stop():
    api = windows_api()
    event = api.OpenEventW(0x0002, False, EVENT_NAME)  # EVENT_MODIFY_STATE
    if not event:
        print('GL30 USB clock is not running')
        return
    try:
        if not api.SetEvent(event):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        api.CloseHandle(event)
    print('GL30 USB clock stop requested')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('watch', 'once', 'stop'))
    parser.add_argument('--device-serial')
    parser.add_argument('--log', type=Path)
    args = parser.parse_args()
    if args.action == 'stop':
        stop()
        return
    if not args.device_serial:
        parser.error('--device-serial is required to avoid writing to other boards')
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(args.log, maxBytes=1_000_000, backupCount=1, encoding='utf-8')
        logging.basicConfig(level=logging.INFO, handlers=[handler],
                            format='%(asctime)s %(levelname)s %(message)s')
    else:
        logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    if args.action == 'once':
        port = find_device(args.device_serial)
        if not port:
            raise RuntimeError('The selected GL30 USB device is not connected')
        print(json.dumps(synchronize(port, force=True)))
    else:
        watch(args.device_serial)


if __name__ == '__main__':
    main()
