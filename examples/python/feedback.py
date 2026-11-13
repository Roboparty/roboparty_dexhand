#!/usr/bin/env python3
"""Read/set DH116S feedback timing and measure feedback over Linux SocketCAN."""
import argparse
from contextlib import ExitStack
from decimal import Decimal, InvalidOperation
import math
import re
import socket
import statistics
import struct
import subprocess
import sys
import time

import ota

REGISTERS = tuple(0x201d + 0x40 * axis for axis in range(6))
GROUPS = {0x50: 'position', 0x5a: 'status'}


class FeedbackBus(ota.CanBus):
    def __init__(self, interface, node_id, trace):
        self.feedback_id, self.config_id = 0x480 + node_id, 0x500 + node_id

        def filtered_trace(event, **fields):
            if event != 'rx' or fields.get('can_id') != hex(self.feedback_id):
                trace(event, **fields)

        super().__init__(interface, node_id, filtered_trace)

    def open(self):
        super().open()
        try:
            filters = struct.pack('=II', self.feedback_id, 0x7ff | ota.EFF | ota.RTR)
            filters += self.ids.filters
            filters += struct.pack('=II', self.config_id, 0x7ff | ota.EFF | ota.RTR)
            self.sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, filters)
        except BaseException:
            self.close()
            raise

    def receive(self, timeout):
        frame = super().receive(timeout)
        if frame is not None and frame[0] == self.config_id:
            raise ota.Failure('Another control/configuration sender is active; stop it before changing feedback')
        return frame


def request(bus, index, sub, timeout, attempts, value=None):
    command = 0x40 if value is None else 0x2b
    address = struct.pack('<HB', index, sub)
    payload = bytes((command,)) + address + struct.pack('<I', value or 0)
    for attempt in range(1, attempts + 1):
        bus.send(bus.ids.sdo_tx, payload)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = bus.receive(deadline - time.monotonic())
            if frame is None:
                break
            ident, data = frame
            if ident == bus.feedback_id:
                continue
            if ident != bus.ids.sdo_rx:
                raise ota.Failure('Unexpected non-SDO response during parameter access')
            if len(data) != 8:
                raise ota.Failure('Malformed SDO response')
            # Replies may arrive after a preceding request has timed out.
            if data[1:4] != address:
                bus.trace('unmatched_sdo', data=data.hex())
                continue
            if data[0] == 0x80:
                raise ota.Failure(f'SDO 0x{index:04x}:{sub:02x} aborted: '
                                  f'0x{int.from_bytes(data[4:8], "little"):08x}')
            if value is None and data[0] in (0x4f, 0x4b, 0x43):
                return ota.decode_sdo(data, index, sub)
            if value is not None and data[0] == 0x60:
                return None
            bus.trace('unmatched_sdo_command', data=data.hex())
        bus.trace('sdo_timeout', index=hex(index), sub=sub, attempt=attempt, write=value is not None)
        print(f'SDO 0x{index:04x}:{sub:02x} timeout ({attempt}/{attempts})', file=sys.stderr, flush=True)
    raise ota.ResponseTimeout(f'SDO 0x{index:04x}:{sub:02x} timed out after {attempts} attempts')


def write_register(bus, axis, value, timeout, attempts):
    index = REGISTERS[axis - 1]
    ack = True
    try:
        request(bus, index, 0x14, timeout, attempts, value)
    except ota.ResponseTimeout:
        ack = False
    actual = request(bus, index, 0x14, timeout, attempts)
    if actual != value:
        raise ota.Failure(f'Axis {axis} readback mismatch: requested {value}, got {actual}')
    bus.trace('register_verified', axis=axis, index=hex(index), raw=actual, write_ack_received=ack)
    if not ack:
        print(f'Axis {axis}: write ACK missing; requested value confirmed by SDO readback.')
    return actual


