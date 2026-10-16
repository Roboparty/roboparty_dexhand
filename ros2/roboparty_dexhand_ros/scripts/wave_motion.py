"""Time-based thumb sweeps followed by a four-finger wave, without ROS I/O."""
import math

THUMB_SECONDS = 2.0
WAVE_SECONDS = 6.0


def target(seconds, amplitude, wave_seconds=WAVE_SECONDS):
    values = [0] * 6
    if seconds < 0 or seconds >= 2 * THUMB_SECONDS + wave_seconds:
        return values
    if seconds < 2 * THUMB_SECONDS:
        axis = 0 if seconds < THUMB_SECONDS else 1
        phase = 2 * math.pi * (seconds - axis * THUMB_SECONDS) / THUMB_SECONDS
        values[axis] = math.floor(5000 * (1 - math.cos(phase)) + 0.5)
    else:
        local = seconds - 2 * THUMB_SECONDS
        edge = max(0.0, min(1.0, local, wave_seconds - local))
        envelope = (1 - math.cos(math.pi * edge)) / 2
        values[2:] = [math.floor(amplitude * envelope *
                     (1 - math.cos(2 * math.pi * local + j * math.pi / 2)) / 2 + 0.5)
                     for j in range(4)]
    return values
