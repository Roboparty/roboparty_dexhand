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

## R5-R6 Deployment Incidents and Mandatory R9 Restart

The first fixed local deployment stage (R1)
`/tmp/roboparty-dexhand-deploy-db2da9f` is a historical failed attempt and is
consumed. Its only `remote_fresh_root` capture ended with `rc=143`. Its stdout
and stderr were both zero bytes; the command and selected-environment files
existed and contained no credential value. The controller had inherited
`SSH_AUTH_SOCK=/run/user/1000/keyring/ssh`; that desktop agent caused
`ssh-add -l` itself to hang. After the SSH TCP connection, the socket remained
in `CLOSE-WAIT`, so the evidence cannot prove whether the remote command ran.
That historical local stage and historical remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f` must never be reused or
deleted. Because remote execution is uncertain, the old remote root is already
consumed even if it later appears absent.

The second fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r2` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r2` are the historical,
consumed R2 Task 4 attempt. Task 4 passed archive/provenance, configuration and
configuration-contract, build, exactly 8/8 CTests, plain-install,
install/export relocation, and AArch64 artifact gates. The subsequent
`python_construction` gate failed with `ModuleNotFoundError`; its enclosing
`remote_operator_session` capture produced a complete tuple with `rc=1`.
The locked invocation had the argument shape
`python3 -I -c CODE dexhand-construction "$PYTHON_SITE" "$PREFIX_DIR"`.
A local exact-shape reproduction produced
`['-c', 'dexhand-construction', '/example/site', '/example/prefix']`.
The code reads the site path from `sys.argv[1]` and the prefix from
`sys.argv[2]`, so the extra `dexhand-construction` label shifted both
positions and prevented the installed module from resolving. No CAN access,
SDK initialization, `init_hand`, or motion occurred. The R2 local stage and
remote root are consumed and must never be reused or deleted.

The third fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r3` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r3` are the historical,
consumed R3 Task 4 attempt. Each of the nine remote deployment gates—
`remote_archive_provenance`, `configure`, `configure_contract`, `build`,
`ctest`, `plain_install`, `install_export`, `artifact_gate`, and
`python_construction`—has a complete command/stdout/stderr/rc/timestamp/
environment tuple with rc `0`. Those gates passed archive and configuration
contracts, the warning-clean build, exactly 8/8 CTests, both install gates,
AArch64/runtime-manifest validation, and the construction-only Python probe.
A subsequent read-only audit rechecked all nine tuples, the runtime manifest
and anchor, the installed objects and runpaths, the fixed SDK hash, the exact
CTest result, the exact construction stdout and empty stderr, and both source
manifest records. The audit reached its final explicit PASS `printf` and
returned to the remote prompt.

The enclosing `remote_operator_session` was opened as a generic interactive
SSH login without a fixed remote shell command. After the successful audit,
the next input was a plain `exit`, not `exit 0`. Visible output showed logout
and connection closure, but the complete local live-capture tuple recorded rc
`1`. Existing evidence cannot determine why: the exact cause of that final rc
remains unknown. There is no evidence that a login profile or
`PROMPT_COMMAND` caused it. No CAN access, SDK initialization, `init_hand`, or
motion occurred. The R3 local stage and remote root are consumed and must
never be reused or deleted.

The fourth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r4` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r4` are the historical,
consumed R4 Task 4 attempt. The local `source_archive` gate completed with a
complete six-field tuple and rc `0`. The first and only SSH action was the
captured `remote_fresh_root` gate. There was no password prompt, and no
password was entered. OpenSSH timed out before the operator could
authenticate. Its complete six-field tuple, consisting of
command/stdout/stderr/rc/timestamp/environment, recorded rc `255`; stdout was
zero bytes, and stderr was one CRLF-terminated line, 65 bytes. Its visible
text was
`ssh: connect to host 192.168.13.1 port 22: Connection timed out`.

That timeout cannot prove that any remote command executed. `source_transfer`
never began, no `remote_operator_session` opened, and neither Task 5 nor Task 6
started. There was no CAN access, SDK initialization, `init_hand`, or motion.
After execution stopped, read-only local diagnostics showed `enp131s0` in
`NO-CARRIER` and `DOWN` state with no direct-link address, while the route to
`192.168.13.1` traversed a Wi-Fi gateway. Those observations are diagnostic
evidence only, not captured preflight evidence. Both R4 namespaces are
consumed and must never be reused or deleted, even though the remote root may
not exist because remote execution was never proved.

The fifth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r5` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r5` are the historical,
consumed R5 Task 4 attempt. `source_archive`, `remote_fresh_root`, and
`source_transfer` each produced a complete six-field tuple with rc `0`. The
root gate recorded boot ID `6bdd0764-008b-4163-a254-90ad60d16a99`; the closed
source stream had SHA-256
`f5734225a52517cb2c455dccf9ffaeb0471df47b0e44c7bab9ac097e1f485ba3`.
The transfer stderr contained exactly seven future-timestamp warnings caused
by the board clock lagging the archive timestamps; its successful tuple still
recorded rc `0`. Exactly two direct SSH connections were opened. Each displayed
a real OpenSSH password prompt and accepted one password entry.

The executor then started the `remote_gate_bootstrap` fragment in a fresh shell
even though the R5 plan placed it after `source_transfer` in the same fenced
block. That fresh shell had no inherited `CAPTURE` or other preceding variable
definitions, so local execution failed before a third SSH process could start
with visible error `/bin/bash: line 3: : command not found` and rc `127`.
`remote_gate_bootstrap` has no capture tuple, `remote_operator_session` has no
artifact, and zero remote deployment gate tuples exist. The inner source
archive extraction, build, CTest, and python construction never began. There
was no CAN access, SDK initialization, `init_hand`, or motion. The R5 remote
root was created and received the seven files from the closed transfer, but it
must not be queried, deleted, or reused. Both R5 namespaces are consumed and
must never be reused or deleted.

## R6 Phase A Preflight Incident and Mandatory R9 Restart

The sixth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r6` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r6` completed Task 4 and the
motion-artifact transfer. The remote report contained complete rc `0` tuples
for all ten deployment gates, the AArch64/runtime/Python construction gates
passed, and the four transferred evidence files matched their hashes. No SDK
initialization or motion occurred.

The controller then created `PHASE_SMALL_DISPATCHED` and opened the unique
`motion_setup_session`. Its complete live-TTY tuple returned rc `1` with zero
captured stdout and stderr. The remote helper output was visible, but
`capture_snapshot.sh phase-small pre` failed closed because the board had no
`can0`: the captured address list contained `can_top`, `can_hipnuc`, and
`can_bottom`, while `ip link show can0`, CAN counters, and the receiver gate
failed. No `phase-small.attempt` marker was created, and no SDK initialization,
`init_hand`, CAN command, or motion was run. The R6 local stage, remote root,
and Phase A namespace are permanently consumed and must not be replayed,
filled in, queried for retry authority, or deleted.

A later read-only board check found a USB CAN-FD `can0` in `UP`,
`ERROR-ACTIVE` state at 1 Mbit/s nominal and 5 Mbit/s data rate. That later
observation is not captured R6 preflight evidence and cannot repair R6. This
revision therefore starts a fresh R9 deployment and requires a new complete
preflight from the current power/network state.

## R7 Local Stage Incident and Mandatory R9 Restart

Before the exact R7 `source_archive` execution unit was run, a local operator
probe created `/tmp/roboparty-dexhand-deploy-db2da9f-r7` and wrote a direct
`git archive` stream (`source-stream.tar`) plus a member listing and hash. It
did not create the plan's capture helpers or six-field `source_archive` tuple,
and it did not create a remote root, open SSH, transfer files, access CAN, load
the SDK, initialize the hand, or issue motion. The path is nevertheless
consumed because the plan requires a fresh stage to be absent before any
execution unit. Its files are preserved as incident evidence and must not be
deleted, overwritten, or reused as R9 proof.

The only executable suffix after this local-only incident is R9. R9 must use a
new local stage and remote root and must run the exact source-archive unit from
the beginning before any remote connection.

## R8 Phase A Controller Incident and Mandatory R9 Restart

The eighth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r8` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r8` completed the non-motion
deployment gates and motion-artifact transfer. The controller created
`PHASE_SMALL_DISPATCHED` and opened `motion_setup_session`, but its PTY
synchronization accepted an unanchored generic shell token (`[$#] `) before the
required process-review prompt. The controller then terminated the session
without submitting `process_review=clean` and without sending the Phase A
motion block.

The resulting local live tuple is permanently partial: `.command`,
`.timestamp`, `.environment`, `.capture_mode`, and zero-byte stdout/stderr
exist, but `.rc` is absent. The R8 Phase A marker exists, while no local record
shows a process-review result or a sent motion command. This is a controller
protocol failure, not permission to reconnect or infer a physical result. R8's
local and remote namespaces are consumed and must not be replayed, filled in,
queried for retry authority, or deleted.

R9 must synchronize the live shell only with an anchored `bash-*` prompt (or a
unique explicit marker), and must treat any ambiguous output or missing prompt
as a consumed failure. It must never use a broad `[#$] ` match.

## R9 Phase A Preflight Incident and Mandatory R10 Recovery

The ninth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r9` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r9` completed Task 4 and the
motion-artifact transfer. The controller created the durable
`PHASE_SMALL_DISPATCHED` marker and opened `motion_setup_session`. The remote
Phase A preflight returned shell rc `1` before the process-review prompt was
reached. The controller therefore sent neither `process_review=clean` nor the
Phase A motion block.

The local live tuple is permanently partial: `.command`, `.timestamp`,
`.environment`, `.capture_mode`, and zero-byte stdout/stderr exist, but `.rc`
is absent. The remote preflight may have written partial read-only snapshot
evidence, but the live session has no transcript proving which snapshot command
failed. No SDK initialization, `init_hand`, CAN command, or motion command was
sent by the controller. The R9 local marker, stage, remote root, motion
evidence, and any partial preflight evidence are consumed and must not be
replayed, completed by hand, queried for retry authority, or deleted.

R10 is a read-only recovery only. It may inspect the already-created R9
evidence root and record the exact failure cause. It must not create another
`phase-small.attempt`, call `init_hand`, issue CAN commands, or authorize Phase
B. Any later physical validation would require a separate operator decision
and safety review; it is not a retry of R9.

## R11 Fresh Physical-Validation Amendment

R9 is permanently consumed after its Phase A dispatch marker and preflight
controller failure. Any future physical attempt must use the fresh local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r11` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r11`, rebuild and reverify all
non-motion gates, and never use R9 evidence as motion authority.

The R11 motion transfer has a fifth member, `phase_a_controller.sh`, with
SHA-256
`95a03618458573a9de0aa80a1f879f7fef8ff2904d7d409b2aec68601b93d070`.
The controller file is executed remotely only after its hash is checked; it
creates the capture tools, performs the Phase A preflight, reads process review
from `/dev/tty`, and runs the existing frozen harness once. It is deliberately
not pasted into an interactive shell. The old R9 Task 4/5 command blocks below
are historical templates only; no unmodified R9 block may be executed.

R11 was consumed before any remote connection. Its `remote_fresh_root` unit
was launched without a controlling TTY, so the checksum-bound TTY gate
returned rc `1` while checking `/dev/tty`. No SSH process, password prompt,
remote root, CAN command, SDK call, or motion action occurred. The R11 local
stage and partial tuple are preserved and cannot be reused. The next fresh
boundary is R12; every SSH-containing execution unit must start from a PTY.

R13 completed the fresh deployment and transferred the corrected controller,
but its one `motion_setup_session` ended before the process-review prompt.
The local live tuple is partial with zero-byte stdout/stderr and no `.rc`; the
dispatch marker exists. No review response, `phase-small.attempt`, SDK init,
CAN command, or motion command was sent. R13 is consumed. R14 may perform
only a read-only inventory of the R13 evidence leaf to identify the controller
failure; it may not replay the session or authorize motion.

R14 read-only inventory completed with no new remote action. The R13 evidence
leaf contains both generated postflight drivers and their checksum file, but
does not contain `EVIDENCE_TOOLS_SHA256SUMS`. Exact local extraction of the R13
controller proves the cause: it expected the older fixed driver hashes
(`a213ce...` and `e79ea...`), while the generated drivers hash as
`a0aeea2c0107a1cccdcb876cbe2f75a04cc25423824d01ca78774c389459ee47` and
`63ec9a5b7e4df1ea0329fa71ce62a00bf330ff2894d56526fa764b37fc0f0cb8`.
The controller therefore exited at the fixed-driver gate before writing
`EVIDENCE_TOOLS_SHA256SUMS` or reaching process review. R14 is consumed. The
next fresh physical boundary is R15, using a path-substituted controller whose
generated-driver hashes are recomputed before any SSH or CAN action.

R15 uses local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r15`, remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r15`, motion stage
`/tmp/roboparty-dexhand-motion-1a7c820-r15`, and corrected controller SHA-256
`64d5575b061b67df9e0cab3083caf9e3baf14faf3e918e28e06d209eeda69963`.
The generated postflight driver hashes are
`357c25b5d8b708e9a8c78e40be028e78aa1a370b824ffda0aee810233526f1b8` and
`9ac995803ccf49c33dc8b320b0c16333ac64953b8eabb8385b83171cd279e4be`.

R15 was consumed at `source_transfer` after the remote source stream, member
ordering, manifest, and extraction all passed. The only failure was a local
controller-script mistake in the final cleanup: it created two payload marker
files and then required the incoming directory to be empty, so the final
`rmdir` returned rc `1`. The captured tuple contains the seven expected
future-timestamp warnings; no bootstrap, operator session, SDK, CAN, or motion
command ran. The R15 remote root and local stage are permanently consumed.
The next fresh boundary is R16, whose transfer script omits the unnecessary
marker files and proves the incoming directory is empty before removal.

R16 uses local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r16`, remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r16`, motion stage
`/tmp/roboparty-dexhand-motion-1a7c820-r16`, and controller SHA-256
`653f9c3efb2efdb82c05c468adebfe0c10eef9da5f58f556272ddbb8d4bdf32f`.
Its generated driver hashes are
`aac66424b35cdba9efc763ddbf29803481d103871106304613faeb08e623bb76` and
`78bdaae3ed226320b67d9353ac45944ffcf63460d282f99ab385b0deb51f5e6b`.

R16 completed `remote_fresh_root`, source transfer, helper bootstrap, source
provenance, configure, build, CTest 8/8, plain install, and install/export.
Its operator shell then exited at the first `artifact_gate`: the manually
typed outer single-quoted `bash -c` contained an inner `trap '...'`, so the
shell parsed the command at the wrong quote boundary and reported an
unbound positional argument before the artifact gate. The operator tuple was
partial; no SDK initialization, CAN command, or motion command ran. R16 is
consumed. R17 must send Task 4 through a quoted here-document in the fixed
operator shell and must not reuse the R16 root or labels.

R17 uses local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r17`, remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r17`, motion stage
`/tmp/roboparty-dexhand-motion-1a7c820-r17`, and controller SHA-256
`ea54ee0ce69e3a9f4ec284a2ce7b95570966278f0119356f84c1ffb8b3ff1df4`.
Its generated driver hashes are
`fec5030cd5991f7da0e5cd6e23878359aa79993d4e77c4c627e48f2aeb14767b` and
`fef0dfa150e3a31f26e4ed4804e740ef757a7fca37c602924d0579b04821063a`.

R17 completed the fresh root, source transfer, helper bootstrap, Task 4
operator session, and all nine remote build/export/construction gates. The
unique `motion_artifact_transfer` then returned rc `1` with zero-byte
stdout/stderr before creating the motion evidence leaf. No SDK initialization,
CAN command, or motion command ran. R17 is consumed. R18 may perform only a
read-only inventory of the R17 deployment root to identify which precondition
failed; it may not retry the transfer or authorize motion.

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r13
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -u
root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r13
evidence="$root/evidence/motion-validation-bacf6612"
printf 'root=%s\n' "$root"
if test -e "$evidence/phase-small.attempt"; then
  printf 'phase_small_attempt=present\n'
else
  printf 'phase_small_attempt=absent\n'
fi
find "$evidence" -maxdepth 1 -type f -printf 'entry=%f type=%y\n' |
  LC_ALL=C sort
for name in capture_snapshot.sh record_process_review.sh \
    EVIDENCE_TOOLS_SHA256SUMS phase_a_controller.sh; do
  if test -e "$evidence/$name"; then
    printf 'check=%s present\n' "$name"
  else
    printf 'check=%s absent\n' "$name"
  fi
done
REMOTE
)
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" r14_readonly_inventory \
  /usr/bin/timeout --foreground --preserve-status --signal=TERM \
  --kill-after=5s 120s "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh -F /dev/null -T \
  -o IdentityAgent=none -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 -o ServerAliveCountMax=2 \
  -o ControlMaster=no -o ControlPath=none -o ControlPersist=no \
  -o ProxyCommand=none -o ProxyJump=none -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

