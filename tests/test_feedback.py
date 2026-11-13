"""Offline feedback configuration tests; no CAN hardware is opened."""
from collections import deque
from contextlib import nullcontext, redirect_stderr, redirect_stdout
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'examples/python'))
import feedback


class Clock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now


class FakeBus:
    def __init__(self, clock, node_id=7, ack=True, respond=True, abort=False, readback=None):
        self.clock, self.ack, self.respond, self.abort = clock, ack, respond, abort
        self.override = readback
        self.ids = feedback.ota.CanIds(node_id)
        self.feedback_id, self.config_id = 0x480 + node_id, 0x500 + node_id
        self.registers = {index: 200 for index in feedback.REGISTERS}
        self.sent, self.queue = [], deque()
        self.trace, self.close = MagicMock(), MagicMock()

    def send(self, ident, data):
        self.sent.append((ident, data))
        if ident == self.config_id:
            return
        if ident != self.ids.sdo_tx:
            raise AssertionError(f'Unexpected CAN ID {ident:x}')
        index, sub = struct.unpack_from('<HB', data, 1)
        if index == 0x1010:
            return
        if sub != 0x14 or index not in self.registers:
            raise AssertionError('Unexpected register access')
        if self.abort:
            self.queue.append((self.ids.sdo_rx, b'\x80' + data[1:4] + struct.pack('<I', 0x06090030)))
        elif data[0] == 0x2b:
            self.registers[index] = int.from_bytes(data[4:6], 'little')
            if self.ack:
                self.queue.append((self.ids.sdo_rx, b'\x60' + data[1:4] + bytes(4)))
        elif self.respond:
            value = self.registers[index] if self.override is None else self.override
            self.queue.append((self.ids.sdo_rx, b'\x4b' + data[1:4] + struct.pack('<I', value)))

    def receive(self, timeout):
        self.clock.now += .001 if self.queue else max(0, timeout)
        return self.queue.popleft() if self.queue else None


class FeedbackTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.timer = patch.object(feedback.time, 'monotonic', self.clock.monotonic)
        self.timer.start()
        self.addCleanup(self.timer.stop)

    def test_read_ignores_feedback_and_stale_other_register(self):
        bus = FakeBus(self.clock)
        bus.queue.extend(((bus.feedback_id, bytes.fromhex('5008') + bytes(62)),
                          (bus.ids.sdo_rx, bytes.fromhex('4b5d2014c8000000'))))
        self.assertEqual(feedback.request(bus, 0x201d, 0x14, .5, 3), 200)
        self.assertEqual(bus.sent, [(0x607, bytes.fromhex('401d201400000000'))])

    def test_timeout_is_bounded(self):
        bus = FakeBus(self.clock, respond=False)
        with redirect_stderr(io.StringIO()), self.assertRaises(feedback.ota.ResponseTimeout):
            feedback.request(bus, 0x201d, 0x14, .5, 2)
        self.assertEqual(len(bus.sent), 2)

    def test_abort_is_not_retried(self):
        bus = FakeBus(self.clock, abort=True)
        with self.assertRaisesRegex(feedback.ota.Failure, '06090030'):
            feedback.write_register(bus, 1, 50, .5, 3)
        self.assertEqual(len(bus.sent), 1)

    def test_write_without_ack_requires_matching_readback(self):
        bus = FakeBus(self.clock, ack=False)
        with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
            self.assertEqual(feedback.write_register(bus, 6, 50, .5, 2), 50)
        self.assertEqual([data[0] for _, data in bus.sent], [0x2b, 0x2b, 0x40])
        self.assertEqual(bus.sent[0], (0x607, bytes.fromhex('2b5d211432000000')))

    def test_write_readback_mismatch_fails(self):
        bus = FakeBus(self.clock, readback=200)
        with self.assertRaisesRegex(feedback.ota.Failure, 'readback mismatch'):
            feedback.write_register(bus, 1, 50, .5, 3)

    def test_divider_only_sends_feedback_configuration(self):
        bus = FakeBus(self.clock, node_id=127)
        with patch.object(feedback.time, 'sleep'), redirect_stdout(io.StringIO()):
            feedback.set_divider(bus, 20)
        self.assertEqual(bus.sent, [(0x57f, bytes.fromhex('000450145a14'))] * 3)

    def test_save_without_ack_sends_vendor_sequence_once_with_gap(self):
        bus = FakeBus(self.clock)
        stamps = []
        send = bus.send

        def record(ident, data):
            stamps.append(self.clock.now)
            send(ident, data)

        with patch.object(bus, 'send', side_effect=record), redirect_stdout(io.StringIO()):
            feedback.save_eeprom(bus, .5)
        self.assertEqual(bus.sent, [(0x607, bytes.fromhex('0010100000000000')),
                                    (0x607, bytes.fromhex('2f10100173617665'))])
        self.assertGreaterEqual(stamps[1] - stamps[0], .005)
        self.assertAlmostEqual(self.clock.now - stamps[1], 1)
        bus.trace.assert_any_call('save_sequence_sent', persistence_verified=False)

    def test_save_abort_stops_sequence_without_retry(self):
        for abort_command, expected_count in ((0, 1), (0x2f, 2)):
            with self.subTest(abort_command=abort_command):
                bus = FakeBus(self.clock)
                send = bus.send

                def abort(ident, data):
                    send(ident, data)
                    if data[0] == abort_command:
                        bus.queue.append((bus.ids.sdo_rx, b'\x80' + data[1:4] +
                                          struct.pack('<I', 0x08000020)))

                with patch.object(bus, 'send', side_effect=abort), redirect_stdout(io.StringIO()), \
                        self.assertRaisesRegex(feedback.ota.Failure, 'EEPROM save aborted'):
                    feedback.save_eeprom(bus, .5)
                self.assertEqual(len(bus.sent), expected_count)

    def test_save_send_failure_is_not_retried(self):
        bus = FakeBus(self.clock)
        with patch.object(bus, 'send', side_effect=OSError('send failed')) as send, \
                redirect_stdout(io.StringIO()), self.assertRaises(OSError):
            feedback.save_eeprom(bus, .5)
        send.assert_called_once()

    def test_socket_filters_feedback_sdo_and_other_configuration_senders(self):
        sock = MagicMock()
        with patch.object(feedback.socket, 'socket', return_value=sock), \
                patch.object(feedback.socket, 'if_nametoindex', return_value=2):
            bus = feedback.FeedbackBus('can0', 7, MagicMock())
        filters = [call.args[2] for call in sock.setsockopt.call_args_list
                   if call.args[:2] == (feedback.socket.SOL_CAN_RAW, feedback.socket.CAN_RAW_FILTER)]
        mask = 0x7ff | feedback.ota.EFF | feedback.ota.RTR
        self.assertEqual(filters[-1], struct.pack('=II', 0x487, mask) + bus.ids.filters +
                         struct.pack('=II', 0x507, mask))
        bus.close()

    def test_measurement_is_passive_and_reports_missing_group(self):
        bus = FakeBus(self.clock)
        bus.queue.extend([(bus.feedback_id, bytes.fromhex('5008') + bytes(62))] * 3)
        result = feedback.observe(bus, .1)
        self.assertEqual([row['frames'] for row in result['groups']], [3, 0])
        self.assertAlmostEqual(result['groups'][0]['p50_ms'], 1)
        self.assertEqual(bus.sent, [])

    def test_invalid_feedback_is_not_counted_as_valid(self):
        bus = FakeBus(self.clock)
        bus.queue.append((bus.feedback_id, bytes.fromhex('5a1f00')))
        with self.assertRaisesRegex(feedback.ota.Failure, 'Malformed feedback'):
            feedback.observe(bus, .1)

    def test_exact_register_units_and_cli_validation(self):
        self.assertEqual(feedback.register_ms('20'), 200)
        self.assertEqual(feedback.register_ms('0.3'), 3)
        prefix = ['set-sdo']
        self.assertEqual(feedback.parse_args(prefix + ['20']).register_raw, 200)
        for extra in ([], ['0'], ['0.01'], ['NaN'], ['6553.6'], ['1e1000000'],
                      ['20', '--save', '--axis', '2'], ['20', '--duration', '5'],
                      ['20', '--axis', '7'], ['20', '--attempts', '0']):
            with self.subTest(extra=extra), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                feedback.parse_args(prefix + extra)
        self.assertTrue(feedback.parse_args(['set-sdo', '20', '--save']).save)
        for extra in ([], ['0'], ['256'], ['20', '--save'], ['20', '--axis', '2'],
                      ['20', '--register-ms', '20'], ['20', '--duration', 'nan']):
            with self.subTest(extra=extra), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                feedback.parse_args(['set-feedback'] + extra)

    def test_simple_commands_default_to_can0_node1(self):
        for command in (['read-sdo'], ['set-sdo', '20'], ['read-feedback'], ['set-feedback', '20']):
            with self.subTest(command=command):
                args = feedback.parse_args(command)
                self.assertEqual((args.interface, args.node_id), ('can0', 1))
                if args.command == 'set-feedback':
                    self.assertEqual(args.divider, 20)

    def test_interface_and_node_overrides(self):
        for options in (['--can', 'can1', '--id', '7'], ['--interface', 'can1', '--node-id', '7']):
            for command in (['read-sdo'], ['set-sdo', '10'], ['read-feedback'], ['set-feedback', '10']):
                with self.subTest(options=options, command=command):
                    args = feedback.parse_args(command + options)
                    self.assertEqual((args.interface, args.node_id), ('can1', 7))
        for command in (['read'], ['set', '20'], ['read-sdo', '--id', '0'],
                        ['set-feedback', '20', '--can', 'bad/name']):
            with self.subTest(command=command), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                feedback.parse_args(command)

    def test_main_set_sdo_only_changes_selected_axis_without_measuring_feedback(self):
        bus = FakeBus(self.clock)
        link = dict(ifname='can0', flags=['UP'], mtu=72,
                    linkinfo=dict(info_kind='can', info_data=dict(state='ERROR-ACTIVE', ctrlmode=['FD'],
                        bittiming=dict(bitrate=1000000), data_bittiming=dict(bitrate=5000000))))
        report = dict(elapsed_s=1, groups=[dict(group=hex(group), name=name, frames=50, hz=50,
                      mean_interval_ms=20, p50_ms=20, p95_ms=20, max_ms=20)
                      for group, name in feedback.GROUPS.items()])
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(feedback, 'FeedbackBus', return_value=bus), \
                patch.object(feedback.ota, 'snapshot', return_value=link), \
                patch.object(feedback.ota, 'maintenance', return_value=nullcontext()), \
                patch.object(feedback, 'observe', return_value=report) as observe, \
                patch.object(feedback.time, 'sleep'), redirect_stdout(io.StringIO()):
            code = feedback.main(['set-sdo', '5', '--id', '7', '--axis', '2',
                                  '--log-dir', directory])
        observe.assert_not_called()
        self.assertEqual(code, 0)
        self.assertEqual([ident for ident, _ in bus.sent], [0x607, 0x607])
        self.assertEqual(bus.registers[0x205d], 50)
        self.assertEqual(bus.registers[0x201d], 200)
        bus.close.assert_called_once()

    def test_main_saves_only_after_validation_then_reads_all_axes(self):
        for mismatch, errors in ((False, 0), (True, 0), (False, 1)):
            with self.subTest(mismatch=mismatch, errors=errors):
                bus = FakeBus(self.clock, readback=100 if mismatch else None)
                output = io.StringIO()
                with tempfile.TemporaryDirectory() as directory, \
                        patch.object(feedback, 'FeedbackBus', return_value=bus), \
                        patch.object(feedback.ota, 'snapshot', side_effect=[{'rx.errors': 0}, {'rx.errors': errors}]), \
                        patch.object(feedback.ota, 'counters', side_effect=lambda snapshot: snapshot), \
                        patch.object(feedback.ota, 'validate_link'), \
                        patch.object(feedback.ota, 'print_link'), \
                        patch.object(feedback.ota, 'maintenance', return_value=nullcontext()), \
                        patch.object(feedback, 'observe') as observe, \
                        patch.object(feedback.time, 'sleep'), \
                        redirect_stdout(output), redirect_stderr(io.StringIO()):
                    code = feedback.main(['set-sdo', '20', '--save',
                                          '--log-dir', directory])
                observe.assert_not_called()
                saved = [i for i, (_, data) in enumerate(bus.sent) if data[1:3] == b'\x10\x10']
                if mismatch or errors:
                    self.assertEqual(code, 1)
                    self.assertEqual(saved, [])
                else:
                    self.assertEqual(code, 0)
                    self.assertEqual(saved, [12, 13])
                    expected_reads = [b'\x40' + struct.pack('<HB', index, 0x14) + bytes(4)
                                      for index in feedback.REGISTERS]
                    self.assertEqual([data for _, data in bus.sent[1:12:2]], expected_reads)
                    self.assertEqual([data for _, data in bus.sent[14:]], expected_reads)
                    self.assertIn('Persistence is NOT verified', output.getvalue())
                bus.close.assert_called_once()

    def test_main_commands_keep_sdo_and_feedback_independent(self):
        for command in (['read-sdo'], ['read-feedback'], ['set-feedback', '20']):
            for frames in (0, 50):
                with self.subTest(command=command, frames=frames):
                    bus = FakeBus(self.clock)
                    report = dict(elapsed_s=1, groups=[dict(group=hex(group), name=name, frames=frames,
                                  hz=frames, mean_interval_ms=20, p50_ms=20, p95_ms=20, max_ms=20)
                                  for group, name in feedback.GROUPS.items()])
                    with tempfile.TemporaryDirectory() as directory, \
                            patch.object(feedback, 'FeedbackBus', return_value=bus), \
                            patch.object(feedback.ota, 'snapshot', return_value={}), \
                            patch.object(feedback.ota, 'validate_link'), \
                            patch.object(feedback.ota, 'print_link'), \
                            patch.object(feedback.ota, 'maintenance', return_value=nullcontext()), \
                            patch.object(feedback, 'observe', return_value=report) as observe, \
                            patch.object(feedback.time, 'sleep'), \
                            redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                        code = feedback.main(command + ['--log-dir', directory])
                    if command[0] == 'read-sdo':
                        self.assertEqual(code, 0)
                        observe.assert_not_called()
                        self.assertEqual(len(bus.sent), 6)
                        self.assertTrue(all(ident == 0x607 and data[0] == 0x40 for ident, data in bus.sent))
                    else:
                        self.assertEqual(code, 0 if frames else 1)
                        if command[0] == 'read-feedback':
                            self.assertEqual(bus.sent, [])
                        else:
                            self.assertEqual(bus.sent, [(0x507, bytes.fromhex('000450145a14'))] * 3)


if __name__ == '__main__':
    unittest.main()
