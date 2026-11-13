#!/usr/bin/env python3
"""DH116S CANFD maintenance over preconfigured Linux SocketCAN (no SDK/ROS required)."""
import argparse
from contextlib import contextmanager, ExitStack
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import select
import socket
import statistics
import stat
import struct
import subprocess
import sys
import time

try:
    import fcntl
except ImportError:
    fcntl = None

ROOT = Path(__file__).resolve().parents[2]
SIDES = {1: 'Right', 2: 'Left'}
BUNDLED_FILES = {
    1: 'DH116SR-CANFD-APP-H6E001-V1.22.upd',
    2: 'DH116SL-CANFD-APP-H6E001-V1.22.upd',
}
KNOWN_SHA256 = {
    'DH116SR-CANFD-APP-H6E001-V1.22.upd': '9d1264863a0464f59a32445ccc91bdcc0e6e357febb1085d87b9f010b75daaac',
    'DH116SL-CANFD-APP-H6E001-V1.22.upd': '21f6ab15342844190bbe7e2f98e36a74bc5528408c327eca84ec682c18a1db37',
}
EFF, RTR, ERR = 0x80000000, 0x40000000, 0x20000000
FRAME = struct.Struct('=IBBBB64s')


class CanIds:
    def __init__(self, node_id):
        if type(node_id) is not int or not 1 <= node_id <= 127:
            raise ValueError('node-id must be in 1..127')
        self.node_id = node_id
        self.sdo_tx, self.sdo_rx = 0x600 + node_id, 0x580 + node_id
        self.ota_tx, self.ota_rx = EFF | (0x9000 + node_id), EFF | (0x7000 + node_id)
        # CAN_ERR_FLAG in the mask selects error frames, not normal frames.
        self.filters = b''.join(struct.pack('=II', ident, mask) for ident, mask in (
            (self.sdo_rx, 0x7ff | EFF | RTR), (self.sdo_tx, 0x7ff | EFF | RTR),
            (self.ota_rx, 0x1fffffff | EFF | RTR), (self.ota_tx, 0x1fffffff | EFF | RTR)))


class Failure(RuntimeError):
    pass


class ResponseTimeout(Failure):
    pass


def firmware(path):
    path = Path(path)
    match = re.fullmatch(r'DH116S([LR])-CANFD-APP-([A-Z0-9]+)-V([0-9]+)\.([0-9]{2})\.upd', path.name)
    if not match:
        raise Failure('Expected original filename DH116SL/R-CANFD-APP-<hardware>-V<major>.<minor:02>.upd')
    if len(path.name) + 5 > 64:
        raise Failure('Firmware filename exceeds the OTA trigger frame capacity')
    data = path.read_bytes()
    if not data:
        raise Failure('Empty firmware')
    digest = hashlib.sha256(data).hexdigest()
    expected = KNOWN_SHA256.get(path.name)
    if expected is not None and digest != expected:
        raise Failure(f'Firmware SHA-256 mismatch: {path.name}')
    return dict(name=path.name, path=str(path.resolve()), data=data,
                side_raw=2 if match[1] == 'L' else 1,
                version_raw=int(match[3]) * 100 + int(match[4]),
                version=f'{int(match[3])}.{match[4]}', sha256=digest,
                checksum_verified=expected is not None)


def select_firmware(identity, path=None):
    if identity['version_raw'] <= 120:
        raise Failure('Upgrade requires the current firmware version to be greater than 1.20')
    image = firmware(path if path else ROOT / 'firmware' / BUNDLED_FILES[identity['side_raw']])
    if image['side_raw'] != identity['side_raw']:
        raise Failure(f'Firmware is for {SIDES[image["side_raw"]]}, but the connected hand is {identity["side"]}')
    if image['version_raw'] < identity['version_raw']:
        raise Failure(f'Downgrade rejected: {identity["firmware"]} -> {image["version"]}; '
                      'select a matching or newer image with --firmware')
    return image


def encode_frame(ident, data):
    if len(data) > 64:
        raise Failure('CAN FD payload exceeds 64 bytes')
    return FRAME.pack(ident, 64, 1, 0, 0, data.ljust(64, b'\0'))


def decode_frame(raw):
    if len(raw) not in (16, 72):
        raise Failure('Malformed SocketCAN frame length')
    ident, size = struct.unpack_from('=IB', raw)
    if ident & RTR or size > (8 if len(raw) == 16 else 64):
        raise Failure('Unexpected RTR or invalid CAN payload length')
    return ident, raw[8:8 + size]


