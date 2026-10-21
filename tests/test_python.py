"""Bindings: unittest discovery. Examples: --examples PATH_TO_CPP_DRIVER (no SDK required)."""
import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class BindingTests(unittest.TestCase):
    def setUp(self):
        from rphand import Hand
        self.hand = Hand()

    def tearDown(self):
        self.hand.disconnect()

    def test_offline(self):
        self.assertFalse(self.hand.is_connected())
        for name in ("enable", "disable", "stop", "home"):
            self.assertFalse(getattr(self.hand, name)())
        for args in (("", 1), ("can0", 0)):
            self.assertFalse(self.hand.connect(*args))
        self.assertFalse(self.hand.set_positions([0] * 6))
        self.assertFalse(self.hand.set_positions((0,) * 6))
        self.assertFalse(self.hand.set_position(1, 0))
        self.assertFalse(self.hand.set_speed(20000))
        self.assertFalse(self.hand.set_current_limit(400))
        self.assertFalse(self.hand.set_torque_reached_mode(True))
        self.hand.disconnect()
        self.hand.disconnect()

    def test_connection_options(self):
        from rphand import Backend, ConnectionOptions
        options = ConnectionOptions()
        self.assertEqual((options.bitrate, options.data_bitrate), (1000000, 5000000))
        self.assertEqual((options.interface, options.node_id, options.channel), ('can0', 1, 0))
        options.backend = Backend.GsUsbWinUsb
        self.assertFalse(self.hand.connect(options))  # Empty serial: no hardware access.
        for field in ("node_id", "channel", "bitrate", "data_bitrate"):
            for value in (True, 1.0, "1"):
                with self.assertRaises(TypeError):
                    setattr(options, field, value)
            with self.assertRaises(ValueError):
                setattr(options, field, 2**31)

    def test_conversion(self):
        calls = [lambda v: self.hand.connect("can0", v),
                 lambda v: self.hand.set_position(v, 0),
                 lambda v: self.hand.set_position(1, v),
                 self.hand.set_speed, self.hand.set_current_limit,
                 lambda v: self.hand.set_positions([0, 0, v, 0, 0, 0])]
        for call in calls:
            for value in (True, False, 1.0, "1", None):
                with self.subTest(value=value), self.assertRaises(TypeError):
                    call(value)
            for value in (2**31, -(2**31) - 1, 2**100, -(2**100)):
                with self.subTest(value=value), self.assertRaises(ValueError):
                    call(value)
        for value in ([], [0] * 5, [0] * 7):
            with self.assertRaises(ValueError):
                self.hand.set_positions(value)
        for value in ("000000", range(6), None):
            with self.assertRaises(TypeError):
                self.hand.set_positions(value)
        for value in (0, 1, "true", None):
            with self.assertRaises(TypeError):
                self.hand.set_torque_reached_mode(value)
        with self.assertRaises(TypeError):
            self.hand.connect(b"can0", 1)
        self.assertFalse(self.hand.set_positions([10001] * 6))
        self.assertFalse(self.hand.set_speed(-(2**31)))
        self.assertFalse(self.hand.set_speed(2**31 - 1))

    def test_snapshot(self):
        from rphand import Hand
        state = self.hand.get_state()
        self.assertFalse(state.feedback_valid)
        for name in ("positions", "velocities", "currents", "enabled", "homed", "torque_reached", "fault_codes"):
            self.assertIsInstance(getattr(state, name), tuple)
            self.assertEqual(len(getattr(state, name)), 6)
            with self.assertRaises(AttributeError):
                setattr(state, name, (1,) * 6)
        with self.assertRaises(AttributeError):
            state.feedback_valid = True
        self.hand.disconnect()
        del self.hand
        self.assertEqual(state.positions, (0,) * 6)
        self.hand = Hand()



