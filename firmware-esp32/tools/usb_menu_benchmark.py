"""Measure continuous menu rendering using USB UI inputs; never arms a motor.

Begin on the home page. The test rotates in both directions, checks all input
arrives, returns to home, and saves raw STATUS evidence plus timing summaries.
"""
import argparse
import json
from pathlib import Path
import time

import serial
from serial.tools import list_ports


def run(port, seconds, log):
    device = next((p for p in list_ports.comports() if p.device.upper() == port.upper()), None)
    if device is None or (device.vid, device.pid) != (0x303A, 0x1001):
        raise RuntimeError('Select the Espressif USB Serial/JTAG port')
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.02)
    connection.port = port
    connection.dtr = connection.rts = False
    raw, partial, states = bytearray(), bytearray(), []

    def send(command, delay=0):
        connection.write((command + '\n').encode())
        if delay:
            time.sleep(delay)

    def read():
        chunk = connection.read(connection.in_waiting or 1)
        raw.extend(chunk); partial.extend(chunk)
        result = None
        while b'\n' in partial:
            line, _, rest = partial.partition(b'\n'); partial[:] = rest
            if b'GL30_STATUS ' not in line:
                continue
            result = json.loads(line.split(b'GL30_STATUS ', 1)[1]); states.append(result)
            assert result['frame_errors'] == result['touch_errors'] == result['motor_tx'] == 0, result
            assert not result['motor_armed'] and result['ui']['display_error'] == 0, result
            assert result['input_queue_drops'] == 0, result
        return result

    def status():
        send('STATUS')
        until = time.monotonic() + 8
        while time.monotonic() < until:
            value = read()
            if value is not None:
                return value
        raise RuntimeError('STATUS timeout')

    connection.open()
    summary = None
    try:
        before = status(); assert before['ui']['page'] == 0, 'Start from home'
        send('BUTTON 1', 0.1); send('BUTTON 0', 0.5)
        menu = status(); assert menu['ui']['page'] == 1
        started = time.monotonic(); next_rotate = next_status = started; total = 0
        benchmark_states = len(states)
        while time.monotonic() - started < seconds:
            now = time.monotonic()
            if now >= next_rotate:
                steps = 1 if now - started < seconds / 2 else -1
                send(f'ROTATE {steps}'); total += steps; next_rotate += 0.08
            if now >= next_status:
                send('STATUS'); next_status += 0.25
            read()
        # Drain replies before the final assertion; the last animation settles.
        deadline = time.monotonic() + 0.8
        while time.monotonic() < deadline:
            read()
        final = status()
        assert final['ui']['page'] == 1
        assert final['ui']['menu'] == (menu['ui']['menu'] + total) % 9
        samples = [s for s in states[benchmark_states:]
                   if s['uptime_ms'] >= menu['uptime_ms'] + 3000]
        assert len(samples) >= 10
        first, last = samples[0], samples[-1]
        elapsed = (last['uptime_ms'] - first['uptime_ms']) / 1000
        updates = sorted(s['ui_update_us'] for s in samples)
        summary = {
            'input': 'USB ROTATE +/-1 at 12.5 Hz; motor never armed',
            'status_samples': len(samples), 'duration_seconds': elapsed,
            'raster_fps': (last['ui_frames'] - first['ui_frames']) / elapsed,
            'panel_fps': (last['frames_sent'] - first['frames_sent']) / elapsed,
            'sampled_update_p50_us': updates[len(updates) // 2],
            'sampled_update_p95_us': updates[(len(updates) * 95 + 99) // 100 - 1],
            'firmware_last_128_update_p95_us': last['ui_p95_us'],
            'last_draw_us': last['draw_us'], 'last_transfer_us': last['last_frame_us'],
            'internal_free': last['internal_free'], 'stack_free': last['stack_free'],
            'motor_poll_max_us': last['motor_poll_max_us'],
            'motor_tx': last['motor_tx'], 'input_queue_drops': last['input_queue_drops'],
            'frame_errors': last['frame_errors'], 'touch_errors': last['touch_errors'],
        }
        send('BUTTON 1', 0.08); send('BUTTON 0', 0.08)
        send('BUTTON 1', 0.08); send('BUTTON 0', 0.5)
        assert status()['ui']['page'] == 0
        print(json.dumps(summary, indent=2), flush=True)
    finally:
        connection.close()
        log.write_bytes(raw)
        log.with_suffix('.json').write_text(json.dumps({'summary': summary, 'states': states}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=float, default=18)
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 10:
        parser.error('Use at least 10 seconds for the steady-state sample')
    run(args.port, args.seconds, args.log)