def decode_sdo(data, index, sub):
    if len(data) != 8 or data[1:4] != struct.pack('<HB', index, sub):
        raise Failure('Unmatched/malformed SDO response; stop without retry')
    if data[0] == 0x80:
        raise Failure(f'SDO abort 0x{int.from_bytes(data[4:8], "little"):08x}')
    size = {0x4f: 1, 0x4b: 2, 0x43: 4}.get(data[0])
    if size is None:
        raise Failure(f'Unexpected SDO command 0x{data[0]:02x}')
    return int.from_bytes(data[4:4 + size], 'little')


def data_batches(data):
    count = (len(data) + 31) // 32
    for start in range(0, count, 16):
        batch = []
        for number in range(start, min(start + 16, count)):
            final = number == count - 1
            header = bytes((0xff, 0x23, 0, 0xab if final else 0xaa, number - start))
            batch.append(header + data[number * 32:(number + 1) * 32].ljust(32, b'\0'))
        yield start, batch, start + len(batch) == count


class Trace:
    def __init__(self, directory, command, interface, node_id):
        directory = Path(directory).resolve()
        directory.mkdir(parents=True, exist_ok=True)
        self.path = directory / f'{datetime.now():%Y%m%d-%H%M%S-%f}-{command}-{interface}-{node_id}-{os.getpid()}.jsonl'
        self.stream = self.path.open('x', buffering=1)
        print('Log:', self.path, flush=True)

    def __call__(self, event, **fields):
        self.stream.write(json.dumps(dict(time=datetime.now().astimezone().isoformat(),
                                          monotonic_ns=time.monotonic_ns(), event=event, **fields)) + '\n')

    def close(self):
        self.stream.close()


class CanBus:
    def __init__(self, interface, node_id, trace):
        self.interface, self.trace = interface, trace
        self.ids = CanIds(node_id)
        self.index = socket.if_nametoindex(interface)
        self.sock = None
        self.open()

    def open(self):
        if socket.if_nametoindex(self.interface) != self.index:
            raise Failure('CAN interface was replaced during this session')
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        try:
            sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FD_FRAMES, 1)
            sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS, 0)
            sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, self.ids.filters)
            # CAN_RAW_ERR_FILTER = 2 in Linux UAPI; some Python builds omit the name.
            sock.setsockopt(socket.SOL_CAN_RAW, 2, struct.pack('=I', 0x1fffffff))
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 262144)
            sock.setsockopt(socket.SOL_SOCKET, 40, 1)  # Linux SO_RXQ_OVFL.
            sock.bind((self.interface,))
            sock.setblocking(False)
        except BaseException:
            sock.close()
            raise
        self.sock = sock

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def reconnect(self):
        self.close()
        time.sleep(0.2)
        self.open()
        self.trace('socket_reopened', interface=self.interface)

    def send(self, ident, data):
        if not select.select([], [self.sock], [], 0.5)[1]:
            raise Failure('CAN send readiness timed out; frame not retried')
        raw = encode_frame(ident, data)
        if self.sock.send(raw) != len(raw):
            raise Failure('Short CAN write; frame not retried')
        self.trace('tx', can_id=f'0x{ident:x}', data=data.hex(), wire_length=64)

    def receive(self, timeout):
        timeout = max(0, timeout)
        deadline = time.monotonic() + timeout
        while timeout == 0 or time.monotonic() < deadline:
            if not select.select([self.sock], [], [], max(0, deadline - time.monotonic()))[0]:
                return None
            raw, ancillary, flags, _ = self.sock.recvmsg(72, 64)
            if flags & (socket.MSG_TRUNC | socket.MSG_CTRUNC):
                raise Failure('Truncated CAN frame or ancillary data')
            for level, kind, value in ancillary:
                if level == socket.SOL_SOCKET and kind == 40 and struct.unpack('=I', value)[0]:
                    raise Failure('Socket receive queue overflow; stop without retry')
            ident, data = decode_frame(raw)
            if ident & ERR:
                # Linux CAN_ERR_CRTL (+ optional CAN_ERR_CNT): warning/passive/active, not overflow.
                state_change = (ident & 0x4 and not ident & ~(ERR | 0x204) and len(data) == 8
                                and data[1] & 0x7c and not data[1] & ~0x7c)
                if state_change:
                    self.trace('can_state', can_id=f'0x{ident:x}', data=data.hex())
                    continue
                self.trace('can_error', can_id=f'0x{ident:x}', data=data.hex())
                raise Failure(f'CAN error frame: id=0x{ident:x}, data={data.hex()}')
            self.trace('rx', can_id=f'0x{ident:x}', data=data.hex())
            if ident in (self.ids.sdo_tx, self.ids.ota_tx):
                raise Failure('Other sender/request echo detected; require exclusive CAN access')
            return ident, data
        return None

    def guard_silence(self, duration=0):
        if self.receive(duration) is not None:
            raise Failure('Unsolicited/early response; cannot safely correlate transactions')


