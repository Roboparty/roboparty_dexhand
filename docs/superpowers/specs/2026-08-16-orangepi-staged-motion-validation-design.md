# Orange Pi Staged Motion Validation Design

## Status

Approved in conversation on 2026-08-16. Execution remains gated by the
operator confirmations defined below.

## Context

The Orange Pi AArch64 build, install/export checks, non-motion initialization
probe, and real kernel vcan two-socket test have passed for commit `92d742c`.
The connected device is an LHandPro 6DOF S hand on SocketCAN `can0`, CAN-FD
node ID 1. The operator has confirmed that the mechanism is secured, the
workspace is clear, an observer is present, and emergency stop or power
removal is immediately available.

The repository's existing `scripts/test_dexhand.py` runs the intended full
motion profile, but its `finally` block only deinitializes the driver. The
release validation therefore uses a separate, evidence-bound test script with
explicit stop and disable cleanup. The repository script and production code
are not modified by this validation.

## Scope

The validation has two separately invoked phases:

1. a low-speed, short-travel qualification phase; and
2. the existing full motion profile after explicit operator approval.

Both phases use the installed AArch64 artifacts under the authoritative
Orange Pi validation prefix. They select the public 6DOF model, `can0`, and
node ID 1. Neither phase changes the configured maximum current.

The callback-quiescence and SDK redistribution release gates are independent
of this physical validation and remain blocked on written vendor responses.

## Safety Invariants

Before either phase, the controller must prove all of the following without
sending a dexhand command:

- `can0` is `UP`, `LOWER_UP`, CAN-FD, and `ERROR-ACTIVE`;
- arbitration bitrate is 1,000,000 and data bitrate is 5,000,000;
- CAN protocol, RX, and TX error/drop counters are zero;
- no dexhand, motor-control, or inference process is active;
- `/proc/net/can/rcvlist_*` contains no receiver for `can0`;
- the installed AArch64 SDK hash is
  `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`;
  and
- the test script hash matches the value recorded before execution.

Failure of any precondition prevents that phase from starting. The test must
not reconfigure, restart, or bring down `can0`.

The script runs under an outer timeout that sends `SIGINT`, allowing Python to
execute its `finally` block. A five-second kill grace bounds a stuck process.
If a vendor C call cannot return and Python cannot run cleanup, the on-site
emergency stop or power removal is the authoritative safety mechanism.

## Evidence-Bound Script

A single Python script supports `--phase small` and `--phase full`. Before the
first invocation, its complete bytes are saved under the remote validation
log directory and SHA-256 is recorded. Each command line, environment, script
hash, installed module path, SDK hash, stdout, stderr, exit status, and local
controller timestamp is recorded before or alongside execution.

The script is never supplied through anonymous stdin. Phase A and Phase B
each execute at most once. A failed or interrupted phase is not automatically
retried.

Every initialized path executes the following cleanup steps independently so
that failure of one does not skip the rest:

1. `stop_motors(0)`;
2. `set_enable(0, false)`;
3. `set_move_no_home(0)`; and
4. `deinit_hand()`.

The Python API returns `void` for these calls. A successful return plus the
absence of an SDK error log proves that the cleanup command path ran; it does
not constitute an independent device acknowledgement.

## Phase A: Short-Travel Qualification

Phase A uses a 30-second outer timeout and performs this sequence:

1. Create the public 6DOF driver for `can0`, node ID 1.
2. Call `init_hand(enable_motors=true, home_motors=true,
   home_wait_time=6.0)`.
3. Require initialization success and exact DOF `(11, 6)`.
4. Immediately call `set_move_no_home(0)`.
5. Record position, status, current, and alarm for all six active motors.
6. Set every motor target to 1,000 counts and velocity to 1,000 counts/s,
   broadcast `move_motors(0)`, wait two seconds, and record all feedback.
7. Set every motor target to 0 at the same velocity, broadcast motion, wait
   two seconds, and record all feedback.
8. Run the cleanup sequence.
9. Record postflight CAN state, process state, and socket ownership.

Phase A fails immediately if any alarm is nonzero, a reported position leaves
the conservative range `[-1000, 11000]`, a high-target position fails to
increase by at least 100 counts, a return position fails to decrease by at
least 100 counts, an SDK error is logged, or a CAN error/drop counter grows.
Status and current are recorded but are not compared with undocumented hard
limits.

After Phase A, execution pauses. The observer must explicitly report that the
homing and short-travel motion were physically normal, with no collision,
unexpected direction, noise, or binding. Phase B cannot start without that
new confirmation.

## Phase B: Full Motion Profile

Phase B uses a 60-second outer timeout and repeats the full preflight. It then
performs this sequence:

1. Initialize, enable, and home as in Phase A.
2. Require exact DOF `(11, 6)` and immediately restore
   `set_move_no_home(0)`.
3. For three cycles, set all six targets to 5,000 counts at 15,000 counts/s,
   broadcast motion, wait two seconds, sample every motor, then return all six
   targets to 0 at the same velocity, broadcast motion, wait two seconds, and
   sample every motor.
4. Issue a final target of 0, wait one second, and sample every motor.
5. Run the cleanup sequence and postflight checks.

Each high-target sample must increase by at least 1,000 counts relative to the
preceding open sample. Each open sample must decrease by at least 1,000 counts
relative to the preceding high sample. Every alarm must remain zero and every
position must remain within `[-1000, 11000]`. Any violation aborts the
remaining cycles and enters cleanup.

## Acceptance Criteria

The staged physical gate passes only when all of the following hold:

- both preflights satisfy every safety invariant;
- Phase A completes once and the observer explicitly approves its motion;
- Phase B completes exactly three full cycles and returns to the open target;
- all six motors meet the direction and range checks at every sample;
- all six alarm values remain zero;
- no SDK error or Python exception is present;
- CAN protocol/error/drop counters do not increase;
- cleanup paths return, and no dexhand process or CAN socket remains; and
- the observer confirms no collision, binding, unexpected direction, or
  abnormal sound.

Packet counts are expected to increase and are recorded, but are not treated
as errors. Physical observation, API evidence, and CAN evidence are reported
separately; none is overstated as proof of the others.

## Failure Handling

On any failure, the current phase stops, runs all cleanup attempts, captures
postflight state, and reports the exact last completed step. There is no
automatic retry and Phase B is not attempted after a Phase A failure.

If the hand does not stop or the process is stuck, the observer uses emergency
stop or removes power immediately. Software cleanup is not treated as a
substitute for the physical safety control.
