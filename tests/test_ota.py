"""Offline protocol tests. Uses fake peers and AF_UNIX only; never opens AF_CAN."""
from collections import deque
from contextlib import nullcontext, redirect_stdout, redirect_stderr
import io
import copy
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'examples/python'))
import ota

IDS = ota.CanIds(1)
LOCK_HELPER = None
REAL_SOCKET = socket.socket
LINK = dict(ifname='can0', ifindex=10, flags=['UP'], mtu=72,
            linkinfo=dict(info_kind='can', info_data=dict(
                state='ERROR-ACTIVE', ctrlmode=['FD'],
                bittiming=dict(bitrate=1000000, sample_point=.75),
                data_bittiming=dict(bitrate=5000000, sample_point=.75))))


def command(name, *extra):
    return [name, '--interface', 'can0', '--node-id', '1', *extra]


REGISTERS = {(0x200e, 0x1d): 3, (0x2030, 5): 2, (0x200e, 0x1e): 122,
             (0x2030, 6): 1, (0x2070, 6): 0x7600, (0x20b0, 6): 0x66,
             (0x20f0, 6): 0x26, (0x2130, 6): 0x713, (0x2170, 6): 0x17}


def sdo_reply(request, registers=REGISTERS):
    index, sub = struct.unpack_from('<HB', request, 1)
    return b'\x4b' + request[1:4] + struct.pack('<I', registers[index, sub])


class FakeBus:
    def __init__(self, prepare_timeout=False, drop_batch=False, bad_final=False, early_ack=False,
                 node_id=1, side=2, version=122, application_running=True):
        self.ids = ota.CanIds(node_id)
        self.index = 10
        self.close = MagicMock()
        self.sent, self.events, self.queue = [], [], deque()
        self.registers = dict(REGISTERS)
        self.registers[0x2030, 5] = side
        self.registers[0x200e, 0x1e] = version
        self.prepare_timeout, self.drop_batch = prepare_timeout, drop_batch
        self.bad_final, self.early_ack = bad_final, early_ack
        self.application_running = application_running
        self.prepare_calls = self.reopens = self.data_count = 0

    def trace(self, event, **fields):
        self.events.append((event, fields))

    def send(self, ident, data):
        self.sent.append((ident, data))
        if ident == self.ids.sdo_tx:
            if self.application_running:
                self.queue.append((self.ids.sdo_rx, sdo_reply(data, self.registers)))
            return
        if ident != self.ids.ota_tx:
            raise AssertionError(f'Unexpected ID {ident:x}')
        command = data[3]
        status = {0x50: 0, 0x51: 2, 0x52: 122 if self.application_running else 0,
                  0x53: 0x5a, 0xaa: 0x5a, 0xab: 0x5a}[command]
        if command == 0x53:
            self.prepare_calls += 1
            if self.prepare_timeout and self.prepare_calls == 1:
                return
        if command in (0xaa, 0xab):
            self.data_count += 1
            if self.drop_batch:
                return
            if not self.early_ack and data[4] != 15 and command != 0xab:
                return
            if command == 0xab and self.bad_final:
                status = 0x55
        self.queue.append((self.ids.ota_rx, bytes((0xff, 5 if command == 0x52 else 4, 0, 0, command, status, 0, 0))))

    def receive(self, timeout):
        return self.queue.popleft() if self.queue else None

    def guard_silence(self, duration=0):
        if self.queue:
            raise ota.Failure('Unsolicited/early response')

    def reconnect(self):
        self.reopens += 1


