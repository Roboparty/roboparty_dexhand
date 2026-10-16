"""Compare both example trajectories without importing the SDK or opening CAN."""
import importlib.util
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("wave_demo", root / "examples/wave_demo.py")
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)
spec = importlib.util.spec_from_file_location("wave_motion", root / "ros2/roboparty_dexhand_ros/scripts/wave_motion.py")
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

commands = [[sys.argv[1]], [sys.executable, str(root / "examples/wave_demo.py")]]
for finger in range(4):
    assert demo.target(6.5 - finger / 4, 10000)[finger + 2] == 10000
    assert demo.target(7 - finger / 4, 10000)[finger + 2] == 0
for command in commands:
    default = subprocess.run(command + ["--dry-run"], capture_output=True, text=True, check=True, timeout=10)
    explicit = subprocess.run(command + ["--dry-run", "--amplitude", "6000"],
                          capture_output=True, text=True, check=True, timeout=10)
    assert default.stdout == explicit.stdout, "default wave amplitude must be 6000"
    for side, amplitude in [("left", 6000), ("right", 10000), ("left", 1)]:
        result = subprocess.run(command + ["--dry-run", "--side", side, "--amplitude", str(amplitude)],
                                capture_output=True, text=True, check=True, timeout=10)
        samples = {}
        for line in result.stdout.splitlines():
            if not line.startswith("tick="):
                continue
            label, data = line.split(" positions:")
            tick = int(label.removeprefix("tick="))
            values = list(map(int, data.split()))
            assert len(values) == 6
            expected = demo.target(tick / 50, amplitude)
            assert all(abs(a - b) <= 1 for a, b in zip(values, expected)), (command, tick, values, expected)
            samples[tick] = values
        assert sorted(samples) == list(range(0, 501, 5))
        assert samples[50][0] == samples[150][1] == 10000
        assert samples[500] == [0] * 6
    for arguments in (["--amplitude", "0"], ["--amplitude", "10001"], ["--side", "both"]):
        result = subprocess.run(command + ["--dry-run"] + arguments, capture_output=True, timeout=10)
        assert result.returncode == 2
print("C++/Python/ROS trajectories, full-stroke targets, boundaries, duration and CLI checks passed")