def check_examples(driver):
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "examples/python"))
    spec = importlib.util.spec_from_file_location("wave_demo", root / "examples/python/wave_demo.py")
    demo = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(demo)
    spec = importlib.util.spec_from_file_location("wave_motion", root / "ros2/rphand_ros/scripts/wave_motion.py")
    ros_wave = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ros_wave)
    for amplitude in (1, 6000, 10000):
        for tick in range(501):
            assert ros_wave.target(tick / 50, amplitude) == demo.target(tick / 50, amplitude)
    for duration in (0.5, 6, 8):
        assert ros_wave.target(1, 6000, duration) == [10000, 0, 0, 0, 0, 0]
        assert ros_wave.target(3, 6000, duration) == [0, 10000, 0, 0, 0, 0]
        assert ros_wave.target(4 + duration, 6000, duration) == [0] * 6
        assert ros_wave.target(5 + duration, 6000, duration) == [0] * 6

    for seconds, expected in [(0, [0] * 6), (1, [10000, 0, 0, 0, 0, 0]),
                              (2, [0] * 6), (3, [0, 10000, 0, 0, 0, 0]),
                              (4, [0] * 6), (10, [0] * 6)]:
        assert demo.target(seconds, 6000) == expected
    for boundary in (2, 4, 10):
        assert max(demo.target(boundary - 0.0001, 6000)) <= 1
        assert max(demo.target(boundary + 0.0001, 6000)) <= 1
    for tick in range(500):
        values = demo.target(tick / 50, 6000)
        assert values == demo.target(10 + tick / 50, 6000), "round must repeat"
        if tick < 100:
            assert values[1:] == [0] * 5
        elif tick < 200:
            assert values[0] == 0 and values[2:] == [0] * 4
        else:
            assert values[:2] == [0, 0]

    for finger in range(4):
        assert demo.target(6.5 - finger / 4, 10000)[finger + 2] == 10000
        assert demo.target(7 - finger / 4, 10000)[finger + 2] == 0
    print("Python/ROS trajectories, boundaries and duration checks passed")

    # The diagnostic mode must never reach enable/home/motion, and cleanup failure
    # must still change the process result. No native SDK or USB backend is loaded.

    for feedback_ok, cleanup_ok in ((True, True), (False, True), (True, False)):
        calls = []

        class FakeHand:
            def connect(self, options):
                calls.append('connect')
                return True

            def is_connected(self):
                return True

            def get_state(self):
                calls.append('state')
                return SimpleNamespace(feedback_valid=feedback_ok, fault_codes=(0,) * 6)

            def disable(self):
                calls.append('disable')
                return cleanup_ok

            def disconnect(self):
                calls.append('disconnect')

            def __getattr__(self, name):
                calls.append(name)
                raise AssertionError('diagnostic attempted unexpected operation: ' + name)

        fake_sdk = SimpleNamespace(Hand=FakeHand, ConnectionOptions=SimpleNamespace,
                                   Backend=SimpleNamespace(GsUsbWinUsb=1))
        ticks = iter((0, 1, 6, 7))
        argv = ['wave_demo.py', '--serial', 'F000', '--connect-only']
        with patch.dict(sys.modules, {'rphand': fake_sdk}), patch.object(sys, 'argv', argv), \
                patch.object(sys, 'platform', 'win32'), \
                patch.object(demo, 'time', SimpleNamespace(monotonic=lambda: next(ticks))):
            assert demo.main() == (0 if feedback_ok and cleanup_ok else 1)
        assert calls == ['connect', 'state', 'disable', 'disconnect'], calls
    print('Connection-only mode: no enable/home/motion; feedback and cleanup failures propagate')

    # Run the compiled C++ trajectory, parser and diagnostics against a fake Hand.
    # No real transport is linked into this helper, even for valid arguments.
    rows = subprocess.check_output([driver, 'targets'], text=True).splitlines()
    assert len(rows) == 3 * 501
    for row in rows:
        amplitude, tick, *values = map(int, row.split())
        # libm implementations can land on opposite sides of an exact half unit.
        # One stroke unit (1/10000 full scale) is the rounding tolerance, not a phase tolerance.
        assert max(abs(a - b) for a, b in zip(values, demo.target(tick / 50, amplitude))) <= 1, row
    subprocess.run([driver, 'diagnostic'], check=True, timeout=20)

    from connection import parse_options, connection_options
    prefix = ['--serial', 'F000'] if sys.platform == 'win32' else ['--interface', 'can0']
    valid = [prefix, prefix + ['--node-id', '7', '--amplitude', '1', '--connect-only']]
    if sys.platform == 'win32':
        valid += [prefix + ['--bitrate', '500000', '--data-bitrate', '2000000']]
    else:
        valid += [[], ['--side', 'right'], ['--interface', 'can3']]
    invalid = [prefix + args for args in (
        ['--node-id', '0'], ['--node-id', '128'], ['--amplitude', '0'],
        ['--amplitude', '10001'], ['--node-id', 'bad'], ['--node-id'], ['--unknown'],
        ['--side', 'right'], ['--bitrate', '0'], ['--data-bitrate', '-1'])]
    invalid += [['--serial', 'ZZZ'], ['--interface', ''], ['--serial', '']]
    invalid += [[]] if sys.platform == 'win32' else [['--bitrate', '1000000']]
    for args in valid + invalid:
        expected = 0 if args in valid else 2
        result = subprocess.run([driver, 'parse', *args], capture_output=True, text=True, timeout=5)
        assert result.returncode == expected, (args, result.stdout, result.stderr)
        with contextlib.redirect_stderr(io.StringIO()):
            try:
                options = parse_options(args)
                assert expected == 0, args
            except SystemExit as error:
                assert expected == error.code == 2, args
                continue
        # Compare effective fields, including the C++ product defaults.
        fake_options = lambda: SimpleNamespace(bitrate=1000000, data_bitrate=5000000)
        fake_sdk = SimpleNamespace(ConnectionOptions=fake_options,
                                   Backend=SimpleNamespace(GsUsbWinUsb=1, SocketCan=0))
        with patch.dict(sys.modules, {'rphand': fake_sdk}):
            connection = connection_options(options)
        assert result.stdout.split() == [options.interface, str(options.node_id),
            str(connection.bitrate), str(connection.data_bitrate), str(options.amplitude),
            str(int(options.connect_only))], args
    print('C++/Python trajectories, CLI defaults/validation and diagnostics passed')


