"""Exercise the flashed UI through USB software inputs, not physical touch.

Start immediately after flashing/rebooting to the home page. No motor commands.
"""
import argparse
import json
from pathlib import Path
import time

import serial
from serial.tools import list_ports


def run(port, log):
    device = next((p for p in list_ports.comports() if p.device.upper() == port.upper()), None)
    if device is None or (device.vid, device.pid) != (0x303A, 0x1001):
        raise RuntimeError('Select the Espressif USB Serial/JTAG port')
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    connection.port = port
    connection.dtr = connection.rts = False
    raw, partial, evidence = bytearray(), bytearray(), []

    def send(command, delay=0):
        connection.write((command + '\n').encode())
        if delay:
            time.sleep(delay)

    def state(label, expect=None):
        send('STATUS')
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            chunk = connection.read(connection.in_waiting or 1)
            raw.extend(chunk)
            partial.extend(chunk)
            while b'\n' in partial:
                line, _, rest = partial.partition(b'\n')
                partial[:] = rest
                if b'GL30_STATUS ' not in line:
                    continue
                result = json.loads(line.split(b'GL30_STATUS ', 1)[1])
                entry = {'check': label, 'state': result}
                evidence.append(entry)
                assert result['frame_errors'] == 0 and result['touch_errors'] == 0, entry
                assert result['ui']['display_error'] == 0, entry
                if expect:
                    for key, value in expect.items():
                        assert result['ui'][key] == value, (label, key, result['ui'][key], value)
                print(json.dumps({'check': label, 'page': result['ui']['page'],
                                  'app': result['ui']['app'], 'loop_us': result['loop_us'],
                                  'frame_us': result['last_frame_us']}) , flush=True)
                return result['ui']
        raise RuntimeError('STATUS timed out: ' + label)

    def click():
        send('BUTTON 1', 0.10)
        send('BUTTON 0', 0.45)

    def back():
        send('BUTTON 1', 0.08)
        send('BUTTON 0', 0.08)
        send('BUTTON 1', 0.08)
        send('BUTTON 0', 0.45)

    def rotate(steps):
        send(f'ROTATE {steps}', 0.45)

    connection.open()
    try:
        state('home', {'page': 0, 'off': False})
        click()
        state('single opens menu', {'page': 1, 'menu': 0})
        rotate(9)
        state('menu wraps', {'page': 1, 'menu': 0})
        rotate(1)
        click()
        state('volume open', {'page': 2, 'app': 1, 'volume': 42})
        rotate(-9)
        state('volume 33', {'volume': 33})
        rotate(-34)
        state('volume zero clamp', {'volume': 0})
        click()
        state('volume mute', {'muted': True})
        back()
        state('double returns', {'page': 1})
        rotate(-1)
        click()
        rotate(2)
        state('timer two minutes', {'app': 0, 'remaining_ms': 120000})
        click()
        before = state('timer running', {'phase': 1})
        time.sleep(1.1)
        after = state('timer advances', {'phase': 1})
        assert after['remaining_ms'] < before['remaining_ms']
        rotate(-1)
        adjusted = state('timer adjusts while running', {'phase': 1})
        assert 50000 < adjusted['remaining_ms'] < 60000
        back()
        rotate(2)
        click()
        state('stopwatch open', {'app': 2})
        click()
        time.sleep(1.05)
        click()
        stopwatch = state('stopwatch pause', {'stopwatch_running': False})
        assert stopwatch['stopwatch_ms'] > 1000
        back()
        rotate(6)
        click()
        state('lighting open', {'app': 8, 'light_field': 0})
        click()
        rotate(1)
        click()
        state('lighting effect saved', {'light_effect': 1, 'light_editing': False})
        rotate(1)
        click()
        rotate(2)
        click()
        state('lighting color saved', {'light_color': 2, 'light_editing': False})
        back()
        state('lighting back', {'page': 1})
        back()
        state('home restored', {'page': 0})
        send('ROTATE 999999999999999999999999')
        send('ROTATE 361')
        send('ROTATE 1junk')
        send('BUTTON 2')
        send('TIME 0', 0.3)
        state('invalid commands rejected', {'page': 0})
        print(f'PASS: {len(evidence)} USB software-input checks; physical touch not tested', flush=True)
    finally:
        connection.close()
        log.write_bytes(raw)
        log.with_suffix('.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    run(args.port, args.log)