R12 completed its fresh Task 4 build/export gates, but the unique
`motion_artifact_transfer` failed closed before any motion-capable session.
The controller hash manifest contained an absolute local `/tmp` path, so the
remote `sha256sum -c` could not resolve it and returned nonzero. No
`phase-small.attempt`, SDK initialization, CAN command, or motion action was
created or sent. R12's local stage, remote root, and partial evidence leaf are
consumed and cannot be retried. R13 must use a relative controller member name
and prove that manifest locally before transfer.

R13 uses the fresh local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r13` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r13`. Its path-bound
controller SHA-256 is
`693ae85c118c108b5f1416897f74eabee207402f4bba77e84022d457d44b263c`.

R10 `r10_readonly_recovery` returned rc `0` and reported no
`phase-small.attempt`. `r10_readonly_inventory` confirmed that the R9 root and
motion evidence leaf exist, but no `phase-small.pre.*` artifact was written.
`r10_readonly_snapshot_script` confirmed mode `0555`, successful `bash -n`,
and `ip` resolving to `/usr/sbin/ip`. The live tuple has no transcript and the
remote preflight stopped before its first captured snapshot, so the exact
failing command remains unknown. This proves no motion attempt; it does not
repair or authorize R9.

### R10 Read-only Recovery: Diagnose the R9 Preflight Failure

The Task 4 and Task 5 blocks below are retained as the historical R9
execution record and must not be replayed. Run only this new read-only label
against the existing R9 evidence root. It must not create or remove a motion
attempt and must not run `ip`, `candump`, the SDK, Python, or a motion helper:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -u
root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
evidence="$root/evidence/motion-validation-bacf6612"
printf 'recovery_root=%s\n' "$root"
if test -e "$evidence/phase-small.attempt"; then
  printf 'phase_small_attempt=present\n'
else
  printf 'phase_small_attempt=absent\n'
fi
find "$evidence" -maxdepth 1 -type f -name 'phase-small.pre.*' \
  -printf 'artifact=%f\n' | LC_ALL=C sort
for rc_file in "$evidence"/phase-small.pre.*.rc; do
  test -f "$rc_file" || continue
  printf 'rc=%s value=' "${rc_file##*/}"
  cat "$rc_file"
  complete_file=${rc_file%.rc}.complete
  if test -f "$complete_file"; then
    printf 'complete=%s yes\n' "${complete_file##*/}"
  else
    printf 'complete=%s no\n' "${complete_file##*/}"
  fi
done
for stderr_file in "$evidence"/phase-small.pre.*.stderr; do
  test -s "$stderr_file" || continue
  printf 'stderr=%s\n' "${stderr_file##*/}"
  sed -n '1,80p' "$stderr_file"
done
REMOTE
)
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" r10_readonly_recovery \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null -T \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

If that report contains no preflight artifacts, run the separate
`r10_readonly_inventory` label once to distinguish an absent evidence leaf
from an empty preflight. It only lists the existing R9 root/evidence paths and
never changes them:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -u
root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
evidence="$root/evidence/motion-validation-bacf6612"
if test -d "$root"; then printf 'root=present\n'; else printf 'root=absent\n'; fi
if test -d "$evidence"; then printf 'evidence=present\n'; else printf 'evidence=absent\n'; fi
if test -d "$root"; then
  find "$root" -maxdepth 3 -mindepth 1 -printf 'entry=%P type=%y\n' |
    LC_ALL=C sort
fi
REMOTE
)
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" r10_readonly_inventory \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh -F /dev/null -T \
  -o IdentityAgent=none -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 -o ServerAliveCountMax=2 \
  -o ControlMaster=no -o ControlPath=none -o ControlPersist=no \
  -o ProxyCommand=none -o ProxyJump=none -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

If the inventory shows the evidence leaf but no preflight files, the
`r10_readonly_snapshot_script` label may be used once to inspect only the
installed snapshot script's path, mode, and shell syntax. It does not execute
the script or any of its child commands:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -u
evidence=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
if test -f "$evidence/capture_snapshot.sh"; then
  printf 'snapshot_script=present mode=%s\n' "$(stat -c '%a' "$evidence/capture_snapshot.sh")"
  /usr/bin/bash -n "$evidence/capture_snapshot.sh"
  printf 'snapshot_script_syntax=ok\n'
  command -v ip || true
else
  printf 'snapshot_script=absent\n'
fi
REMOTE
)
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" r10_readonly_snapshot_script \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh -F /dev/null -T \
  -o IdentityAgent=none -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 -o ServerAliveCountMax=2 \
  -o ControlMaster=no -o ControlPath=none -o ControlPersist=no \
  -o ProxyCommand=none -o ProxyJump=none -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

This revision starts only with read-only recovery stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r9` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r9`; every deployment,
source, build, prefix, install, evidence, and motion path below derives from
those historical r9 paths. The R10 recovery uses a new read-only connection
label and never replays the R9 operator session. The R9 operator session used
the fixed live-TTY remote command
`/bin/bash --noprofile --norc`, records a tenth remote gate named
`task4_completion`, and ends only with explicit `exit 0`. A parameterless
`exit` is forbidden.

Before the local durable dispatch marker is created for Phase A—the first
motion-capable session—any connection failure, timeout, or agent anomaly
consumes the capture label and the entire r9 suffix. The `.command` file is
created first under shell `noclobber`; any `<stem>.*` artifact permanently
consumes that label. INT, TERM, HUP, controller failure, or capture-process
failure may leave a partial tuple, including a missing `.rc`. Such evidence
must not be rerun, completed by hand, or treated as retry authority, and neither
path may be reused or deleted. Continuing at that boundary requires a reviewed
documentation revision with a new suffix. The same rule applies whenever
remote execution is uncertain.

After a local dispatch marker exists, the operator must never replay motion
for that phase, even if no remote motion can be proved. A postflight connection
failure preserves every artifact actually written, whether a complete or
partial tuple, and blocks every later step. Only another reviewed
documentation revision may add a new, unique, read-only recovery label in the
same evidence root to resume evidence collection. Recovery never authorizes
motion or reuse of a failed label, and it must not delete or abandon the
physical evidence root. The remote physical attempt marker remains evidence,
but it does not decide whether the controller may reconnect or replay motion.
Any anomaly after Phase A dispatch blocks Phase B; Phase B remains blocked
unless all Phase A postflight and acceptance evidence is complete and
successful.

The local `PHASE_SMALL_DISPATCHED` and `PHASE_FULL_DISPATCHED` directories use
an atomic `mkdir`, an atomically installed and synced binding, and a synced
parent directory. Each binds the production commit, frozen harness, connection
label, and boot evidence. Its creation is the conservative no-replay boundary.
phase_small_postflight_session is strictly read-only with respect to motion,
and phase_full_postflight_session is strictly read-only with respect to motion.
The only Phase B motion-capable label is phase_full_execution_session. Only
fully accepted Phase A evidence permits Phase B dispatch. Each local boundary
uses atomic mkdir semantics before the connection opens.

The postflight labels run the checksum-bound postflight drivers
`phase_small_postflight_driver.sh` and `phase_full_postflight_driver.sh` through
a fixed remote command that revalidates their hashes before `exec`. A forced
TTY exposes only the drivers' documented review/acceptance prompts and does not
provide a general login shell. The small driver contains no Phase B command;
the full driver contains only Phase B postflight evidence collection.

