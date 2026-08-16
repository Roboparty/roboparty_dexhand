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

The frozen Task 1 contract is:

- path:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`;
- SHA-256:
  `df774043b20156d541f2cd7bbf6611d96c8922ffe7b66ab0f4b7591dd4be45ce`;
- size: 143,279 bytes and 4,087 lines; and
- inventory: 55 test definitions, with the same 55 tests registered by
  `main()`.

The reviewed Task 2 motion harness is:

- path:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`;
- SHA-256:
  `bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9`;
- size: 15,158 bytes and 498 lines; and
- local binding files:
  `/tmp/roboparty-dexhand-motion-1a7c820/SHA256SUMS` and
  `/tmp/roboparty-dexhand-motion-1a7c820/MOTION_SCRIPT_SHA256SUM`.

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

Task 2 implemented the smallest harness that makes this approved artifact
GREEN. In particular, it rejects an invalid phase before factory creation and
does not copy the historical harness listing verbatim. Any byte change to
either frozen file invalidates the corresponding hash and both reviews;
Tasks 1-3 must then be repeated before any Orange Pi access.

Motion execution additionally requires the installed production code from
commit `db2da9fb90f407bdd5e3bbd3de691e775d27abd3` (tree
`aed385f28d3010fc167914872550f4bbb0a51057`). The historical Orange Pi result
for `92d742c` is background evidence only and its installed prefix must not be
used by either motion phase.

---

## File Map

- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`
  - Offline fake contract for both phases and every cleanup path.
- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
  - Dependency-injected physical harness with `small` and `full` modes.
- Create locally:
  `/tmp/roboparty-dexhand-deploy-db2da9f/bootstrap-evidence/`
  - Closed local evidence for source archive, fresh remote root, source
    transfer, and remote gate bootstrap; this directory is never recursively
    copied while it is being written.
- Create remotely from the clean production Git object:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/source/`
  - Exact `git archive` source for production commit `db2da9f`.
- Create remotely and keep disjoint:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/build/`,
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/`, and
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/install-gate/`
  - Native build, authoritative motion install, and separate relocatable
    install/export gate.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f/`
  - Immutable command, stdout, stderr, rc, timestamp, and selected-environment
    tuples for every authoritative non-motion gate.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/`
  - Immutable script and contract copies, hashes, exact commands, phase logs,
    and CAN pre/postflight evidence.
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
55-test artifact and hash in the execution amendment are authoritative.

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

Expected: fifty-five `PASS test_*` lines, `PASS all=55`, exit 0. No SDK or
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

### Task 4: Build Exact `db2da9f` Artifacts on Orange Pi Before Motion

**Files:**
- Read locally:
  `/home/sjh/leisai_hand/roboparty_dexhand` Git objects.
- Create locally:
  `/tmp/roboparty-dexhand-deploy-db2da9f/`.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/`.

- [ ] **Step 1: Create an exact source archive and provenance manifest**

Run locally. This archives the clean commit object, never working-tree bytes:

```bash
set -euo pipefail
DEXHAND_REPO=/home/sjh/leisai_hand/roboparty_dexhand
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f
DEPLOY_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
PRODUCTION_COMMIT=db2da9fb90f407bdd5e3bbd3de691e775d27abd3
PRODUCTION_TREE=aed385f28d3010fc167914872550f4bbb0a51057
test ! -e "$DEPLOY_STAGE"
mkdir "$DEPLOY_STAGE"
mkdir "$DEPLOY_EVIDENCE"
set -o noclobber
cat > "$DEPLOY_EVIDENCE/capture_gate.sh" <<'SH'
#!/bin/bash
set -euo pipefail
set -o noclobber

EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
LABEL=$1
shift
case "$LABEL" in
  *[!A-Za-z0-9_.-]*|'') exit 2 ;;
esac
STEM="$EVIDENCE/$LABEL"
{
  printf '%q ' "$@"
  printf '\n'
} > "$STEM.command"
/usr/bin/date --iso-8601=seconds > "$STEM.timestamp"
/bin/bash -c \
  'for name in PWD PATH PYTHONPATH LD_LIBRARY_PATH PYTHONOPTIMIZE CMAKE_PREFIX_PATH AMENT_PREFIX_PATH COLCON_PREFIX_PATH; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$STEM.environment"
set +e
"$@" > "$STEM.stdout" 2> "$STEM.stderr"
GATE_RC=$?
set -e
printf '%s\n' "$GATE_RC" > "$STEM.rc"
cat "$STEM.stdout"
cat "$STEM.stderr" >&2
test "$GATE_RC" = 0
SH
chmod 0555 "$DEPLOY_EVIDENCE/capture_gate.sh"
(cd "$DEPLOY_EVIDENCE" && \
  sha256sum capture_gate.sh > CAPTURE_GATE_SHA256SUM)
CAPTURE="$DEPLOY_EVIDENCE/capture_gate.sh"
"$CAPTURE" source_archive /bin/bash -c '
  set -euo pipefail
  repo=$1
  stage=$2
  commit=$3
  tree=$4
  archive="$stage/roboparty_dexhand-db2da9f.tar"
  test "$(git -C "$repo" rev-parse "$commit^{commit}")" = "$commit"
  test "$(git -C "$repo" rev-parse "$commit^{tree}")" = "$tree"
  test "$(git -C "$repo" cat-file -t "$commit")" = commit
  git -C "$repo" archive --format=tar --prefix=source/ \
    --output="$archive" "$commit"
  archive_sha256_line=$(sha256sum "$archive")
  read -r archive_sha256 archive_path <<< "$archive_sha256_line"
  test "$archive_path" = "$archive"
  printf "%s\n" \
    "source_commit=$commit" \
    "source_tree=$tree" \
    source_object_type=commit \
    archive_format=git-archive-tar \
    archive_prefix=source/ \
    archive_provenance="git archive --format=tar --prefix=source/ $commit" \
    "archive_sha256=$archive_sha256" \
    > "$stage/SOURCE_MANIFEST"
  cd "$stage"
  sha256sum roboparty_dexhand-db2da9f.tar SOURCE_MANIFEST \
    > SOURCE_TRANSFER_SHA256SUMS
  sha256sum -c SOURCE_TRANSFER_SHA256SUMS
  test "$(git get-tar-commit-id < roboparty_dexhand-db2da9f.tar)" = "$commit"
  grep -Fx "source_commit=$commit" SOURCE_MANIFEST
  grep -Fx "source_tree=$tree" SOURCE_MANIFEST
' _ "$DEXHAND_REPO" "$DEPLOY_STAGE" "$PRODUCTION_COMMIT" "$PRODUCTION_TREE"
```

Expected: local bundle `bootstrap-evidence` contains
`source_archive.command/.stdout/.stderr/.rc/.timestamp/.environment` with rc
`0`. `SOURCE_MANIFEST` binds the commit, tree, archive hash, prefix, and
generating command. This bundle later records the fresh-root and transfer
commands but is never recursively copied while being written. It contains
selected environment only, never a password. If the fixed local staging path
exists, stop and amend this plan with a new explicit suffix; do not reuse or
delete it.

- [ ] **Step 2: Re-establish power/network state and prove the root absent**

After the operator confirms stable board power, network cabling, physical
restraint, observer, and emergency stop, run locally through the capture
helper. OpenSSH obtains the password only from the controlling TTY; it is not
an argument, environment value, or logged input:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f
CAPTURE="$DEPLOY_STAGE/bootstrap-evidence/capture_gate.sh"
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f
REMOTE_SCRIPT='set -euo pipefail; root=/home/orangepi/roboparty_dexhand_motion_db2da9f; test ! -e "$root"; test "$(uname -m)" = aarch64; mkdir "$root"; mkdir "$root/evidence"'
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" remote_fresh_root /usr/bin/ssh orangepi "$REMOTE_COMMAND"
```

Expected: `remote_fresh_root` records the exact absence assertion, exact
`aarch64` assertion, and atomic parent/evidence `mkdir` with its complete
evidence tuple and rc `0`. The prior power loss invalidates every earlier
power, network, CAN, process, socket, and counter observation. If the root
exists, the captured gate fails; never reuse or delete the path.

- [ ] **Step 3: Transfer and verify the exact archive**

Run locally. `source_transfer` copies only closed source files and the capture
helper itself, not the bootstrap evidence directory that is recording the
transfer. Both `scp` and the following `ssh` read credentials only from TTY:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
"$CAPTURE" source_transfer /usr/bin/scp \
  "$DEPLOY_STAGE/roboparty_dexhand-db2da9f.tar" \
  "$DEPLOY_STAGE/SOURCE_MANIFEST" \
  "$DEPLOY_STAGE/SOURCE_TRANSFER_SHA256SUMS" \
  "$BOOTSTRAP_EVIDENCE/capture_gate.sh" \
  "$BOOTSTRAP_EVIDENCE/CAPTURE_GATE_SHA256SUM" \
  orangepi:/home/orangepi/roboparty_dexhand_motion_db2da9f/
REMOTE_SCRIPT='set -euo pipefail; root=/home/orangepi/roboparty_dexhand_motion_db2da9f; gate="$root/evidence/deployment-db2da9f"; test ! -e "$gate"; mkdir "$gate"; mv "$root/capture_gate.sh" "$root/CAPTURE_GATE_SHA256SUM" "$gate/"; cd "$gate"; sha256sum -c CAPTURE_GATE_SHA256SUM'
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" remote_gate_bootstrap /usr/bin/ssh orangepi "$REMOTE_COMMAND"
```

Expected: the local bootstrap bundle now has complete `source_transfer` and
`remote_gate_bootstrap` evidence. No active capture directory is recursively
copied. Reconnect interactively, then run the first independently captured
remote gate:

