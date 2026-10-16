"""Compare both example trajectories without importing the SDK or opening CAN."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("wave_demo", root / "examples/wave_demo.py")
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
print("C++/Python/ROS trajectories, full-stroke targets, boundaries and duration checks passed")