The executable transport inventory has nine unique connection labels, all
implemented by `/usr/bin/ssh`. Every call below goes through a checksum-bound
normal or live capture helper plus an outer GNU timeout with `--foreground`,
`--preserve-status`, TERM, and a five-second kill grace. Every intermediate
wrapper uses `exec`, making SSH the timeout's direct final command. Each
transfer is a single-process SSH tar stream: a closed local tar is supplied as
stdin, command evidence binds the stdin archive path and SHA-256, and remote
tar extracts only into a new fixed incoming directory. No SCP process exists.
Bootstrap and transfer calls use 120 seconds, operator/motion sessions use
1,800 seconds, and postflight sessions use 600 seconds. On normal completion
the live helper sends output live-to-TTY without transcription and creates
zero-byte stdout/stderr sentinels, a capture-mode record, and rc. A terminal
signal or controller failure may instead leave only a partial tuple.

`remote_operator_session` forces a live TTY and supplies the fixed remote
command `/bin/bash --noprofile --norc`; it is not a generic login. After the
captured `task4_completion` audit passes, the final command in that shell is
`exit 0`. Task 5 does not keep that shell open. Its already-counted
`motion_artifact_transfer` fixed remote script revalidates the deployment
bundle and runtime anchors, proves the motion evidence leaf absent, atomically
creates it, copies and verifies the source/runtime manifests, and only then
reads stdin into a fresh spool directory. Any failure consumes the connection
label and R9 namespace; it never authorizes a retry.

Before OpenSSH starts, a checksum-bound wrapper opens `/dev/tty`, proves it is
a controlling TTY, and fails closed otherwise. Every child explicitly runs
through `/usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never`, uses
`/usr/bin/ssh`, ignores user configuration with `-F /dev/null`, selects only
`orangepi@192.168.13.1`, preserves strict known-host checking, disables
ControlMaster, proxy, forwarding, and local commands, and carries
`-o IdentityAgent=none`,
`-o PreferredAuthentications=password`, `-o PubkeyAuthentication=no`,
`-o NumberOfPasswordPrompts=1`, `-o ConnectTimeout=10`,
`-o ConnectionAttempts=1`, `-o ServerAliveInterval=5`, and
`-o ServerAliveCountMax=2`. Only after a real OpenSSH password prompt appears
may the operator enter the password once on the controlling TTY. If no prompt
appears, enter nothing. Password values must never appear in arguments,
environment values, or evidence.

The successful remote deployment inventory alone requires exactly ten
complete tuples with rc `0`: the nine functional gates followed by
`task4_completion`. The distinct SSH connection-label inventory remains
exactly nine. Failure and recovery start by enumerating every matching
`<stem>.*` artifact. A partial tuple or missing `.rc` is reported as
permanently consumed evidence; it never changes a failed label into an unused
label.

---

## File Map

- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/test_staged_motion_validation.py`
  - Offline fake contract for both phases and every cleanup path.
- Create temporarily:
  `/tmp/roboparty-dexhand-motion-1a7c820/staged_motion_validation.py`
  - Dependency-injected physical harness with `small` and `full` modes.
- Create locally:
  `/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence/`
  - Closed local evidence for source archive, fresh remote root, source
    transfer, and remote gate bootstrap; this directory is never recursively
    copied while it is being written.
- Create remotely from the clean production Git object:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/source/`
  - Exact `git archive` source for production commit `db2da9f`.
- Create remotely and keep disjoint:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/build/`,
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/`, and
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/install-gate/`
  - Native build, authoritative motion install, and separate relocatable
    install/export gate.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f/`
  - Immutable command, stdout, stderr, rc, timestamp, and selected-environment
    tuples for every authoritative non-motion gate.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612/`
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
  `/tmp/roboparty-dexhand-deploy-db2da9f-r9/`.
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/`.

- [ ] **Precondition: Regress the Python `-c` argv contract locally**

Run locally before creating the deployment stage or attempting any remote
connection. This probe imports only `sys`; any shifted argument or synthetic
construction label exits nonzero and blocks Task 4:

```bash
set -euo pipefail
/usr/bin/python3 -I -c '
import sys

if "dexhand-construction" in sys.argv:
    raise SystemExit("synthetic construction label is forbidden")
if sys.argv[1:] != ["/example/site", "/example/prefix"]:
    raise SystemExit(f"unexpected python -c argv tail: {sys.argv!r}")
print("PASS python -c argv contract")
' /example/site /example/prefix
```

Expected: exactly `PASS python -c argv contract`. No SDK module is imported,
and no deployment path, network connection, CAN interface, or hardware is
accessed.

- [ ] **Precondition: Regress explicit remote-shell completion locally**

Run locally before creating the deployment stage. This imports no SDK module,
opens no network connection, and proves both the shell exit-status behavior and
the Task 4 static ban on a parameterless operator-session exit:

```bash
set -euo pipefail
set +e
/bin/bash --noprofile --norc -c 'false; exit 0'
REMOTE_SHELL_RC=$?
set -e
test "$REMOTE_SHELL_RC" = 0
PLAN_PATH=docs/superpowers/plans/2026-08-16-orangepi-staged-motion-validation.md
test -f "$PLAN_PATH"
TASK4_TEXT=$(sed -n '/^### Task 4:/,/^### Task 5:/p' "$PLAN_PATH")
REMOTE_OPERATOR_TEXT=$(sed -n \
  '/^"\$LIVE_CAPTURE" remote_operator_session \\$/,/^```$/p' \
  <<< "$TASK4_TEXT")
test -n "$REMOTE_OPERATOR_TEXT"
test "$(grep -Fxc '  -tt \' <<< "$REMOTE_OPERATOR_TEXT")" = 1
test "$(grep -Fxc \
  '  orangepi@192.168.13.1 /bin/bash --noprofile --norc' \
  <<< "$REMOTE_OPERATOR_TEXT")" = 1
if grep -Fxq exit <<< "$TASK4_TEXT"; then
  printf '%s\n' 'parameterless Task 4 exit is forbidden' >&2
  exit 1
fi
test "$(grep -Fxc 'exit 0' <<< "$TASK4_TEXT")" = 1
printf '%s\n' 'PASS no-profile Bash explicit exit 0; plain exit absent'
```

Expected: exactly
`PASS no-profile Bash explicit exit 0; plain exit absent`, with exit `0`.
Changing Task 4's final command to plain `exit`, omitting the explicit
`exit 0`, adding another exact `exit 0` line, removing the forced `-tt`, or
changing the fixed no-profile Bash command makes this regression fail.

- [ ] **Step 1: Create an exact source archive and provenance manifest**

Run locally. This archives the clean commit object, never working-tree bytes:

```bash
set -euo pipefail
DEXHAND_REPO=/home/sjh/leisai_hand/roboparty_dexhand
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
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
shopt -s nullglob
PRIOR=("$STEM".*)
if ! test "${#PRIOR[@]}" = 0; then
  printf 'connection label already consumed: %s\n' "$LABEL" >&2
  exit 125
fi
STDIN_FILE=
STDIN_SHA256=
if test "${1-}" = --stdin-file && test "$#" -ge 4; then
  STDIN_FILE=$2
  if test "${3-}" = --stdin-sha256; then
    STDIN_SHA256=$4
  fi
fi
{
  printf 'stdin_file=%s\n' "${STDIN_FILE:-<none>}"
  printf 'stdin_sha256=%s\n' "${STDIN_SHA256:-<none>}"
  printf 'command='
  printf '%q ' "$@"
  printf '\n'
} > "$STEM.command"
/usr/bin/sync -f "$STEM.command"
/usr/bin/sync -f "$EVIDENCE"
if test -n "$STDIN_FILE"; then
  test "$#" -ge 5
  test "$1" = --stdin-file
  test "$3" = --stdin-sha256
  test "$5" = --
  shift 5
  test -f "$STDIN_FILE"
  test "${STDIN_FILE#/}" != "$STDIN_FILE"
  test -n "$STDIN_SHA256"
  read -r ACTUAL_STDIN_SHA256 ACTUAL_STDIN_PATH < <(
    sha256sum "$STDIN_FILE"
  )
  test "$ACTUAL_STDIN_PATH" = "$STDIN_FILE"
  test "$ACTUAL_STDIN_SHA256" = "$STDIN_SHA256"
fi
/usr/bin/date --iso-8601=seconds > "$STEM.timestamp"
/bin/bash -c \
  'for name in PWD PATH PYTHONPATH LD_LIBRARY_PATH PYTHONOPTIMIZE CMAKE_PREFIX_PATH AMENT_PREFIX_PATH COLCON_PREFIX_PATH; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$STEM.environment"
set +e
if test -n "$STDIN_FILE"; then
  "$@" < "$STDIN_FILE" > "$STEM.stdout" 2> "$STEM.stderr"
else
  "$@" > "$STEM.stdout" 2> "$STEM.stderr"
fi
GATE_RC=$?
set -e
printf '%s\n' "$GATE_RC" > "$STEM.rc"
cat "$STEM.stdout"
cat "$STEM.stderr" >&2
test "$GATE_RC" = 0
SH

cat > "$DEPLOY_EVIDENCE/capture_live_gate.sh" <<'SH'
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
shopt -s nullglob
PRIOR=("$STEM".*)
if ! test "${#PRIOR[@]}" = 0; then
  printf 'connection label already consumed: %s\n' "$LABEL" >&2
  exit 125
fi
{
  printf '%q ' "$@"
  printf '\n'
} > "$STEM.command"
/usr/bin/sync -f "$STEM.command"
/usr/bin/sync -f "$EVIDENCE"
/usr/bin/date --iso-8601=seconds > "$STEM.timestamp"
/bin/bash -c \
  'for name in PWD PATH PYTHONPATH LD_LIBRARY_PATH PYTHONOPTIMIZE CMAKE_PREFIX_PATH AMENT_PREFIX_PATH COLCON_PREFIX_PATH; do printf "%s=%s\n" "$name" "${!name-<unset>}"; done' \
  > "$STEM.environment"
printf '%s\n' live-to-TTY-no-transcript > "$STEM.capture_mode"
: > "$STEM.stdout"
: > "$STEM.stderr"
TTY_OPEN_RC=0
TTY_TEST_RC=125
GATE_RC=125
set +e
exec 8<>/dev/tty
TTY_OPEN_RC=$?
if test "$TTY_OPEN_RC" = 0; then
  test -t 8
  TTY_TEST_RC=$?
  if test "$TTY_TEST_RC" = 0; then
    "$@" <&8 >&8 2>&8
    GATE_RC=$?
  fi
fi
set -e
if test "$TTY_OPEN_RC" = 0; then
  exec 8>&-
fi
printf '%s\n' "$GATE_RC" > "$STEM.rc"
test "$GATE_RC" = 0
SH

cat > "$DEPLOY_EVIDENCE/require_tty_exec.sh" <<'SH'
#!/bin/bash
set -euo pipefail
test "$#" -ge 6
test "$1" = /usr/bin/env
test "$2" = -u
test "$3" = SSH_AUTH_SOCK
test "$4" = SSH_ASKPASS_REQUIRE=never
test "$5" = /usr/bin/ssh
exec 9<>/dev/tty
test -t 9
exec 9>&-
exec "$@"
SH

chmod 0555 \
  "$DEPLOY_EVIDENCE/capture_gate.sh" \
  "$DEPLOY_EVIDENCE/capture_live_gate.sh" \
  "$DEPLOY_EVIDENCE/require_tty_exec.sh"
(cd "$DEPLOY_EVIDENCE" && \
  sha256sum capture_gate.sh capture_live_gate.sh require_tty_exec.sh > CAPTURE_GATE_SHA256SUM)
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
SOURCE_STREAM="$DEPLOY_STAGE/source-transfer-bundle.tar"
SOURCE_STREAM_MANIFEST="$DEPLOY_STAGE/SOURCE_TRANSFER_STREAM.sha256"
test ! -e "$SOURCE_STREAM"
test ! -e "$SOURCE_STREAM_MANIFEST"
/usr/bin/tar --create --format=posix --file="$SOURCE_STREAM" \
  -C "$DEPLOY_STAGE" \
  roboparty_dexhand-db2da9f.tar \
  SOURCE_MANIFEST \
  SOURCE_TRANSFER_SHA256SUMS \
  -C "$DEPLOY_EVIDENCE" \
  capture_gate.sh \
  capture_live_gate.sh \
  require_tty_exec.sh \
  CAPTURE_GATE_SHA256SUM