```bash
set -euo pipefail
ssh orangepi
```

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f
PRODUCTION_COMMIT=db2da9fb90f407bdd5e3bbd3de691e775d27abd3
PRODUCTION_TREE=aed385f28d3010fc167914872550f4bbb0a51057
DEPLOY_EVIDENCE="$REMOTE_ROOT/evidence/deployment-db2da9f"
CAPTURE="$DEPLOY_EVIDENCE/capture_gate.sh"
test -x "$CAPTURE"
(cd "$DEPLOY_EVIDENCE" && sha256sum -c CAPTURE_GATE_SHA256SUM)
"$CAPTURE" remote_archive_provenance /bin/bash -c '
  set -euo pipefail
  root=$1
  commit=$2
  tree=$3
  cd "$root"
  sha256sum -c SOURCE_TRANSFER_SHA256SUMS
  test "$(git get-tar-commit-id < roboparty_dexhand-db2da9f.tar)" = "$commit"
  grep -Fx "source_commit=$commit" SOURCE_MANIFEST
  grep -Fx "source_tree=$tree" SOURCE_MANIFEST
  tar --version | head -n 1 | grep -F "GNU tar"
  test ! -e "$root/source"
  tar --touch --no-same-owner --no-same-permissions \
    -xf roboparty_dexhand-db2da9f.tar -C "$root"
  test -d "$root/source"
' _ "$REMOTE_ROOT" "$PRODUCTION_COMMIT" "$PRODUCTION_TREE"
```

Expected: checks pass and GNU tar extracts without retaining potentially
future host mtimes. Source, build, prefix, install gate, and evidence remain
separate children of the new root.

- [ ] **Step 4: Configure, build, and run exactly eight default tests**

Run remotely using Unix Makefiles because Ninja is unavailable on the board:

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f
SOURCE_DIR="$REMOTE_ROOT/source"
BUILD_DIR="$REMOTE_ROOT/build"
PREFIX_DIR="$REMOTE_ROOT/prefix"
CAPTURE="$REMOTE_ROOT/evidence/deployment-db2da9f/capture_gate.sh"
test ! -e "$BUILD_DIR"
test ! -e "$PREFIX_DIR"
"$CAPTURE" configure /usr/bin/env \
  -u AMENT_PREFIX_PATH -u CMAKE_PREFIX_PATH -u COLCON_PREFIX_PATH \
  -u LD_LIBRARY_PATH -u PYTHONPATH \
  PYTHONDONTWRITEBYTECODE=1 PATH=/usr/bin:/bin \
  /usr/bin/cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror' \
  -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=OFF \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
"$CAPTURE" configure_contract /bin/bash -c '
  set -euo pipefail
  cache=$1/CMakeCache.txt
  grep -Fx "CMAKE_GENERATOR:INTERNAL=Unix Makefiles" "$cache"
  grep -Fx "CMAKE_BUILD_TYPE:STRING=Debug" "$cache"
  grep -Fx "CMAKE_CXX_FLAGS:STRING=-Wall -Wextra -Wpedantic -Werror" "$cache"
  grep -Fx "BUILD_TESTING:BOOL=ON" "$cache"
  grep -Fx "DEXHAND_ENABLE_VCAN_TESTS:BOOL=OFF" "$cache"
  grep -Eq "^CMAKE_FIND_USE_PACKAGE_REGISTRY:[^=]+=OFF$" "$cache"
  grep -Eq "^CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY:[^=]+=OFF$" "$cache"
  grep -Eq "^(_)?Python3_EXECUTABLE:[^=]+=/usr/bin/python3$" "$cache"
  fmt_dir=$(sed -n "s/^fmt_DIR:PATH=//p" "$cache")
  spdlog_dir=$(sed -n "s/^spdlog_DIR:PATH=//p" "$cache")
  pybind11_dir=$(sed -n "s/^pybind11_DIR:PATH=//p" "$cache")
  test -n "$fmt_dir"
  test -n "$spdlog_dir"
  test -n "$pybind11_dir"
  test "${fmt_dir#/usr/}" != "$fmt_dir"
  test "${spdlog_dir#/usr/}" != "$spdlog_dir"
  test "${pybind11_dir#/usr/}" != "$pybind11_dir"
' _ "$BUILD_DIR"
"$CAPTURE" build /usr/bin/env \
  -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONDONTWRITEBYTECODE=1 \
  PATH=/usr/bin:/bin /usr/bin/cmake --build "$BUILD_DIR" --parallel 2
"$CAPTURE" ctest /usr/bin/env \
  -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONDONTWRITEBYTECODE=1 PYTHONNOUSERSITE=1 PATH=/usr/bin:/bin \
  /bin/bash -c '
    set -euo pipefail
    build=$1
    test_count=$(/usr/bin/ctest --test-dir "$build" -N | \
      sed -n "s/^Total Tests: //p")
    test "$test_count" = 8
    /usr/bin/ctest --test-dir "$build" --output-on-failure --timeout 30
  ' _ "$BUILD_DIR"
```

Expected: system `fmt`, AArch64 `/usr/bin/python3`, warning-clean Debug build,
and exactly `100% tests passed, 0 tests failed out of 8`. Any failure blocks
motion.

- [ ] **Step 5: Run the plain install and separate relocatable gate**

Run remotely:

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f
SOURCE_DIR="$REMOTE_ROOT/source"
BUILD_DIR="$REMOTE_ROOT/build"
PREFIX_DIR="$REMOTE_ROOT/prefix"
INSTALL_GATE="$REMOTE_ROOT/install-gate"
CAPTURE="$REMOTE_ROOT/evidence/deployment-db2da9f/capture_gate.sh"
"$CAPTURE" plain_install /usr/bin/env \
  -u DESTDIR -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONDONTWRITEBYTECODE=1 PATH=/usr/bin:/bin \
  /bin/bash -c '
    set -euo pipefail
    build=$1
    prefix=$2
    test ! -e "$prefix"
    /usr/bin/cmake --install "$build"
    test -d "$prefix"
  ' _ "$BUILD_DIR" "$PREFIX_DIR"
"$CAPTURE" install_export /usr/bin/env \
  -u DESTDIR -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONDONTWRITEBYTECODE=1 PATH=/usr/bin:/bin \
  /bin/bash -c '
    set -euo pipefail
    build=$1
    source=$2
    gate=$3
    relocated=$4
    test ! -e "$gate"
    test ! -e "$relocated"
    /usr/bin/cmake \
      -DBUILD_DIR="$build" \
      -DSOURCE_DIR="$source" \
      -DPREFIX="$gate" \
      -DPYTHON_EXECUTABLE=/usr/bin/python3 \
      -P "$source/tests/check_install_export.cmake"
    test -d "$relocated"
  ' _ "$BUILD_DIR" "$SOURCE_DIR" "$INSTALL_GATE" \
  "$REMOTE_ROOT/install-gate-relocated"
```

Expected: the motion prefix is the plain install at `prefix`; the independent
gate installs, relocates, and validates its own `install-gate-relocated`
prefix. Neither prefix overlaps source or build. Any failure blocks motion.

- [ ] **Step 6: Prove AArch64 linkage, exact-one SDK, hash, and Python API**

Run remotely without initializing a hand or touching CAN:

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f
PREFIX_DIR="$REMOTE_ROOT/prefix"
PYTHON_SITE="$PREFIX_DIR/lib/python3.10/site-packages"
SDK_LIBRARY="$PREFIX_DIR/lib/libLHandProLib.so"
DEXHAND_LIBRARY="$PREFIX_DIR/lib/libdexhand.so"
CAPTURE="$REMOTE_ROOT/evidence/deployment-db2da9f/capture_gate.sh"
"$CAPTURE" artifact_gate /bin/bash -c '
  set -euo pipefail
  prefix=$1
  deploy_evidence=$2
  python_site="$prefix/lib/python3.10/site-packages"
  sdk="$prefix/lib/libLHandProLib.so"
  dexhand="$prefix/lib/libdexhand.so"
  set +e
  module_listing=$(find "$python_site" -maxdepth 1 -type f \
    -name "dexhand_py*.so" -print)
  module_find_rc=$?
  sdk_listing=$(find "$prefix" -type f -name libLHandProLib.so -print)
  sdk_find_rc=$?
  dexhand_listing=$(find "$prefix" -type f -name libdexhand.so -print)
  dexhand_find_rc=$?
  set -e
  test "$module_find_rc" = 0
  test "$sdk_find_rc" = 0
  test "$dexhand_find_rc" = 0
  modules=()
  sdks=()
  dexhands=()
  if test -n "$module_listing"; then
    mapfile -t modules <<< "$module_listing"
  fi
  if test -n "$sdk_listing"; then
    mapfile -t sdks <<< "$sdk_listing"
  fi
  if test -n "$dexhand_listing"; then
    mapfile -t dexhands <<< "$dexhand_listing"
  fi
  test "${#modules[@]}" = 1
  module=${modules[0]}
  test "${#sdks[@]}" = 1
  test "${sdks[0]}" = "$sdk"
  test "${#dexhands[@]}" = 1
  test "${dexhands[0]}" = "$dexhand"
  sdk_hash_line=$(sha256sum "$sdk")
  read -r sdk_hash sdk_path <<< "$sdk_hash_line"
  test "$sdk_path" = "$sdk"
  test "$sdk_hash" = \
    476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044
  for artifact in "$sdk" "$dexhand" "$module"; do
    readelf -h "$artifact" | grep -Eq "Machine:[[:space:]]+AArch64"
    set +e
    ldd_output=$(ldd "$artifact" 2>&1)
    ldd_rc=$?
    set -e
    printf "%s\n" "$ldd_output"
    if ! test "$ldd_rc" = 0; then
      exit 1
    fi
    if grep -F "not found" <<< "$ldd_output"; then
      exit 1
    fi
  done
  sdk_hash_line=$(sha256sum "$sdk")
  dexhand_hash_line=$(sha256sum "$dexhand")
  module_hash_line=$(sha256sum "$module")
  read -r sdk_hash sdk_path <<< "$sdk_hash_line"
  read -r dexhand_hash dexhand_path <<< "$dexhand_hash_line"
  read -r module_hash module_path <<< "$module_hash_line"
  test "$sdk_path" = "$sdk"
  test "$dexhand_path" = "$dexhand"
  test "$module_path" = "$module"
  manifest="$deploy_evidence/RUNTIME_MANIFEST.sha256"
  anchor="$deploy_evidence/RUNTIME_MANIFEST_ANCHOR.sha256"
  test ! -e "$manifest"
  test ! -e "$anchor"
  manifest_tmp=$(mktemp "$deploy_evidence/.runtime-manifest.XXXXXX")
  anchor_tmp=$(mktemp "$deploy_evidence/.runtime-anchor.XXXXXX")
  cleanup_runtime_files() {
    test -z "${manifest_tmp:-}" || rm -f "$manifest_tmp"
    test -z "${anchor_tmp:-}" || rm -f "$anchor_tmp"
  }
  trap cleanup_runtime_files EXIT
  printf "%s  %s\n%s  %s\n%s  %s\n" \
    "$sdk_hash" "$sdk" \
    "$dexhand_hash" "$dexhand" \
    "$module_hash" "$module" > "$manifest_tmp"
  ln "$manifest_tmp" "$manifest"
  rm "$manifest_tmp"
  manifest_tmp=
  (cd "$deploy_evidence" && \
    sha256sum RUNTIME_MANIFEST.sha256 > "$anchor_tmp")
  ln "$anchor_tmp" "$anchor"
  rm "$anchor_tmp"
  anchor_tmp=
  /usr/bin/sync -f "$deploy_evidence"
  (cd "$deploy_evidence" && \
    sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
  sha256sum -c "$manifest"
' _ "$PREFIX_DIR" "$REMOTE_ROOT/evidence/deployment-db2da9f"
"$CAPTURE" python_construction /usr/bin/env \
  -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1 \
  /usr/bin/python3 -I -c '
import sys
from pathlib import Path

sys.path.insert(0, sys.argv[1])
prefix = Path(sys.argv[2]).resolve()
import dexhand_py
module_path = Path(dexhand_py.__file__).resolve()
if not module_path.is_relative_to(prefix):
    raise RuntimeError(f"module outside prefix: {module_path}")
if not hasattr(dexhand_py.HandDriver, "check_health"):
    raise RuntimeError("check_health is missing")
hand = dexhand_py.HandDriver.create_hand(
    hand_type="LHandPro",
    interface_type="canfd",
    interface="construction-only-no-can",
    hand_model=dexhand_py.HandModel.LHANDPRO_6DOF,
    canfd_node_id=1,
)
hand.check_health()
if hand.get_can_name() != "construction-only-no-can":
    raise RuntimeError("constructed interface mismatch")
print(module_path)
print("PASS construction-only check_health; init_hand not called")
' dexhand-construction "$PYTHON_SITE" "$PREFIX_DIR"
```