class Hand:
    def __init__(self, bus, sdo_timeout=0.5, ota_timeout=1, packet_gap=0.001):
        self.bus = bus
        self.ids = bus.ids
        self.sdo_timeout, self.ota_timeout, self.packet_gap = sdo_timeout, ota_timeout, packet_gap

    def read(self, index, sub):
        self.bus.guard_silence()
        start = time.monotonic()
        self.bus.send(self.ids.sdo_tx, b'\x40' + struct.pack('<HB', index, sub) + bytes(4))
        reply = self.bus.receive(max(0, self.sdo_timeout - (time.monotonic() - start)))
        elapsed = time.monotonic() - start
        if reply is None or elapsed > self.sdo_timeout:
            raise ResponseTimeout(f'SDO 0x{index:04x}:{sub:02x} timed out; no retry')
        if reply[0] != self.ids.sdo_rx:
            raise Failure('Unexpected CAN ID during SDO query')
        return decode_sdo(reply[1], index, sub), elapsed * 1000

    def probe(self):
        model, _ = self.read(0x200e, 0x1d)
        try:
            side, _ = self.read(0x2030, 5)
            version, _ = self.read(0x200e, 0x1e)
            if model != 3 or side not in SIDES or version == 0:
                raise Failure(f'Expected DH116S CANFD with a valid direction/version; got {model=}, {side=}, {version=}')
            words = [self.read(index, 6)[0] for index in (0x2030, 0x2070, 0x20b0, 0x20f0, 0x2130, 0x2170)]
        except ResponseTimeout as exc:
            raise Failure(f'Application SDO responded, but identity reading is incomplete: {exc}. '
                          'Retry with a longer --sdo-timeout-ms; this does not establish a need for OTA recovery.') from exc
        if any(word > 0xffff for word in words) or not any(words):
            raise Failure('Invalid hand serial number')
        return dict(model_raw=model, side=SIDES[side], side_raw=side, version_raw=version,
                    firmware=f'{version // 100}.{version % 100:02d}',
                    serial=f'{words[0]:X}' + ''.join(f'{word:04X}' for word in words[1:]))

    def wait_ack(self, command, strict=False):
        deadline = time.monotonic() + self.ota_timeout
        while time.monotonic() < deadline:
            reply = self.bus.receive(deadline - time.monotonic())
            if reply is None or time.monotonic() > deadline:
                break
            ident, data = reply
            if ident != self.ids.ota_rx or len(data) < 6 or data[0] != 0xff:
                raise Failure('Malformed/unexpected OTA response')
            if data[4] != command:
                if strict:
                    raise Failure(f'Expected OTA ACK 0x{command:02x}, got 0x{data[4]:02x}')
                continue
            return data
        raise ResponseTimeout(f'OTA command 0x{command:02x} timed out after {self.ota_timeout:g}s')

    def command(self, command, argument, expected=None):
        payload = b'\xff' + struct.pack('<H', len(argument) + 2) + bytes((command,)) + argument
        for attempt in range(10):
            try:
                self.bus.send(self.ids.ota_tx, payload)
                reply = self.wait_ack(command)
            except (ResponseTimeout, OSError) as exc:
                if isinstance(exc, OSError) and not (command == 0x53 and attempt == 0):
                    raise
                failure = exc
            else:
                if expected is None or reply[5] == expected:
                    return reply
                failure = Failure(f'OTA command 0x{command:02x} rejected: status 0x{reply[5]:02x}')
                if command == 0x50 and reply[5] == 0xee:
                    raise failure
            if attempt == 9:
                raise failure
            # main_ota reopens once after any failed first prepare attempt, including a non-ready ACK.
            if command == 0x53 and attempt == 0:
                print(f'OTA prepare: {failure}; reconnecting CAN socket and retrying.', flush=True)
                self.bus.reconnect()
            self.bus.trace('handshake_retry', command=command, next_attempt=attempt + 2, reason=str(failure))
        raise AssertionError('Unreachable handshake state')

    def upgrade(self, data, filename):
        if not data:
            raise Failure('Empty firmware')
        self.bus.guard_silence()
        self.command(0x50, b'\0' + filename.encode('ascii'), expected=0)
        product = self.command(0x51, b'\0')
        version = self.command(0x52, b'\0')
        if len(version) < 7:
            raise Failure('Truncated bootloader version response')
        # Boot product codes and version scaling are not the application's SDO identity fields.
        self.bus.trace('bootloader_info', product=product.hex(), version=version.hex(),
                       version_raw=int.from_bytes(version[5:7], 'little'))
        self.command(0x53, b'\x02', expected=0x5a)
        total = (len(data) + 31) // 32
        last_percent = -1
        for start, packets, final in data_batches(data):
            for packet in packets:
                self.bus.send(self.ids.ota_tx, packet)
                time.sleep(self.packet_gap)
            try:
                # Like main_ota, ignore delayed handshake replies while waiting for a regular batch ACK.
                ack = self.wait_ack(0xab if final else 0xaa, strict=final)
            except ResponseTimeout as exc:
                raise ResponseTimeout(f'Batch {start}..{start + len(packets) - 1}: {exc}; NOT retransmitted') from exc
            if ack[5] != 0x5a:
                raise Failure(f'Firmware write failed at packet {start}: status 0x{ack[5]:02x}')
            percent = (start + len(packets)) * 100 // total
            if percent != last_percent:
                print(f'OTA {percent:3d}% ({start + len(packets)}/{total})', flush=True)
                last_percent = percent
        self.bus.trace('transfer_complete', packets=total, final_ack=ack.hex())
        print('Transfer complete: final AB/5A ACK received. Run probe after device restart.')


