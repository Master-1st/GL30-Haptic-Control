import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import usb_clock


class FakeSerial:
    def __init__(self, valid=False, epoch=0, acknowledge=True, apply=True):
        self.ui = {'clock_valid': valid, 'clock_unix': epoch}
        self.acknowledge = acknowledge
        self.apply = apply
        self.buffer = bytearray()
        self.commands = []
        self.closed = False

    def open(self):
        self.reset_lines = (self.dtr, self.rts)

    def close(self):
        self.closed = True

    def reset_input_buffer(self):
        self.buffer.clear()

    @property
    def in_waiting(self):
        return len(self.buffer)

    def read(self, size):
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def write(self, data):
        self.commands.append(data)
        if data == b'STATUS\n':
            state = {'uptime_ms': 1000, 'ui': self.ui}
            self.buffer.extend(('GL30_STATUS ' + json.dumps(state) + '\n').encode())
        elif data.startswith(b'TIME '):
            epoch = int(data.split()[1])
            if self.apply:
                self.ui = {'clock_valid': True, 'clock_unix': epoch}
            if self.acknowledge:
                self.buffer.extend(f'I gl30_app: clock set to {epoch}\n'.encode())


class UsbClockTests(unittest.TestCase):
    def setUp(self):
        self.monotonic = 0
        self.time_patch = patch.object(usb_clock.time, 'time', return_value=1800000000)
        self.monotonic_patch = patch.object(usb_clock.time, 'monotonic', side_effect=self.next_time)
        self.time_patch.start()
        self.monotonic_patch.start()
        self.addCleanup(self.time_patch.stop)
        self.addCleanup(self.monotonic_patch.stop)

    def next_time(self):
        self.monotonic += 0.1
        return self.monotonic

    def run_sync(self, connection, force=False):
        with patch.object(usb_clock.serial, 'Serial', return_value=connection):
            return usb_clock.synchronize('COM11', force)

    def test_boot_clock_sync_has_ack_and_readback_without_reset(self):
        connection = FakeSerial()
        result = self.run_sync(connection)
        self.assertTrue(result['synced'])
        self.assertEqual(result['error_seconds'], 0)
        self.assertEqual(connection.commands, [b'STATUS\n', b'TIME 1800000000\n', b'STATUS\n'])
        self.assertEqual(connection.reset_lines, (False, False))
        self.assertTrue(connection.closed)

    def test_close_clock_is_only_queried(self):
        connection = FakeSerial(True, 1799999999)
        self.assertFalse(self.run_sync(connection)['synced'])
        self.assertEqual(connection.commands, [b'STATUS\n'])
        self.assertTrue(connection.closed)

    def test_periodic_sync_even_without_drift(self):
        self.assertTrue(self.run_sync(FakeSerial(True, 1800000000), force=True)['synced'])

    def test_drift_is_corrected(self):
        self.assertTrue(self.run_sync(FakeSerial(True, 1799999980))['synced'])

    def test_missing_ack_does_not_claim_success(self):
        connection = FakeSerial(acknowledge=False)
        with self.assertRaisesRegex(RuntimeError, 'acknowledge'):
            self.run_sync(connection)
        self.assertTrue(connection.closed)

    def test_ack_without_clock_change_does_not_claim_success(self):
        connection = FakeSerial(apply=False)
        with self.assertRaisesRegex(RuntimeError, 'readback'):
            self.run_sync(connection)
        self.assertTrue(connection.closed)

    def test_invalid_pc_clock_never_sent(self):
        connection = FakeSerial()
        with patch.object(usb_clock.time, 'time', return_value=0):
            with self.assertRaisesRegex(RuntimeError, 'PC clock'):
                self.run_sync(connection)
        self.assertEqual(connection.commands, [b'STATUS\n'])
        self.assertTrue(connection.closed)

    def test_unknown_firmware_gets_no_time_command(self):
        connection = FakeSerial()
        connection.ui = {'page': 0}
        with self.assertRaisesRegex(RuntimeError, 'firmware'):
            self.run_sync(connection)
        self.assertEqual(connection.commands, [b'STATUS\n'])
        self.assertTrue(connection.closed)

    def test_port_number_can_change_but_board_identity_must_match(self):
        ports = [SimpleNamespace(device='COM4', vid=0x0483, pid=0x374e, serial_number='OTHER'),
                 SimpleNamespace(device='COM11', vid=0x303a, pid=0x1001, serial_number='OTHER'),
                 SimpleNamespace(device='COM15', vid=0x303a, pid=0x1001, serial_number='ac:27:6e:d2:f6:5c')]
        with patch.object(usb_clock.list_ports, 'comports', return_value=ports):
            self.assertEqual(usb_clock.find_device('AC:27:6E:D2:F6:5C'), 'COM15')
            self.assertIsNone(usb_clock.find_device('MISSING'))


if __name__ == '__main__':
    unittest.main()
