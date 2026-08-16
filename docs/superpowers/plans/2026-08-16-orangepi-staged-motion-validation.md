# Orange Pi Staged Motion Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute an evidence-bound, two-phase physical validation of the
LHandPro 6DOF S hand on Orange Pi `can0`, with an operator checkpoint between
short-travel and full-profile motion.

**Architecture:** Build and test a dependency-injected Python motion harness
in an isolated `/tmp` staging directory. After offline fake coverage and two
independent reviews, copy the exact hashed script to the Orange Pi evidence
directory. Execute Phase A once, stop for operator confirmation, then execute
Phase B once; every initialized path attempts stop, disable, no-home reset,
and deinitialization.

**Tech Stack:** Python 3.10/3.12, pybind11 `dexhand_py`, LHandPro SDK,
SocketCAN CAN-FD, shell/SSH evidence capture, SHA-256.

---

## Execution Amendment: Approved Offline Contract

Task 1's seven-test listing and Task 2's harness listing below are the initial
implementation sketch. They are retained to show the design's evolution, but
they are no longer the executable source of truth. Independent specification
and quality review required a stronger offline contract before any harness or
hardware action.

The approved Task 1 artifact is:

- path:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`;
- SHA-256:
  `f16ec7de265f937576678fca7da4a2dd7d1dcb9c06137fee118ba15287d0ee15`;
- size: 68,414 bytes and 1,959 lines; and
- inventory: 42 test definitions, with the same 42 tests registered by
  `main()`.

Its required RED is exit 1 solely because
`staged_motion_validation.py` does not yet exist. The staging directory must
contain only that test file at the Task 1 boundary. The contract additionally
locks all-six-joint command and telemetry ordering, exact 100/1,000-count
direction boundaries, alarm/range checks at every sample, delayed failures,
primary and cleanup interrupts, globally exact cleanup calls, structured
evidence values and ordering, deferred `dexhand_py` import, and the public
`LHANDPRO_6DOF` CLI binding. Factory creation failure is explicitly the only
path with no device cleanup because no hand object was acquired. Cleanup
attribute lookup is covered by the corresponding step's exception boundary,
and exception handling never relies on an exception object's truth value.

Task 2 must implement the smallest harness that makes this approved artifact
GREEN. In particular, it must reject an invalid phase before factory creation
and must not copy the historical harness listing verbatim. Any change to the
approved test artifact invalidates this hash and both reviews; Tasks 1-3 must
then be repeated before any Orange Pi access.

---

## File Map

- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`
  - Offline fake contract for both phases and every cleanup path.
- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
  - Dependency-injected physical harness with `small` and `full` modes.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_6dofs_92d742c/logs/motion-validation-1a7c820/`
  - Immutable script copy, hashes, exact commands, phase logs, and CAN
    pre/postflight evidence.
- Do not modify production source, `scripts/test_dexhand.py`, the installed
  prefix, `can0` configuration, or any sibling repository.

### Task 1: Establish the Offline RED Contract

**Files:**
- Create:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`
- Test:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`

- [x] **Step 1: Prove the staging directory is new**

Run:

```bash
test ! -e /tmp/roboparty-dexhand-motion-1a7c820
```

Expected: exit 0. If it already exists, stop and choose a new design-bound
suffix before creating anything.

- [x] **Step 2: Create the isolated directory**

Run:

```bash
mkdir /tmp/roboparty-dexhand-motion-1a7c820
```

Expected: exit 0.

- [x] **Step 3: Add the reviewed fake contract with `apply_patch`**

The following block is the superseded initial seven-test sketch. The approved
42-test artifact and hash in the execution amendment are authoritative.

Historical sketch:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

import importlib.util
from pathlib import Path


SCRIPT_PATH = Path(__file__).with_name('staged_motion_validation.py')


def load_module():
    spec = importlib.util.spec_from_file_location(
        'staged_motion_validation', SCRIPT_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError('unable to load staged motion module')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeHand:
    def __init__(self, *, init_result=True, alarm=0,
                 bad_direction=False, interrupt_motion=False,
                 fail_cleanup=None):
        self.init_result = init_result
        self.alarm = alarm
        self.bad_direction = bad_direction
        self.interrupt_motion = interrupt_motion
        self.fail_cleanup = fail_cleanup
        self.events = []
        self.positions = [0] * 6
        self.targets = [0] * 6

    def init_hand(self, **kwargs):
        self.events.append(('init_hand', kwargs))
        return self.init_result

    def get_dof(self):
        self.events.append(('get_dof',))
        return (11, 6)

    def set_move_no_home(self, value):
        self.events.append(('set_move_no_home', value))
        if self.fail_cleanup == 'set_move_no_home' and value == 0:
            raise RuntimeError('fake no-home cleanup failure')

    def set_target_position(self, joint, value):
        self.events.append(('set_target_position', joint, value))
        self.targets[joint - 1] = value

    def set_position_velocity(self, joint, value):
        self.events.append(('set_position_velocity', joint, value))

    def move_motors(self, joint):
        self.events.append(('move_motors', joint))
        if self.interrupt_motion:
            raise KeyboardInterrupt()
        if not self.bad_direction:
            self.positions = list(self.targets)

    def get_now_position(self, joint):
        return self.positions[joint - 1]

    def get_now_status(self, joint):
        return 0

    def get_now_current(self, joint):
        return 0

    def get_now_alarm(self, joint):
        return self.alarm

    def stop_motors(self, joint):
        self.events.append(('stop_motors', joint))
        if self.fail_cleanup == 'stop_motors':
            raise RuntimeError('fake stop failure')

    def set_enable(self, joint, enabled):
        self.events.append(('set_enable', joint, enabled))
        if self.fail_cleanup == 'set_enable':
            raise RuntimeError('fake disable failure')

    def deinit_hand(self):
        self.events.append(('deinit_hand',))
        if self.fail_cleanup == 'deinit_hand':
            raise RuntimeError('fake deinit failure')


class Factory:
    def __init__(self, hand):
        self.hand = hand
        self.kwargs = None

    def __call__(self, **kwargs):
        self.kwargs = kwargs
        return self.hand


def event_names(hand):
    return [event[0] for event in hand.events]


def assert_cleanup_order(hand):
    names = event_names(hand)
    expected = [
        'stop_motors', 'set_enable', 'set_move_no_home', 'deinit_hand']
    assert names[-4:] == expected, names[-4:]


def run_case(module, phase, hand):
    factory = Factory(hand)
    output = []
    hand_model = object()
    result = module.run_phase(
        factory,
        hand_model=hand_model,
        phase=phase,
        sleep_fn=lambda _: None,
        output=output.append,
    )
    assert factory.kwargs == {
        'hand_type': 'LHandPro',
        'interface_type': 'canfd',
        'interface': 'can0',
        'hand_model': hand_model,
        'canfd_node_id': 1,
    }
    return result, output


def test_small_success(module):
    hand = FakeHand()
    result, output = run_case(module, 'small', hand)
    assert result == 0, output
    assert event_names(hand).count('move_motors') == 2
    assert 'set_max_current' not in event_names(hand)
    assert_cleanup_order(hand)


def test_full_success(module):
    hand = FakeHand()
    result, output = run_case(module, 'full', hand)
    assert result == 0, output
    assert event_names(hand).count('move_motors') == 7
    assert 'set_max_current' not in event_names(hand)
    assert_cleanup_order(hand)


def test_alarm_aborts_before_motion(module):
    hand = FakeHand(alarm=7)
    result, output = run_case(module, 'small', hand)
    assert result == 1, output
    assert 'move_motors' not in event_names(hand)
    assert_cleanup_order(hand)


def test_direction_failure_aborts(module):
    hand = FakeHand(bad_direction=True)
    result, output = run_case(module, 'small', hand)
    assert result == 1, output
    assert event_names(hand).count('move_motors') == 1
    assert_cleanup_order(hand)


def test_interrupt_runs_cleanup(module):
    hand = FakeHand(interrupt_motion=True)
    result, output = run_case(module, 'small', hand)
    assert result == 130, output
    assert_cleanup_order(hand)


def test_init_false_runs_cleanup(module):
    hand = FakeHand(init_result=False)
    result, output = run_case(module, 'small', hand)
    assert result == 1, output
    assert 'get_dof' not in event_names(hand)
    assert_cleanup_order(hand)


def test_cleanup_failure_does_not_skip_later_steps(module):
    hand = FakeHand(fail_cleanup='stop_motors')
    result, output = run_case(module, 'small', hand)
    assert result == 1, output
    assert_cleanup_order(hand)
    assert any('cleanup_error' in line for line in output)


def main():
    module = load_module()
    tests = (
        test_small_success,
        test_full_success,
        test_alarm_aborts_before_motion,
        test_direction_failure_aborts,
        test_interrupt_runs_cleanup,
        test_init_false_runs_cleanup,
        test_cleanup_failure_does_not_skip_later_steps,
    )
    for test in tests:
        test(module)
        print(f'PASS {test.__name__}')
    print(f'PASS all={len(tests)}')


if __name__ == '__main__':
    main()
```