def snapshot(interface):
    result = subprocess.run(['ip', '-details', '-statistics', '-json', 'link', 'show', 'dev', interface],
                            check=True, capture_output=True, text=True, timeout=5)
    return json.loads(result.stdout)[0]


def validate_link(info):
    can = info.get('linkinfo', {}).get('info_data', {})
    if (info.get('linkinfo', {}).get('info_kind') != 'can' or 'UP' not in info.get('flags', [])
            or info.get('mtu') != 72):
        raise Failure('Interface must be CAN, UP and MTU 72; configure it in the operating system')
    states = ('ERROR-ACTIVE', 'ERROR-WARNING', 'ERROR-PASSIVE')
    if can.get('state') not in states:
        raise Failure(f'CAN state is {can.get("state")}; expected {" or ".join(states)}')
    if ('FD' not in can.get('ctrlmode', [])
            or {'LOOPBACK', 'LISTEN-ONLY'} & set(can.get('ctrlmode', []))):
        raise Failure('Expected FD without loopback/listen-only')
    for name, rate in (('bittiming', 1000000), ('data_bittiming', 5000000)):
        timing = can.get(name, {})
        if timing.get('bitrate') != rate:
            raise Failure('Expected CAN FD 1M/5M; configure rates in the operating system')


def print_link(info):
    can = info['linkinfo']['info_data']
    points = [can.get(key, {}).get('sample_point') for key in ('bittiming', 'data_bittiming')]
    print(f'Interface: {info["ifname"]}, UP, {can["state"]}, CAN FD 1M/5M; '
          f'sample points: {points[0]} / {points[1]}')


def counters(info):
    stats = info.get('stats64', info.get('stats', {}))
    result = {f'{direction}.{key}': stats.get(direction, {}).get(key)
              for direction in ('rx', 'tx') for key in ('packets', 'errors', 'dropped', 'over_errors')}
    result.update({f'can.{key}': info.get('linkinfo', {}).get('info_xstats', {}).get(key)
                   for key in ('bus_error', 'bus_off', 'error_warning', 'error_passive', 'restarts', 'arbitration_lost')})
    return result