def set_divider(bus, divider):
    payload = bytes((0, 4, 0x50, divider, 0x5a, divider))
    for attempt in range(3):
        bus.send(bus.config_id, payload)
        if attempt < 2:
            time.sleep(.01)
    bus.trace('divider_requested', divider=divider, groups=list(GROUPS))
    print(f'Requested position/status feedback every {divider} base cycles.')
    print('This configuration has no ACK/readback; check the measured rates below.')


def save_eeprom(bus, timeout):
    """Send the vendor save sequence once; ACKs do not prove persistence."""
    def monitor(duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            frame = bus.receive(deadline - time.monotonic())
            if frame is None or frame[0] == bus.feedback_id:
                continue
            ident, data = frame
            if ident != bus.ids.sdo_rx or len(data) != 8:
                raise ota.Failure('Unexpected/malformed response during EEPROM save')
            if data[1:3] != b'\x10\x10':
                bus.trace('unmatched_save_response', data=data.hex())
                continue
            bus.trace('save_response', data=data.hex())
            if data[0] == 0x80:
                raise ota.Failure(f'EEPROM save aborted: 0x{int.from_bytes(data[4:8], "little"):08x}')

    print('Saving SDO parameters to EEPROM; other modified drive parameters may also be saved.')
    print('Feedback divider persistence is unknown. Do not power off during this command.')
    bus.trace('save_started', persistence_verified=False)
    # Preserve the vendor-specific preamble and nonstandard save command exactly.
    bus.send(bus.ids.sdo_tx, bytes.fromhex('0010100000000000'))
    monitor(.005)
    bus.send(bus.ids.sdo_tx, bytes.fromhex('2f10100173617665'))
    monitor(max(1.0, timeout))
    bus.trace('save_sequence_sent', persistence_verified=False)


def observe(bus, duration):
    stamps = {group: [] for group in GROUPS}
    start = time.monotonic()
    deadline = start + duration
    while time.monotonic() < deadline:
        frame = bus.receive(deadline - time.monotonic())
        now = time.monotonic()
        if frame is None or now > deadline:
            continue
        ident, data = frame
        if ident != bus.feedback_id:
            continue
        if not data or data[0] not in GROUPS:
            continue
        group = data[0]
        slots = data[1] if len(data) >= 2 else 0
        valid = (6 <= slots <= 8 and len(data) >= 50 + slots) if group == 0x50 else (
            6 <= slots <= 31 and len(data) >= 2 + 2 * slots)
        if not valid:
            raise ota.Failure(f'Malformed feedback group 0x{group:02x}: {data.hex()}')
        stamps[group].append(now)
    elapsed = time.monotonic() - start
    groups = []
    for group, values in stamps.items():
        gaps = sorted((b - a) * 1000 for a, b in zip(values, values[1:]))
        groups.append(dict(group=hex(group), name=GROUPS[group], frames=len(values),
                           hz=len(values) / elapsed if elapsed else 0,
                           mean_interval_ms=statistics.mean(gaps) if gaps else None,
                           p50_ms=statistics.median(gaps) if gaps else None,
                           p95_ms=gaps[max(0, math.ceil(len(gaps) * .95) - 1)] if gaps else None,
                           max_ms=max(gaps) if gaps else None))
    return dict(elapsed_s=elapsed, groups=groups)


def print_measurement(report):
    print(f'Host receive measurement: {report["elapsed_s"]:.3f} s (USB batching affects intervals)')
    print('Group       Frames       Hz    Mean ms     P50 ms     P95 ms     Max ms')
    for row in report['groups']:
        intervals = ' '.join(f'{row[key]:10.3f}' if row[key] is not None else f'{"-":>10}'
                             for key in ('mean_interval_ms', 'p50_ms', 'p95_ms', 'max_ms'))
        print(f'{row["name"]:<10} {row["frames"]:7d} {row["hz"]:8.2f} {intervals}')


def register_ms(text):
    try:
        milliseconds = Decimal(text)
        if not milliseconds.is_finite() or not Decimal('0.1') <= milliseconds <= Decimal('6553.5'):
            raise ValueError
        value = milliseconds * 10
        if value != value.to_integral_value():
            raise ValueError
        return int(value)
    except (InvalidOperation, ValueError):
        raise argparse.ArgumentTypeError('register-ms must be 0.1..6553.5 in exact 0.1 ms steps') from None


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name, description in (
            ('read-sdo', 'Read SDO timing registers only'),
            ('set-sdo', 'Write and verify SDO timing registers; optionally save to EEPROM'),
            ('read-feedback', 'Passively measure actual feedback periods and rates'),
            ('set-feedback', 'Set feedback every N base cycles, then measure')):
        command = commands.add_parser(name, help=description)
        command.add_argument('--can', '--interface', dest='interface', default='can0',
                             help='SocketCAN interface (default: can0)')
        command.add_argument('--id', '--node-id', dest='node_id', type=int, default=1,
                             help='Node ID (default: 1)')
        command.add_argument('--log-dir', default='logs/feedback')
        if name.endswith('-sdo'):
            command.add_argument('--axis', type=int, default=0, help='0=all six axes, or 1..6')
            command.add_argument('--sdo-timeout-ms', type=float, default=500)
            command.add_argument('--attempts', type=int, default=3, help='Attempts per SDO request (default: 3)')
        else:
            command.add_argument('--duration', type=float, default=5, help='Measurement seconds (default: 5)')
            command.add_argument('--settle', type=float, default=1,
                                 help='Drain feedback before measurement, seconds (default: 1)')
        if name == 'set-sdo':
            command.add_argument('register_raw', type=register_ms, metavar='MS',
                                 help='SDO period in ms, encoded in 0.1 ms units; not measured upload period')
            command.add_argument('--save', action='store_true',
                                 help='Save verified six-axis SDO parameters to EEPROM; '
                                      'power-cycle verification is required')
        if name == 'set-feedback':
            command.add_argument('divider', type=int, metavar='N',
                                 help='Upload each group every N base cycles (1..255)')
    args = parser.parse_args(argv)
    if sys.platform != 'linux' or ota.fcntl is None:
        parser.error('Linux SocketCAN is required')
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.:-]{0,14}', args.interface):
        parser.error('Invalid SocketCAN interface name')
    if not 1 <= args.node_id <= 127:
        parser.error('node-id must be in 1..127')
    for name, low, high in (('duration', .2, 120), ('settle', 0, 30), ('sdo_timeout_ms', 50, 3000)):
        value = getattr(args, name, None)
        if value is not None and (not math.isfinite(value) or not low <= value <= high):
            parser.error(f'{name} must be finite and in {low}..{high}')
    if not 0 <= getattr(args, 'axis', 0) <= 6 or not 1 <= getattr(args, 'attempts', 3) <= 10:
        parser.error('axis must be 0..6; attempts must be 1..10')
    if args.command == 'set-feedback' and not 1 <= args.divider <= 255:
        parser.error('divider must be in 1..255')
    if args.command == 'set-sdo' and args.save and args.axis != 0:
        parser.error('--save requires all six axes (--axis 0)')
    return args


