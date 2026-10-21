"""Command-line connection options shared by the Python examples, not the SDK."""
import argparse
import sys


def parse_options(argv=None, *, wave=True):
    parser = argparse.ArgumentParser(description=(
        'Connect, enable, home and wave until Ctrl+C.' if wave else
        'Connect and print one feedback snapshot; do not enable or home.'))
    parser.add_argument('--serial', help='Windows USB adapter hexadecimal serial number')
    parser.add_argument('--interface', help='Linux SocketCAN interface (default: can0)')
    parser.add_argument('--node-id', type=int, default=1, help='CAN node ID (1..127, default: 1)')
    parser.add_argument('--side', choices=['left', 'right'], help='Legacy Linux alias: left=can0, right=can3')
    advanced = parser.add_argument_group('advanced USB overrides (product defaults: 1000000 / 5000000 bit/s)')
    advanced.add_argument('--bitrate', type=int)
    advanced.add_argument('--data-bitrate', type=int)
    if wave:
        parser.add_argument('--amplitude', type=int, default=6000, help='Four-finger amplitude (1..10000, default: 6000)')
        parser.add_argument('--connect-only', action='store_true',
                            help='Check feedback for 5 seconds without enable, home or wave')
    options = parser.parse_args(argv)
    if not 1 <= options.node_id <= 127:
        parser.error('node-id must be in 1..127')
    if wave and not 1 <= options.amplitude <= 10000:
        parser.error('amplitude must be in 1..10000')
    if options.interface is not None and (not options.interface or options.side is not None):
        parser.error('--interface must be nonempty and cannot be combined with --side')
    if options.serial is not None:
        if sys.platform != 'win32':
            parser.error('--serial requires Windows; use --interface on Linux')
        if options.interface is not None or options.side is not None:
            parser.error('--serial cannot be combined with --interface or --side')
        if not options.serial or any(ch not in '0123456789abcdefABCDEF' for ch in options.serial):
            parser.error('serial must be the adapter hexadecimal serial number')
        if any(value is not None and not 1 <= value <= 2147483647
               for value in (options.bitrate, options.data_bitrate)):
            parser.error('USB bitrates must be positive 32-bit integers')
    else:
        if sys.platform == 'win32':
            parser.error('Windows requires --serial')
        if options.bitrate is not None or options.data_bitrate is not None:
            parser.error('SocketCAN bitrates must be configured in the operating system')
    options.interface = options.interface or ('can3' if options.side == 'right' else 'can0')
    return options


def connection_options(options):
    from rphand import Backend, ConnectionOptions
    connection = ConnectionOptions()
    connection.node_id = options.node_id
    if options.serial is not None:
        connection.backend = Backend.GsUsbWinUsb
        connection.serial = options.serial
        # Defaults are defined in C++; do not duplicate product rates here.
        if options.bitrate is not None:
            connection.bitrate = options.bitrate
        if options.data_bitrate is not None:
            connection.data_bitrate = options.data_bitrate
    else:
        connection.backend = Backend.SocketCan
        connection.interface = options.interface
    return connection