def health(hand, interface, duration, rate):
    before = counters(snapshot(interface))
    times, attempted, failure = [], 0, None
    start = time.monotonic()
    try:
        while time.monotonic() - start < duration:
            sent = time.monotonic()
            attempted += 1
            model, elapsed = hand.read(0x200e, 0x1d)
            if model != 3:
                raise Failure('Model changed during health check')
            times.append(elapsed)
            hand.bus.guard_silence(max(0, min(start + duration, sent + 1 / rate) - time.monotonic()))
    except (Failure, OSError) as exc:
        failure = exc
    after = snapshot(interface)
    delta = {key: None if value is None or counters(after).get(key) is None else counters(after)[key] - value
             for key, value in before.items()}
    if failure is None:
        validate_link(after)
        if any(value is not None and value > 0 for key, value in delta.items()
               if key.endswith(('.errors', '.dropped', '.over_errors')) or key in ('can.bus_error', 'can.bus_off')):
            failure = Failure('CAN error/drop counters changed during health check')
    ordered = sorted(times)
    report = dict(status='failed' if failure else 'completed', attempted=attempted, successful=len(times),
                  success_rate=len(times) / attempted if attempted else None,
                  elapsed_s=time.monotonic() - start, error=str(failure) if failure else None,
                  rtt_ms=dict(p50=statistics.median(times) if times else None,
                              p95=ordered[max(0, math.ceil(len(times) * .95) - 1)] if times else None,
                              max=max(times) if times else None), counter_delta=delta)
    hand.bus.trace('health_summary', **report)
    print(json.dumps(report, indent=2))
    if failure:
        raise failure


@contextmanager
def maintenance(interface):
    # Shared with the SDK's Linux transport and the original standalone OTA tool.
    name = f'/tmp/dh116s-ota-{interface}.lock'
    try:
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    except FileNotFoundError:
        try:
            fd = os.open(name, os.O_RDONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, 0o644)
        except FileExistsError:
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise Failure('CAN maintenance lock must be a regular file')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise Failure('Interface is in use by an SDK/OTA session; stop it before maintenance') from exc
        yield
    finally:
        os.close(fd)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, help_text in (
            ('check', 'Validate firmware/identity and run a read-only communication test'),
            ('upgrade', 'Flash a matching DH116S CANFD image (current version must be > 1.20)'),
            ('probe', 'Read identity/version only, including after a device restart')):
        command = commands.add_parser(name, help=help_text)
        command.add_argument('--interface', required=True, help='Preconfigured SocketCAN interface')
        command.add_argument('--node-id', type=int, required=True, help='Hand node ID (1..127)')
        command.add_argument('--log-dir', default='logs/ota', help='Writable log directory (default: ./logs/ota)')
        command.add_argument('--sdo-timeout-ms', type=float, default=500)
        if name != 'probe':
            command.add_argument('--firmware', help='Original .upd file; defaults to the bundled image for the detected side')
        if name == 'check':
            command.add_argument('--duration', type=float, default=30, help='Test duration in seconds (default: 30)')
            command.add_argument('--rate', type=float, default=10, help='Queries per second (default: 10)')
        if name == 'upgrade':
            command.add_argument('--recover', action='store_true',
                                 help='Restart interrupted OTA without application SDO identity; requires --firmware')
            command.add_argument('--ota-timeout-ms', type=float, default=1000)
            command.add_argument('--packet-gap-ms', type=float, default=1)
    args = parser.parse_args(argv)
    if args.command == 'upgrade' and args.recover and not args.firmware:
        parser.error('--recover requires --firmware with the original image for this hand')
    if sys.platform != 'linux' or fcntl is None:
        parser.error('OTA maintenance requires Linux SocketCAN')
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.:-]{0,14}', args.interface):
        parser.error('Invalid SocketCAN interface name')
    if not 1 <= args.node_id <= 127:
        parser.error('node-id must be in 1..127')
    for name, low, high in (('sdo_timeout_ms', 50, 3000), ('ota_timeout_ms', 1000, 10000),
                             ('packet_gap_ms', 1, 5), ('duration', 1, 600), ('rate', .1, 10)):
        value = getattr(args, name, low)
        if not math.isfinite(value) or not low <= value <= high:
            parser.error(f'{name} must be finite and in {low}..{high}')
    return args