test "$(/usr/bin/tar --list --file="$SOURCE_STREAM")" = \
  "$(printf '%s\n' \
    roboparty_dexhand-db2da9f.tar \
    SOURCE_MANIFEST \
    SOURCE_TRANSFER_SHA256SUMS \
    capture_gate.sh \
    capture_live_gate.sh \
    require_tty_exec.sh \
    CAPTURE_GATE_SHA256SUM)"
(cd "$DEPLOY_STAGE" && \
  sha256sum source-transfer-bundle.tar > SOURCE_TRANSFER_STREAM.sha256)
read -r SOURCE_STREAM_SHA256 SOURCE_STREAM_NAME < "$SOURCE_STREAM_MANIFEST"
test "$SOURCE_STREAM_NAME" = source-transfer-bundle.tar
test -n "$SOURCE_STREAM_SHA256"
```

Expected: local bundle `bootstrap-evidence` contains the normal capture
helper, live capture helper, TTY gate, and their three-record checksum
manifest. Each helper creates `.command` first and refuses a label when any
matching artifact already exists. On normal completion the live helper writes
the remaining selected environment, capture mode, zero-byte stdout/stderr
sentinels, and rc while sending session output only to the controlling TTY; a
signal may leave a permanently consumed partial tuple. The TTY gate fails
before OpenSSH when it cannot open and prove `/dev/tty`. The bundle also contains
`source_archive.command/.stdout/.stderr/.rc/.timestamp/.environment` with rc
`0`. `SOURCE_MANIFEST` binds the commit, tree, archive hash, prefix, and
generating command. The closed source transfer tar and its separate SHA-256
manifest contain exactly the seven listed files. This bundle later records the fresh-root and transfer
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
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
REMOTE_SCRIPT='set -euo pipefail; root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9; test ! -e "$root"; test "$(uname -m)" = aarch64; boot_id=$(cat /proc/sys/kernel/random/boot_id); test "$(printf "%s" "$boot_id" | wc -c)" = 36; mkdir "$root"; mkdir "$root/evidence"; printf "boot_id=%s\n" "$boot_id"'
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" remote_fresh_root \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

Expected: `remote_fresh_root` records the exact absence assertion, exact
`aarch64` assertion, atomic parent/evidence `mkdir`, and the new root's
`boot_id=<36-byte-kernel-boot-id>` line with its complete evidence tuple and
rc `0`. This stdout is later checksum-bound into each local dispatch marker.
The 120-second outer timeout bounds the whole
TTY/password/SSH/remote-command lifetime, including a responsive but stuck
remote command. The prior power loss invalidates every earlier
power, network, CAN, process, socket, and counter observation. If the root
exists, the captured gate fails; never reuse or delete the path.

- [ ] **Step 3a: Transfer and verify the exact archive**

Run locally. `source_transfer` sends the previously closed seven-file tar on
stdin to one SSH process; it never recursively reads the bootstrap evidence
directory while that directory is recording the transfer. The normal helper
rechecks the tar hash and records its absolute stdin path and SHA-256 in
`.command`. SSH reads the password only from the proved controlling TTY:

Each marked Bash execution unit must be run in a fresh shell. Never split a
marked unit, and never carry variables or shell state from one marked unit to
the next.

<!-- R9_EXECUTION_UNIT: source_transfer -->

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
SOURCE_STREAM="$DEPLOY_STAGE/source-transfer-bundle.tar"
SOURCE_STREAM_MANIFEST="$DEPLOY_STAGE/SOURCE_TRANSFER_STREAM.sha256"
read -r SOURCE_STREAM_SHA256 SOURCE_STREAM_NAME < "$SOURCE_STREAM_MANIFEST"
test "$SOURCE_STREAM_NAME" = source-transfer-bundle.tar
(cd "$DEPLOY_STAGE" && sha256sum -c SOURCE_TRANSFER_STREAM.sha256)
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -euo pipefail
EXPECTED_STREAM_SHA256=$1
root=$2
incoming="$root/.source-transfer-incoming"
PAYLOAD="$incoming/payload.tar"
UNPACK="$incoming/unpack"
test ! -e "$incoming"
mkdir "$incoming"
set -o noclobber
/bin/cat > "$PAYLOAD"
/usr/bin/sync -f "$PAYLOAD"
read -r ACTUAL_STREAM_SHA256 ACTUAL_PAYLOAD_PATH < <(
  sha256sum "$PAYLOAD"
)
test "$ACTUAL_PAYLOAD_PATH" = "$PAYLOAD"
test "$ACTUAL_STREAM_SHA256" = "$EXPECTED_STREAM_SHA256"
/usr/bin/python3 - "$PAYLOAD" \
  roboparty_dexhand-db2da9f.tar \
  SOURCE_MANIFEST \
  SOURCE_TRANSFER_SHA256SUMS \
  capture_gate.sh \
  capture_live_gate.sh \
  require_tty_exec.sh \
  CAPTURE_GATE_SHA256SUM <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], "r:") as archive:
    members = archive.getmembers()
expected = sys.argv[2:]
if [member.name for member in members] != expected:
    raise SystemExit("unexpected or reordered source-transfer member")
if not all(member.isfile() for member in members):
    raise SystemExit("source-transfer member is not a regular file")
PY
mkdir "$UNPACK"
/usr/bin/tar --extract --file="$PAYLOAD" --directory="$UNPACK" \
  --no-same-owner --no-same-permissions
test -z "$(find "$UNPACK" -mindepth 1 -maxdepth 1 ! -type f -print -quit)"
test "$(find "$UNPACK" -mindepth 1 -maxdepth 1 -type f | wc -l)" = 7
actual=$(find "$UNPACK" -mindepth 1 -maxdepth 1 -type f \
  -printf '%f\n' | LC_ALL=C sort)
expected=$(printf '%s\n' \
  roboparty_dexhand-db2da9f.tar \
  SOURCE_MANIFEST \
  SOURCE_TRANSFER_SHA256SUMS \
  capture_gate.sh \
  capture_live_gate.sh \
  require_tty_exec.sh \
  CAPTURE_GATE_SHA256SUM | LC_ALL=C sort)
test "$actual" = "$expected"
(cd "$UNPACK" && sha256sum -c SOURCE_TRANSFER_SHA256SUMS)
(cd "$UNPACK" && sha256sum -c CAPTURE_GATE_SHA256SUM)
mv "$UNPACK"/* "$root"/
/usr/bin/sync -f "$root"
rmdir "$UNPACK"
rm -f "$PAYLOAD"
rmdir "$incoming"
REMOTE
)
REMOTE_COMMAND=
printf -v REMOTE_COMMAND '/bin/bash -c %q _ %q %q' \
  "$REMOTE_SCRIPT" "$SOURCE_STREAM_SHA256" "$REMOTE_ROOT"
"$CAPTURE" source_transfer \
  --stdin-file "$SOURCE_STREAM" \
  --stdin-sha256 "$SOURCE_STREAM_SHA256" \
  -- \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -T \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

Expected: `source_transfer` has a complete six-field tuple with rc `0`. No
active capture directory is recursively copied. The source stream is synced
and outer-hash/type/member validated before extraction into its fresh `unpack`
child. A timeout result is saved in `.rc` only when control returns to the
helper; an external signal may instead leave the label permanently partial.

- [ ] **Step 3b: Install and verify the remote capture helpers**

Run this second unit in a new local shell. It deliberately redeclares every
path, helper, remote-script, and remote-command dependency; it does not depend
on Step 3a variables or any preceding shell state.

<!-- R9_EXECUTION_UNIT: remote_gate_bootstrap -->

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
REMOTE_SCRIPT='set -euo pipefail; root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9; gate="$root/evidence/deployment-db2da9f"; test ! -e "$gate"; mkdir "$gate"; mv "$root/capture_gate.sh" "$root/capture_live_gate.sh" "$root/require_tty_exec.sh" "$root/CAPTURE_GATE_SHA256SUM" "$gate/"; cd "$gate"; sha256sum -c CAPTURE_GATE_SHA256SUM'
REMOTE_COMMAND=
printf -v REMOTE_COMMAND '/bin/bash -c %q' "$REMOTE_SCRIPT"
"$CAPTURE" remote_gate_bootstrap \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

Expected: the local bootstrap bundle now also has complete
`remote_gate_bootstrap` evidence. Reconnect through the live helper for the
operator session, then run the first independently captured remote gate:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
LIVE_CAPTURE="$BOOTSTRAP_EVIDENCE/capture_live_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
"$LIVE_CAPTURE" remote_operator_session \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 1800s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -tt \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 /bin/bash --noprofile --norc
```

The 1,800-second bound covers the complete interactive session. The forced TTY
remains live, but the remote command is fixed to
`/bin/bash --noprofile --norc`; that Bash reads neither profile nor rc files
and does not alter the manual Task 4 commands below. Output is live on the
controlling TTY and is not transcribed. The helper creates the explicit
zero-byte stdout/stderr sentinels, `capture_mode`, and final rc after the
operator's mandatory `exit 0` or after the timeout terminates the session,
provided control returns to the helper. A controller-side signal can leave any
suffix absent.

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
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
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
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
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
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
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
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
' "$PYTHON_SITE" "$PREFIX_DIR"
```

The construction invocation intentionally passes no synthetic label after the
`-c` program. For the exact shape
`python3 -I -c CODE "$PYTHON_SITE" "$PREFIX_DIR"`, Python provides `-c` as
`sys.argv[0]`, the site path as `sys.argv[1]`, and the prefix as `sys.argv[2]`.
A static regression must reject any intervening token, and a local dynamic
`python3 -I -c` argv probe (without importing `dexhand_py` or any SDK module)
must reproduce exactly `['-c', '/example/site', '/example/prefix']` for example
arguments before this remote gate is eligible to run.

Expected: all three runtime objects are AArch64, `ldd` has no missing entry,
exactly one installed SDK has the fixed hash, and Python resolves inside the
new prefix with `check_health`. The probe only constructs the object; it does
not call `init_hand`, open `can0`, or issue a CAN command.

- [ ] **Step 7: Capture Task 4 completion and exit the fixed shell with rc 0**

Run remotely in the same fixed `remote_operator_session` shell immediately
after `python_construction`. This independently revalidates the nine earlier
remote gates and records its own complete tuple before the shell exits:

```bash
set -euo pipefail
REMOTE_ROOT=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
DEPLOY_EVIDENCE="$REMOTE_ROOT/evidence/deployment-db2da9f"
CAPTURE="$DEPLOY_EVIDENCE/capture_gate.sh"
REMOTE_LABELS=(
  remote_archive_provenance configure configure_contract build ctest
  plain_install install_export artifact_gate python_construction
)
test "${#REMOTE_LABELS[@]}" = 9
"$CAPTURE" task4_completion /bin/bash -c '
  set -euo pipefail
  root=$1
  deploy_evidence="$root/evidence/deployment-db2da9f"
  prefix="$root/prefix"
  python_site="$prefix/lib/python3.10/site-packages"
  sdk="$prefix/lib/libLHandProLib.so"
  dexhand="$prefix/lib/libdexhand.so"
  remote_labels=(
    remote_archive_provenance configure configure_contract build ctest
    plain_install install_export artifact_gate python_construction
  )
  test "${#remote_labels[@]}" = 9
  for label in "${remote_labels[@]}"; do
    for suffix in command stdout stderr rc timestamp environment; do
      test -f "$deploy_evidence/$label.$suffix"
    done
    test "$(cat "$deploy_evidence/$label.rc")" = 0
  done
  set +e
  rc_listing=$(find "$deploy_evidence" -maxdepth 1 -type f \
    -name "*.rc" -print)
  rc_find_rc=$?
  command_listing=$(find "$deploy_evidence" -maxdepth 1 -type f \
    -name "*.command" -print)
  command_find_rc=$?
  set -e
  test "$rc_find_rc" = 0
  test "$command_find_rc" = 0
  rc_files=()
  command_files=()
  if test -n "$rc_listing"; then
    mapfile -t rc_files <<< "$rc_listing"
  fi
  if test -n "$command_listing"; then
    mapfile -t command_files <<< "$command_listing"
  fi
  test "${#rc_files[@]}" = 9
  test "${#command_files[@]}" = 10
  (cd "$deploy_evidence" && \
    sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
  sha256sum -c "$deploy_evidence/RUNTIME_MANIFEST.sha256"
  test "$(wc -l < "$deploy_evidence/RUNTIME_MANIFEST.sha256")" = 3
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
  for artifact in "$sdk" "$dexhand" "$module"; do
    readelf -h "$artifact" | grep -Eq "Machine:[[:space:]]+AArch64"
  done
  dexhand_runpath=$(readelf -d "$dexhand" | \
    sed -n "s/.*(RUNPATH).*\\[\\(.*\\)\\].*/\\1/p")
  module_runpath=$(readelf -d "$module" | \
    sed -n "s/.*(RUNPATH).*\\[\\(.*\\)\\].*/\\1/p")
  test "$dexhand_runpath" = "\$ORIGIN"
  test "$module_runpath" = "\$ORIGIN/../.."
  read -r sdk_hash sdk_path < <(sha256sum "$sdk")
  test "$sdk_path" = "$sdk"
  test "$sdk_hash" = \
    476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044
  test ! -s "$deploy_evidence/python_construction.stderr"
  expected_python_stdout=$(printf "%s\n%s\n" "$module" \
    "PASS construction-only check_health; init_hand not called")
  test "$(cat "$deploy_evidence/python_construction.stdout")" = \
    "$expected_python_stdout"
  ! grep -Fq dexhand-construction \
    "$deploy_evidence/python_construction.command"
  python_command_line=$(sed -n "s/^command=//p" \
    "$deploy_evidence/python_construction.command")
  case "$python_command_line" in
    *"/usr/bin/python3 -I -c "*" $python_site $prefix ") ;;
    *) exit 1 ;;
  esac
  test "$(grep -Fxc \
    "100% tests passed, 0 tests failed out of 8" \
    "$deploy_evidence/ctest.stdout")" = 1
  grep -Fx \
    "source_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3" \
    "$root/SOURCE_MANIFEST"
  grep -Fx \
    "source_tree=aed385f28d3010fc167914872550f4bbb0a51057" \
    "$root/SOURCE_MANIFEST"
  printf "PASS Task4 remote completion gates=9 manifest=3 aarch64=3 dexhand_runpath=%s module_runpath=%s sdk_sha256=%s python_construction=PASS\n" \
    "$dexhand_runpath" "$module_runpath" "$sdk_hash"