@unittest.skipUnless(sys.platform == 'linux', 'Linux SocketCAN maintenance only')
class ProtocolTests(unittest.TestCase):
    def setUp(self):
        def offline_socket(family=socket.AF_INET, *args, **kwargs):
            if family == socket.AF_CAN:
                raise AssertionError('Offline tests must never open AF_CAN')
            return REAL_SOCKET(family, *args, **kwargs)
        guard = patch('ota.socket.socket', side_effect=offline_socket)
        guard.start()
        self.addCleanup(guard.stop)

    def run_upgrade(self, bus, data, filename=ota.BUNDLED_FILES[2]):
        with patch('ota.time.sleep'), redirect_stdout(io.StringIO()):
            ota.Hand(bus).upgrade(data, filename)

    def test_firmware_integrity(self):
        for side in (1, 2):
            identity = ota.Hand(FakeBus(side=side)).probe()
            image = ota.select_firmware(identity)
            self.assertEqual(image['name'], ota.BUNDLED_FILES[side])
            self.assertEqual(image['side_raw'], side)
            self.assertEqual(len(image['data']), 278528)
            self.assertTrue(image['checksum_verified'])
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / image['name']
                damaged = bytearray(image['data'])
                damaged[-1] ^= 1
                path.write_bytes(damaged)
                with self.assertRaisesRegex(ota.Failure, 'SHA-256 mismatch'):
                    ota.firmware(path)

    def test_custom_firmware_name_content_and_side(self):
        with tempfile.TemporaryDirectory() as directory:
            for name, data in (('image.upd', b'image'),
                               ('DH116SL-CANFD-APP-H6E001-V1.23.upd', b''),
                               ('DH116SL-CANFD-APP-' + 'A' * 40 + '-V1.23.upd', b'image')):
                path = Path(directory) / name
                path.write_bytes(data)
                with self.subTest(name=name), self.assertRaises(ota.Failure):
                    ota.firmware(path)
            path = Path(directory) / 'DH116SR-CANFD-APP-H6E001-V1.23.upd'
            path.write_bytes(b'new image')
            with self.assertRaisesRegex(ota.Failure, 'for Right'):
                ota.select_firmware(ota.Hand(FakeBus(side=2)).probe(), path)
            image = ota.select_firmware(ota.Hand(FakeBus(side=1)).probe(), path)
            self.assertFalse(image['checksum_verified'])
            self.assertEqual(image['version'], '1.23')
            self.assertEqual(image['data'], b'new image')

    def test_wire_format_and_filter_masks(self):
        payload = bytes.fromhex('ff 03 00 53 02')
        raw = ota.encode_frame(IDS.ota_tx, payload)
        self.assertEqual(len(raw), 72)
        self.assertEqual(raw[:8], bytes.fromhex('01 90 00 80 40 01 00 00'))
        self.assertEqual(raw[8:], payload.ljust(64, b'\0'))
        self.assertEqual(ota.decode_frame(raw), (0x80009001, payload.ljust(64, b'\0')))
        for offset in range(0, len(IDS.filters), 8):
            _, mask = struct.unpack_from('=II', IDS.filters, offset)
            self.assertEqual(mask & ota.ERR, 0)
        with self.assertRaises(ota.Failure):
            ota.encode_frame(IDS.ota_tx, bytes(65))

    def test_malformed_can_frames(self):
        for raw in (bytes(71),
                    ota.FRAME.pack(ota.RTR | IDS.sdo_rx, 8, 0, 0, 0, bytes(64)),
                    ota.FRAME.pack(IDS.sdo_rx, 65, 0, 0, 0, bytes(64))):
            with self.subTest(raw=raw.hex()), self.assertRaises(ota.Failure):
                ota.decode_frame(raw)
        classic = struct.pack('=IB3x8s', IDS.sdo_rx, 8, bytes.fromhex('4b0e201d03000000'))
        self.assertEqual(ota.decode_frame(classic)[1], bytes.fromhex('4b0e201d03000000'))

    def test_controller_notifications_and_fatal_can_errors(self):
        states = [(4, flag) for flag in (4, 8, 0x10, 0x20, 0x40)] + [(0x204, 4)]
        failures = [(0x40, 0), (4, 1), (4, 2), (4, 5), (0x44, 4), (8, 0)]
        for kind, status in states + failures:
            bus = object.__new__(ota.CanBus)
            bus.ids = IDS
            bus.sock, bus.trace = MagicMock(), MagicMock()
            raw = struct.pack('=IB3x8s', ota.ERR | kind, 8, bytes((0, status)) + bytes(6))
            bus.sock.recvmsg.return_value = (raw, [], 0, None)
            with self.subTest(kind=kind, status=status), \
                    patch('ota.select.select', side_effect=[([bus.sock], [], []), ([], [], [])]):
                if (kind, status) in states:
                    self.assertIsNone(bus.receive(.1))
                    self.assertEqual(bus.trace.call_args.args[0], 'can_state')
                else:
                    with self.assertRaisesRegex(ota.Failure, 'CAN error frame'):
                        bus.receive(.1)

    def test_controller_notifications_do_not_extend_ack_deadline(self):
        bus = object.__new__(ota.CanBus)
        bus.ids = IDS
        bus.sock, bus.trace = MagicMock(), MagicMock()
        raw = struct.pack('=IB3x8s', ota.ERR | 4, 8, bytes.fromhex('0004000000000000'))
        bus.sock.recvmsg.return_value = (raw, [], 0, None)
        with patch('ota.select.select', return_value=([bus.sock], [], [])), \
                patch('ota.time.monotonic', side_effect=[i / 100 for i in range(100)]):
            with self.assertRaises(ota.ResponseTimeout):
                ota.Hand(bus, ota_timeout=.1).wait_ack(0x53)
        self.assertGreater(bus.sock.recvmsg.call_count, 0)
        self.assertLess(bus.sock.recvmsg.call_count, 10)

    def test_controller_notification_does_not_hide_unsolicited_ack(self):
        bus = object.__new__(ota.CanBus)
        bus.ids = IDS
        bus.sock, bus.trace = MagicMock(), MagicMock()
        state = struct.pack('=IB3x8s', ota.ERR | 4, 8, bytes.fromhex('0040000000000000'))
        ack = struct.pack('=IB3x8s', IDS.ota_rx, 8, bytes.fromhex('ff040000aa5a0000'))
        bus.sock.recvmsg.side_effect = [(state, [], 0, None), (ack, [], 0, None)]
        with patch('ota.select.select', return_value=([bus.sock], [], [])), \
                self.assertRaisesRegex(ota.Failure, 'Unsolicited'):
            bus.guard_silence()

    def test_sdo_validation(self):
        self.assertEqual(ota.decode_sdo(bytes.fromhex('4b0e201d03000000'), 0x200e, 0x1d), 3)
        for data in ('800e201d00000206', '4b0e201e03000000', '600e201d00000000', '4b0e201d03'):
            with self.subTest(data=data), self.assertRaises(ota.Failure):
                ota.decode_sdo(bytes.fromhex(data), 0x200e, 0x1d)

    def test_probe_matches_recorded_identity(self):
        bus = FakeBus()
        identity = ota.Hand(bus).probe()
        self.assertEqual(identity['firmware'], '1.22')
        self.assertEqual(identity['serial'], '176000066002607130017')
        self.assertEqual(len(bus.sent), 9)
        self.assertTrue(all(ident == IDS.sdo_tx and data[0] == 0x40 for ident, data in bus.sent))
        self.assertEqual(identity['side'], 'Left')

    def test_identity_and_version_eligibility(self):
        for key, value in (((0x2030, 5), 0), ((0x2030, 5), 3),
                           ((0x200e, 0x1d), 6), ((0x200e, 0x1e), 0)):
            bus = FakeBus()
            bus.registers[key] = value
            with self.assertRaises(ota.Failure):
                ota.Hand(bus).probe()
        for side, label in ((1, 'Right'), (2, 'Left')):
            for version in (119, 120, 121, 122, 123):
                identity = ota.Hand(FakeBus(side=side, version=version)).probe()
                self.assertEqual(identity['side'], label)
                if version <= 120 or version > 122:
                    reason = 'greater than 1.20' if version <= 120 else 'Downgrade'
                    with self.assertRaisesRegex(ota.Failure, reason):
                        ota.select_firmware(identity)
                else:
                    self.assertEqual(ota.select_firmware(identity)['version_raw'], 122)
            with tempfile.TemporaryDirectory() as directory:
                for version in (123, 200):
                    name = f'DH116S{label[0]}-CANFD-APP-H6E001-V{version // 100}.{version % 100:02d}.upd'
                    path = Path(directory) / name
                    path.write_bytes(b'future image')
                    for current in (122, version):
                        identity = ota.Hand(FakeBus(side=side, version=current)).probe()
                        self.assertEqual(ota.select_firmware(identity, path)['version_raw'], version)

    def test_sdo_timeout_has_no_retry(self):
        bus = FakeBus()
        with patch.object(bus, 'receive', return_value=None), self.assertRaises(ota.ResponseTimeout):
            ota.Hand(bus).read(0x200e, 0x1d)
        self.assertEqual(len(bus.sent), 1)

    def test_full_firmware_transfer_and_prepare_reconnect(self):
        for side in (1, 2):
            image = ota.firmware(ota.ROOT / 'firmware' / ota.BUNDLED_FILES[side])
            bus = FakeBus(prepare_timeout=True, side=side)
            self.run_upgrade(bus, image['data'], image['name'])
            frames = [payload for _, payload in bus.sent]
            self.assertEqual([p[3] for p in frames[:5]], [0x50, 0x51, 0x52, 0x53, 0x53])
            self.assertEqual(frames[0], bytes.fromhex('ff25005000') + image['name'].encode())
            self.assertEqual(frames[3], bytes.fromhex('ff03005302'))
            self.assertEqual(bus.reopens, 1)
            packets = frames[5:]
            self.assertEqual(len(packets), 8704)
            self.assertEqual(b''.join(p[5:] for p in packets), image['data'])
            for index, payload in enumerate(packets):
                self.assertEqual(len(payload), 37)
                self.assertEqual(payload[:3], b'\xff\x23\0')
                self.assertEqual(payload[4], index % 16)
                self.assertEqual(payload[3], 0xab if index == 8703 else 0xaa)
            self.assertEqual(bus.events[-1][0], 'transfer_complete')

    def test_bootloader_metadata_is_distinct_from_application_identity(self):
        for truncated in (False, True):
            bus = FakeBus()
            def receive(timeout):
                ident, data = FakeBus.receive(bus, timeout)
                if data[4] == 0x51:
                    data = data[:5] + b'\x91' + data[6:]
                elif data[4] == 0x52:
                    data = data[:5] + b'\x34\x12'
                    if truncated:
                        data = data[:6]
                return ident, data
            with patch.object(bus, 'receive', side_effect=receive):
                if truncated:
                    with self.assertRaisesRegex(ota.Failure, 'Truncated bootloader version'):
                        self.run_upgrade(bus, bytes(32))
                    self.assertEqual(bus.data_count, 0)
                else:
                    self.run_upgrade(bus, bytes(32))
                    info = next(fields for event, fields in bus.events if event == 'bootloader_info')
                    self.assertEqual(info['version_raw'], 0x1234)
                    self.assertEqual(bus.data_count, 1)

    def test_partial_final_packet_padding(self):
        batches = list(ota.data_batches(bytes(range(33))))
        self.assertEqual(len(batches), 1)
        start, packets, final = batches[0]
        self.assertTrue(final)
        self.assertEqual(start, 0)
        self.assertEqual(packets[-1][:5], bytes.fromhex('ff2300ab01'))
        self.assertEqual(packets[-1][5:], b'\x20' + bytes(31))

    def test_batch_timeout_stops_without_retransmit(self):
        bus = FakeBus(drop_batch=True)
        with self.assertRaisesRegex(ota.ResponseTimeout, 'NOT retransmitted'):
            self.run_upgrade(bus, bytes(1024))
        self.assertEqual(bus.data_count, 16)
        self.assertFalse(any(event == 'transfer_complete' for event, _ in bus.events))

    def test_write_failure_and_duplicate_acks_cannot_report_success(self):
        bus = FakeBus(bad_final=True)
        with self.assertRaisesRegex(ota.Failure, 'write failed'):
            self.run_upgrade(bus, bytes(512))
        bus = FakeBus(early_ack=True)
        with self.assertRaisesRegex(ota.Failure, 'Expected OTA ACK 0xab'):
            self.run_upgrade(bus, bytes(1024))
        self.assertFalse(any(event == 'transfer_complete' for event, _ in bus.events))

    def test_delayed_handshake_reply_during_data_waits_for_batch_ack(self):
        bus = FakeBus()
        send = bus.send
        def delayed_reply(ident, data):
            if data[3] == 0xaa and bus.data_count == 0:
                bus.queue.append((bus.ids.ota_rx, bytes.fromhex('ff040000535a0000')))
            send(ident, data)
        with patch.object(bus, 'send', side_effect=delayed_reply):
            self.run_upgrade(bus, bytes(1024))
        self.assertEqual(bus.data_count, 32)
        self.assertEqual(bus.events[-1][0], 'transfer_complete')

    def test_send_failure_is_not_hidden(self):
        bus = FakeBus()
        with patch.object(bus, 'send', side_effect=OSError('ENOBUFS')) as send:
            with self.assertRaises(OSError):
                self.run_upgrade(bus, bytes(512))
        self.assertEqual(send.call_count, 1)

    def test_handshake_rejection_and_timeout_are_bounded(self):
        bus = FakeBus()
        denied = (IDS.ota_rx, bytes.fromhex('ff04000050ee0000'))
        with patch.object(bus, 'receive', return_value=denied), self.assertRaisesRegex(ota.Failure, 'rejected'):
            self.run_upgrade(bus, bytes(512))
        self.assertEqual(len(bus.sent), 1)
        bus = FakeBus()
        with patch.object(bus, 'receive', return_value=None), self.assertRaises(ota.ResponseTimeout):
            self.run_upgrade(bus, bytes(512))
        self.assertEqual(len(bus.sent), 10)
        self.assertEqual(bus.data_count, 0)

    def test_prepare_transport_failure_reconnects_once_and_timeouts_are_bounded(self):
        bus = FakeBus()
        send = bus.send
        attempts = []
        def fail_first_prepare(ident, data):
            if data[3] == 0x53:
                attempts.append(data)
                if len(attempts) == 1:
                    raise OSError('temporary send failure')
            send(ident, data)
        with patch.object(bus, 'send', side_effect=fail_first_prepare):
            self.run_upgrade(bus, bytes(32))
        self.assertEqual((len(attempts), bus.reopens, bus.data_count), (2, 1, 1))
        bus = FakeBus()
        with patch.object(bus, 'receive', return_value=None), redirect_stdout(io.StringIO()), \
                self.assertRaises(ota.ResponseTimeout):
            ota.Hand(bus).command(0x53, b'\x02', expected=0x5a)
        self.assertEqual((bus.prepare_calls, bus.reopens, bus.data_count), (10, 1, 0))

    def test_non_ready_handshake_ack_retries_like_vendor(self):
        for command, expected in ((0x50, 0), (0x53, 0x5a)):
            for persistent in (False, True):
                bus = FakeBus()
                receive = bus.receive
                replies = []
                def non_ready(timeout):
                    ident, data = receive(timeout)
                    replies.append(data)
                    if persistent or len(replies) == 1:
                        data = data[:5] + b'\x55' + data[6:]
                    return ident, data
                with self.subTest(command=command, persistent=persistent), \
                        patch.object(bus, 'receive', side_effect=non_ready), redirect_stdout(io.StringIO()):
                    hand = ota.Hand(bus)
                    if persistent:
                        with self.assertRaisesRegex(ota.Failure, 'status 0x55'):
                            hand.command(command, b'\x02' if command == 0x53 else b'\0', expected)
                    else:
                        self.assertEqual(hand.command(command, b'\x02' if command == 0x53 else b'\0', expected)[5], expected)
                self.assertEqual(len(bus.sent), 10 if persistent else 2)
                self.assertEqual(bus.reopens, 1 if command == 0x53 else 0)
                self.assertEqual(bus.data_count, 0)

    def test_reconnect_reopens_same_interface_without_state_gate(self):
        with patch('ota.socket.if_nametoindex', return_value=10), patch('ota.socket.socket') as create, \
                patch('ota.snapshot') as snapshot, patch('ota.time.sleep'):
            bus = ota.CanBus('can0', 1, MagicMock())
            first_socket = bus.sock
            bus.reconnect()
            first_socket.close.assert_called_once()
            self.assertEqual(create.call_count, 2)
            snapshot.assert_not_called()
            bus.close()

    def test_socket_overflow_and_short_write_fail(self):
        bus = object.__new__(ota.CanBus)
        bus.ids = IDS
        bus.sock, bus.trace = MagicMock(), lambda *args, **kwargs: None
        raw = ota.FRAME.pack(IDS.sdo_rx, 8, 0, 0, 0, bytes(64))
        bus.sock.recvmsg.return_value = (raw, [(socket.SOL_SOCKET, 40, struct.pack('=I', 1))], 0, None)
        with patch('ota.select.select', return_value=([bus.sock], [], [])), self.assertRaisesRegex(ota.Failure, 'overflow'):
            bus.receive(.5)
        bus.sock.send.return_value = 71
        with patch('ota.select.select', return_value=([], [bus.sock], [])), self.assertRaisesRegex(ota.Failure, 'Short'):
            bus.send(IDS.ota_tx, b'\xff\x03\0\x53\x02')

    def test_hotplug_is_rejected_before_reopening_socket(self):
        bus = object.__new__(ota.CanBus)
        bus.interface, bus.index = 'can0', 10
        with patch('ota.socket.if_nametoindex', return_value=11), patch('ota.socket.socket') as create:
            with self.assertRaisesRegex(ota.Failure, 'replaced'):
                bus.open()
            create.assert_not_called()

    def test_confirmation_gate_never_opens_can(self):
        with patch('ota.CanBus') as bus, redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            with patch('ota.sys.stdin.isatty', return_value=False):
                self.assertEqual(ota.main(command('upgrade')), 1)
            bus.assert_not_called()

    def test_cancel_and_identity_change_never_enter_ota(self):
        identity = dict(model_raw=3, side_raw=2, side='Left', version_raw=122, firmware='1.22', serial='test-A')
        for confirmation in ('cancel', 'UPGRADE LEFT can0 1'):
            with self.subTest(confirmation=confirmation), patch('ota.maintenance', return_value=nullcontext()), \
                    patch('ota.snapshot', return_value=LINK), patch('ota.Trace'), \
                    patch('ota.socket.if_nametoindex', return_value=10), \
                    patch('ota.CanBus', return_value=FakeBus()), patch('ota.Hand') as constructor, \
                    patch('ota.sys.stdin.isatty', return_value=True), patch('builtins.input', return_value=confirmation), \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                hand = constructor.return_value
                hand.probe.side_effect = [identity, dict(identity, serial='test-B')]
                self.assertEqual(ota.main(command('upgrade')), 0 if confirmation == 'cancel' else 1)
                hand.upgrade.assert_not_called()

    def test_cli_requires_explicit_target_and_valid_options(self):
        with patch('ota.CanBus') as bus, redirect_stderr(io.StringIO()):
            for arguments in (['check'], ['upgrade'], ['probe', '--interface', 'can0'],
                              ['check', '--node-id', '1'], command('setup'),
                              command('upgrade', '--yes'), command('upgrade', '--duration', '1'),
                              command('upgrade', '--recover'), command('check', '--recover'),
                              command('probe', '--recover'),
                              command('check', '--duration', 'nan'), command('check', '--rate', '0'),
                              command('upgrade', '--packet-gap-ms', 'inf'),
                              command('upgrade', '--ota-timeout-ms', '0'),
                              command('check', '--interface', '../can0')):
                with self.subTest(arguments=arguments), self.assertRaises(SystemExit):
                    ota.main(arguments)
            for node in ('0', '128', '-1', 'bad'):
                with self.subTest(node=node), self.assertRaises(SystemExit):
                    ota.main(command('check', '--node-id', node))
            bus.assert_not_called()
        for name in ('check', 'upgrade', 'probe'):
            for node in (1, 7, 127):
                args = ota.parse_args(command(name, '--node-id', str(node)))
                self.assertEqual((args.interface, args.node_id), ('can0', node))
                if name == 'check':
                    self.assertEqual(args.duration, 30)
                if name == 'upgrade':
                    self.assertEqual((args.ota_timeout_ms, args.packet_gap_ms), (1000, 1))
        self.assertEqual(ota.parse_args(command('check', '--duration', '60')).duration, 60)

    def test_all_node_ids_and_filters_follow_the_selected_target(self):
        for node in (1, 7, 127):
            bus = FakeBus(node_id=node)
            ids = bus.ids
            self.assertEqual((ids.sdo_tx, ids.sdo_rx, ids.ota_tx, ids.ota_rx),
                             (0x600 + node, 0x580 + node, 0x80009000 + node, 0x80007000 + node))
            self.assertEqual(ota.Hand(bus).probe()['side'], 'Left')
            self.assertTrue(all(ident == ids.sdo_tx for ident, _ in bus.sent))
            bus.sent.clear()
            self.run_upgrade(bus, bytes(1024))
            self.assertEqual(bus.data_count, 32)
            self.assertTrue(all(ident == ids.ota_tx for ident, _ in bus.sent))
            filters = list(struct.iter_unpack('=II', ids.filters))
            self.assertEqual([ident for ident, _ in filters],
                             [ids.sdo_rx, ids.sdo_tx, ids.ota_rx, ids.ota_tx])
            for ident, mask in filters:
                self.assertFalse(mask & ota.ERR)
                self.assertTrue(mask & ota.EFF)
                self.assertNotEqual(ident & mask, (ident ^ 1) & mask)
        for node in (0, 128, True, '1', 1.0):
            with self.assertRaises(ValueError):
                ota.CanIds(node)

    def test_socket_installs_selected_node_filters(self):
        with patch('ota.socket.if_nametoindex', return_value=10), patch('ota.socket.socket') as create:
            bus = ota.CanBus('can3', 7, lambda *args, **kwargs: None)
            sock = create.return_value
            sock.bind.assert_called_once_with(('can3',))
            sock.setsockopt.assert_any_call(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, ota.CanIds(7).filters)
            sock.setsockopt.assert_any_call(socket.SOL_CAN_RAW, 2, struct.pack('=I', 0x1fffffff))
            bus.close()
            sock.close.assert_called_once()

    def test_wrong_node_responses_are_rejected(self):
        bus = FakeBus(node_id=7)
        reply = (IDS.sdo_rx, bytes.fromhex('4b0e201d03000000'))
        with patch.object(bus, 'receive', return_value=reply), self.assertRaisesRegex(ota.Failure, 'CAN ID'):
            ota.Hand(bus).read(0x200e, 0x1d)
        reply = (IDS.ota_rx, bytes.fromhex('ff040000ab5a0000'))
        with patch.object(bus, 'receive', return_value=reply), self.assertRaisesRegex(ota.Failure, 'unexpected OTA'):
            ota.Hand(bus).wait_ack(0xab, strict=True)

    def test_existing_board_timing_is_preserved_and_bad_links_rejected(self):
        original = copy.deepcopy(LINK)
        ota.validate_link(LINK)  # 75% / 75% is a possible board configuration.
        self.assertEqual(LINK, original)
        for field, value in (('state', 'BUS-OFF'), ('ctrlmode', ['FD', 'LOOPBACK']),
                             ('ctrlmode', ['FD', 'LISTEN-ONLY']), ('ctrlmode', []),
                             ('bittiming', dict(bitrate=500000)), ('data_bittiming', dict(bitrate=2000000))):
            link = copy.deepcopy(LINK)
            link['linkinfo']['info_data'][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ota.Failure):
                ota.validate_link(link)
        for field, value in (('flags', []), ('mtu', 16)):
            link = dict(LINK, **{field: value})
            with self.assertRaises(ota.Failure):
                ota.validate_link(link)
        for state in ('ERROR-WARNING', 'ERROR-PASSIVE', 'BUS-OFF'):
            link = copy.deepcopy(LINK)
            link['linkinfo']['info_data']['state'] = state
            if state == 'BUS-OFF':
                with self.assertRaises(ota.Failure):
                    ota.validate_link(link)
            else:
                ota.validate_link(link)
        with patch('ota.subprocess.run', return_value=MagicMock(stdout=json.dumps([LINK]))) as run:
            self.assertEqual(ota.snapshot('can0'), LINK)
            self.assertEqual(run.call_args.args[0],
                             ['ip', '-details', '-statistics', '-json', 'link', 'show', 'dev', 'can0'])

    def test_check_is_read_only_and_records_health(self):
        for side, version, custom in ((2, 122, False), (1, 121, False), (1, 123, True)):
            bus, clock = FakeBus(node_id=7, side=side, version=version), [0.0]
            before, after = copy.deepcopy(LINK), copy.deepcopy(LINK)
            before['linkinfo']['info_data']['state'] = 'ERROR-WARNING'
            after['linkinfo']['info_data']['state'] = 'ERROR-PASSIVE' if custom else 'ERROR-WARNING'
            before['linkinfo']['info_xstats'] = dict(error_warning=2, error_passive=1)
            after['linkinfo']['info_xstats'] = dict(error_warning=3, error_passive=2)
            def create_bus(interface, node_id, trace):
                bus.trace = trace
                return bus
            def silence(duration=0):
                FakeBus.guard_silence(bus, duration)
                clock[0] += duration
            with tempfile.TemporaryDirectory() as directory, \
                    patch('ota.maintenance', return_value=nullcontext()), \
                    patch('ota.snapshot', side_effect=[before, before, after, after]), \
                    patch('ota.CanBus', side_effect=create_bus) as create, \
                    patch('ota.time.monotonic', side_effect=lambda: clock[0]), \
                    patch.object(bus, 'guard_silence', side_effect=silence), redirect_stdout(io.StringIO()) as output:
                extra = []
                filename = ota.BUNDLED_FILES[side]
                if custom:
                    filename = 'DH116SR-CANFD-APP-H6E001-V1.24.upd'
                    path = Path(directory) / filename
                    path.write_bytes(bytes(513))
                    extra = ['--firmware', str(path)]
                result = ota.main(command('check', '--node-id', '7', '--log-dir', directory, *extra))
                records = [json.loads(line) for line in next(Path(directory).glob('*.jsonl')).read_text().splitlines()]
            self.assertEqual(result, 0)
            self.assertEqual(create.call_args.args[:2], ('can0', 7))
            self.assertTrue(all(ident == bus.ids.sdo_tx and payload[0] == 0x40 for ident, payload in bus.sent))
            report = next(row for row in records if row['event'] == 'health_summary')
            self.assertEqual((report['status'], report['success_rate']), ('completed', 1))
            self.assertEqual(report['counter_delta']['can.error_warning'], 1)
            self.assertEqual(report['counter_delta']['can.error_passive'], 1)
            self.assertAlmostEqual(report['elapsed_s'], 30)
            self.assertIn('CHECK PASSED: Ready to upgrade.', output.getvalue())
            self.assertIn('can0, node 7', output.getvalue())
            readiness = next(row for row in records if row['event'] == 'check_result')
            self.assertTrue(readiness['ready_to_upgrade'])
            self.assertEqual(records[-1]['status'], 'completed')
            selected = next(row for row in records if row['event'] == 'firmware_selected')
            self.assertEqual(selected['name'], filename)
            bus.close.assert_called_once()

    def test_probe_needs_no_firmware_and_never_enters_ota(self):
        bus = FakeBus()
        with patch('ota.maintenance', return_value=nullcontext()), patch('ota.snapshot', return_value=LINK), \
                patch('ota.Trace'), patch('ota.CanBus', return_value=bus), patch('ota.firmware') as image, \
                redirect_stdout(io.StringIO()):
            self.assertEqual(ota.main(command('probe')), 0)
            image.assert_not_called()
        self.assertEqual(len(bus.sent), 9)
        self.assertTrue(all(ident == IDS.sdo_tx for ident, _ in bus.sent))

    def test_invalid_firmware_never_enters_ota_or_reports_ready(self):
        right_image = ota.ROOT / 'firmware' / ota.BUNDLED_FILES[1]
        for version, extra, reason in ((120, [], 'greater than 1.20'), (123, [], 'Downgrade'),
                                       (122, ['--firmware', str(right_image)], 'for Right'),
                                       (122, ['--firmware', 'image.upd'], 'original filename')):
            for name in ('check', 'upgrade'):
                bus = FakeBus(version=version)
                with self.subTest(version=version, command=name, extra=extra), \
                        patch('ota.maintenance', return_value=nullcontext()), patch('ota.Trace') as trace, \
                        patch('ota.CanBus', return_value=bus), patch('ota.snapshot', return_value=LINK), \
                        patch('ota.sys.stdin.isatty', return_value=True), patch('builtins.input') as confirm, \
                        patch('ota.health') as health, redirect_stdout(io.StringIO()) as output, \
                        redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(ota.main(command(name, *extra)), 1)
                self.assertEqual(len(bus.sent), 9)
                self.assertTrue(all(ident == IDS.sdo_tx for ident, _ in bus.sent))
                bus.close.assert_called_once()
                confirm.assert_not_called()
                health.assert_not_called()
                self.assertIn(reason, errors.getvalue())
                self.assertNotIn('Ready to upgrade', output.getvalue())
                if name == 'check':
                    self.assertIn('CHECK FAILED: Normal upgrade readiness could not be confirmed', errors.getvalue())
                    result = next(call.kwargs for call in trace.return_value.call_args_list
                                  if call.args[0] == 'check_result')
                    self.assertFalse(result['ready_to_upgrade'])

    def test_upgrade_cli_rechecks_identity_and_records_final_ack(self):
        for side, version, custom, drop in ((2, 122, False, False), (1, 122, False, False),
                                            (1, 123, True, False), (2, 122, False, True)):
            bus = FakeBus(node_id=7, side=side, version=version, drop_batch=drop)
            link = copy.deepcopy(LINK)
            link['linkinfo']['info_data']['state'] = (
                'ERROR-PASSIVE' if custom else 'ERROR-WARNING' if side == 1 else 'ERROR-ACTIVE')
            confirmation = f'UPGRADE {"RIGHT" if side == 1 else "LEFT"} can0 7'
            def create_bus(interface, node_id, trace):
                bus.trace = trace
                return bus
            with tempfile.TemporaryDirectory() as directory, \
                    patch('ota.maintenance', return_value=nullcontext()), patch('ota.snapshot', return_value=link), \
                    patch('ota.socket.if_nametoindex', return_value=10), patch('ota.CanBus', side_effect=create_bus), \
                    patch('ota.sys.stdin.isatty', return_value=True), patch('ota.time.sleep'), \
                    patch('builtins.input', return_value=confirmation) as confirm, \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                extra = []
                filename = ota.BUNDLED_FILES[side]
                if custom:
                    filename = 'DH116SR-CANFD-APP-H6E001-V1.24.upd'
                    path = Path(directory) / filename
                    path.write_bytes(bytes(513))
                    extra = ['--firmware', str(path)]
                result = ota.main(command('upgrade', '--node-id', '7', '--log-dir', directory, *extra))
                records = [json.loads(line) for line in next(Path(directory).glob('*.jsonl')).read_text().splitlines()]
            self.assertEqual(result, 1 if drop else 0)
            self.assertIn(confirmation, confirm.call_args.args[0])
            self.assertEqual(sum(ident == bus.ids.sdo_tx for ident, _ in bus.sent), 18)
            self.assertEqual(bus.data_count, 16 if drop else 17 if custom else 8704)
            trigger = next(payload for ident, payload in bus.sent if ident == bus.ids.ota_tx)
            self.assertEqual(trigger[5:], filename.encode('ascii'))
            image = next(row for row in records if row['event'] == 'firmware_selected')
            self.assertEqual(image['name'], filename)
            self.assertEqual(image['checksum_verified'], not custom)
            self.assertEqual(any(row['event'] == 'transfer_complete' for row in records), not drop)
            self.assertEqual(records[-1]['status'], 'failed' if drop else 'completed')
            if drop:
                self.assertTrue(next(row for row in records if row['event'] == 'failure')['ota_started'])
            bus.close.assert_called_once()

    def test_recovery_cli_restarts_full_transfer_without_application_identity(self):
        for side, node, custom in ((2, 1, False), (1, 7, False), (1, 127, True)):
            bus = FakeBus(node_id=node, side=side, application_running=False, prepare_timeout=True)
            link = copy.deepcopy(LINK)
            link['linkinfo']['info_data']['state'] = 'ERROR-WARNING'
            confirmation = f'RECOVER {ota.SIDES[side].upper()} can0 {node}'
            def create_bus(interface, node_id, trace):
                bus.trace = trace
                return bus
            with self.subTest(side=side, node=node), tempfile.TemporaryDirectory() as directory, \
                    patch('ota.maintenance', return_value=nullcontext()), patch('ota.snapshot', return_value=link), \
                    patch('ota.socket.if_nametoindex', return_value=10), patch('ota.CanBus', side_effect=create_bus), \
                    patch('ota.sys.stdin.isatty', return_value=True), patch('ota.time.sleep'), \
                    patch('builtins.input', return_value=confirmation) as confirm, redirect_stdout(io.StringIO()) as output:
                path = ota.ROOT / 'firmware' / ota.BUNDLED_FILES[side]
                if custom:
                    path = Path(directory) / 'DH116SR-CANFD-APP-H6E001-V1.24.upd'
                    path.write_bytes(bytes(range(256)) * 4)
                result = ota.main(command('upgrade', '--recover', '--firmware', str(path),
                                          '--node-id', str(node), '--log-dir', directory))
                records = [json.loads(line) for line in next(Path(directory).glob('*.jsonl')).read_text().splitlines()]
                expected_data = path.read_bytes()
            self.assertEqual(result, 0)
            self.assertIn(confirmation, confirm.call_args.args[0])
            self.assertIn('current version and serial are not verified', output.getvalue())
            self.assertIn('Flash: unknown ->', output.getvalue())
            self.assertTrue(all(ident == bus.ids.ota_tx for ident, _ in bus.sent))
            frames = [data for _, data in bus.sent]
            self.assertEqual([data[3] for data in frames[:5]], [0x50, 0x51, 0x52, 0x53, 0x53])
            self.assertEqual(frames[0][5:], path.name.encode('ascii'))
            self.assertEqual(frames[5][4], 0)
            self.assertEqual(b''.join(data[5:] for data in frames[5:]), expected_data)
            self.assertEqual(bus.reopens, 1)
            self.assertFalse(any(row['event'] == 'identity' for row in records))
            self.assertFalse(next(row for row in records if row['event'] == 'recovery_mode')['identity_verified'])
            self.assertEqual(next(row for row in records if row['event'] == 'bootloader_info')['version_raw'], 0)
            self.assertEqual(records[-1]['status'], 'completed')
            self.assertTrue(any(row['event'] == 'transfer_complete' for row in records))
            bus.close.assert_called_once()

    def test_recovery_cancel_bad_file_and_unresponsive_target(self):
        for case in ('cancel', 'bad_file', 'no_response'):
            bus = FakeBus(application_running=False)
            if case == 'no_response':
                bus.receive = lambda timeout: None
            path = 'image.upd' if case == 'bad_file' else str(ota.ROOT / 'firmware' / ota.BUNDLED_FILES[2])
            with self.subTest(case=case), patch('ota.maintenance', return_value=nullcontext()), \
                    patch('ota.snapshot', return_value=LINK), patch('ota.Trace') as trace, \
                    patch('ota.CanBus', return_value=bus), patch('ota.socket.if_nametoindex', return_value=10), \
                    patch('ota.sys.stdin.isatty', return_value=True), \
                    patch('builtins.input', return_value='RECOVER LEFT can0 1' if case == 'no_response' else 'cancel') as confirm, \
                    redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                result = ota.main(command('upgrade', '--recover', '--firmware', path))
            self.assertEqual(result, 0 if case == 'cancel' else 1)
            if case == 'bad_file':
                confirm.assert_not_called()
            self.assertEqual(len(bus.sent), 10 if case == 'no_response' else 0)
            self.assertEqual(bus.data_count, 0)
            self.assertFalse(any(call.args[0] == 'transfer_complete' for call in trace.return_value.call_args_list))

    def test_sdo_timeout_diagnoses_ota_reachability_without_triggering_upgrade(self):
        for name in ('check', 'probe', 'upgrade'):
            for responds in (True, False):
                bus = FakeBus(application_running=False)
                if not responds:
                    bus.receive = lambda timeout: None
                link = copy.deepcopy(LINK)
                link['linkinfo']['info_data']['state'] = 'ERROR-WARNING'
                with self.subTest(command=name, responds=responds), \
                        patch('ota.maintenance', return_value=nullcontext()), patch('ota.snapshot', return_value=link), \
                        patch('ota.Trace') as trace, patch('ota.CanBus', return_value=bus), \
                        patch('ota.sys.stdin.isatty', return_value=True), patch('builtins.input') as confirm, \
                        patch('ota.health') as health, redirect_stdout(io.StringIO()) as output, \
                        redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(ota.main(command(name)), 2 if responds else 1)
                self.assertEqual(bus.sent, [
                    (bus.ids.sdo_tx, bytes.fromhex('400e201d00000000')),
                    (bus.ids.ota_tx, bytes.fromhex('ff03005100'))])
                self.assertEqual(bus.data_count, 0)
                confirm.assert_not_called()
                health.assert_not_called()
                if responds:
                    self.assertIn('OTA query responded; application identity is unavailable.', output.getvalue())
                    self.assertIn('--recover --firmware', output.getvalue())
                    self.assertEqual(errors.getvalue(), '')
                    self.assertEqual(trace.return_value.call_args_list[-1].kwargs['status'], 'recovery_required')
                    if name == 'check':
                        result = next(call.kwargs for call in trace.return_value.call_args_list
                                      if call.args[0] == 'check_result')
                        self.assertFalse(result['ready_to_upgrade'])
                        self.assertTrue(result['recovery_available'])
                else:
                    self.assertIn('OTA product query also timed out', errors.getvalue())

    def test_partial_identity_timeout_does_not_suggest_recovery(self):
        for name in ('check', 'probe', 'upgrade'):
            for missing in ((0x2030, 5), (0x200e, 0x1e), (0x2170, 6)):
                bus = FakeBus(side=1)
                send = bus.send
                def drop_identity_reply(ident, data):
                    send(ident, data)
                    if ident == bus.ids.sdo_tx and struct.unpack_from('<HB', data, 1) == missing:
                        bus.queue.clear()
                with self.subTest(command=name, missing=missing), \
                        patch('ota.maintenance', return_value=nullcontext()), patch('ota.snapshot', return_value=LINK), \
                        patch('ota.Trace') as trace, patch('ota.CanBus', return_value=bus), \
                        patch.object(bus, 'send', side_effect=drop_identity_reply), \
                        patch('ota.sys.stdin.isatty', return_value=True), patch('builtins.input') as confirm, \
                        redirect_stdout(io.StringIO()) as output, redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(ota.main(command(name)), 1)
                self.assertIn('Application SDO responded, but identity reading is incomplete', errors.getvalue())
                self.assertIn('--sdo-timeout-ms', errors.getvalue())
                self.assertNotIn('--recover', output.getvalue())
                self.assertTrue(all(ident == bus.ids.sdo_tx for ident, _ in bus.sent))
                self.assertFalse(any(call.args[0] == 'ota_detected' for call in trace.return_value.call_args_list))
                confirm.assert_not_called()

    def test_health_fails_on_timeout_or_counter_growth(self):
        for timeout in (True, False):
            bus, clock = FakeBus(), [0.0]
            def silence(duration=0):
                FakeBus.guard_silence(bus, duration)
                clock[0] += duration
            before, after = copy.deepcopy(LINK), copy.deepcopy(LINK)
            before['stats64'] = {'rx': {'errors': 0}}
            after['stats64'] = {'rx': {'errors': 1}}
            if timeout:
                bus.receive = lambda duration: None
            with patch('ota.snapshot', side_effect=[before, after]), \
                    patch('ota.time.monotonic', side_effect=lambda: clock[0]), \
                    patch.object(bus, 'guard_silence', side_effect=silence), redirect_stdout(io.StringIO()), \
                    self.assertRaises(ota.Failure):
                ota.health(ota.Hand(bus), 'can0', 1, 10)
            report = next(fields for event, fields in bus.events if event == 'health_summary')
            self.assertEqual(report['status'], 'failed')

    def test_maintenance_locks_conflict_and_release(self):
        interface = f'otatest{os.getpid()}'[:15]
        path = Path(f'/tmp/dh116s-ota-{interface}.lock')
        other_interface = f'other{os.getpid()}'[:15]
        other_path = Path(f'/tmp/dh116s-ota-{other_interface}.lock')
        try:
            with ota.maintenance(interface):
                with self.assertRaisesRegex(ota.Failure, 'in use'):
                    with ota.maintenance(interface):
                        self.fail('Two exclusive sessions acquired the same interface')
                with ota.maintenance(other_interface):
                    pass
            with ota.maintenance(interface):
                pass
        finally:
            path.unlink(missing_ok=True)
            other_path.unlink(missing_ok=True)

    def test_cpp_sdk_and_python_ota_share_the_interface_lock(self):
        if not LOCK_HELPER:
            self.skipTest('Run through CTest or supply --lock-helper to test the C++ transport lock')
        interface = f'otacpp{os.getpid()}'[:15]
        path = Path(f'/tmp/dh116s-ota-{interface}.lock')
        try:
            with subprocess.Popen([LOCK_HELPER, '--hold-interface-lock', interface],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True) as holder:
                try:
                    import select
                    self.assertTrue(select.select([holder.stdout], [], [], 5)[0], 'SDK lock helper timed out')
                    self.assertEqual(holder.stdout.readline().strip(), 'LOCKED')
                    with self.assertRaises(ota.Failure):
                        with ota.maintenance(interface):
                            self.fail('OTA entered while SDK holds the interface')
                    # Other SDK sessions may share the bus (e.g. a different node).
                    other = subprocess.run([LOCK_HELPER, '--hold-interface-lock', interface],
                                          input='\n', capture_output=True, text=True, timeout=5)
                    self.assertEqual(other.returncode, 0)
                finally:
                    holder.communicate('\n', timeout=5)
            with ota.maintenance(interface):
                blocked = subprocess.run([LOCK_HELPER, '--hold-interface-lock', interface],
                                         input='\n', capture_output=True, text=True, timeout=5)
                self.assertNotEqual(blocked.returncode, 0)
            released = subprocess.run([LOCK_HELPER, '--hold-interface-lock', interface],
                                      input='\n', capture_output=True, text=True, timeout=5)
            self.assertEqual(released.returncode, 0)
        finally:
            path.unlink(missing_ok=True)

    def test_transport_with_real_unix_socket_pair(self):
        local, peer = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        local.setblocking(False)
        peer.settimeout(2)
        bus = object.__new__(ota.CanBus)
        bus.ids = IDS
        bus.sock, bus.trace = local, lambda *args, **kwargs: None
        errors = []

        def respond():
            try:
                for _ in range(9):
                    ident, request = ota.decode_frame(peer.recv(72))
                    self.assertEqual(ident, IDS.sdo_tx)
                    self.assertEqual(request[0], 0x40)
                    reply = sdo_reply(request)
                    peer.send(struct.pack('=IB3x8s', ota.ERR | 4, 8, bytes.fromhex('0004000000000000')))
                    peer.send(ota.FRAME.pack(IDS.sdo_rx, 8, 1, 0, 0, reply.ljust(64, b'\0')))
            except BaseException as exc:
                errors.append(exc)

        worker = threading.Thread(target=respond)
        worker.start()
        try:
            self.assertEqual(ota.Hand(bus).probe()['serial'], '176000066002607130017')
        finally:
            bus.close()
            worker.join(3)
            peer.close()
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])

    def test_ota_transport_with_real_unix_socket_pair(self):
        local, peer = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        local.setblocking(False)
        peer.settimeout(2)
        bus = object.__new__(ota.CanBus)
        bus.ids = IDS
        bus.sock, bus.trace = local, MagicMock()
        bus.reconnect = MagicMock()
        errors, received = [], bytearray()
        prepare_calls = []
        firmware = bytes(range(256)) * 4

        def respond():
            try:
                while True:
                    ident, packet = ota.decode_frame(peer.recv(72))
                    self.assertEqual(ident, IDS.ota_tx)
                    self.assertEqual(len(packet), 64)
                    command = packet[3]
                    if command == 0x53:
                        prepare_calls.append(packet)
                        if len(prepare_calls) == 1:
                            for status in (4, 0x10):
                                peer.send(struct.pack('=IB3x8s', ota.ERR | 4, 8,
                                                      bytes((0, status)) + bytes(6)))
                            continue
                    if command in (0xaa, 0xab):
                        received.extend(packet[5:37])
                        if packet[4] != 15 and command != 0xab:
                            continue
                        # A recovery notification can arrive during data transfer too.
                        peer.send(struct.pack('=IB3x8s', ota.ERR | 4, 8, bytes.fromhex('0040000000000000')))
                    status = {0x50: 0, 0x51: 2, 0x52: 122, 0x53: 0x5a, 0xaa: 0x5a, 0xab: 0x5a}[command]
                    ack = bytes((0xff, 4, 0, 0, command, status, 0, 0))
                    peer.send(ota.FRAME.pack(IDS.ota_rx, 8, 1, 0, 0, ack.ljust(64, b'\0')))
                    if command == 0xab:
                        break
            except BaseException as exc:
                errors.append(exc)

        worker = threading.Thread(target=respond)
        worker.start()
        try:
            with redirect_stdout(io.StringIO()):
                ota.Hand(bus, ota_timeout=.2, packet_gap=.001).upgrade(firmware, ota.BUNDLED_FILES[2])
        finally:
            bus.close()
            worker.join(3)
            peer.close()
        self.assertFalse(worker.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(received, firmware)
        self.assertEqual(len(prepare_calls), 2)
        bus.reconnect.assert_called_once()
        self.assertEqual(sum(call.args[0] == 'can_state' for call in bus.trace.call_args_list), 4)


if __name__ == '__main__':
    if '--lock-helper' in sys.argv:
        index = sys.argv.index('--lock-helper')
        LOCK_HELPER = sys.argv[index + 1]
        del sys.argv[index:index + 2]
    unittest.main()