def main(argv=None):
    args = parse_args(argv)
    recover = args.command == 'upgrade' and args.recover
    trace, bus, outcome = None, None, 'failed'
    started_ota = False
    try:
        if args.command == 'upgrade' and not sys.stdin.isatty():
            raise Failure('Upgrade requires an interactive terminal and typed confirmation')
        with ExitStack() as session:
            session.enter_context(maintenance(args.interface))
            trace = Trace(args.log_dir, args.command, args.interface, args.node_id)
            trace('start', arguments=vars(args))
            before = snapshot(args.interface)
            trace('interface_before', interface=before)
            validate_link(before)
            print_link(before)
            bus = CanBus(args.interface, args.node_id, trace)
            session.callback(bus.close)
            hand = Hand(bus, args.sdo_timeout_ms / 1000,
                        getattr(args, 'ota_timeout_ms', 1000) / 1000,
                        getattr(args, 'packet_gap_ms', 1) / 1000)
            bus.guard_silence(.2)
            identity = None
            if recover:
                print(f'Recovery target: {args.interface}, node {args.node_id}\n'
                      'Application identity is unavailable in recovery mode.\n'
                      'Hand side comes from the selected file; current version and serial are not verified.\n'
                      'Use the original image for this hand. The entire image will be sent from packet 0.')
                trace('recovery_mode', identity_verified=False)
            else:
                try:
                    identity = hand.probe()
                except ResponseTimeout as exc:
                    # Read-only product query: distinguish OTA reachability from an unresponsive target.
                    bus.send(bus.ids.ota_tx, b'\xff\x03\0\x51\0')
                    try:
                        reply = hand.wait_ack(0x51)
                    except ResponseTimeout:
                        raise ResponseTimeout(f'{exc}; OTA product query also timed out') from exc
                    trace('ota_detected', response=reply.hex(), application_error=str(exc))
                    outcome = 'recovery_required'
                    if args.command == 'check':
                        trace('check_result', ready_to_upgrade=False, recovery_available=True)
                    print('OTA query responded; application identity is unavailable.')
                    print('For an interrupted upgrade, use the original image for this hand:')
                    print(f'  /usr/bin/python3 -B {Path(__file__).resolve()} upgrade '
                          f'--interface {args.interface} --node-id {args.node_id} '
                          '--recover --firmware <original.upd>')
                    return 2
                trace('identity', **identity)
                print(f'Target: {args.interface}, node {args.node_id}, DH116S {identity["side"]}\n'
                      f'Firmware: {identity["firmware"]}\nSerial: {identity["serial"]}')
            if args.command != 'probe':
                image = firmware(args.firmware) if recover else select_firmware(identity, args.firmware)
                trace('firmware_selected', **{key: value for key, value in image.items() if key != 'data'},
                      size=len(image['data']))
                print(f'Selected firmware: {image["name"]} ({len(image["data"])} bytes), {SIDES[image["side_raw"]]}\n'
                      f'Flash: {identity["firmware"] if identity else "unknown"} -> {image["version"]}\n'
                      f'SHA-256: {image["sha256"]}')
                print('Checksum matches the bundled record.' if image['checksum_verified'] else
                      'SHA-256 recorded; no reference checksum is available for this image.')
            if args.command == 'check':
                health(hand, args.interface, args.duration, args.rate)
                trace('check_result', ready_to_upgrade=True)
                print('CHECK PASSED: Ready to upgrade.')
                print(f'Run upgrade separately for {args.interface}, node {args.node_id}; '
                      'the target identity will be checked again before flashing.')
            elif args.command == 'upgrade':
                print('Same-version reflash is allowed. Keep power/CAN connected.')
                action = 'RECOVER' if recover else 'UPGRADE'
                confirmation = f'{action} {SIDES[image["side_raw"]].upper()} {args.interface} {args.node_id}'
                if input(f'Type {confirmation} to begin: ') != confirmation:
                    outcome = 'cancelled'
                    trace('cancelled')
                    print('Cancelled before OTA.')
                    return 0
                validate_link(snapshot(args.interface))
                if socket.if_nametoindex(args.interface) != bus.index:
                    raise Failure('CAN interface was replaced during confirmation')
                if not recover and hand.probe() != identity:
                    raise Failure('Hand identity/version changed during confirmation')
                started_ota = True
                trace('ota_begin')
                hand.upgrade(image['data'], image['name'])
            outcome = 'completed'
            return 0
    except (Failure, OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt, EOFError) as exc:
        error = str(exc) or type(exc).__name__
        print('FAILED:', error, file=sys.stderr)
        if args.command == 'check':
            print('CHECK FAILED: Normal upgrade readiness could not be confirmed; see the error above.', file=sys.stderr)
            if trace:
                trace('check_result', ready_to_upgrade=False, reason=error)
        if started_ota:
            print('OTA may be incomplete. Keep power connected, retain this log; no automatic rollback/reflash.', file=sys.stderr)
        if trace:
            trace('failure', error=error, ota_started=started_ota)
        return 1
    finally:
        if trace:
            try:
                trace('finish', status=outcome, interface_after=snapshot(args.interface))
            except Exception as exc:
                trace('finish', status=outcome, interface_read_error=str(exc))
            trace.close()


if __name__ == '__main__':
    raise SystemExit(main())