Expected: all three runtime objects are AArch64, `ldd` has no missing entry,
exactly one installed SDK has the fixed hash, and Python resolves inside the
new prefix with `check_health`. The probe only constructs the object; it does
not call `init_hand`, open `can0`, or issue a CAN command.

### Task 5: Stage Evidence and Run Phase A Once

**Files:**
- Copy the frozen harness, contract, and both local hash files from:
  `/tmp/roboparty-dexhand-motion-1a7c820/`
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/`

- [ ] **Step 1: Confirm the evidence directory is absent**

First verify the closed local bootstrap/transfer evidence:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f/bootstrap-evidence
BOOTSTRAP_GATES=(
  source_archive remote_fresh_root source_transfer remote_gate_bootstrap
)
for gate in "${BOOTSTRAP_GATES[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
  done
  test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
done
```

Then run through interactive SSH:

```bash
set -euo pipefail
test -d \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f
test ! -e \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
```

Expected: exit 0. Do not reuse a partial evidence directory.

- [ ] **Step 2: Create the directory and copy the exact script**

Run remotely:

```bash
set -euo pipefail
mkdir \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
cp /home/orangepi/roboparty_dexhand_motion_db2da9f/SOURCE_MANIFEST \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/
cp \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f/RUNTIME_MANIFEST.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f/RUNTIME_MANIFEST_ANCHOR.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/
```

Exit SSH and copy the exact local files with interactive `scp`:

```bash
set -euo pipefail
MOTION_STAGE=/tmp/roboparty-dexhand-motion-1a7c820
scp \
  "$MOTION_STAGE/staged_motion_validation.py" \
  "$MOTION_STAGE/test_staged_motion_validation.py" \
  "$MOTION_STAGE/SHA256SUMS" \
  "$MOTION_STAGE/MOTION_SCRIPT_SHA256SUM" \
  orangepi:/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/
```

Do not place credentials in a command log.

- [ ] **Step 3: Verify local and remote script hashes match**

Reconnect, change to the evidence directory, and run remotely:

```bash
set -euo pipefail
cd /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
sha256sum -c SHA256SUMS
sha256sum -c MOTION_SCRIPT_SHA256SUM
script_hash_line=$(sha256sum staged_motion_validation.py)
read -r script_hash script_path <<< "$script_hash_line"
test "$script_path" = staged_motion_validation.py
test "$script_hash" = \
  bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9
test_hash_line=$(sha256sum test_staged_motion_validation.py)
read -r test_hash test_path <<< "$test_hash_line"
test "$test_path" = test_staged_motion_validation.py
test "$test_hash" = \
  df774043b20156d541f2cd7bbf6611d96c8922ffe7b66ab0f4b7591dd4be45ce
(cd \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f && \
  sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
cmp RUNTIME_MANIFEST.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f/RUNTIME_MANIFEST.sha256
cmp RUNTIME_MANIFEST_ANCHOR.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f/RUNTIME_MANIFEST_ANCHOR.sha256
sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256
sha256sum -c RUNTIME_MANIFEST.sha256
```

Expected: both files are `OK` and match their fixed hashes. The frozen test is
provenance evidence only and is never executed on the board. Verify every
authoritative non-motion gate and its complete evidence tuple before creating
the motion capture tools:

```bash
set -euo pipefail
DEPLOY_EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f
DEPLOY_GATES=(
  remote_archive_provenance configure configure_contract build ctest
  plain_install install_export artifact_gate python_construction
)
for gate in "${DEPLOY_GATES[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$DEPLOY_EVIDENCE/$gate.$suffix"
  done
  test "$(cat "$DEPLOY_EVIDENCE/$gate.rc")" = 0
done
```

Expected: archive/provenance, configuration, build, exact 8/8 CTest, plain
install, independent install/export relocation, AArch64 artifact, and
construction-only Python gates are independently reproducible from command,
stdout, stderr, rc, timestamp, and selected-environment records. No password
or credential is present in the bundle.

- [ ] **Step 4: Install the exact capture protocol and capture Phase A preflight**

Create the reusable capture helper remotely in the new evidence directory.
Each command gets distinct `.command`, `.stdout`, `.stderr`, and `.rc` files;
shell `noclobber` prevents a second capture from replacing evidence:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
set -o noclobber
cat > "$EVIDENCE/capture_snapshot.sh" <<'SH'
#!/bin/bash
set -euo pipefail
set -o noclobber

EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
PHASE=$1
WINDOW=$2
IP_COMMAND=$(command -v ip)
test -n "$IP_COMMAND"
test "${IP_COMMAND#/}" != "$IP_COMMAND"
test -x "$IP_COMMAND"
case "$PHASE:$WINDOW" in
  phase-small:pre|phase-small:post|phase-full:pre|phase-full:post) ;;
  *) exit 2 ;;
esac

capture() {
  local name=$1
  shift
  local stem="$EVIDENCE/$PHASE.$WINDOW.$name"
  local command_rc child_rc rc_record_rc complete_rc pre_sync_rc post_sync_rc
  command_rc=0
  child_rc=125
  rc_record_rc=0
  complete_rc=0
  pre_sync_rc=125
  post_sync_rc=125
  set +e
  {
    printf '%q ' "$@"
    printf '\n'
  } > "$stem.command" 2>/dev/null
  command_rc=$?
  if test "$command_rc" = 0; then
    "$@" > "$stem.stdout" 2> "$stem.stderr"
    child_rc=$?
  fi
  printf '%s\n' "$child_rc" > "$stem.rc" 2>/dev/null
  rc_record_rc=$?
  if test "$command_rc" = 0 && test "$rc_record_rc" = 0 && \
      test -f "$stem.stdout" && test -f "$stem.stderr"; then
    /usr/bin/sync -f "$EVIDENCE"
    pre_sync_rc=$?
    if test "$pre_sync_rc" = 0; then
      : > "$stem.complete" 2>/dev/null
      complete_rc=$?
      if test "$complete_rc" = 0; then
        /usr/bin/sync -f "$EVIDENCE"
        post_sync_rc=$?
      fi
    fi
  else
    complete_rc=125
  fi
  set -e
  if ! test "$command_rc" = 0 || ! test "$child_rc" = 0 || \
      ! test "$rc_record_rc" = 0 || ! test "$pre_sync_rc" = 0 || \
      ! test "$complete_rc" = 0 || ! test "$post_sync_rc" = 0; then
    SNAPSHOT_FAILURE=1
  fi
  return 0
}

snapshot_can_counters() {
  /usr/bin/python3 - "$IP_COMMAND" <<'PY'
import json
from pathlib import Path
import re
import subprocess
import sys

text = subprocess.run(
    [sys.argv[1], '-details', '-statistics', 'link', 'show', 'can0'],
    check=True,
    capture_output=True,
    text=True,
).stdout

def match(pattern, label, flags=0):
    found = re.search(pattern, text, flags)
    if found is None:
        raise RuntimeError(f'missing CAN field: {label}')
    return found

flags = match(r'^\d+:\s+can0:\s+<([^>]*)>', 'flags', re.MULTILINE)
berr = match(r'berr-counter\s+tx\s+(\d+)\s+rx\s+(\d+)', 'berr')
protocol = match(
    r're-started\s+bus-errors\s+arbit-lost\s+error-warn\s+'
    r'error-pass\s+bus-off\s*\n\s*(\d+)\s+(\d+)\s+(\d+)\s+'
    r'(\d+)\s+(\d+)\s+(\d+)',
    'protocol counters',
)
data = {
    'config': {
        'flags': sorted(flags.group(1).split(',')),
        'mtu': int(match(r'\bmtu\s+(\d+)', 'mtu').group(1)),
        'state': match(
            r'\bcan\s+state\s+([A-Z-]+)', 'can state').group(1),
        'bitrate': int(match(
            r'^\s+bitrate\s+(\d+)', 'bitrate', re.MULTILINE).group(1)),
        'dbitrate': int(match(
            r'^\s+dbitrate\s+(\d+)', 'dbitrate', re.MULTILINE).group(1)),
    },
    'berr_tx': int(berr.group(1)),
    'berr_rx': int(berr.group(2)),
    'restarted': int(protocol.group(1)),
    'bus_errors': int(protocol.group(2)),
    'arbit_lost': int(protocol.group(3)),
    'error_warn': int(protocol.group(4)),
    'error_pass': int(protocol.group(5)),
    'bus_off': int(protocol.group(6)),
}
stats = Path('/sys/class/net/can0/statistics')
for direction in ('rx', 'tx'):
    for field in ('errors', 'dropped', 'packets', 'bytes'):
        key = f'{direction}_{field}'
        data[key] = int((stats / key).read_text(encoding='ascii').strip())
print(json.dumps(data, sort_keys=True, separators=(',', ':')))
PY
}

