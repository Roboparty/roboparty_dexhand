# Orange Pi Staged Motion Validation Design

## Status

Approved in conversation on 2026-08-16. Execution remains gated by the
operator confirmations defined below.

## Context

The Orange Pi AArch64 build, install/export checks, non-motion initialization
probe, and real kernel vcan two-socket test previously passed for commit
`92d742c`. That result is historical evidence only. It does not authorize
motion with that commit or with its old installed prefix.

The motion driver added fail-closed state handling, sticky `check_health()`
reporting, callback admission control, and ordered teardown at production
commit `db2da9fb90f407bdd5e3bbd3de691e775d27abd3` (tree
`aed385f28d3010fc167914872550f4bbb0a51057`). Every binary and Python module
used for this physical validation must be built and installed from that exact
Git object. The connected device is an LHandPro 6DOF S hand on SocketCAN
`can0`, CAN-FD node ID 1. The operator has confirmed that the mechanism is
secured, the workspace is clear, an observer is present, and emergency stop
or power removal is immediately available.

The repository's existing `scripts/test_dexhand.py` runs the intended full
motion profile, but its `finally` block only deinitializes the driver. The
release validation therefore uses a separate, evidence-bound test script with
explicit stop and disable cleanup. The repository script and production code
are not modified by this validation.

## Scope

The validation has two separately invoked phases:

1. a low-speed, short-travel qualification phase; and
2. the existing full motion profile after explicit operator approval.

Both phases use only the installed AArch64 artifacts under
`/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix`. They select the
public 6DOF model, `can0`, and node ID 1. Neither phase changes the configured
maximum current. Source, build, plain-install prefix, relocatable install gate,
and evidence paths are new, disjoint children of the new remote root. The
remote root must be proved absent before it is created; a partial path is
never reused or deleted for a retry.

The callback-quiescence and SDK redistribution release gates are independent
of this physical validation and remain blocked on written vendor responses.

## Safety Invariants

The board has lost power since the earlier evidence was collected. After
power and network are restored, all board and CAN preflight evidence is stale
and must be collected again from the beginning. A successful SSH connection
does not substitute for the operator confirming stable power, secure cabling,
and an available emergency stop.

Before either phase, the controller must prove all of the following without
sending a dexhand command:

- the board is reachable after the latest power-up and reports `aarch64`;
- `can0` is `UP`, `LOWER_UP`, CAN-FD, and `ERROR-ACTIVE`;
- arbitration bitrate is 1,000,000 and data bitrate is 5,000,000;
- CAN protocol, RX, and TX error/drop counters are zero;
- no dexhand, motor-control, or inference process is active;
- all six `/proc/net/can/rcvlist_*` sections report exact `no entry` records
  for both `any` and `can0`;
- the deployed source archive identifies production commit
  `db2da9fb90f407bdd5e3bbd3de691e775d27abd3` and tree
  `aed385f28d3010fc167914872550f4bbb0a51057`;
- the native Debug warning-clean build passes exactly eight default CTests,
  the plain install and separate relocatable install/export gate pass, and
  neither uses the vcan test option;
- `readelf` identifies the installed SDK, `libdexhand.so`, and `dexhand_py`
  extension as AArch64, `ldd` reports no missing dependency, and exactly one
  installed SDK exists;
- the installed AArch64 SDK hash is
  `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`;
- the installed Python module resolves under the new prefix, exports
  `check_health`, and passes a construction-only probe that does not
  initialize the driver or access CAN; and
- the motion script hash is
  `bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9`,
  while its frozen offline contract hash is
  `df774043b20156d541f2cd7bbf6611d96c8922ffe7b66ab0f4b7591dd4be45ce`.

Failure of any precondition prevents that phase from starting. The test must
not reconfigure, restart, or bring down `can0`.

The script runs under an outer GNU timeout with `--preserve-status` and
`--signal=INT`, allowing Python to execute its cleanup path and preserving its
exit status. Without `--preserve-status`, GNU timeout converts the timed-out
child's cleanup-aware `130` into `124`, masking the evidence that SIGINT was
handled. A five-second kill grace bounds a stuck process. If a vendor C call
cannot return and Python cannot run cleanup, the on-site emergency stop or
power removal is the authoritative safety mechanism.

## Deployment Provenance