- [x] **Step 4: Run the contract and capture the required RED**

Run:

```bash
/usr/bin/python3 \
  /tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py
```

Expected: nonzero exit because `staged_motion_validation.py` does not exist.
The failure must be an import/file error, not a syntax error in the test.

### Task 2: Implement the Dependency-Injected Harness

**Files:**
- Create:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
- Test:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`

- [ ] **Step 1: Implement the minimal harness against the approved contract**

The following block is the superseded initial harness sketch. It is useful as
design context only and is not the Task 2 implementation specification.

Historical sketch:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

import argparse
import json
import time


POSITION_MIN = -1000
POSITION_MAX = 11000


def emit_json(output, event, **fields):
    payload = {'event': event, **fields}
    output(json.dumps(payload, sort_keys=True, separators=(',', ':')))


def sample_all(hand, label, output):
    samples = []
    for joint in range(1, 7):
        sample = {
            'joint': joint,
            'position': hand.get_now_position(joint),
            'status': hand.get_now_status(joint),
            'current': hand.get_now_current(joint),
            'alarm': hand.get_now_alarm(joint),
        }
        if sample['alarm'] != 0:
            raise RuntimeError(
                f"joint {joint} alarm={sample['alarm']} at {label}")
        if not POSITION_MIN <= sample['position'] <= POSITION_MAX:
            raise RuntimeError(
                f"joint {joint} position={sample['position']} "
                f'outside safety range at {label}')
        samples.append(sample)
    emit_json(output, 'sample', label=label, motors=samples)
    return samples


def require_direction(before, after, minimum, increasing, label):
    for old, new in zip(before, after):
        delta = new['position'] - old['position']
        valid = delta >= minimum if increasing else delta <= -minimum
        if not valid:
            direction = 'increase' if increasing else 'decrease'
            raise RuntimeError(
                f"joint {new['joint']} failed to {direction} by {minimum} "
                f'counts at {label}: delta={delta}')


def move_and_sample(hand, target, velocity, wait_seconds, label,
                    sleep_fn, output):
    for joint in range(1, 7):
        hand.set_target_position(joint, target)
        hand.set_position_velocity(joint, velocity)
    hand.move_motors(0)
    emit_json(
        output, 'motion_command', label=label, target=target,
        velocity=velocity)
    sleep_fn(wait_seconds)
    return sample_all(hand, label, output)


def cleanup(hand, output):
    failures = []
    actions = (
        ('stop_motors', lambda: hand.stop_motors(0)),
        ('set_enable_false', lambda: hand.set_enable(0, False)),
        ('set_move_no_home_0', lambda: hand.set_move_no_home(0)),
        ('deinit_hand', hand.deinit_hand),
    )
    for name, action in actions:
        try:
            action()
            emit_json(output, 'cleanup_complete', action=name)
        except BaseException as error:
            failures.append(name)
            emit_json(
                output, 'cleanup_error', action=name,
                error=f'{type(error).__name__}: {error}')
    return failures


def run_phase(create_hand, hand_model, phase, *, sleep_fn=time.sleep,
              output=print):
    hand = create_hand(
        hand_type='LHandPro',
        interface_type='canfd',
        interface='can0',
        hand_model=hand_model,
        canfd_node_id=1,
    )
    result = 0
    try:
        emit_json(output, 'phase_start', phase=phase)
        initialized = hand.init_hand(
            enable_motors=True,
            home_motors=True,
            home_wait_time=6.0,
        )
        if not initialized:
            raise RuntimeError('init_hand returned false')
        dof = hand.get_dof()
        if tuple(dof) != (11, 6):
            raise RuntimeError(f'unexpected DOF: {dof}')
        emit_json(output, 'init_complete', dof=list(dof))
        hand.set_move_no_home(0)
        emit_json(output, 'move_no_home_reset', value=0)
        sleep_fn(0.5)
        opened = sample_all(hand, f'{phase}.baseline', output)

        if phase == 'small':
            high = move_and_sample(
                hand, 1000, 1000, 2.0, 'small.high', sleep_fn, output)
            require_direction(
                opened, high, 100, True, 'small.high')
            low = move_and_sample(
                hand, 0, 1000, 2.0, 'small.open', sleep_fn, output)
            require_direction(high, low, 100, False, 'small.open')
        elif phase == 'full':
            for cycle in range(1, 4):
                high = move_and_sample(
                    hand, 5000, 15000, 2.0,
                    f'full.cycle{cycle}.high', sleep_fn, output)
                require_direction(
                    opened, high, 1000, True,
                    f'full.cycle{cycle}.high')
                low = move_and_sample(
                    hand, 0, 15000, 2.0,
                    f'full.cycle{cycle}.open', sleep_fn, output)
                require_direction(
                    high, low, 1000, False,
                    f'full.cycle{cycle}.open')
                opened = low
            move_and_sample(
                hand, 0, 15000, 1.0, 'full.final_open',
                sleep_fn, output)
        else:
            raise ValueError(f'unsupported phase: {phase}')
        emit_json(output, 'phase_complete', phase=phase)
    except KeyboardInterrupt:
        result = 130
        emit_json(output, 'phase_interrupted', phase=phase)
    except BaseException as error:
        result = 1
        emit_json(
            output, 'phase_error', phase=phase,
            error=f'{type(error).__name__}: {error}')
    finally:
        if cleanup(hand, output):
            result = 1
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Evidence-bound staged LHandPro motion validation')
    parser.add_argument('--phase', choices=('small', 'full'), required=True)
    args = parser.parse_args(argv)

    from dexhand_py import HandDriver, HandModel

    return run_phase(
        HandDriver.create_hand,
        HandModel.LHANDPRO_6DOF,
        args.phase,
    )


if __name__ == '__main__':
    raise SystemExit(main())
```