SNAPSHOT_FAILURE=0
capture timestamp /usr/bin/date --iso-8601=seconds
capture uptime /usr/bin/uptime -s
capture uname /usr/bin/uname -a
capture boot_id /usr/bin/cat /proc/sys/kernel/random/boot_id
capture address "$IP_COMMAND" -brief address
capture can_brief "$IP_COMMAND" -brief link show can0
capture can_details "$IP_COMMAND" -details -statistics link show can0
capture can_counters snapshot_can_counters
capture rcvlist /bin/bash -c \
  'set -euo pipefail; for name in all eff err fil inv sff; do path="/proc/net/can/rcvlist_$name"; printf "=== %s ===\n" "$name"; /usr/bin/sed "/^[[:space:]]*$/d" "$path"; done'
capture processes /usr/bin/ps -eo pid=,comm=,args=
capture source_manifest /usr/bin/cat \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/SOURCE_MANIFEST
capture sdk_hash /usr/bin/sha256sum \
  /home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/lib/libLHandProLib.so
capture sdk_hash_gate /bin/bash -c \
  'set -euo pipefail; read -r sdk_hash _ < "$1"; test "$sdk_hash" = 476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044' \
  _ "$EVIDENCE/$PHASE.$WINDOW.sdk_hash.stdout"
capture script_hash /usr/bin/sha256sum \
  "$EVIDENCE/staged_motion_validation.py"
capture script_hash_gate /bin/bash -c \
  'set -euo pipefail; read -r script_hash script_path < "$1"; test "$script_hash" = bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9; test "$script_path" = "$2"' \
  _ "$EVIDENCE/$PHASE.$WINDOW.script_hash.stdout" \
  "$EVIDENCE/staged_motion_validation.py"
capture environment /bin/bash -c \
  'set -euo pipefail; for name in PATH PYTHONPATH LD_LIBRARY_PATH PYTHONNOUSERSITE PYTHONDONTWRITEBYTECODE CMAKE_PREFIX_PATH AMENT_PREFIX_PATH COLCON_PREFIX_PATH; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done'
capture receiver_gate /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_can_receivers.py" \
  "$EVIDENCE/$PHASE.$WINDOW.rcvlist.stdout"
capture can_gate /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_can_evidence.py" \
  "$EVIDENCE/$PHASE.$WINDOW.can_counters.stdout"
capture runtime_gate /bin/bash "$EVIDENCE/check_runtime_gate.sh"
if test "$PHASE" = phase-full; then
  capture phase_a_binding /bin/bash \
    "$EVIDENCE/check_phase_a_acceptance.sh" \
    "$EVIDENCE/$PHASE.$WINDOW.boot_id.stdout"
fi

test "$SNAPSHOT_FAILURE" = 0
SH

cat > "$EVIDENCE/check_can_evidence.py" <<'PY'
#!/usr/bin/python3
import json
from pathlib import Path
import sys

ZERO_KEYS = (
    'berr_tx', 'berr_rx', 'restarted', 'bus_errors', 'arbit_lost',
    'error_warn', 'error_pass', 'bus_off', 'rx_errors', 'rx_dropped',
    'tx_errors', 'tx_dropped',
)
TRAFFIC_KEYS = ('rx_packets', 'rx_bytes', 'tx_packets', 'tx_bytes')

def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))

def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)

before = load(sys.argv[1])
config = before['config']
require({'UP', 'LOWER_UP'} <= set(config['flags']), f'bad flags: {config}')
require(config['mtu'] == 72, f'bad MTU: {config}')
require(config['state'] == 'ERROR-ACTIVE', f'bad state: {config}')
require(config['bitrate'] == 1000000, f'bad bitrate: {config}')
require(config['dbitrate'] == 5000000, f'bad dbitrate: {config}')
for key in ZERO_KEYS:
    require(before[key] == 0, f'nonzero preflight {key}: {before[key]}')

result = {'pre': before}
if len(sys.argv) == 3:
    after = load(sys.argv[2])
    require(after['config'] == config,
            f'CAN configuration changed: {config} != {after["config"]}')
    deltas = {key: after[key] - before[key]
              for key in ZERO_KEYS + TRAFFIC_KEYS}
    for key in ZERO_KEYS:
        require(deltas[key] == 0, f'nonzero {key} delta: {deltas[key]}')
    for key in TRAFFIC_KEYS:
        require(deltas[key] >= 0, f'negative {key} delta: {deltas[key]}')
    result = {'config': config, 'deltas': deltas}
print(json.dumps(result, sort_keys=True, separators=(',', ':')))
PY

cat > "$EVIDENCE/check_can_receivers.py" <<'PY'
#!/usr/bin/python3
from pathlib import Path
import sys

SECTIONS = ('all', 'eff', 'err', 'fil', 'inv', 'sff')

def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)

def parse(text):
    lines = text.splitlines()
    cursor = 0
    for name in SECTIONS:
        header = f'=== {name} ==='
        require(cursor < len(lines), f'missing section: {name}')
        require(lines[cursor] == header,
                f'expected {header!r}, got {lines[cursor]!r}')
        cursor += 1
        expected = [
            f"receive list 'rx_{name}':",
            '  (any: no entry)',
            '  (can0: no entry)',
        ]
        body = []
        while cursor < len(lines) and not lines[cursor].startswith('=== '):
            body.append(lines[cursor])
            cursor += 1
        require(body == expected,
                f'{name}: expected exact any/can0 no-entry state, got {body!r}')
    require(cursor == len(lines), f'unknown trailing receiver data: {lines[cursor:]!r}')

def clean_text():
    return '\n'.join(
        line
        for name in SECTIONS
        for line in (f'=== {name} ===',
                     f"receive list 'rx_{name}':",
                     '  (any: no entry)',
                     '  (can0: no entry)')
    ) + '\n'

def expect_failure(text):
    try:
        parse(text)
    except (RuntimeError, ValueError):
        return
    raise RuntimeError('negative receiver self-test unexpectedly passed')

def self_test():
    clean = clean_text()
    parse(clean)
    expect_failure(clean.replace(
        '  (can0: no entry)',
        '  can0  00000000  00000000  0000000000000000  0', 1))
    expect_failure(clean.replace(
        '  (any: no entry)',
        '  any   00000000  00000000  0000000000000000  0', 1))
    print('PASS receiver parser self-test')

if sys.argv[1:] == ['--self-test']:
    self_test()
elif len(sys.argv) == 2:
    parse(Path(sys.argv[1]).read_text(encoding='ascii'))
    print('PASS all six receiver lists are empty for any/can0')
else:
    raise SystemExit('usage: check_can_receivers.py FILE | --self-test')
PY

cat > "$EVIDENCE/check_runtime_manifest.py" <<'PY'
#!/usr/bin/python3
import hashlib
import importlib
from pathlib import Path
import re
import sys

SDK_SHA = '476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044'
LINE = re.compile(r'([0-9a-f]{64})  (/[^\n]+)')
ANCHOR = re.compile(r'([0-9a-f]{64})  RUNTIME_MANIFEST\.sha256\n')

def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def load_anchor(path):
    text = Path(path).read_text(encoding='ascii')
    match = ANCHOR.fullmatch(text)
    require(match is not None, f'malformed runtime anchor: {path}')
    return match.group(1)

def load_manifest(path):
    lines = Path(path).read_text(encoding='ascii').splitlines()
    require(len(lines) == 3, f'expected 3 runtime records: {lines!r}')
    records = []
    for line in lines:
        match = LINE.fullmatch(line)
        require(match is not None, f'malformed runtime record: {line!r}')
        records.append((match.group(1), Path(match.group(2))))
    require(len({path for _, path in records}) == 3,
            'runtime paths are not unique')
    return records

deploy_manifest = Path(sys.argv[1])
deploy_anchor = Path(sys.argv[2])
motion_manifest = Path(sys.argv[3])
motion_anchor = Path(sys.argv[4])
prefix = Path(sys.argv[5]).resolve()
require(deploy_manifest.read_bytes() == motion_manifest.read_bytes(),
        'deployment/motion runtime manifests differ')
require(deploy_anchor.read_bytes() == motion_anchor.read_bytes(),
        'deployment/motion runtime anchors differ')
manifest_sha = digest(deploy_manifest)
require(load_anchor(deploy_anchor) == manifest_sha,
        'deployment runtime anchor mismatch')
require(load_anchor(motion_anchor) == manifest_sha,
        'motion runtime anchor mismatch')
records = load_manifest(deploy_manifest)
site = prefix / 'lib/python3.10/site-packages'
sdks = sorted(prefix.rglob('libLHandProLib.so'))
dexhands = sorted(prefix.rglob('libdexhand.so'))
modules = sorted(prefix.rglob('dexhand_py*.so'))
require(len(sdks) == 1, f'expected one installed SDK: {sdks!r}')
require(len(dexhands) == 1, f'expected one dexhand library: {dexhands!r}')
require(len(modules) == 1, f'expected one installed module: {modules!r}')
expected = {
    sdks[0].resolve(),
    dexhands[0].resolve(),
    modules[0].resolve(),
}
require(sdks[0].resolve() == prefix / 'lib/libLHandProLib.so',
        f'SDK is not at fixed path: {sdks[0]}')
require(dexhands[0].resolve() == prefix / 'lib/libdexhand.so',
        f'dexhand library is not at fixed path: {dexhands[0]}')
require(modules[0].parent.resolve() == site,
        f'module is not in fixed Python site: {modules[0]}')
actual = {path.resolve() for _, path in records}
require(actual == expected, f'runtime path mismatch: {actual!r} != {expected!r}')
for expected_sha, path in records:
    require(path.is_absolute(), f'non-absolute runtime path: {path}')
    require(digest(path) == expected_sha, f'runtime hash mismatch: {path}')