def main(argv=None):
    args = parse_args(argv)
    trace, changed, save_started = None, False, False
    try:
        with ExitStack() as stack:
            stack.enter_context(ota.maintenance(args.interface))
            trace = ota.Trace(args.log_dir, 'feedback-' + args.command, args.interface, args.node_id)
            trace('start', arguments=vars(args))
            before = ota.snapshot(args.interface)
            ota.validate_link(before)
            ota.print_link(before)
            trace('interface_before', interface=before)
            bus = FeedbackBus(args.interface, args.node_id, trace)
            stack.callback(bus.close)
            failures = []
            if args.command == 'set-feedback':
                observe(bus, .2)
                changed = True
                set_divider(bus, args.divider)
            if args.command.endswith('-sdo'):
                axes = range(1, 7) if args.axis == 0 else (args.axis,)
                print('Axis  Register       Raw    Parameter ms (SDK unit; not measured upload period)')
                for axis in axes:
                    index = REGISTERS[axis - 1]
                    if args.command == 'set-sdo':
                        changed = True
                        value = write_register(bus, axis, args.register_raw,
                                               args.sdo_timeout_ms / 1000, args.attempts)
                    else:
                        try:
                            value = request(bus, index, 0x14, args.sdo_timeout_ms / 1000, args.attempts)
                        except ota.ResponseTimeout as exc:
                            failures.append(str(exc))
                            print(f'{axis:4d}  0x{index:04x}:14   TIMEOUT')
                            continue
                    print(f'{axis:4d}  0x{index:04x}:14  {value:6d}    {value / 10:.1f}')
                    trace('register_read', axis=axis, index=hex(index), raw=value, sdk_ms=value / 10)
            else:
                print('Feedback divider cannot be read back; actual receive timing follows.')
                observe(bus, args.settle)
                report = observe(bus, args.duration)
                trace('measurement', **report)
                print_measurement(report)
                if any(row['frames'] < 2 for row in report['groups']):
                    failures.append('Too few frames in one or both feedback groups; period cannot be confirmed')
            after = ota.snapshot(args.interface)
            delta = {key: None if value is None or ota.counters(after).get(key) is None else
                     ota.counters(after)[key] - value for key, value in ota.counters(before).items()}
            trace('interface_after', interface=after, counter_delta=delta)
            if any(value is not None and value > 0 for key, value in delta.items()
                   if key.endswith(('.errors', '.dropped', '.over_errors')) or key in ('can.bus_error', 'can.bus_off')):
                failures.append('CAN error/drop counters increased; see the log')
            for failure in failures:
                print('FAILED:', failure, file=sys.stderr)
            if args.command == 'set-sdo' and args.save:
                if failures:
                    print('EEPROM save skipped because validation failed.', file=sys.stderr)
                else:
                    save_started = True
                    save_eeprom(bus, args.sdo_timeout_ms / 1000)
                    for axis in axes:
                        actual = request(bus, REGISTERS[axis - 1], 0x14,
                                         args.sdo_timeout_ms / 1000, args.attempts)
                        if actual != args.register_raw:
                            raise ota.Failure(f'Axis {axis} post-save readback mismatch: '
                                              f'requested {args.register_raw}, got {actual}')
                        trace('post_save_readback', axis=axis, raw=actual)
                    print('EEPROM save sequence sent; six-axis runtime readback matches.')
                    print('Persistence is NOT verified; neither ACK nor runtime readback proves EEPROM completion.')
                    print('After device save completion, power-cycle and run read-sdo before SDK/wave_demo.')
                    print('Use read-feedback separately to measure rates; divider retention is still unknown.')
            if changed:
                if not save_started:
                    print('Runtime settings are left on the device; no EEPROM save command was sent.')
                print('SDK connect/wave_demo overwrites feedback settings. Stop those programs while testing.')
            trace('finish', status='failed' if failures else 'completed', errors=failures,
                  save_started=save_started, persistence_verified=False)
            return 1 if failures else 0
    except KeyboardInterrupt:
        print('\nInterrupted. Any configuration already sent remains on the device.', file=sys.stderr)
        if trace:
            trace('finish', status='interrupted', configuration_may_have_changed=changed,
                  save_started=save_started, persistence_verified=False)
        return 130
    except (ota.Failure, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f'FAILED: {exc}', file=sys.stderr)
        if changed:
            print('Configuration may be partially applied; read back before continuing.', file=sys.stderr)
        if trace:
            trace('finish', status='failed', error=str(exc), configuration_may_have_changed=changed,
                  save_started=save_started, persistence_verified=False)
        return 1
    finally:
        if save_started:
            print('EEPROM save was attempted; completion/retention is unverified. Do not blindly repeat the save.')
        if trace:
            trace.close()


if __name__ == '__main__':
    raise SystemExit(main())