- [ ] **Step 2: Run the fake contract for GREEN**

Run:

```bash
/usr/bin/python3 \
  /tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py
```

Expected: forty-two `PASS test_*` lines, `PASS all=42`, exit 0. No SDK or
`dexhand_py` import occurs because the fake calls `run_phase` directly.

- [ ] **Step 3: Parse both Python files without creating source caches**

Run:

```bash
/usr/bin/python3 - <<'PY'
import ast
from pathlib import Path

root = Path('/tmp/roboparty-dexhand-motion-1a7c820')
for name in ('staged_motion_validation.py',
             'test_staged_motion_validation.py'):
    ast.parse((root / name).read_text(encoding='utf-8'), filename=name)
    print(f'PASS syntax {name}')
PY
```

Expected: exit 0.

### Task 3: Enforce Static Safety Policy and Bind the Artifact

**Files:**
- Inspect:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
- Inspect:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`

- [ ] **Step 1: Prove forbidden operations are absent**

Run:

```bash
rg -n 'set_max_current|clear_alarm|set_target_angle|subprocess|os\.system' \
  /tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py
```

Expected: no output, exit 1.

- [ ] **Step 2: Prove exact public configuration and cleanup order**

Run:

```bash
rg -n \
  "interface='can0'|canfd_node_id=1|LHANDPRO_6DOF|stop_motors|set_enable|set_move_no_home|deinit_hand" \
  /tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py