sdk_record = next(
    sha for sha, path in records if path.resolve() == prefix / 'lib/libLHandProLib.so')
require(sdk_record == SDK_SHA, f'unexpected AArch64 SDK hash: {sdk_record}')
sys.path.insert(0, str(site))
module = importlib.import_module('dexhand_py')
module_path = Path(module.__file__).resolve()
require(module_path == modules[0].resolve(),
        f'imported unexpected module: {module_path}')
require(module_path.is_relative_to(prefix),
        f'imported module outside prefix: {module_path}')
loaded_paths = {
    Path(line.rsplit(maxsplit=1)[-1]).resolve()
    for line in Path('/proc/self/maps').read_text(encoding='ascii').splitlines()
    if line.rsplit(maxsplit=1)[-1].startswith('/')
}
require(sdks[0].resolve() in loaded_paths,
        f'installed SDK is not the loaded SDK: {sdks[0]}')
require(dexhands[0].resolve() in loaded_paths,
        f'installed dexhand library is not loaded: {dexhands[0]}')
print(f'PASS runtime manifest/import gate: {module_path}')
PY

cat > "$EVIDENCE/check_motion_output.py" <<'PY'
#!/usr/bin/python3
import json
from pathlib import Path
import sys

LABELS = {
    'small': ('small_high', 'small_return'),
    'full': (
        'cycle_1_high', 'cycle_1_return',
        'cycle_2_high', 'cycle_2_return',
        'cycle_3_high', 'cycle_3_return', 'final_open',
    ),
}
CLEANUP = ('stop_motors', 'set_enable', 'set_move_no_home', 'deinit_hand')

def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)

def expected_sequence(phase):
    sequence = [
        ('phase_start', None),
        ('init_complete', None),
        ('move_no_home_reset', None),
        ('sample', 'baseline'),
    ]
    for label in LABELS[phase]:
        sequence.extend((('motion_command', label), ('sample', label)))
    sequence.extend(('cleanup_complete', step) for step in CLEANUP)
    sequence.append(('phase_complete', None))
    return sequence

def validate_text(text, phase):
    require(phase in LABELS, f'unknown phase: {phase}')
    lines = text.splitlines()
    require(lines, 'empty motion output')
    events = []
    for number, line in enumerate(lines, 1):
        require(line != '', f'blank JSON line: {number}')
        try:
            def reject_constant(value):
                raise ValueError(f'non-standard JSON constant: {value}')
            def unique_object(pairs):
                result = {}
                for key, value in pairs:
                    require(key not in result, f'duplicate JSON key: {key}')
                    result[key] = value
                return result
            payload = json.loads(
                line,
                parse_constant=reject_constant,
                object_pairs_hook=unique_object,
            )
        except json.JSONDecodeError as error:
            raise RuntimeError(f'non-JSON line {number}: {error}') from error
        require(type(payload) is dict, f'line {number} is not an object')
        require(payload.get('phase') == phase,
                f'line {number} phase mismatch: {payload!r}')
        event = payload.get('event')
        require(type(event) is str, f'line {number} missing event')
        if event in ('sample', 'motion_command'):
            detail = payload.get('label')
        elif event == 'cleanup_complete':
            detail = payload.get('step')
        else:
            detail = None
        events.append((event, detail))
    expected = expected_sequence(phase)
    require(events == expected,
            f'event sequence mismatch:\nexpected={expected!r}\nactual={events!r}')
    require(sum(event == 'phase_complete' for event, _ in events) == 1,
            'phase_complete is not unique')
    forbidden = {'phase_error', 'cleanup_error', 'interrupted'}
    require(not forbidden.intersection(event for event, _ in events),
            f'error/interrupted event present: {events!r}')

def make_valid(phase):
    output = []
    for event, detail in expected_sequence(phase):
        payload = {'event': event, 'phase': phase}
        if event in ('sample', 'motion_command'):
            payload['label'] = detail
        elif event == 'cleanup_complete':
            payload['step'] = detail
        output.append(json.dumps(payload, separators=(',', ':')))
    return '\n'.join(output) + '\n'

def expect_failure(text, phase):
    try:
        validate_text(text, phase)
    except (RuntimeError, ValueError):
        return
    raise RuntimeError('negative motion-output self-test unexpectedly passed')

def self_test():
    for phase in LABELS:
        valid = make_valid(phase)
        validate_text(valid, phase)
        expect_failure(valid + '{"event":"unknown","phase":"%s"}\n' % phase,
                       phase)
        expect_failure('not-json\n' + valid, phase)
        expect_failure(valid.replace('"phase_complete"', '"interrupted"', 1),
                       phase)
        expect_failure('\n'.join(valid.splitlines()[:-1]) + '\n', phase)
        complete = json.dumps({'event': 'phase_complete', 'phase': phase})
        expect_failure(valid + complete + '\n', phase)
        expect_failure('{"event":"phase_start","event":"phase_start",'
                       f'"phase":"{phase}"}}\n' + valid, phase)
        expect_failure('{"event":"phase_start","phase":"%s",'
                       '"value":NaN}\n' % phase + valid, phase)
    print('PASS strict motion-output self-test')

if sys.argv[1:] == ['--self-test']:
    self_test()
elif len(sys.argv) == 3:
    validate_text(Path(sys.argv[2]).read_text(encoding='utf-8'), sys.argv[1])
    print(f'PASS strict {sys.argv[1]} motion output')
else:
    raise SystemExit('usage: check_motion_output.py PHASE FILE | --self-test')
PY

cat > "$EVIDENCE/check_runtime_gate.sh" <<'SH'
#!/bin/bash
set -euo pipefail
EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
DEPLOY=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/deployment-db2da9f
PREFIX=/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix
(cd "$DEPLOY" && sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
(cd "$EVIDENCE" && sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
cmp "$DEPLOY/RUNTIME_MANIFEST.sha256" "$EVIDENCE/RUNTIME_MANIFEST.sha256"
cmp "$DEPLOY/RUNTIME_MANIFEST_ANCHOR.sha256" \
  "$EVIDENCE/RUNTIME_MANIFEST_ANCHOR.sha256"
sha256sum -c "$DEPLOY/RUNTIME_MANIFEST.sha256"
sha256sum -c "$EVIDENCE/RUNTIME_MANIFEST.sha256"
/usr/bin/env -u LD_LIBRARY_PATH -u PYTHONPATH -u PYTHONOPTIMIZE \
  PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1 \
  /usr/bin/python3 -I "$EVIDENCE/check_runtime_manifest.py" \
  "$DEPLOY/RUNTIME_MANIFEST.sha256" \
  "$DEPLOY/RUNTIME_MANIFEST_ANCHOR.sha256" \
  "$EVIDENCE/RUNTIME_MANIFEST.sha256" \
  "$EVIDENCE/RUNTIME_MANIFEST_ANCHOR.sha256" \
  "$PREFIX"
SH

cat > "$EVIDENCE/capture_command.sh" <<'SH'
#!/bin/bash
set -euo pipefail
set -o noclobber

EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
PHASE=$1
WINDOW=$2
NAME=$3
shift 3
case "$PHASE:$WINDOW" in
  phase-small:pre|phase-small:post|phase-full:pre|phase-full:post) ;;
  *) exit 2 ;;
esac
case "$NAME" in
  *[!A-Za-z0-9_.-]*|'') exit 2 ;;
esac
STEM="$EVIDENCE/$PHASE.$WINDOW.$NAME"
COMMAND_RC=0
CHILD_RC=125
RC_RECORD_RC=0
TIMESTAMP_RC=0
ENVIRONMENT_RC=0
COMPLETE_RC=0
PRE_SYNC_RC=125
POST_SYNC_RC=125
set +e
{
  printf '%q ' "$@"
  printf '\n'
} > "$STEM.command" 2>/dev/null
COMMAND_RC=$?
/usr/bin/date --iso-8601=seconds > "$STEM.timestamp" 2>/dev/null
TIMESTAMP_RC=$?
/bin/bash -c \
  'for name in PWD PATH PYTHONPATH LD_LIBRARY_PATH PYTHONOPTIMIZE; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$STEM.environment" 2>/dev/null
ENVIRONMENT_RC=$?
if test "$COMMAND_RC" = 0; then
  "$@" > "$STEM.stdout" 2> "$STEM.stderr"
  CHILD_RC=$?
fi
printf '%s\n' "$CHILD_RC" > "$STEM.rc" 2>/dev/null
RC_RECORD_RC=$?
if test "$COMMAND_RC" = 0 && test "$TIMESTAMP_RC" = 0 && \
    test "$ENVIRONMENT_RC" = 0 && test "$RC_RECORD_RC" = 0 && \
    test -f "$STEM.stdout" && test -f "$STEM.stderr"; then
  /usr/bin/sync -f "$EVIDENCE"
  PRE_SYNC_RC=$?
  if test "$PRE_SYNC_RC" = 0; then
    : > "$STEM.complete" 2>/dev/null
    COMPLETE_RC=$?
    if test "$COMPLETE_RC" = 0; then
      /usr/bin/sync -f "$EVIDENCE"
      POST_SYNC_RC=$?
    fi
  fi
else
  COMPLETE_RC=125
fi
set -e
if ! test "$COMMAND_RC" = 0 || ! test "$TIMESTAMP_RC" = 0 || \
    ! test "$ENVIRONMENT_RC" = 0 || ! test "$RC_RECORD_RC" = 0 || \
    ! test "$PRE_SYNC_RC" = 0 || ! test "$COMPLETE_RC" = 0 || \
    ! test "$POST_SYNC_RC" = 0; then
  printf 'capture record failure: command=%s timestamp=%s environment=%s rc_record=%s pre_sync=%s complete=%s post_sync=%s\n' \
    "$COMMAND_RC" "$TIMESTAMP_RC" "$ENVIRONMENT_RC" "$RC_RECORD_RC" \
    "$PRE_SYNC_RC" "$COMPLETE_RC" "$POST_SYNC_RC" >&2
  exit 125
fi
exit 0
SH