' _ "$REMOTE_ROOT"
ALL_REMOTE_LABELS=("${REMOTE_LABELS[@]}" task4_completion)
test "${#ALL_REMOTE_LABELS[@]}" = 10
for label in "${ALL_REMOTE_LABELS[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$DEPLOY_EVIDENCE/$label.$suffix"
  done
  test "$(cat "$DEPLOY_EVIDENCE/$label.rc")" = 0
done
set +e
all_rc_listing=$(find "$DEPLOY_EVIDENCE" -maxdepth 1 -type f \
  -name '*.rc' -print)
all_rc_find_rc=$?
set -e
test "$all_rc_find_rc" = 0
all_rc_files=()
if test -n "$all_rc_listing"; then
  mapfile -t all_rc_files <<< "$all_rc_listing"
fi
test "${#all_rc_files[@]}" = 10
exit 0
```

Expected: `task4_completion` captures the final PASS line with a complete
six-field tuple and rc `0`; the exact ten-label inventory is closed; the fixed
no-profile shell exits with status `0`; and the local
`remote_operator_session` helper can therefore record rc `0`. `exit 0` is the
last command in the operator-session execution. A plain `exit` is forbidden.
No command in this gate initializes the SDK, calls `init_hand`, accesses CAN,
or moves hardware.

### Task 5: Stage Evidence and Run Phase A Once

**Files:**
- Copy the frozen harness, contract, and both local hash files from:
  `/tmp/roboparty-dexhand-motion-1a7c820/`
- Create remotely:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612/`

- [ ] **Step 1: Confirm Task 4 closed successfully**

Verify the closed local bootstrap/transfer evidence and the complete live
operator-session tuple. This step opens no connection:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
NORMAL_BOOTSTRAP_GATES=(
  source_archive remote_fresh_root source_transfer remote_gate_bootstrap
)
for gate in "${NORMAL_BOOTSTRAP_GATES[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
  done
  test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
done
gate=remote_operator_session
for suffix in command stdout stderr rc timestamp environment capture_mode; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
grep -Fx live-to-TTY-no-transcript \
  "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
```

Expected: exit `0`. `remote_operator_session` is already closed with rc `0`;
Task 5 must not depend on an open shell. The fixed
`motion_artifact_transfer` remote command below, under its already-counted
connection label, owns the authoritative deployment-bundle checks, motion-leaf
absence assertion, and atomic creation. Do not open an extra connection and do
not reuse a partial evidence directory.

- [ ] **Step 2: Atomically create the motion evidence and copy the exact script**

Build a closed four-file tar and separate hash manifest, then send that tar on
stdin to a single SSH process under the independent
`motion_artifact_transfer` label. The block rechecks the closed operator tuple
before dispatch:

```bash
set -euo pipefail
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
BOOTSTRAP_EVIDENCE="$DEPLOY_STAGE/bootstrap-evidence"
MOTION_STAGE=/tmp/roboparty-dexhand-motion-1a7c820
CAPTURE="$BOOTSTRAP_EVIDENCE/capture_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
gate=remote_operator_session
for suffix in command stdout stderr rc timestamp environment capture_mode; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
grep -Fx live-to-TTY-no-transcript \
  "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
MOTION_STREAM="$DEPLOY_STAGE/motion-artifact-transfer-bundle.tar"
MOTION_STREAM_MANIFEST="$DEPLOY_STAGE/MOTION_ARTIFACT_TRANSFER_STREAM.sha256"
test ! -e "$MOTION_STREAM"
test ! -e "$MOTION_STREAM_MANIFEST"
/usr/bin/tar --create --format=posix --file="$MOTION_STREAM" \
  -C "$MOTION_STAGE" \
  staged_motion_validation.py \
  test_staged_motion_validation.py \
  SHA256SUMS \
  MOTION_SCRIPT_SHA256SUM
test "$(/usr/bin/tar --list --file="$MOTION_STREAM")" = \
  "$(printf '%s\n' \
    staged_motion_validation.py \
    test_staged_motion_validation.py \
    SHA256SUMS \
    MOTION_SCRIPT_SHA256SUM)"
(cd "$DEPLOY_STAGE" && \
  sha256sum motion-artifact-transfer-bundle.tar \
    > MOTION_ARTIFACT_TRANSFER_STREAM.sha256)