```

Expected: exact configuration plus all four cleanup operations. Review the
line order against the design; a text match alone is not sufficient.

- [ ] **Step 3: Record immutable local hashes**

Run:

```bash
cd /tmp/roboparty-dexhand-motion-1a7c820
sha256sum staged_motion_validation.py test_staged_motion_validation.py \
  > SHA256SUMS
sha256sum staged_motion_validation.py > MOTION_SCRIPT_SHA256SUM
```

Run:

```bash
cd /tmp/roboparty-dexhand-motion-1a7c820
sha256sum -c SHA256SUMS
```

Expected: both files `OK`.

- [ ] **Step 4: Obtain two independent reviews**

Dispatch one spec reviewer and one safety/quality reviewer. Both must inspect
the exact hashed files and fake RED/GREEN evidence. They must not run
`main()`, import `dexhand_py`, connect to Orange Pi, or execute CAN commands.

Expected: no unresolved Critical or Important finding. If either reviewer
requests changes, change the scratch script, rerun Tasks 1-3 from GREEN, and
record a new hash before proceeding.

### Task 4: Stage Evidence and Run Phase A Once

**Files:**
- Copy:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
- Create remotely:
  `/home/orangepi/roboparty_dexhand_6dofs_92d742c/logs/motion-validation-1a7c820/`

- [ ] **Step 1: Confirm the evidence directory is absent**

Run through interactive SSH:

```bash
test ! -e \
  /home/orangepi/roboparty_dexhand_6dofs_92d742c/logs/motion-validation-1a7c820
```

Expected: exit 0. Do not reuse a partial evidence directory.

- [ ] **Step 2: Create the directory and copy the exact script**

Run remotely:

```bash
mkdir \
  /home/orangepi/roboparty_dexhand_6dofs_92d742c/logs/motion-validation-1a7c820
```

Copy the script with interactive `scp` to:

```text
/home/orangepi/roboparty_dexhand_6dofs_92d742c/logs/
motion-validation-1a7c820/staged_motion_validation.py
```

Do not place credentials in a command log.

- [ ] **Step 3: Verify local and remote script hashes match**

Run local and remote `sha256sum` on the script and compare exact output hashes.
Copy the local `MOTION_SCRIPT_SHA256SUM` into the evidence directory and run
remote:

```bash
sha256sum -c MOTION_SCRIPT_SHA256SUM
```

Expected: the motion script is `OK`. The fake file is intentionally not
executed or required on the board.

- [ ] **Step 4: Capture Phase A preflight**

Record each command, stdout, stderr, and exit code separately under the
evidence directory:

```bash
ip -brief link show can0
ip -details -statistics link show can0
cat /proc/net/can/rcvlist_all
ps -eo pid=,comm=,args=
sha256sum \
  /home/orangepi/roboparty_dexhand_6dofs_92d742c/clean-prefix-v2/lib/libLHandProLib.so
sha256sum staged_motion_validation.py
```

Expected: every invariant in the design passes. Independently inspect process
and receiver output; do not rely only on an automated string filter.

- [ ] **Step 5: Execute Phase A exactly once**

From the evidence directory, run exactly, without `set -e`:

```bash
env -u LD_LIBRARY_PATH \
  PYTHONNOUSERSITE=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  PYTHONPATH=/home/orangepi/roboparty_dexhand_6dofs_92d742c/clean-prefix-v2/lib/python3.10/site-packages \
  timeout --signal=INT --kill-after=5s 30s \
  /usr/bin/python3 -u staged_motion_validation.py --phase small \
  > phase-small.stdout.log 2> phase-small.stderr.log