cat > "$EVIDENCE/check_phase_a_acceptance.sh" <<'SH'
#!/bin/bash
set -euo pipefail
EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
CURRENT_BOOT=${1:-/proc/sys/kernel/random/boot_id}
SUCCESS_RECORD=PHASE_SMALL_AUTOMATIC_SUCCESS.sha256
SUCCESS_ANCHOR=PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256
ACCEPTANCE=PHASE_SMALL_OPERATOR_ACCEPTANCE
ACCEPTANCE_ANCHOR=PHASE_SMALL_OPERATOR_ACCEPTANCE_ANCHOR.sha256
cd "$EVIDENCE"
sha256sum -c "$SUCCESS_ANCHOR"
sha256sum -c "$SUCCESS_RECORD"
sha256sum -c "$ACCEPTANCE_ANCHOR"
success_anchor_line=$(cat "$SUCCESS_ANCHOR")
read -r success_hash success_path <<< "$success_anchor_line"
test "$success_path" = "$SUCCESS_RECORD"
acceptance_anchor_line=$(cat "$ACCEPTANCE_ANCHOR")
read -r acceptance_hash acceptance_path <<< "$acceptance_anchor_line"
test "$acceptance_path" = "$ACCEPTANCE"
acceptance_hash_line=$(sha256sum "$ACCEPTANCE")
read -r actual_acceptance_hash actual_acceptance_path <<< \
  "$acceptance_hash_line"
test "$actual_acceptance_path" = "$ACCEPTANCE"
test "$acceptance_hash" = "$actual_acceptance_hash"
mapfile -t acceptance_lines < "$ACCEPTANCE"
test "${#acceptance_lines[@]}" = 5
test "${acceptance_lines[0]}" = \
  "phase_small_success_sha256=$success_hash"
test "${acceptance_lines[1]}" = 'confirmation=小行程正常'
test "${acceptance_lines[2]}" = \
  "boot_id=$(cat phase-small.pre.boot_id.stdout)"
test "${acceptance_lines[3]}" = \
  production_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3
test "${acceptance_lines[4]}" = \
  motion_script_sha256=bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9
cmp phase-small.pre.boot_id.stdout "$CURRENT_BOOT"
SH

cat > "$EVIDENCE/record_process_review.sh" <<'SH'
#!/bin/bash
set -euo pipefail
REVIEW=$1
DISPLAY_RC=$2
case "$REVIEW" in
  clean|found|unknown) ;;
  *) exit 2 ;;
esac
printf 'process_review=%s\n' "$REVIEW"
if ! test -f "$DISPLAY_RC"; then
  printf 'missing display rc: %s\n' "$DISPLAY_RC" >&2
  exit 1
fi
/usr/bin/env -u PYTHONOPTIMIZE /usr/bin/python3 -I -c '
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
if data != b"0\n":
    raise SystemExit(f"invalid display rc bytes: {data!r}")
' process-review "$DISPLAY_RC"
if ! test "$REVIEW" = clean; then
  printf 'process review is not clean: %s\n' "$REVIEW" >&2
  exit 1
fi
SH
chmod 0555 "$EVIDENCE/capture_snapshot.sh" \
  "$EVIDENCE/check_can_evidence.py" \
  "$EVIDENCE/check_can_receivers.py" \
  "$EVIDENCE/check_runtime_manifest.py" \
  "$EVIDENCE/check_motion_output.py" \
  "$EVIDENCE/check_runtime_gate.sh" \
  "$EVIDENCE/capture_command.sh" \
  "$EVIDENCE/check_phase_a_acceptance.sh" \
  "$EVIDENCE/record_process_review.sh"
/usr/bin/env -u PYTHONOPTIMIZE /usr/bin/python3 -I \
  "$EVIDENCE/check_can_receivers.py" --self-test
/usr/bin/env -u PYTHONOPTIMIZE /usr/bin/python3 -I \
  "$EVIDENCE/check_motion_output.py" --self-test
sha256sum "$EVIDENCE/capture_snapshot.sh" \
  "$EVIDENCE/check_can_evidence.py" \
  "$EVIDENCE/check_can_receivers.py" \
  "$EVIDENCE/check_runtime_manifest.py" \
  "$EVIDENCE/check_motion_output.py" \
  "$EVIDENCE/check_runtime_gate.sh" \
  "$EVIDENCE/capture_command.sh" \
  "$EVIDENCE/check_phase_a_acceptance.sh" \
  "$EVIDENCE/record_process_review.sh" \
  "$EVIDENCE/RUNTIME_MANIFEST.sha256" \
  "$EVIDENCE/RUNTIME_MANIFEST_ANCHOR.sha256" \
  > "$EVIDENCE/EVIDENCE_TOOLS_SHA256SUMS"
/usr/bin/sync -f "$EVIDENCE"
/bin/bash "$EVIDENCE/capture_snapshot.sh" phase-small pre
```

Expected: every `phase-small.pre.*.rc` is `0`; the machine-readable CAN gate
requires `UP`, `LOWER_UP`, MTU 72, `ERROR-ACTIVE`, 1M/5M bitrates, zero CAN
protocol/error/drop counters, and no `can0` receiver. Independently inspect
`phase-small.pre.processes.stdout` for dexhand, motor-control, inference, CAN
diagnostic, or other controller processes. Record that human inspection in
the phase command log before motion. This fresh evidence replaces every
observation made before the board lost power; any failure blocks motion.

- [ ] **Step 5: Execute Phase A exactly once**

First inspect the captured process list and explicitly enter exactly one of
`clean`, `found`, or `unknown`. The value is recorded in the review command
and stdout; only `clean` with a successful display/capture rc produces review
rc `0`. `found`, `unknown`, or a failed display blocks motion while preserving
the review evidence:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
cat "$EVIDENCE/phase-small.pre.processes.stdout"
read -r -p 'Process review [clean/found/unknown]: ' PROCESS_REVIEW
case "$PROCESS_REVIEW" in
  clean|found|unknown) ;;
  *) exit 2 ;;
esac
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small pre process_review \
  /bin/bash "$EVIDENCE/record_process_review.sh" "$PROCESS_REVIEW" \
  "$EVIDENCE/phase-small.pre.processes.rc"
test -f "$EVIDENCE/phase-small.pre.process_review.complete"
/usr/bin/env -u PYTHONOPTIMIZE /usr/bin/python3 -I -c '
from pathlib import Path
import sys
if Path(sys.argv[1]).read_bytes() != b"0\n":
    raise SystemExit("process review failed")
' process-review "$EVIDENCE/phase-small.pre.process_review.rc"
```

Then run the phase block. All preconditions run fail-closed before the atomic
marker is created. It becomes the persistent once-only marker only after the
immediate `/usr/bin/sync -f` succeeds, and is never removed even if Python,
timeout, SSH, or the board fails:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-small.attempt"
cd "$EVIDENCE"
sha256sum -c SHA256SUMS
sha256sum -c MOTION_SCRIPT_SHA256SUM
sha256sum -c EVIDENCE_TOOLS_SHA256SUMS
grep -Fx 'source_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3' SOURCE_MANIFEST
grep -Fx 'source_tree=aed385f28d3010fc167914872550f4bbb0a51057' SOURCE_MANIFEST
for rc_file in "$EVIDENCE/phase-small.pre."*.rc; do
  test "$(wc -c < "$rc_file")" = 2
  test "$(cat "$rc_file")" = 0
  test -f "${rc_file%.rc}.complete"
done
test "$(cat "$EVIDENCE/phase-small.pre.process_review.stdout")" = \
  process_review=clean
test ! -e "$ATTEMPT"
test ! -e "$ATTEMPT/stdout"
test ! -e "$ATTEMPT/stderr"
test ! -e "$ATTEMPT/rc"
test ! -e "$EVIDENCE/phase-small.stdout.log"
test ! -e "$EVIDENCE/phase-small.stderr.log"
test ! -e "$EVIDENCE/phase-small.rc"
mkdir "$ATTEMPT"
/usr/bin/sync -f "$EVIDENCE"
set -o noclobber
attempt_gate() {
  local name=$1
  shift
  local stem="$ATTEMPT/gate.$name"
  {
    printf '%q ' "$@"
    printf '\n'
  } > "$stem.command"
  set +e
  "$@" > "$stem.stdout" 2> "$stem.stderr"
  local rc=$?
  set -e
  printf '%s\n' "$rc" > "$stem.rc"
  test "$rc" = 0
}
attempt_gate boot_continuity /usr/bin/cmp \
  "$EVIDENCE/phase-small.pre.boot_id.stdout" \
  /proc/sys/kernel/random/boot_id
attempt_gate runtime /bin/bash "$EVIDENCE/check_runtime_gate.sh"
MOTION_COMMAND=(
  env -u LD_LIBRARY_PATH
  PYTHONNOUSERSITE=1
  PYTHONDONTWRITEBYTECODE=1
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/lib/python3.10/site-packages
  /usr/bin/timeout --preserve-status --signal=INT --kill-after=5s 30s
  /usr/bin/python3 -u staged_motion_validation.py --phase small
)
{
  printf '%q ' "${MOTION_COMMAND[@]}"
  printf '\n'
} > "$ATTEMPT/command"
/usr/bin/date --iso-8601=seconds > "$ATTEMPT/timestamp"
env -u LD_LIBRARY_PATH \
  PYTHONNOUSERSITE=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/lib/python3.10/site-packages \
  /bin/bash -c \
  'for name in PATH PYTHONPATH LD_LIBRARY_PATH PYTHONNOUSERSITE PYTHONDONTWRITEBYTECODE; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$ATTEMPT/environment"
set +e
"${MOTION_COMMAND[@]}" \
  > "$ATTEMPT/stdout" 2> "$ATTEMPT/stderr"