read -r MOTION_STREAM_SHA256 MOTION_STREAM_NAME < "$MOTION_STREAM_MANIFEST"
test "$MOTION_STREAM_NAME" = motion-artifact-transfer-bundle.tar
(cd "$DEPLOY_STAGE" && sha256sum -c MOTION_ARTIFACT_TRANSFER_STREAM.sha256)
REMOTE_SCRIPT=$(cat <<'REMOTE'
set -euo pipefail
EXPECTED_STREAM_SHA256=$1
root=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9
deploy_evidence="$root/evidence/deployment-db2da9f"
evidence="$root/evidence/motion-validation-bacf6612"
source_manifest="$root/SOURCE_MANIFEST"
remote_labels=(
  remote_archive_provenance configure configure_contract build ctest
  plain_install install_export artifact_gate python_construction
  task4_completion
)
test "${#remote_labels[@]}" = 10
test -d "$deploy_evidence"
test -f "$source_manifest"
for label in "${remote_labels[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$deploy_evidence/$label.$suffix"
  done
  test "$(cat "$deploy_evidence/$label.rc")" = 0
done
set +e
deploy_rc_listing=$(find "$deploy_evidence" -maxdepth 1 -type f \
  -name '*.rc' -print)
deploy_rc_find_rc=$?
set -e
test "$deploy_rc_find_rc" = 0
deploy_rc_files=()
if test -n "$deploy_rc_listing"; then
  mapfile -t deploy_rc_files <<< "$deploy_rc_listing"
fi
test "${#deploy_rc_files[@]}" = 10
test ! -s "$deploy_evidence/task4_completion.stderr"
test "$(grep -Fc 'PASS Task4 remote completion gates=9 manifest=3 aarch64=3' \
  "$deploy_evidence/task4_completion.stdout")" = 1
(cd "$deploy_evidence" && \
  sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
sha256sum -c "$deploy_evidence/RUNTIME_MANIFEST.sha256"
test "$(wc -l < "$deploy_evidence/RUNTIME_MANIFEST.sha256")" = 3
grep -Fx \
  'source_commit=db2da9fb90f407bdd5e3bbd3de691e775d27abd3' \
  "$source_manifest"
grep -Fx \
  'source_tree=aed385f28d3010fc167914872550f4bbb0a51057' \
  "$source_manifest"
test ! -e "$evidence"
mkdir "$evidence"
/bin/cp "$source_manifest" \
  "$deploy_evidence/RUNTIME_MANIFEST.sha256" \
  "$deploy_evidence/RUNTIME_MANIFEST_ANCHOR.sha256" \
  "$evidence/"
cmp "$source_manifest" "$evidence/SOURCE_MANIFEST"
cmp "$deploy_evidence/RUNTIME_MANIFEST.sha256" \
  "$evidence/RUNTIME_MANIFEST.sha256"
cmp "$deploy_evidence/RUNTIME_MANIFEST_ANCHOR.sha256" \
  "$evidence/RUNTIME_MANIFEST_ANCHOR.sha256"
(cd "$evidence" && sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
sha256sum -c "$evidence/RUNTIME_MANIFEST.sha256"
/usr/bin/sync -f "$evidence"
incoming="$evidence/.motion-artifact-transfer-incoming"
PAYLOAD="$incoming/payload.tar"
UNPACK="$incoming/unpack"
test ! -e "$incoming"
mkdir "$incoming"
set -o noclobber
/bin/cat > "$PAYLOAD"
/usr/bin/sync -f "$PAYLOAD"
read -r ACTUAL_STREAM_SHA256 ACTUAL_PAYLOAD_PATH < <(
  sha256sum "$PAYLOAD"
)
test "$ACTUAL_PAYLOAD_PATH" = "$PAYLOAD"
test "$ACTUAL_STREAM_SHA256" = "$EXPECTED_STREAM_SHA256"
/usr/bin/python3 - "$PAYLOAD" \
  staged_motion_validation.py \
  test_staged_motion_validation.py \
  SHA256SUMS \
  MOTION_SCRIPT_SHA256SUM <<'PY'
import sys
import tarfile

with tarfile.open(sys.argv[1], "r:") as archive:
    members = archive.getmembers()
expected = sys.argv[2:]
if [member.name for member in members] != expected:
    raise SystemExit("unexpected or reordered motion-transfer member")
if not all(member.isfile() for member in members):
    raise SystemExit("motion-transfer member is not a regular file")
PY
mkdir "$UNPACK"
/usr/bin/tar --extract --file="$PAYLOAD" --directory="$UNPACK" \
  --no-same-owner --no-same-permissions
test -z "$(find "$UNPACK" -mindepth 1 -maxdepth 1 ! -type f -print -quit)"
test "$(find "$UNPACK" -mindepth 1 -maxdepth 1 -type f | wc -l)" = 4
actual=$(find "$UNPACK" -mindepth 1 -maxdepth 1 -type f \
  -printf '%f\n' | LC_ALL=C sort)
expected=$(printf '%s\n' \
  staged_motion_validation.py \
  test_staged_motion_validation.py \
  SHA256SUMS \
  MOTION_SCRIPT_SHA256SUM | LC_ALL=C sort)
test "$actual" = "$expected"
(cd "$UNPACK" && sha256sum -c SHA256SUMS)
(cd "$UNPACK" && sha256sum -c MOTION_SCRIPT_SHA256SUM)
mv "$UNPACK"/* "$evidence"/
/usr/bin/sync -f "$evidence"
rmdir "$UNPACK"
rm -f "$PAYLOAD"
rmdir "$incoming"
REMOTE
)
printf -v REMOTE_COMMAND '/bin/bash -c %q _ %q' \
  "$REMOTE_SCRIPT" "$MOTION_STREAM_SHA256"
"$CAPTURE" motion_artifact_transfer \
  --stdin-file "$MOTION_STREAM" \
  --stdin-sha256 "$MOTION_STREAM_SHA256" \
  -- \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 120s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -T \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$REMOTE_COMMAND"
```

Before reading stdin, the fixed remote command verifies the exact ten-gate
deployment inventory and rc values, the successful completion tuple, both
runtime-manifest layers, and the production source manifest. It then proves
the motion evidence leaf absent, atomically creates it, copies and verifies the
source/runtime manifests, and syncs the fresh leaf. Only after those gates pass
does it create a fresh incoming directory, spool and sync stdin, and verify the
outer SHA-256 passed in the fixed remote command. It rejects reordered,
non-regular, or extra members before tar extracts into a separate fresh
`unpack` child, rechecks both inner manifests, and moves the four verified
files into the evidence directory. Any failure consumes the transfer label and
R9 namespace; no motion is authorized and no retry is allowed. Do not place
credentials in a command log.

- [ ] **Step 3: Dispatch Phase A, then verify local and remote script hashes**

First prove the closed local inputs and the boot evidence from the successful
fresh-root gate. Create and sync the local dispatch marker before opening the
only session allowed to execute Phase A, then reconnect with the same bounded
password-only policy:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
MOTION_STAGE=/tmp/roboparty-dexhand-motion-1a7c820
SMALL_DISPATCH="$BOOTSTRAP_EVIDENCE/PHASE_SMALL_DISPATCHED"
gate=motion_artifact_transfer
for suffix in command stdout stderr rc timestamp environment; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
PRODUCTION_COMMIT=db2da9fb90f407bdd5e3bbd3de691e775d27abd3
PRODUCTION_TREE=aed385f28d3010fc167914872550f4bbb0a51057
MOTION_SCRIPT_SHA256=bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9
FROZEN_TEST_SHA256=df774043b20156d541f2cd7bbf6611d96c8922ffe7b66ab0f4b7591dd4be45ce
grep -Fx "source_commit=$PRODUCTION_COMMIT" "$DEPLOY_STAGE/SOURCE_MANIFEST"
grep -Fx "source_tree=$PRODUCTION_TREE" "$DEPLOY_STAGE/SOURCE_MANIFEST"
(cd "$MOTION_STAGE" && sha256sum -c SHA256SUMS)
(cd "$MOTION_STAGE" && sha256sum -c MOTION_SCRIPT_SHA256SUM)
grep -Fx "$MOTION_SCRIPT_SHA256  staged_motion_validation.py" \
  "$MOTION_STAGE/MOTION_SCRIPT_SHA256SUM"
grep -Fx "$FROZEN_TEST_SHA256  test_staged_motion_validation.py" \
  "$MOTION_STAGE/SHA256SUMS"
test "$(cat "$BOOTSTRAP_EVIDENCE/remote_fresh_root.rc")" = 0
BOOT_EVIDENCE="$BOOTSTRAP_EVIDENCE/remote_fresh_root.stdout"
test "$(wc -l < "$BOOT_EVIDENCE")" = 1
REMOTE_BOOT_LINE=$(cat "$BOOT_EVIDENCE")
[[ "$REMOTE_BOOT_LINE" =~ \
  ^boot_id=[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12}$ ]]
read -r BOOT_EVIDENCE_SHA256 BOOT_EVIDENCE_PATH < <(
  /usr/bin/sha256sum "$BOOT_EVIDENCE"
)
test "$BOOT_EVIDENCE_PATH" = "$BOOT_EVIDENCE"
test ! -e "$SMALL_DISPATCH"
DISPATCH_TMP=$(mktemp -d \
  "$BOOTSTRAP_EVIDENCE/.phase-small-dispatched.XXXXXX")
set -o noclobber
printf '%s\n' \
  "production_commit=$PRODUCTION_COMMIT" \
  "production_tree=$PRODUCTION_TREE" \
  "motion_script_sha256=$MOTION_SCRIPT_SHA256" \
  "frozen_test_sha256=$FROZEN_TEST_SHA256" \
  small_postflight_driver_sha256=a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e \
  full_postflight_driver_sha256=e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a \
  connection_label=motion_setup_session \
  boot_evidence_label=remote_fresh_root.stdout \
  "boot_evidence_sha256=$BOOT_EVIDENCE_SHA256" \
  "$REMOTE_BOOT_LINE" \
  > "$DISPATCH_TMP/binding"
/usr/bin/sync -f "$DISPATCH_TMP/binding"
mkdir "$SMALL_DISPATCH"
/usr/bin/mv -T "$DISPATCH_TMP/binding" "$SMALL_DISPATCH/binding"
rmdir "$DISPATCH_TMP"
/usr/bin/sync -f "$SMALL_DISPATCH/binding"
/usr/bin/sync -f "$BOOTSTRAP_EVIDENCE"
test "$(wc -l < "$SMALL_DISPATCH/binding")" = 10
grep -Fx "production_commit=$PRODUCTION_COMMIT" "$SMALL_DISPATCH/binding"
grep -Fx "production_tree=$PRODUCTION_TREE" "$SMALL_DISPATCH/binding"
grep -Fx "motion_script_sha256=$MOTION_SCRIPT_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx "frozen_test_sha256=$FROZEN_TEST_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx \
  small_postflight_driver_sha256=a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e \
  "$SMALL_DISPATCH/binding"
grep -Fx \
  full_postflight_driver_sha256=e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a \
  "$SMALL_DISPATCH/binding"
grep -Fx connection_label=motion_setup_session "$SMALL_DISPATCH/binding"
grep -Fx boot_evidence_label=remote_fresh_root.stdout \
  "$SMALL_DISPATCH/binding"
grep -Fx "boot_evidence_sha256=$BOOT_EVIDENCE_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx "$REMOTE_BOOT_LINE" "$SMALL_DISPATCH/binding"
LIVE_CAPTURE="$BOOTSTRAP_EVIDENCE/capture_live_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
"$LIVE_CAPTURE" motion_setup_session \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 1800s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1
```

The atomic local `mkdir` is the conservative no-replay boundary. Any failure
before it consumes the r9 suffix; any failure from that point onward preserves
the marker and forbids Phase A replay even if the remote physical attempt
marker was never created. It also blocks Phase B until a reviewed read-only
recovery proves all Phase A postflight and acceptance gates. The live helper
records the session rc only when it regains control and never claims a
stdout/stderr transcript. Then change to the evidence directory and run remotely:

```bash
set -euo pipefail
cd /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f && \
  sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256)
cmp RUNTIME_MANIFEST.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f/RUNTIME_MANIFEST.sha256
cmp RUNTIME_MANIFEST_ANCHOR.sha256 \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f/RUNTIME_MANIFEST_ANCHOR.sha256
sha256sum -c RUNTIME_MANIFEST_ANCHOR.sha256
sha256sum -c RUNTIME_MANIFEST.sha256
```

Expected: both files are `OK` and match their fixed hashes. The frozen test is
provenance evidence only and is never executed on the board. Verify every
authoritative non-motion gate and its complete evidence tuple before creating
the motion capture tools:

```bash
set -euo pipefail
DEPLOY_EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f
DEPLOY_GATES=(
  remote_archive_provenance configure configure_contract build ctest
  plain_install install_export artifact_gate python_construction
  task4_completion
)
test "${#DEPLOY_GATES[@]}" = 10
for gate in "${DEPLOY_GATES[@]}"; do
  for suffix in command stdout stderr rc timestamp environment; do
    test -f "$DEPLOY_EVIDENCE/$gate.$suffix"
  done
  test "$(cat "$DEPLOY_EVIDENCE/$gate.rc")" = 0
done
set +e
inventory_rc_listing=$(find "$DEPLOY_EVIDENCE" -maxdepth 1 -type f \
  -name '*.rc' -print)
inventory_rc_find_rc=$?
set -e
test "$inventory_rc_find_rc" = 0
inventory_rc_files=()
if test -n "$inventory_rc_listing"; then
  mapfile -t inventory_rc_files <<< "$inventory_rc_listing"
fi
test "${#inventory_rc_files[@]}" = 10
```

Expected: archive/provenance, configuration, build, exact 8/8 CTest, plain
install, independent install/export relocation, AArch64 artifact, and
construction-only Python gates plus the final Task 4 completion audit are
independently reproducible from command, stdout, stderr, rc, timestamp, and
selected-environment records. The inventory is exactly ten, including
`task4_completion`; no password or credential is present in the bundle.

- [ ] **Step 4: Install the exact capture protocol and capture Phase A preflight**

Create the reusable capture helper remotely in the new evidence directory.
Each command gets distinct `.command`, `.stdout`, `.stderr`, and `.rc` files;
shell `noclobber` prevents a second capture from replacing evidence:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/SOURCE_MANIFEST
capture sdk_hash /usr/bin/sha256sum \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/lib/libLHandProLib.so
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
DEPLOY=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/deployment-db2da9f
PREFIX=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix
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

cat > "$EVIDENCE/phase_small_postflight_driver.sh" <<'SH'
#!/bin/bash
set -euo pipefail
EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
test "$EVIDENCE" = \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-small.attempt"
test -d "$ATTEMPT"
set +e
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post snapshot_runner \
  /bin/bash "$EVIDENCE/capture_snapshot.sh" phase-small post
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-small post process_display /usr/bin/cat \
  "$EVIDENCE/phase-small.post.processes.stdout"
/usr/bin/cat "$EVIDENCE/phase-small.post.process_display.stdout"
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
test "$FINAL_FAILURE" = 0

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

/usr/bin/cat "$ATTEMPT/stdout"
/usr/bin/cat "$EVIDENCE/phase-small.post.motion_output.stdout"
/usr/bin/cat "$EVIDENCE/phase-small.post.can_delta.stdout"
printf '%s\n' \
  'Confirm only after reviewing every joint, cleanup, and CAN result.' \
  'Enter exactly 小行程正常 to authorize later Phase B dispatch.' \
  > /dev/tty
cd "$EVIDENCE"
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
SH

cat > "$EVIDENCE/phase_full_postflight_driver.sh" <<'SH'
#!/bin/bash
set -euo pipefail
EVIDENCE=$(cd "$(dirname "$0")" && pwd -P)
test "$EVIDENCE" = \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
ATTEMPT="$EVIDENCE/phase-full.attempt"
test -d "$ATTEMPT"
set +e
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post snapshot_runner \
  /bin/bash "$EVIDENCE/capture_snapshot.sh" phase-full post
/bin/bash "$EVIDENCE/capture_command.sh" \
  phase-full post process_display /usr/bin/cat \
  "$EVIDENCE/phase-full.post.processes.stdout"
/usr/bin/cat "$EVIDENCE/phase-full.post.process_display.stdout"
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
test "$FINAL_FAILURE" = 0
/usr/bin/cat "$ATTEMPT/stdout"
/usr/bin/cat "$EVIDENCE/phase-full.post.motion_output.stdout"
/usr/bin/cat "$EVIDENCE/phase-full.post.can_delta.stdout"
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
chmod 0555 "$EVIDENCE/phase_small_postflight_driver.sh" \
  "$EVIDENCE/phase_full_postflight_driver.sh"
(cd "$EVIDENCE" && \
  sha256sum phase_small_postflight_driver.sh \
    phase_full_postflight_driver.sh > POSTFLIGHT_DRIVER_SHA256SUMS)
grep -Fx \
  'a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e  phase_small_postflight_driver.sh' \
  "$EVIDENCE/POSTFLIGHT_DRIVER_SHA256SUMS"
grep -Fx \
  'e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a  phase_full_postflight_driver.sh' \
  "$EVIDENCE/POSTFLIGHT_DRIVER_SHA256SUMS"
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
  "$EVIDENCE/phase_small_postflight_driver.sh" \
  "$EVIDENCE/phase_full_postflight_driver.sh" \
  "$EVIDENCE/POSTFLIGHT_DRIVER_SHA256SUMS" \
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
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/lib/python3.10/site-packages
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
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/lib/python3.10/site-packages \
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
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
gate=motion_setup_session
for suffix in command stdout stderr rc timestamp environment capture_mode; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
grep -Fx live-to-TTY-no-transcript \
  "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
LIVE_CAPTURE="$BOOTSTRAP_EVIDENCE/capture_live_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
SMALL_POSTFLIGHT_REMOTE=$(cat <<'REMOTE'
set -euo pipefail
evidence=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
cd "$evidence"
grep -Fx \
  'a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e  phase_small_postflight_driver.sh' \
  POSTFLIGHT_DRIVER_SHA256SUMS
sha256sum -c EVIDENCE_TOOLS_SHA256SUMS
sha256sum -c POSTFLIGHT_DRIVER_SHA256SUMS
printf '%s\n' \
  'a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e  phase_small_postflight_driver.sh' \
  | sha256sum -c -
exec /bin/bash \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612/phase_small_postflight_driver.sh
REMOTE
)
printf -v SMALL_POSTFLIGHT_COMMAND '/bin/bash -c %q' \
  "$SMALL_POSTFLIGHT_REMOTE"
"$LIVE_CAPTURE" phase_small_postflight_session \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 600s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -tt \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$SMALL_POSTFLIGHT_COMMAND"
```

The preceding rc check permits this predefined postflight label only after the
execution session closed normally and preserved any motion rc inside its remote
attempt record. A nonzero session rc is a connection/session failure: do not
open this label; only a reviewed new read-only recovery label may reconnect in
the same evidence root. `phase_small_postflight_session` is strictly read-only
with respect to motion.
This 600-second live session may complete only Phase A postflight, its
aggregate decision, and the exact operator acceptance record before exit. It
must not run Phase B preflight or motion. A timeout or other connection failure
leaves every artifact actually written, possibly without `.rc`, forbids Phase
A replay, and blocks Phase B.

The fixed driver, already installed and hash-bound before Phase A, now runs
the exact snapshot, process-display/review, CAN-delta, boot-continuity,
motion-output, saved-rc, and aggregate checks defined in its here-document.
It continues collecting obtainable failure evidence where specified, but
exits nonzero if the aggregate is not fully successful. The operator can
only answer its process-review prompt; no remote shell prompt is exposed.

Expected: the distinct Phase A postflight is complete, configuration is
unchanged, CAN protocol/error/drop deltas are zero, traffic counters are
nondecreasing, and no receiver/process remains. Any nonzero or incomplete
motion, snapshot, delta, review, or aggregate evidence fails Phase A and
blocks Phase B without replay.

- [ ] **Step 7: Record exact acceptance through the same fixed driver**

After automatic success, the driver displays the saved joint, cleanup,
motion-output, and CAN-delta evidence. The observer must review those results
and may enter only the exact response `小行程正常` at the driver prompt. The
driver atomically records and verifies the acceptance anchor, rechecks the
production and frozen evidence hashes, proves Phase B artifacts absent, and
then exits. It contains no Phase B preflight or motion command.

Expected: exact acceptance is bound to automatic Phase A success and the
Phase A boot ID. Driver exit `0` closes `phase_small_postflight_session`.
A signal, connection failure, partial tuple, or nonzero rc preserves existing
evidence, never authorizes Phase A replay, and keeps Phase B blocked.

### Task 6: Run Phase B Once After the Operator Gate

**Files:**
- Execute remotely:
  `motion-validation-bacf6612/staged_motion_validation.py`
- Append evidence under:
  `motion-validation-bacf6612/`

- [ ] **Step 1: Dispatch Phase B and open its execution-only session**

Run locally. First require the closed Phase A postflight session's complete
live tuple and rc `0`; that rc is valid only because the preceding remote block
reverified the automatic-success and exact acceptance anchors as its final
work. Reverify the local Phase A dispatch binding, immutable inputs, and root
boot evidence. Then atomically create and sync the separate full-dispatch
marker before opening the one session allowed to preflight and execute Phase
B:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
DEPLOY_STAGE=/tmp/roboparty-dexhand-deploy-db2da9f-r9
MOTION_STAGE=/tmp/roboparty-dexhand-motion-1a7c820
FULL_DISPATCH="$BOOTSTRAP_EVIDENCE/PHASE_FULL_DISPATCHED"
gate=phase_small_postflight_session
for suffix in command stdout stderr rc timestamp environment capture_mode; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
grep -Fx live-to-TTY-no-transcript \
  "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
PRODUCTION_COMMIT=db2da9fb90f407bdd5e3bbd3de691e775d27abd3
PRODUCTION_TREE=aed385f28d3010fc167914872550f4bbb0a51057
MOTION_SCRIPT_SHA256=bacf66129a784e560e6b9ba2ba22e112a404aa0bb046bbe708b2df92a82522f9
FROZEN_TEST_SHA256=df774043b20156d541f2cd7bbf6611d96c8922ffe7b66ab0f4b7591dd4be45ce
grep -Fx "source_commit=$PRODUCTION_COMMIT" "$DEPLOY_STAGE/SOURCE_MANIFEST"
grep -Fx "source_tree=$PRODUCTION_TREE" "$DEPLOY_STAGE/SOURCE_MANIFEST"
(cd "$MOTION_STAGE" && sha256sum -c SHA256SUMS)
(cd "$MOTION_STAGE" && sha256sum -c MOTION_SCRIPT_SHA256SUM)
grep -Fx "$MOTION_SCRIPT_SHA256  staged_motion_validation.py" \
  "$MOTION_STAGE/MOTION_SCRIPT_SHA256SUM"
grep -Fx "$FROZEN_TEST_SHA256  test_staged_motion_validation.py" \
  "$MOTION_STAGE/SHA256SUMS"
BOOT_EVIDENCE="$BOOTSTRAP_EVIDENCE/remote_fresh_root.stdout"
test "$(cat "$BOOTSTRAP_EVIDENCE/remote_fresh_root.rc")" = 0
test "$(wc -l < "$BOOT_EVIDENCE")" = 1
REMOTE_BOOT_LINE=$(cat "$BOOT_EVIDENCE")
[[ "$REMOTE_BOOT_LINE" =~ \
  ^boot_id=[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12}$ ]]
read -r BOOT_EVIDENCE_SHA256 BOOT_EVIDENCE_PATH < <(
  /usr/bin/sha256sum "$BOOT_EVIDENCE"
)
test "$BOOT_EVIDENCE_PATH" = "$BOOT_EVIDENCE"
SMALL_DISPATCH="$BOOTSTRAP_EVIDENCE/PHASE_SMALL_DISPATCHED"
test -d "$SMALL_DISPATCH"
test "$(wc -l < "$SMALL_DISPATCH/binding")" = 10
grep -Fx "production_commit=$PRODUCTION_COMMIT" "$SMALL_DISPATCH/binding"
grep -Fx "production_tree=$PRODUCTION_TREE" "$SMALL_DISPATCH/binding"
grep -Fx "motion_script_sha256=$MOTION_SCRIPT_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx "frozen_test_sha256=$FROZEN_TEST_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx \
  small_postflight_driver_sha256=a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e \
  "$SMALL_DISPATCH/binding"
grep -Fx \
  full_postflight_driver_sha256=e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a \
  "$SMALL_DISPATCH/binding"
grep -Fx connection_label=motion_setup_session "$SMALL_DISPATCH/binding"
grep -Fx boot_evidence_label=remote_fresh_root.stdout \
  "$SMALL_DISPATCH/binding"
grep -Fx "boot_evidence_sha256=$BOOT_EVIDENCE_SHA256" \
  "$SMALL_DISPATCH/binding"
grep -Fx "$REMOTE_BOOT_LINE" "$SMALL_DISPATCH/binding"
read -r PHASE_A_GATE_RC_SHA256 PHASE_A_GATE_RC_PATH < <(
  /usr/bin/sha256sum "$BOOTSTRAP_EVIDENCE/$gate.rc"
)
test "$PHASE_A_GATE_RC_PATH" = "$BOOTSTRAP_EVIDENCE/$gate.rc"
test ! -e "$FULL_DISPATCH"
DISPATCH_TMP=$(mktemp -d \
  "$BOOTSTRAP_EVIDENCE/.phase-full-dispatched.XXXXXX")
set -o noclobber
printf '%s\n' \
  "production_commit=$PRODUCTION_COMMIT" \
  "production_tree=$PRODUCTION_TREE" \
  "motion_script_sha256=$MOTION_SCRIPT_SHA256" \
  "frozen_test_sha256=$FROZEN_TEST_SHA256" \
  small_postflight_driver_sha256=a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e \
  full_postflight_driver_sha256=e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a \
  connection_label=phase_full_execution_session \
  phase_a_authorization_label=phase_small_postflight_session \
  "phase_a_authorization_rc_sha256=$PHASE_A_GATE_RC_SHA256" \
  phase_a_remote_anchors=PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256+PHASE_SMALL_OPERATOR_ACCEPTANCE_ANCHOR.sha256 \
  boot_evidence_label=remote_fresh_root.stdout \
  "boot_evidence_sha256=$BOOT_EVIDENCE_SHA256" \
  "$REMOTE_BOOT_LINE" \
  > "$DISPATCH_TMP/binding"
/usr/bin/sync -f "$DISPATCH_TMP/binding"
mkdir "$FULL_DISPATCH"
/usr/bin/mv -T "$DISPATCH_TMP/binding" "$FULL_DISPATCH/binding"
rmdir "$DISPATCH_TMP"
/usr/bin/sync -f "$FULL_DISPATCH/binding"
/usr/bin/sync -f "$BOOTSTRAP_EVIDENCE"
test "$(wc -l < "$FULL_DISPATCH/binding")" = 13
grep -Fx "production_commit=$PRODUCTION_COMMIT" "$FULL_DISPATCH/binding"
grep -Fx "production_tree=$PRODUCTION_TREE" "$FULL_DISPATCH/binding"
grep -Fx "motion_script_sha256=$MOTION_SCRIPT_SHA256" \
  "$FULL_DISPATCH/binding"
grep -Fx "frozen_test_sha256=$FROZEN_TEST_SHA256" \
  "$FULL_DISPATCH/binding"
grep -Fx \
  small_postflight_driver_sha256=a213ce81e8b90b123d51471876ce6b74832734d98a05c659d1da297ce6db9c4e \
  "$FULL_DISPATCH/binding"
grep -Fx \
  full_postflight_driver_sha256=e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a \
  "$FULL_DISPATCH/binding"
grep -Fx connection_label=phase_full_execution_session \
  "$FULL_DISPATCH/binding"
grep -Fx phase_a_authorization_label=phase_small_postflight_session \
  "$FULL_DISPATCH/binding"
grep -Fx "phase_a_authorization_rc_sha256=$PHASE_A_GATE_RC_SHA256" \
  "$FULL_DISPATCH/binding"
grep -Fx \
  phase_a_remote_anchors=PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256+PHASE_SMALL_OPERATOR_ACCEPTANCE_ANCHOR.sha256 \
  "$FULL_DISPATCH/binding"
grep -Fx boot_evidence_label=remote_fresh_root.stdout \
  "$FULL_DISPATCH/binding"
grep -Fx "boot_evidence_sha256=$BOOT_EVIDENCE_SHA256" \
  "$FULL_DISPATCH/binding"
grep -Fx "$REMOTE_BOOT_LINE" "$FULL_DISPATCH/binding"
LIVE_CAPTURE="$BOOTSTRAP_EVIDENCE/capture_live_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
"$LIVE_CAPTURE" phase_full_execution_session \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 1800s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1
```

The full-dispatch `mkdir` is the conservative no-replay boundary for Phase B.
Because the Phase A dispatch marker already exists, any local prerequisite
failure here preserves the physical evidence root and keeps Phase B blocked;
it does not revert to the pre-Phase-A suffix rule. If the full marker is
created, every execution-session failure preserves both local and remote
evidence and forbids Phase B replay. The next SSH session may perform only
Phase B authorization, preflight, and its unique motion attempt; it can never
run Phase A.

At the start of `phase_full_execution_session`, rerun the exact remote
authorization checks that made the prior read-only session rc `0`:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
cd "$EVIDENCE"
sha256sum -c PHASE_SMALL_AUTOMATIC_SUCCESS_ANCHOR.sha256
sha256sum -c PHASE_SMALL_AUTOMATIC_SUCCESS.sha256
sha256sum -c PHASE_SMALL_OPERATOR_ACCEPTANCE_ANCHOR.sha256
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

Expected: only a Phase A postflight tuple with rc `0`, the exact durable small
binding, and fully accepted Phase A evidence permit Phase B dispatch. The full
marker is synced and verified before OpenSSH starts. Any failure after dispatch
blocks replay and still requires a reviewed, uniquely labelled read-only
recovery; it does not authorize another execution session.

- [ ] **Step 2: Capture the complete Phase B preflight**

Run the distinct Phase B preflight and record the same explicit
`clean`/`found`/`unknown` human process decision:

```bash
set -euo pipefail
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
EVIDENCE=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
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
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/lib/python3.10/site-packages
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
  PYTHONPATH=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix/lib/python3.10/site-packages \
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

Close `phase_full_execution_session` normally and continue to Step 4 for every
saved motion rc. The marker's atomic `mkdir` prevents every retry, and
`noclobber` protects its contents. A connection/session rc failure instead
blocks the predefined postflight label and requires a reviewed, uniquely
labelled read-only recovery.

`--preserve-status` is mandatory here for the same reason as Phase A: a
cleanup-aware `130` must not be rewritten to `124`.

Expected after Step 4 enforces the saved rc: rc `0`, six cycle motion commands
plus one final-open command, one `phase_complete`, no phase/cleanup errors,
and four cleanup completions.

- [ ] **Step 4: Capture Phase B postflight, calculate deltas, then decide**

Reconnect regardless of the saved rc:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
gate=phase_full_execution_session
for suffix in command stdout stderr rc timestamp environment capture_mode; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"
done
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
grep -Fx live-to-TTY-no-transcript \
  "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
LIVE_CAPTURE="$BOOTSTRAP_EVIDENCE/capture_live_gate.sh"
TTY_GATE="$BOOTSTRAP_EVIDENCE/require_tty_exec.sh"
FULL_POSTFLIGHT_REMOTE=$(cat <<'REMOTE'
set -euo pipefail
evidence=/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612
cd "$evidence"
grep -Fx \
  'e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a  phase_full_postflight_driver.sh' \
  POSTFLIGHT_DRIVER_SHA256SUMS
sha256sum -c EVIDENCE_TOOLS_SHA256SUMS
sha256sum -c POSTFLIGHT_DRIVER_SHA256SUMS
printf '%s\n' \
  'e79ea517ced69fb73fcc6b395dd0f0bbc902afaf6aa0634385af3299e5e5de8a  phase_full_postflight_driver.sh' \
  | sha256sum -c -
exec /bin/bash \
  /home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612/phase_full_postflight_driver.sh
REMOTE
)
printf -v FULL_POSTFLIGHT_COMMAND '/bin/bash -c %q' \
  "$FULL_POSTFLIGHT_REMOTE"
"$LIVE_CAPTURE" phase_full_postflight_session \
  /usr/bin/timeout \
  --foreground --preserve-status --signal=TERM --kill-after=5s 600s \
  "$TTY_GATE" \
  /usr/bin/env -u SSH_AUTH_SOCK SSH_ASKPASS_REQUIRE=never \
  /usr/bin/ssh \
  -F /dev/null \
  -tt \
  -o IdentityAgent=none \
  -o PreferredAuthentications=password \
  -o PubkeyAuthentication=no \
  -o NumberOfPasswordPrompts=1 \
  -o ConnectTimeout=10 \
  -o ConnectionAttempts=1 \
  -o ServerAliveInterval=5 \
  -o ServerAliveCountMax=2 \
  -o ControlMaster=no \
  -o ControlPath=none \
  -o ControlPersist=no \
  -o ProxyCommand=none \
  -o ProxyJump=none \
  -o PermitLocalCommand=no \
  -o ClearAllForwardings=yes \
  -o StrictHostKeyChecking=yes \
  -o UserKnownHostsFile=/home/sjh/.ssh/known_hosts \
  orangepi@192.168.13.1 "$FULL_POSTFLIGHT_COMMAND"
```

The preceding rc check permits this predefined postflight only after the Phase
B execution session closed normally and saved its independent motion rc. A
nonzero session rc blocks this label and requires reviewed read-only recovery.
`phase_full_postflight_session` is strictly read-only with respect to motion.
Failure preserves its live tuple and requires a reviewed, uniquely labelled
read-only recovery amendment in the same evidence root; it never authorizes
Phase B replay.

The fixed full driver, already installed and hash-bound before Phase A,
runs the exact Phase B snapshot, process-display/review, CAN-delta,
boot-continuity, motion-output, saved-rc, and aggregate logic from its
here-document. It displays only the documented process-review prompt and
final evidence; it contains no preflight or motion invocation.

Expected: the distinct Phase B postflight completes before the driver
accepts the saved motion rc. Configuration remains unchanged, CAN
protocol/error/drop deltas are zero, traffic counters are nondecreasing, and
no receiver/process remains. Any nonzero or incomplete motion, snapshot,
delta, review, or aggregate evidence fails Phase B after preserving every
artifact actually written; no replay is allowed.

- [ ] **Step 5: Independently review physical evidence**

Dispatch one reviewer for script/output/spec conformance and one reviewer for
CAN/cleanup/hardware-safety evidence. Neither reviewer may rerun either phase.

Expected: reviewers distinguish API command returns from device
acknowledgement and require explicit operator observation before marking the
physical gate complete.

### Task 7: Final Release-Gate Report

**Files:**
- Inspect all evidence under:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence/motion-validation-bacf6612/`
- Inspect repository state:
  `/home/sjh/leisai_hand/roboparty_dexhand`

- [ ] **Step 1: Verify the exact connection-label inventory and tuples**

Run locally after the final live session has closed:

```bash
set -euo pipefail
BOOTSTRAP_EVIDENCE=/tmp/roboparty-dexhand-deploy-db2da9f-r9/bootstrap-evidence
(cd "$BOOTSTRAP_EVIDENCE" && sha256sum -c CAPTURE_GATE_SHA256SUM)
NORMAL_CONNECTION_GATES=(
  remote_fresh_root source_transfer remote_gate_bootstrap
  motion_artifact_transfer
)
LIVE_CONNECTION_GATES=(
  remote_operator_session motion_setup_session
  phase_small_postflight_session phase_full_execution_session
  phase_full_postflight_session
)
test "$(( ${#NORMAL_CONNECTION_GATES[@]} + ${#LIVE_CONNECTION_GATES[@]} ))" = 9
ALL_CONNECTION_GATES=(
  "${NORMAL_CONNECTION_GATES[@]}" "${LIVE_CONNECTION_GATES[@]}"
)
PARTIAL_FAILURE=0
shopt -s nullglob
for gate in "${ALL_CONNECTION_GATES[@]}"; do
  ARTIFACTS=("$BOOTSTRAP_EVIDENCE/$gate".*)
  if ! test "${#ARTIFACTS[@]}" -gt 0; then
    printf 'successful release missing label: %s\n' "$gate" >&2
    PARTIAL_FAILURE=1
    continue
  fi
  printf 'observed label artifacts: %s\n' "$gate"
  printf '  %s\n' "${ARTIFACTS[@]}"
  for suffix in command stdout stderr rc timestamp environment; do
    if ! test -f "$BOOTSTRAP_EVIDENCE/$gate.$suffix"; then
      printf 'consumed partial tuple: %s missing %s\n' \
        "$gate" "$suffix" >&2
      PARTIAL_FAILURE=1
    fi
  done
  if ! test -f "$BOOTSTRAP_EVIDENCE/$gate.rc"; then
    printf 'missing rc is not retry authority: %s\n' "$gate" >&2
  fi
  case " ${LIVE_CONNECTION_GATES[*]} " in
    *" $gate "*)
      if ! test -f "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"; then
        printf 'consumed partial tuple: %s missing capture_mode\n' \
          "$gate" >&2
        PARTIAL_FAILURE=1
      fi
      ;;
  esac
done
if ! test "$PARTIAL_FAILURE" = 0; then
  printf '%s\n' \
    'Failure audit stops here; do not connect merely to complete the inventory.' \
    >&2
  exit 1
fi

INVENTORY_TMP=$(mktemp -d /tmp/motion-connection-inventory.XXXXXX)
EXPECTED_CONNECTION_LABELS="$INVENTORY_TMP/expected-connections"
ACTUAL_CONNECTION_LABELS="$INVENTORY_TMP/actual-connections"
EXPECTED_COMMAND_LABELS="$INVENTORY_TMP/expected-commands"
ACTUAL_COMMAND_LABELS="$INVENTORY_TMP/actual-commands"
cleanup_inventory() {
  rm -f "$EXPECTED_CONNECTION_LABELS" "$ACTUAL_CONNECTION_LABELS" \
    "$EXPECTED_COMMAND_LABELS" "$ACTUAL_COMMAND_LABELS"
  rmdir "$INVENTORY_TMP"
}
trap cleanup_inventory EXIT
printf '%s\n' "${ALL_CONNECTION_GATES[@]}" | LC_ALL=C sort -u \
  > "$EXPECTED_CONNECTION_LABELS"
for command_file in "$BOOTSTRAP_EVIDENCE"/*.command; do
  if grep -Fq /usr/bin/ssh "$command_file"; then
    grep -Eq '(^|[[:space:]])/usr/bin/ssh([[:space:]]|$)' "$command_file"
    basename "$command_file" .command
  fi
done | LC_ALL=C sort -u > "$ACTUAL_CONNECTION_LABELS"
cmp "$EXPECTED_CONNECTION_LABELS" "$ACTUAL_CONNECTION_LABELS"
printf '%s\n' source_archive "${ALL_CONNECTION_GATES[@]}" \
  | LC_ALL=C sort -u > "$EXPECTED_COMMAND_LABELS"
for command_file in "$BOOTSTRAP_EVIDENCE"/*.command; do
  basename "$command_file" .command
done | LC_ALL=C sort -u > "$ACTUAL_COMMAND_LABELS"
cmp "$EXPECTED_COMMAND_LABELS" "$ACTUAL_COMMAND_LABELS"

for gate in "${NORMAL_CONNECTION_GATES[@]}"; do
  test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
done
for gate in "${LIVE_CONNECTION_GATES[@]}"; do
  test -f "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
  test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stdout"
  test ! -s "$BOOTSTRAP_EVIDENCE/$gate.stderr"
  grep -Fx live-to-TTY-no-transcript "$BOOTSTRAP_EVIDENCE/$gate.capture_mode"
  test "$(cat "$BOOTSTRAP_EVIDENCE/$gate.rc")" = 0
done
```

Expected: this is a successful release inventory, not a recovery instruction.
It first reports any absent or consumed partial tuple and fails without opening
another connection; a missing `.rc` never makes a label reusable. Only when
all tuples are complete does it prove the exact set of nine SSH command stems,
reject every extra/missing SSH label, and separately prove that the only local
command stems are `source_archive` plus those nine. Exactly four normal and
five live tuples have rc `0`; live stdout/stderr files are intentional
zero-byte non-transcript sentinels.

- [ ] **Step 2: Verify the repository remained unchanged by execution**

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

- [ ] **Step 3: Report release gates separately**

Report:

- real vcan two-socket: pass;
- Orange Pi native build/install/tests: pass;
- staged physical motion: the actual result, exact completed phase, and
  evidence limitations;
- callback quiescence: still blocked until written vendor confirmation; and
- SDK redistribution: still blocked until written license/authorization and
  GPL compatibility review.

Do not collapse the two external vendor gates into the physical test result.

- [ ] **Step 4: Preserve evidence and do not rerun**

Leave the remote evidence directory intact. Remove no logs and do not rerun a
successful or failed phase merely to improve logging. Any later retest must
use a new evidence directory and new operator authorization.