MOTION_PHASE_RC=$?
/usr/bin/printf '%s\n' "$MOTION_PHASE_RC" > phase-small.rc
exit "$MOTION_PHASE_RC"
```

Do not invoke this command a second time after any result.

Expected: exit 0, one `phase_complete`, no `phase_error` or `cleanup_error`,
two motion commands, and four successful cleanup events.

- [ ] **Step 6: Capture and compare Phase A postflight**

Repeat the CAN, process, and socket commands from Step 4. Compare CAN protocol,
RX error/drop, and TX error/drop counters. Packet counts may increase; error
and drop counters must not.

Expected: `can0` remains UP/FD/ERROR-ACTIVE at 1M/5M, no receiver or process
remains, and all error/drop deltas are zero.

- [ ] **Step 7: Stop for operator confirmation**

Report every joint's baseline, high, and return sample plus all cleanup and CAN
results. Ask the observer to confirm no collision, binding, unexpected
direction, abnormal sound, or failed return to open.

Expected: the workflow stops here. Phase B must not run in the same agent turn
without the new explicit response `小行程正常`.

### Task 5: Run Phase B Once After the Operator Gate

**Files:**
- Execute remotely:
  `motion-validation-1a7c820/staged_motion_validation.py`
- Append evidence under:
  `motion-validation-1a7c820/`

- [ ] **Step 1: Verify the operator gate and artifact identity**

Require the exact new operator response `小行程正常`. Re-run remote
`sha256sum` and require it to match the Phase A script hash. Confirm that no
Phase B rc/stdout/stderr file already exists.

Expected: explicit confirmation, matching hash, and absent Phase B artifacts.

- [ ] **Step 2: Repeat the complete preflight**

Capture fresh interface, CAN statistics, receivers, processes, installed SDK
hash, and script hash exactly as in Phase A.

Expected: every invariant passes. Any failure blocks Phase B.

- [ ] **Step 3: Execute Phase B exactly once**

From the evidence directory, run exactly, without `set -e`:

```bash
env -u LD_LIBRARY_PATH \
  PYTHONNOUSERSITE=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  PYTHONPATH=/home/orangepi/roboparty_dexhand_6dofs_92d742c/clean-prefix-v2/lib/python3.10/site-packages \
  timeout --signal=INT --kill-after=5s 60s \
  /usr/bin/python3 -u staged_motion_validation.py --phase full \
  > phase-full.stdout.log 2> phase-full.stderr.log
MOTION_PHASE_RC=$?
/usr/bin/printf '%s\n' "$MOTION_PHASE_RC" > phase-full.rc
exit "$MOTION_PHASE_RC"
```

Never retry automatically.

Expected: exit 0, six cycle motion commands plus one final-open command,
one `phase_complete`, no phase/cleanup errors, and four cleanup completions.

- [ ] **Step 4: Capture the final postflight**

Repeat the complete CAN/process/socket snapshot and calculate deltas.

Expected: no CAN error/drop growth, no receiver or process remains, and `can0`
configuration is unchanged.

- [ ] **Step 5: Independently review physical evidence**

Dispatch one reviewer for script/output/spec conformance and one reviewer for
CAN/cleanup/hardware-safety evidence. Neither reviewer may rerun either phase.

Expected: reviewers distinguish API command returns from device
acknowledgement and require explicit operator observation before marking the
physical gate complete.

### Task 6: Final Release-Gate Report

**Files:**
- Inspect all evidence under:
  `motion-validation-1a7c820/`
- Inspect repository state:
  `/home/sjh/leisai_hand/roboparty_dexhand`

- [ ] **Step 1: Verify the repository remained unchanged by execution**

Run:

```bash
git status --short --branch
git rev-parse HEAD
```

Expected: clean `main`, HEAD at the plan commit, with no execution artifact in
the repository.

- [ ] **Step 2: Report release gates separately**

Report:

- real vcan two-socket: pass;
- Orange Pi native build/install/tests: pass;
- staged physical motion: the actual result, exact completed phase, and
  evidence limitations;
- callback quiescence: still blocked until written vendor confirmation; and
- SDK redistribution: still blocked until written license/authorization and
  GPL compatibility review.

Do not collapse the two external vendor gates into the physical test result.

- [ ] **Step 3: Preserve evidence and do not rerun**

Leave the remote evidence directory intact. Remove no logs and do not rerun a
successful or failed phase merely to improve logging. Any later retest must
use a new evidence directory and new operator authorization.