MOTION_PHASE_RC=$?
set -e
RC_TMP="$ATTEMPT/.rc.tmp"
test ! -e "$RC_TMP"
test ! -e "$ATTEMPT/rc"
/usr/bin/printf '%s\n' "$MOTION_PHASE_RC" > "$RC_TMP"
/usr/bin/mv -T "$RC_TMP" "$ATTEMPT/rc"
test -f "$ATTEMPT/command"
test -f "$ATTEMPT/environment"
test -f "$ATTEMPT/timestamp"
test -f "$ATTEMPT/stdout"
test -f "$ATTEMPT/stderr"
test -f "$ATTEMPT/rc"
/usr/bin/sync -f "$EVIDENCE"
```

Do not decide success or return the saved motion rc yet. Close the connection
and run Step 6 even when the saved rc is nonzero. A second invocation fails at
`mkdir "$ATTEMPT"`; no file in the marker directory is overwritten.

Expected after Step 6 enforces the saved rc: rc `0`, one `phase_complete`, no
`phase_error` or `cleanup_error`, two motion commands, and four successful
cleanup events.

`--preserve-status` is mandatory: without it GNU timeout masks the harness's
cleanup-aware SIGINT exit `130` as timeout exit `124`.

- [ ] **Step 6: Capture Phase A postflight, calculate deltas, then decide**

Open a fresh connection regardless of the saved motion rc:

```bash
set -euo pipefail
ssh orangepi
```

Run this exact postflight remotely:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-small.attempt"
test -d "$ATTEMPT"
set +e
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post snapshot_runner \
  /bin/bash "$EVIDENCE/capture_snapshot.sh" \
  phase-small post
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post process_display /usr/bin/cat \
  "$EVIDENCE/phase-small.post.processes.stdout"
set -e
```

Inspect `phase-small.post.process_display.stdout`. Whether either command
failed or the list is incomplete, run the following exact continuation so all
remaining obtainable evidence and failure records are preserved:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-small.attempt"
test -d "$ATTEMPT"
set +e
PROCESS_REVIEW=unknown
if IFS= read -r -p 'Process review [clean/found/unknown]: ' \
    PROCESS_REVIEW < /dev/tty; then
  case "$PROCESS_REVIEW" in
    clean|found|unknown) ;;
    *) PROCESS_REVIEW=unknown ;;
  esac
fi
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post process_review \
  /bin/bash "$EVIDENCE/record_process_review.sh" "$PROCESS_REVIEW" \
  "$EVIDENCE/phase-small.post.process_display.rc"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post evidence_tools_hash /usr/bin/sha256sum -c \
  "$EVIDENCE/EVIDENCE_TOOLS_SHA256SUMS"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post can_delta /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_can_evidence.py" \
  "$EVIDENCE/phase-small.pre.can_counters.stdout" \
  "$EVIDENCE/phase-small.post.can_counters.stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post boot_continuity /usr/bin/cmp \
  "$EVIDENCE/phase-small.pre.boot_id.stdout" \
  "$EVIDENCE/phase-small.post.boot_id.stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post motion_output /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_motion_output.py" \
  small "$ATTEMPT/stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post motion_result /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I -c '
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
if data != b"0\n":
    raise SystemExit(f"invalid motion rc bytes: {data!r}")
print("motion_rc=0")
' motion-result "$ATTEMPT/rc"
set -e

POST_NAMES=(
  timestamp uptime uname boot_id address can_brief can_details can_counters
  rcvlist processes source_manifest sdk_hash sdk_hash_gate script_hash
  script_hash_gate environment receiver_gate can_gate runtime_gate
  snapshot_runner process_display process_review evidence_tools_hash can_delta
  boot_continuity motion_output motion_result
)
FINAL_FAILURE=0
for name in "${POST_NAMES[@]}"; do
  rc_file="$EVIDENCE/phase-small.post.$name.rc"
  complete_file="${rc_file%.rc}.complete"
  if ! test -f "$complete_file"; then
    printf 'missing complete marker: %s\n' "$complete_file" >&2
    FINAL_FAILURE=1
  fi
  if ! test -f "$rc_file"; then
    printf 'missing rc: %s\n' "$rc_file" >&2
    FINAL_FAILURE=1
  elif ! test "$(wc -c < "$rc_file")" = 2 || \
      ! test "$(cat "$rc_file")" = 0; then
    printf 'invalid/nonzero rc: %s\n' "$rc_file" >&2
    FINAL_FAILURE=1
  fi
done
if ! test "$FINAL_FAILURE" = 0; then
  exit 1
fi

SUCCESS_RECORD="$EVIDENCE/PHASE_SMALL_AUTOMATIC_SUCCESS.sha256"
SUCCESS_ANCHOR="$EVIDENCE/PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256"
test ! -e "$SUCCESS_RECORD"
test ! -e "$SUCCESS_ANCHOR"
set +e
evidence_listing=$(find "$EVIDENCE" -maxdepth 1 -type f \
  \( -name 'phase-small.pre.*' -o -name 'phase-small.post.*' \
  -o -name EVIDENCE_TOOLS_SHA256SUMS \) -print)
evidence_find_rc=$?
attempt_listing=$(find "$ATTEMPT" -maxdepth 1 -type f -print)
attempt_find_rc=$?
set -e
test "$evidence_find_rc" = 0
test "$attempt_find_rc" = 0
success_listing=$(printf '%s\n%s\n' \
  "$evidence_listing" "$attempt_listing" | LC_ALL=C sort)
test -n "$success_listing"
mapfile -t success_inputs <<< "$success_listing"
record_tmp=$(mktemp "$EVIDENCE/.phase-small-success.XXXXXX")
anchor_tmp=$(mktemp "$EVIDENCE/.phase-small-success-anchor.XXXXXX")
cleanup_success_files() {
  test -z "${record_tmp:-}" || rm -f "$record_tmp"
  test -z "${anchor_tmp:-}" || rm -f "$anchor_tmp"
}
trap cleanup_success_files EXIT
sha256sum "${success_inputs[@]}" > "$record_tmp"
ln "$record_tmp" "$SUCCESS_RECORD"
rm "$record_tmp"
record_tmp=
(cd "$EVIDENCE" && \
  sha256sum PHASE_SMALL_AUTOMATIC_SUCCESS.sha256 > "$anchor_tmp")
ln "$anchor_tmp" "$SUCCESS_ANCHOR"
rm "$anchor_tmp"
anchor_tmp=
/usr/bin/sync -f "$EVIDENCE"
(cd "$EVIDENCE" && \
  sha256sum -c PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256)
sha256sum -c "$SUCCESS_RECORD"
```

Expected: postflight always exists, CAN configuration exactly matches
preflight, CAN protocol/error/drop deltas are zero, packet/byte deltas are
nonnegative, no receiver/process remains, and only then does `motion_result`
require saved rc `0`. If motion or postflight failed, preserve the complete
attempt and stop; never retry in this evidence directory.

- [ ] **Step 7: Stop for operator confirmation**

Report every joint's baseline, high, and return sample plus all cleanup and CAN
results. Ask the observer to confirm no collision, binding, unexpected
direction, abnormal sound, or failed return to open.

Expected: the workflow stops here. Phase B must not run in the same agent turn
without the new explicit response `小行程正常`.

### Task 6: Run Phase B Once After the Operator Gate

**Files:**
- Execute remotely:
  `motion-validation-bacf6612/staged_motion_validation.py`
- Append evidence under:
  `motion-validation-bacf6612/`

- [ ] **Step 1: Verify the operator gate and artifact identity**

Require the exact new operator response `小行程正常`, then record and verify it
with the artifact identities. Run remotely:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
cd "$EVIDENCE"
sha256sum -c PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256
sha256sum -c PHASE_SMALL_AUTOMATIC_SUCCESS.sha256
success_anchor_line=$(cat PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256)
read -r success_hash success_path <<< "$success_anchor_line"
test "$success_path" = PHASE_SMALL_AUTOMATIC_SUCCESS.sha256
test "$(wc -c < phase-small.pre.boot_id.stdout)" = 37
test "$(wc -l < phase-small.pre.boot_id.stdout)" = 1
phase_a_boot_id=$(cat phase-small.pre.boot_id.stdout)
OPERATOR_CONFIRMATION=
IFS= read -r OPERATOR_CONFIRMATION < /dev/tty
test "$OPERATOR_CONFIRMATION" = '小行程正常'
ACCEPTANCE="$EVIDENCE/PHASE_SMALL_OPERATOR_ACCEPTANCE"
ACCEPTANCE_ANCHOR="$EVIDENCE/PHASE_SMALL_OPERATOR_ACCEPTANCE_ANCHOR.sha256"
test ! -e "$ACCEPTANCE"
test ! -e "$ACCEPTANCE_ANCHOR"
acceptance_tmp=$(mktemp "$EVIDENCE/.phase-small-acceptance.XXXXXX")
acceptance_anchor_tmp=$(mktemp \
  "$EVIDENCE/.phase-small-acceptance-anchor.XXXXXX")
cleanup_acceptance_files() {
  test -z "${acceptance_tmp:-}" || rm -f "$acceptance_tmp"
  test -z "${acceptance_anchor_tmp:-}" || rm -f "$acceptance_anchor_tmp"
}
trap cleanup_acceptance_files EXIT
printf '%s\n' \
  "phase_small_success_sha256=$success_hash" \
  "confirmation=$OPERATOR_CONFIRMATION" \
  "boot_id=$phase_a_boot_id" \
  production_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3 \
  motion_script_sha256=bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9 \
  > "$acceptance_tmp"
ln "$acceptance_tmp" "$ACCEPTANCE"
rm "$acceptance_tmp"
acceptance_tmp=
sha256sum PHASE_SMALL_OPERATOR_ACCEPTANCE > "$acceptance_anchor_tmp"
ln "$acceptance_anchor_tmp" "$ACCEPTANCE_ANCHOR"
rm "$acceptance_anchor_tmp"
acceptance_anchor_tmp=
/usr/bin/sync -f "$EVIDENCE"
/bin/bash "$EVIDENCE/check_phase_a_acceptance.sh" \
  /proc/sys/kernel/random/boot_id
sha256sum -c SHA256SUMS
sha256sum -c MOTION_SCRIPT_SHA256SUM
sha256sum -c EVIDENCE_TOOLS_SHA256SUMS
grep -Fx 'source_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3' SOURCE_MANIFEST
grep -Fx 'source_tree=aed385f28d3010fc167914872550f4bbb0a51057' SOURCE_MANIFEST
test ! -e "$EVIDENCE/phase-full.attempt"
test ! -e "$EVIDENCE/phase-full.stdout.log"
test ! -e "$EVIDENCE/phase-full.stderr.log"
test ! -e "$EVIDENCE/phase-full.rc"
```

Expected: raw confirmation is read from `/dev/tty`, is exactly `小行程正常`,
and is bound to the immutable automatic Phase A success checksum and its boot
ID. Matching hashes and absent Phase B artifacts are also required.

- [ ] **Step 2: Capture the complete Phase B preflight**