def check_sdist(output):
    """Check the real archive, then rebuild without Git and with injected junk."""
    import os
    import tarfile
    import tempfile
    from scikit_build_core.build import build_sdist

    def contents(path):
        with tarfile.open(path) as archive:
            result = {}
            for member in archive.getmembers():
                relative = Path(*Path(member.name).parts[1:])
                assert member.isfile() and not relative.is_absolute() and '..' not in relative.parts, member.name
                result[relative.as_posix()] = archive.extractfile(member).read()
            return result

    root = Path(__file__).resolve().parents[1]
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    previous = Path.cwd()
    try:
        os.chdir(root)
        name = build_sdist(str(output))
        files = contents(output / name)
        allowed_roots = {'cmake', 'include', 'src', 'python', 'examples', 'tests', 'docs', 'ros2'}
        for relative in files:
            parts = Path(relative).parts
            assert len(parts) == 1 or parts[0] in allowed_roots, relative
            assert not set(parts) & {'build', 'dist', 'install', '.venv', '__pycache__', 'CMakeFiles'}, relative
        required = {'pyproject.toml', 'CMakeLists.txt', 'README.md', 'PKG-INFO',
                    'include/rphand/types.hpp', 'src/hand.cpp', 'python/bindings.cpp',
                    'src/rphand/backends/gs_usb_winusb.cpp', 'src/rphand/backends/socketcan.cpp',
                    'python/rphand/py.typed', 'examples/cpp/usb_probe.cpp',
                    'cmake/rphandConfig.cmake.in', 'tests/CMakeLists.txt',
                    'ros2/rphand_ros/package.xml', 'docs/getting-started.md'}
        assert required <= files.keys(), required - files.keys()
        junk = ('-', 'scratch.txt', 'local_script.py', '.env', '.python-version',
                'build/generated.cpp', 'dist/old.whl', 'install/include/rphand/local.hpp',
                '.venv/secret.txt', 'src/hand.obj', 'src/rphand/cache.log',
                'python/rphand/_core.pyd', 'python/rphand/__pycache__/cache.pyc',
                'docs/draft.txt', 'examples/python/output.log', 'build/CMakeLists.txt',
                'build/pyproject.toml', '.venv/LICENSE', 'scratch/README.md')
        assert not set(junk) & files.keys(), set(junk) & files.keys()
        with tempfile.TemporaryDirectory(prefix='rphand-sdist-') as temp:
            source, rebuilt = Path(temp) / 'source', Path(temp) / 'dist'
            for relative, data in files.items():
                destination = source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
            for relative in junk:
                destination = source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text('local build artifact', encoding='utf-8')
            rebuilt.mkdir()
            try:
                os.chdir(source)
                roundtrip = contents(rebuilt / build_sdist(str(rebuilt)))
                assert files == roundtrip, 'Source archive changes after extraction or includes local artifacts'
            finally:
                os.chdir(root)  # Windows cannot remove a directory while it is cwd.
        print(f'Source archive checked: {output / name} ({len(files)} files); junk excluded without Git')
    finally:
        os.chdir(previous)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--examples':
        check_examples(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == '--sdist':
        check_sdist(sys.argv[2])
    else:
        unittest.main()
