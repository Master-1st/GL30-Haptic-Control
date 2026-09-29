"""Query the GL30 USB console without resetting the ESP32.

Run with the ESP-IDF Python environment (pyserial is already installed).
"""
import argparse
import json
from pathlib import Path
import time

import serial
from serial.tools import list_ports


def query(port, samples, interval, sync_time, log_path):
    device = next((p for p in list_ports.comports() if p.device.upper() == port.upper()), None)
    if device is None or (device.vid, device.pid) != (0x303A, 0x1001):
        raise RuntimeError(f'{port} is not the connected Espressif USB Serial/JTAG port')
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    connection.port = port
    connection.dtr = False
    connection.rts = False
    raw = bytearray()
    partial = bytearray()
    results = []
    connection.open()
    try:
        for index in range(samples):
            started = time.monotonic()
            connection.write(b'STATUS\n')
            before = len(results)
            while time.monotonic() - started < 8 and len(results) == before:
                chunk = connection.read(connection.in_waiting or 1)
                raw.extend(chunk)
                partial.extend(chunk)
                while b'\n' in partial:
                    line, _, remaining = partial.partition(b'\n')
                    partial[:] = remaining
                    if b'GL30_STATUS ' in line:
                        state = json.loads(line.split(b'GL30_STATUS ', 1)[1].strip())
                        results.append(state)
                        print(json.dumps(state, ensure_ascii=True), flush=True)
            if len(results) == before:
                raise RuntimeError('No STATUS response within 8 seconds; check firmware/log')
            if sync_time and index == 0:
                # STATUS proves that the app has booted and installed its RX
                # driver. A TIME sent immediately after flash can be lost.
                epoch = int(time.time())
                connection.write(f'TIME {epoch}\n'.encode())
                expected = f'clock set to {epoch}'.encode()
                acknowledgement = bytearray()
                deadline = time.monotonic() + 8
                while expected not in acknowledgement and time.monotonic() < deadline:
                    chunk = connection.read(connection.in_waiting or 1)
                    raw.extend(chunk)
                    acknowledgement.extend(chunk)
                if expected not in acknowledgement:
                    raise RuntimeError('Clock synchronization was not acknowledged')
            if index + 1 < samples:
                time.sleep(max(0, interval - (time.monotonic() - started)))
    finally:
        connection.close()
        if log_path:
            log_path.write_bytes(raw)
            log_path.with_suffix('.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--samples', type=int, default=1)
    parser.add_argument('--interval', type=float, default=2)
    parser.add_argument('--sync-time', action='store_true')
    parser.add_argument('--log', type=Path)
    args = parser.parse_args()
    if not 1 <= args.samples <= 300 or args.interval < 0:
        parser.error('samples must be 1..300 and interval must be nonnegative')
    query(args.port, args.samples, args.interval, args.sync_time, args.log)