Run the distinct Phase B preflight and record the same explicit
`clean`/`found`/`unknown` human process decision:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
/bin/bash "$EVIDENCE/check_phase_a_acceptance.sh" \
  /proc/sys/kernel/random/boot_id
/bin/bash "$EVIDENCE/capture_snapshot.sh" phase-full pre
cat "$EVIDENCE/phase-full.pre.processes.stdout"
IFS= read -r -p 'Process review [clean/found/unknown]: ' \
  PROCESS_REVIEW < /dev/tty
case "$PROCESS_REVIEW" in
  clean|found|unknown) ;;
  *) exit 2 ;;
esac
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full pre process_review \
  /bin/bash "$EVIDENCE/record_process_review.sh" "$PROCESS_REVIEW" \
  "$EVIDENCE/phase-full.pre.processes.rc"
for rc_file in "$EVIDENCE/phase-full.pre."*.rc; do
  test "$(wc -c < "$rc_file")" = 2
  test "$(cat "$rc_file")" = 0
  test -f "${rc_file%.rc}.complete"
done
test "$(cat "$EVIDENCE/phase-full.pre.process_review.stdout")" = \
  process_review=clean
```

Expected: Phase B has its own timestamp, power/network/interface details,
machine-readable counters, all receiver lists, process list, provenance,
SDK/script hashes, environment, stdout/stderr/rc files, and human review.
Every invariant must pass or Phase B is blocked.

- [ ] **Step 3: Execute Phase B exactly once**

Run with a separate once-only marker, which is treated as persistent only
after the immediate `/usr/bin/sync -f` succeeds. Preconditions remain
fail-closed; only the timeout call temporarily disables errexit so its rc can
be preserved:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-full.attempt"
cd "$EVIDENCE"
sha256sum -c SHA256SUMS
sha256sum -c MOTION_SCRIPT_SHA256SUM
sha256sum -c EVIDENCE_TOOLS_SHA256SUMS
/bin/bash "$EVIDENCE/check_phase_a_acceptance.sh" \
  /proc/sys/kernel/random/boot_id
for rc_file in "$EVIDENCE/phase-full.pre."*.rc; do
  test "$(wc -c < "$rc_file")" = 2
  test "$(cat "$rc_file")" = 0
  test -f "${rc_file%.rc}.complete"
done
test ! -e "$ATTEMPT"
test ! -e "$ATTEMPT/stdout"
test ! -e "$ATTEMPT/stderr"
test ! -e "$ATTEMPT/rc"
mkdir "$ATTEMPT"
/usr/bin/sync -f "$EVIDENCE"
set -o noclobber
attempt_gate() {
  local name=$1
  shift
  local stem="$ATTEMPT/gate.$name"
  {
    printf '%q ' "$@"
    printf '\n'
  } > "$stem.command"
  set +e
  "$@" > "$stem.stdout" 2> "$stem.stderr"
  local rc=$?
  set -e
  printf '%s\n' "$rc" > "$stem.rc"
  test "$rc" = 0
}
attempt_gate boot_continuity /usr/bin/cmp \
  "$EVIDENCE/phase-full.pre.boot_id.stdout" \
  /proc/sys/kernel/random/boot_id
attempt_gate phase_a_binding /bin/bash \
  "$EVIDENCE/check_phase_a_acceptance.sh" \
  /proc/sys/kernel/random/boot_id
attempt_gate runtime /bin/bash "$EVIDENCE/check_runtime_gate.sh"
MOTION_COMMAND=(
  env -u LD_LIBRARY_PATH
  PYTHONNOUSERSITE=1
  PYTHONDONTWRITEBYTECODE=1
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/lib/python3.10/site-packages
  /usr/bin/timeout --preserve-status --signal=INT --kill-after=5s 60s
  /usr/bin/python3 -u staged_motion_validation.py --phase full
)
{
  printf '%q ' "${MOTION_COMMAND[@]}"
  printf '\n'
} > "$ATTEMPT/command"
/usr/bin/date --iso-8601=seconds > "$ATTEMPT/timestamp"
env -u LD_LIBRARY_PATH \
  PYTHONNOUSERSITE=1 \
  PYTHONDONTWRITEBYTECODE=1 \
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix/lib/python3.10/site-packages \
  /bin/bash -c \
  'for name in PATH PYTHONPATH LD_LIBRARY_PATH PYTHONNOUSERSITE PYTHONDONTWRITEBYTECODE; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$ATTEMPT/environment"
set +e
"${MOTION_COMMAND[@]}" \
  > "$ATTEMPT/stdout" 2> "$ATTEMPT/stderr"
MOTION_PHASE_RC=$?
set -e
RC_TMP="$ATTEMPT/.rc.tmp"
test ! -e "$RC_TMP"
test ! -e "$ATTEMPT/rc"
/usr/bin/printf '%s\n' "$MOTION_PHASE_RC" > "$RC_TMP"
/usr/bin/mv -T "$RC_TMP" "$ATTEMPT/rc"
test -f "$ATTEMPT/command"
test -f "$ATTEMPT/environment"
test -f "$ATTEMPT/timestamp"
test -f "$ATTEMPT/stdout"
test -f "$ATTEMPT/stderr"
test -f "$ATTEMPT/rc"
/usr/bin/sync -f "$EVIDENCE"
```

Close the connection and continue to Step 4 for every saved rc. The marker's
atomic `mkdir` prevents every retry, and `noclobber` protects its contents.

`--preserve-status` is mandatory here for the same reason as Phase A: a
cleanup-aware `130` must not be rewritten to `124`.

Expected after Step 4 enforces the saved rc: rc `0`, six cycle motion commands
plus one final-open command, one `phase_complete`, no phase/cleanup errors,
and four cleanup completions.

- [ ] **Step 4: Capture Phase B postflight, calculate deltas, then decide**

Reconnect regardless of the saved rc:

```bash
set -euo pipefail
ssh orangepi
```

Run remotely:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-full.attempt"
test -d "$ATTEMPT"
set +e
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post snapshot_runner \
  /bin/bash "$EVIDENCE/capture_snapshot.sh" \
  phase-full post
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post process_display /usr/bin/cat \
  "$EVIDENCE/phase-full.post.processes.stdout"
set -e
```

Inspect `phase-full.post.process_display.stdout`, then run this continuation
even if snapshot capture or display failed:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-full.attempt"
test -d "$ATTEMPT"
set +e
PROCESS_REVIEW=unknown
if IFS= read -r -p 'Process review [clean/found/unknown]: ' \
    PROCESS_REVIEW < /dev/tty; then
  case "$PROCESS_REVIEW" in
    clean|found|unknown) ;;
    *) PROCESS_REVIEW=unknown ;;
  esac
fi
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post process_review \
  /bin/bash "$EVIDENCE/record_process_review.sh" "$PROCESS_REVIEW" \
  "$EVIDENCE/phase-full.post.process_display.rc"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post evidence_tools_hash /usr/bin/sha256sum -c \
  "$EVIDENCE/EVIDENCE_TOOLS_SHA256SUMS"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post can_delta /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_can_evidence.py" \
  "$EVIDENCE/phase-full.pre.can_counters.stdout" \
  "$EVIDENCE/phase-full.post.can_counters.stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post boot_continuity /usr/bin/cmp \
  "$EVIDENCE/phase-full.pre.boot_id.stdout" \
  "$EVIDENCE/phase-full.post.boot_id.stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post motion_output /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I "$EVIDENCE/check_motion_output.py" \
  full "$ATTEMPT/stdout"
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post motion_result /usr/bin/env -u PYTHONOPTIMIZE \
  /usr/bin/python3 -I -c '
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
if data != b"0\n":
    raise SystemExit(f"invalid motion rc bytes: {data!r}")
print("motion_rc=0")
' motion-result "$ATTEMPT/rc"
set -e

POST_NAMES=(
  timestamp uptime uname boot_id address can_brief can_details can_counters
  rcvlist processes source_manifest sdk_hash sdk_hash_gate script_hash
  script_hash_gate environment receiver_gate can_gate runtime_gate
  phase_a_binding snapshot_runner process_display process_review
  evidence_tools_hash can_delta boot_continuity motion_output motion_result
)
FINAL_FAILURE=0
for name in "${POST_NAMES[@]}"; do
  rc_file="$EVIDENCE/phase-full.post.$name.rc"
  complete_file="${rc_file%.rc}.complete"
  if ! test -f "$complete_file"; then
    printf 'missing complete marker: %s\n' "$complete_file" >&2
    FINAL_FAILURE=1
  fi
  if ! test -f "$rc_file"; then
    printf 'missing rc: %s\n' "$rc_file" >&2
    FINAL_FAILURE=1
  elif ! test "$(wc -c < "$rc_file")" = 2 || \
      ! test "$(cat "$rc_file")" = 0; then
    printf 'invalid/nonzero rc: %s\n' "$rc_file" >&2
    FINAL_FAILURE=1
  fi
done
if ! test "$FINAL_FAILURE" = 0; then
  exit 1
fi
```

Expected: the distinct Phase B postflight is complete before the saved motion
rc is enforced. Configuration is unchanged, all CAN protocol/error/drop
deltas are zero, traffic counters are nondecreasing, and no receiver/process
remains. Any nonzero motion, snapshot, delta, or review rc fails Phase B after
preserving all evidence; no retry is allowed.

- [ ] **Step 5: Independently review physical evidence**

Dispatch one reviewer for script/output/spec conformance and one reviewer for
CAN/cleanup/hardware-safety evidence. Neither reviewer may rerun either phase.

Expected: reviewers distinguish API command returns from device
acknowledgement and require explicit operator observation before marking the
physical gate complete.

### Task 7: Final Release-Gate Report

**Files:**
- Inspect all evidence under:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence/motion-validation-bacf6612/`
- Inspect repository state:
  `/home/sjh/leisai_hand/roboparty_dexhand`

- [ ] **Step 1: Verify the repository remained unchanged by execution**

Run:

```bash
set -euo pipefail
git status --short --branch
git rev-parse HEAD
```

Expected: clean `main`, HEAD at the plan commit, with no execution artifact in
the repository.

Record two identities separately: deployed production commit
`db2da9fb90f407bdd5e3bbd3de691e775d27abd3`, proven by the source manifest,
and the documentation commit returned by `git rev-parse HEAD`. The latter
records this plan and report; it is not the source of the motion binary or
Python extension.

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