The local deployment input is produced with `git archive` from the exact
production commit object, not from working-tree bytes. A manifest records the
full commit, tree, archive SHA-256, archive prefix, and provenance command.
The board verifies the archive checksum and its embedded Git commit before
extracting it with GNU tar's `--touch` option so host timestamps in the future
are not retained.

The authoritative remote paths are:

- root: `/home/orangepi/roboparty_dexhand_motion_db2da9f`;
- source: `/home/orangepi/roboparty_dexhand_motion_db2da9f/source`;
- build: `/home/orangepi/roboparty_dexhand_motion_db2da9f/build`;
- motion prefix: `/home/orangepi/roboparty_dexhand_motion_db2da9f/prefix`;
- install/export gate:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/install-gate` while the
  gate is running, renamed by the gate to the final
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/install-gate-relocated`;
  and
- evidence:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f/evidence`, containing the
  non-motion gate bundle `deployment-db2da9f` and the disjoint motion bundle
  `motion-validation-bacf6612`.

At first deployment, both the remote root and its parent `evidence` path must
be absent; the captured bootstrap gate proves this and creates them with
atomic `mkdir` operations. By Task 5, that parent and the completed
`deployment-db2da9f` bundle must exist, while only the motion leaf
`motion-validation-bacf6612` must still be absent. If a path violates the
expectation for its stage, execution stops and a new, explicitly reviewed
suffix is selected. Existing evidence is never reused, overwritten, or
deleted.

Every non-motion deployment command runs under Bash with
`set -euo pipefail`, or captures and asserts each command's exit status.
Commands, selected environment values, controller timestamps, stdout, stderr,
and return codes are stored without credentials. Phase attempts use atomically
created marker directories plus shell `noclobber`. A marker is described as
persistent only after `/usr/bin/sync -f` on the evidence directory succeeds;
motion cannot start before that verified sync, and the result/acceptance
records are also synced. A failed or interrupted attempt therefore cannot be
mistaken for an unstarted phase or overwrite earlier evidence. Postflight
capture occurs in a fresh connection
regardless of whether the motion rc is missing, malformed, zero, or nonzero.
An incomplete postflight runner does not suppress the remaining human-review,
CAN-delta, SDK-hash, or motion-result attempts; all available evidence is
captured before one final aggregate decision.

Process inspection is an explicit three-state operator result: `clean`,
`found`, or `unknown`. The selected value is part of the recorded command and
stdout; only `clean` together with a successful process-display capture has rc
zero. The motion process writes its rc through a temporary file and atomic
rename, and postflight accepts only the complete byte sequence `0\n`; missing,
truncated, appended, malformed, or nonzero rc evidence fails the phase.

The deployment gate emits a three-record runtime manifest for the exact-one
AArch64 SDK, `libdexhand.so`, and installed Python module, plus an independent
manifest checksum anchor. Identical copies are hash-bound into the motion
bundle. Every preflight, postflight, and immediate pre-motion gate verifies
both anchors, all three absolute paths and hashes, the unique installed
objects, and the module and linked libraries actually loaded from the new
prefix. Each snapshot also records the kernel boot ID. A phase may move only
when its current boot ID equals its preflight boot ID; Phase B additionally
requires the boot ID accepted after Phase A. A boot change invalidates that
evidence suffix and blocks further motion.

## Evidence-Bound Script

A single Python script supports `--phase small` and `--phase full`. Its exact
bytes and frozen offline contract are copied into the new evidence directory
with the locally generated hash files. Before the first invocation, the board
verifies both files and the motion script's fixed SHA-256. Each command line,
environment, source manifest, script hash, installed module path, SDK hash,
stdout, stderr, exit status, and local controller timestamp is recorded before
or alongside execution.

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
new confirmation. Before prompting, strict JSON validation requires the exact
small-phase success event sequence, labels, four cleanup completions, unique
`phase_complete`, zero saved rc, and all postflight gates. Those files are
bound into a checksum-anchored automatic-success record that this evidence
protocol never overwrites. The only accepted gate text is exactly
`小行程正常`, read raw from `/dev/tty`; the acceptance record binds that text
to the automatic-success checksum and Phase A boot ID. Phase B revalidates
both records before preflight and again after its durable once-only marker is
created.

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
- all motion artifacts are proven to originate from production commit
  `db2da9fb90f407bdd5e3bbd3de691e775d27abd3`, independently of the later
  documentation-only commit that records this execution plan;
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
