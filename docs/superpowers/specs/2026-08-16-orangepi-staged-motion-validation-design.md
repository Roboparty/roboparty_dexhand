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

## R5-R6 Deployment Incidents and R9 Boundary

The first fixed local deployment stage (R1)
`/tmp/roboparty-dexhand-deploy-db2da9f` is a historical failed attempt and is
consumed. Its only `remote_fresh_root` capture ended with `rc=143`. Its stdout
and stderr were both zero bytes; the command and selected-environment files
existed and contained no credential value. The controller had inherited
`SSH_AUTH_SOCK=/run/user/1000/keyring/ssh`; that desktop agent caused
`ssh-add -l` itself to hang. After the SSH TCP connection, the socket was left
in `CLOSE-WAIT`. Those facts prove neither that the remote command ran nor that
it did not run. That historical local stage and historical remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f` must never be reused or
deleted. Because remote execution is uncertain, the old remote root is treated
as consumed even if a later observation suggests it is absent.

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
consumed R3 Task 4 attempt. All nine remote deployment gates—archive
provenance, configuration, configuration contract, build, exactly 8/8 CTests,
plain install, install/export relocation, AArch64 artifact validation, and
`python_construction`—produced complete command/stdout/stderr/rc/timestamp/
environment tuples with rc `0`. A subsequent read-only audit rechecked those
tuples, the runtime manifest and anchor, the exact CTest result, the installed
objects and runpaths, the fixed SDK hash, the exact construction stdout and
empty stderr, and the production source manifest. It reached its final
explicit PASS `printf` and returned to the remote prompt.

The enclosing `remote_operator_session` was a generic interactive SSH login
without a fixed remote shell command. After the audit returned, the operator
sent a plain `exit`, not `exit 0`; visible output showed logout and connection
closure, but the complete local live-capture tuple recorded rc `1`. Existing
evidence cannot determine why: the exact cause of that final rc remains
unknown. In particular, there is no evidence that a login profile or
`PROMPT_COMMAND` caused it. No CAN access, SDK initialization, `init_hand`, or
motion occurred. Both R3 paths are consumed and must never be reused or
deleted.

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

## R6 Phase A Preflight Incident and R9 Boundary

The sixth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r6` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r6` completed Task 4 and the
motion-artifact transfer. The remote deployment reported all ten gates at rc
`0`, the AArch64 runtime and Python construction gates passed, and the four
transferred evidence files matched their hashes. No SDK initialization or
motion had occurred at that point.

The controller then created `PHASE_SMALL_DISPATCHED` and opened the unique
`motion_setup_session`. That complete live-TTY tuple returned rc `1` with zero
bytes of captured stdout and stderr. The visible remote tools had passed, but
`capture_snapshot.sh phase-small pre` failed closed because the board snapshot
had no `can0`: `ip link` showed `can_top`, `can_hipnuc`, and `can_bottom`, while
`ip link show can0`, CAN counters, and the receiver gate failed. No
`phase-small.attempt` marker was created, no SDK initialization or
`init_hand` ran, and no motion command was issued. The R6 local and remote
namespaces are permanently consumed; their evidence must not be replayed,
filled in, queried for retry authority, or deleted.

A later read-only board check showed a USB CAN-FD `can0` in `UP`,
`ERROR-ACTIVE` state at 1 Mbit/s nominal and 5 Mbit/s data rate, but that later
observation is not the failed session's captured preflight and cannot repair
R6. The only executable suffix after this incident is R9, with fresh paths and
fresh evidence captured from the current power/network state.

## R7 Local Stage Incident and R9 Boundary

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

## R8 Phase A Controller Incident and R9 Boundary

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

## R9 Phase A Preflight Incident and R10 Boundary

The ninth fixed local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r9` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r9` completed the non-motion
deployment gates and motion-artifact transfer. The controller then created the
durable `PHASE_SMALL_DISPATCHED` marker and opened the unique
`motion_setup_session`. The remote Phase A preflight returned a nonzero shell
status before the required process-review prompt was reached. The controller
stopped the session without sending `process_review=clean` and without sending
the Phase A motion block.

The local live tuple is permanently partial: `.command`, `.timestamp`,
`.environment`, `.capture_mode`, and zero-byte stdout/stderr exist, but `.rc`
is absent. The remote preflight may have written partial read-only snapshot
evidence, but the local session has no transcript that can establish which
snapshot command failed. No `init_hand`, SDK initialization, CAN command, or
motion command was sent by the controller. R9's local marker, stage, remote
root, motion evidence, and any partial preflight evidence are consumed and
must not be replayed, filled in, queried for retry authority, or deleted.

The only permitted continuation is a reviewed, uniquely labelled R10
read-only recovery against the existing R9 evidence root. R10 may inspect
already-written preflight evidence and record the failure cause, but it must
not create another Phase A attempt, call `init_hand`, issue CAN commands, or
authorize Phase B. Any further physical validation requires a separate future
operator decision and a new safety review; it is not a retry of R9.

The R10 recovery label is `r10_readonly_recovery`. Its remote command only
checks the existing R9 evidence directory, lists the already-created
`phase-small.pre.*` artifacts, reports their saved rc/complete status and
nonempty stderr, and reports whether `phase-small.attempt` exists. It does not
run `ip`, `candump`, the SDK, Python, or any motion helper. A complete rc `0`
for this read-only report records diagnosis only; it cannot make R9 reusable.

R10 `r10_readonly_recovery` returned rc `0` and reported no
`phase-small.attempt`. `r10_readonly_inventory` then confirmed that the R9
root and motion evidence leaf exist, but no `phase-small.pre.*` artifact was
written. `r10_readonly_snapshot_script` confirmed that the installed snapshot
script is mode `0555`, passes `bash -n`, and resolves `ip` to
`/usr/sbin/ip`. Because the live controller tuple has no transcript and the
remote preflight stopped before its first captured snapshot, the exact failing
command remains unknown. These read-only results prove no motion attempt; they
do not repair or authorize R9.

## R11 Fresh Physical-Validation Boundary

R9 is permanently consumed after its Phase A dispatch marker and preflight
controller failure. A future physical attempt, if separately authorized, must
use fresh local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r11` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r11`. It must rebuild and
reverify all non-motion gates; no R9 build, prefix, evidence, or marker may be
treated as motion authority.

The R11 controller is a transferred file, not a heredoc pasted into an
interactive PTY. Its reviewed local SHA-256 is
`95a03618458573a9de0aa80a1f879f7fef8ff2904d7d409b2aec68601b93d070`.
The remote motion session executes only the hash-verified file with a short
command, and the process-review response is read from `/dev/tty`. This fixes
the R9 controller transport failure mode without changing the production
driver or motion harness. R11 still has one motion attempt, explicit process
review, the existing stop/disable/no-home/deinit cleanup, and a fresh
postflight; any R11 failure consumes R11 and blocks another replay.

R11 was consumed before any remote connection. Its `remote_fresh_root` unit
was launched without a controlling TTY, so the checksum-bound TTY gate
returned rc `1` while checking `/dev/tty`. No SSH process, password prompt,
remote root, CAN command, SDK call, or motion action occurred. The R11 local
stage and partial tuple are preserved and cannot be reused. The next fresh
boundary is R12, and all SSH-containing units must be launched from a PTY.

R13 completed the fresh deployment and transferred the corrected controller,
but its one `motion_setup_session` ended before the process-review prompt.
The local live tuple is partial with zero-byte stdout/stderr and no `.rc`; the
dispatch marker exists. No review response, `phase-small.attempt`, SDK init,
CAN command, or motion command was sent. R13 is consumed. R14 may perform
only a read-only inventory of the R13 evidence leaf to identify the controller
failure; it may not replay the session or authorize motion.

R14 read-only inventory completed with no new remote action. The R13 evidence
leaf contains both generated postflight drivers and their checksum file, but
does not contain `EVIDENCE_TOOLS_SHA256SUMS`. Exact local controller extraction
shows the fixed-driver gate expected stale hashes (`a213ce...` and `e79ea...`)
while the generated R13 drivers hash as
`a0aeea2c0107a1cccdcb876cbe2f75a04cc25423824d01ca78774c389459ee47` and
`63ec9a5b7e4df1ea0329fa71ce62a00bf330ff2894d56526fa764b37fc0f0cb8`.
The controller consequently exited before `EVIDENCE_TOOLS_SHA256SUMS` and
before process review. R14 is consumed; the next fresh physical boundary is
R15 with path-substituted drivers and recomputed hashes.

R15 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r15`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r15`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r15`. The corrected controller
SHA-256 is
`64d5575b061b67df9e0cab3083caf9e3baf14faf3e918e28e06d209eeda69963`; its
generated postflight driver hashes are
`357c25b5d8b708e9a8c78e40be028e78aa1a370b824ffda0aee810233526f1b8` and
`9ac995803ccf49c33dc8b320b0c16333ac64953b8eabb8385b83171cd279e4be`.

R15 was consumed at `source_transfer` after the remote stream, exact seven
members, manifest, and extraction passed. The final `rmdir` returned rc `1`
because the recovery script had created two payload marker files and then
required the incoming directory to be empty. No bootstrap, operator session,
SDK, CAN, or motion command ran. R15 is permanently consumed; R16 removes the
unnecessary marker files and keeps the empty-directory postcondition.

R16 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r16`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r16`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r16`. Its controller SHA-256 is
`653f9c3efb2efdb82c05c468adebfe0c10eef9da5f58f556272ddbb8d4bdf32f`; the
generated driver hashes are
`aac66424b35cdba9efc763ddbf29803481d103871106304613faeb08e623bb76` and
`78bdaae3ed226320b67d9353ac45944ffcf63460d282f99ab385b0deb51f5e6b`.

R16 passed the fresh root, transfer, helper bootstrap, provenance, configure,
build, CTest 8/8, plain install, and install/export gates. Its operator shell
failed at `artifact_gate` because a manually typed outer single-quoted
`bash -c` contained an inner `trap '...'`, causing premature quote closure and
an unbound positional argument. No SDK, CAN, or motion action ran. R16 is
consumed; R17 uses a quoted here-document for the Task 4 script in a fresh
operator session.

R17 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r17`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r17`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r17`. Its controller SHA-256 is
`ea54ee0ce69e3a9f4ec284a2ce7b95570966278f0119356f84c1ffb8b3ff1df4`; the
generated driver hashes are
`fec5030cd5991f7da0e5cd6e23878359aa79993d4e77c4c627e48f2aeb14767b` and
`fef0dfa150e3a31f26e4ed4804e740ef757a7fca37c602924d0579b04821063a`.

R17 passed the fresh root, transfer, helper bootstrap, and Task 4 operator
gates. Its unique `motion_artifact_transfer` returned rc `1` with no output
before creating the motion evidence leaf. No SDK, CAN, or motion action ran.
R17 is consumed; R18 is limited to a read-only inventory of the R17 deployment
root and cannot retry transfer or authorize motion.

R18 read-only inventory confirmed the R17 deployment root has all 10 Task 4
rc files at `0`, while the motion evidence leaf and attempt are absent. The
transfer gate rejected R17's completion output because it lacked the exact
`aarch64=3`, runpath, and SDK-hash fields required by the contract. R18 is
consumed; R19 must emit the complete completion line in a fresh root.

R19 passed fresh Task 4 and exact completion gates, but motion transfer failed
because `PHASE_A_CONTROLLER_SHA256SUM` contained an absolute local path. The
remote extracted bundle therefore failed its relative-member checksum gate.
No attempt, SDK, CAN, or motion action ran. R19 is consumed; R20 must create
that checksum while its working directory is the motion stage.

R20 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r20`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r20`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r20`. Its controller SHA-256 is
`0f959f0ec14a6f8b1e6e0f8adf2522a09dab2e2ea86805c28dbfde53e852646f`; the
generated driver hashes are
`983abe9b2a197ea1b9a17cbb84e04840605e7ae6f366fc0dfa7b10aee7e988b9` and
`5a9638e4857f7f87e386deec80e9562041d4a04cc03a7b2913670235970ba08f`.

R20 passed Task 4 and motion artifact transfer, but its setup session exited
before process review during the phase-small pre-snapshot. No attempt, SDK,
CAN, or motion action ran. R20 is consumed; R21 is limited to read-only
inventory of the motion leaf and snapshot failure.

R21 confirmed the board can0 is UP/LOWER_UP, CAN-FD, ERROR-ACTIVE at 1M/5M,
with zero protocol and RX/TX errors. Its `gs_usb` details omit the optional
`berr-counter` line, which made the frozen pre-snapshot parser fail before
process review. R22 records the field's availability explicitly and gates all
available error counters at zero; it does not invent a nonzero-capability
reading.

R22 uses a fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r22`, a
fresh remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r22`, and
motion stage `/tmp/roboparty-dexhand-motion-1a7c820-r22`. The controller
SHA-256 is
`b3f0d5d35c88671c0b4a028e11d53943dfdde5b997555f534d155edf45c11dc0`.
Its generated postflight drivers are pinned to
`c724fdcb682d8551b831d7bd7579eeb0c95d1e3d938ef700facdcfa38bcfbdff` and
`ec8b187e621d6e017a96f4c37a32c4ea2f551f2eb1230a4bf1a95bb86ff56275`.
The optional counter is represented by an explicit availability boolean;
only counters actually reported by the kernel are asserted, and all reported
error counters must remain zero. R22 is fresh and cannot reuse R21 evidence.

R22 passed fresh deployment and all Task 4 gates, but its sole motion artifact
transfer failed closed because `PHASE_A_CONTROLLER_SHA256SUM` named the
controller with an absolute local `/tmp/roboparty-dexhand-motion-1a7c820-r22/`
path. The remote checksum gate therefore returned `rc=1` before installing
the controller or creating a phase attempt. No SDK, CAN, or motion action ran;
R22 is consumed. R23 must create the checksum while its working directory is
the fresh motion stage, producing the relative member name
`phase_a_controller.sh`, and must use a new deployment root and evidence tree.

R23 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r23`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r23`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r23`; its controller and driver
hashes remain those pinned by R22.

R23 completed Task 4 and motion artifact transfer, but a local pre-launch
audit found that its controller had an early `exit 0` immediately before the
motion command. It was not launched; no SDK, CAN, or motion action ran. R24
uses a corrected controller with that early exit and its misleading
pre-motion completion line removed, and a new relative checksum binding.
R24 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r24`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r24`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r24`; controller SHA-256 is
`0fbed7642a7f72c11cf7365265f8d59bf147d47d089e7c33cc4baea797aac5ce`.

R24 passed Task 4 and motion artifact transfer, but its sole setup session
returned `rc=1` before process review, with no captured output because the
session is live-to-TTY. No SDK initialization, CAN command, or motion action
is inferred. R24 is consumed; R25 is read-only inventory of the R24 motion
leaf and must not replay setup or authorize motion.

R25 found the R24 motion leaf had no pre-snapshot files or attempt directory;
the board's can0 and receiver state were healthy. The failure was reproduced
locally before the tool-manifest write: path substitution changed the
postflight driver bytes while the controller retained stale R22 hashes. The
two self-tests and remote artifact hashes passed. R26 recomputes and binds
the path-specific driver hashes before its fresh transfer.

R26 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r26`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r26`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r26`; controller SHA-256 is
`d96b60a4a1d5664b39e534cd54def3f7f3502b18b4984d0b6f62733dcd9c4d8a`, with
driver hashes `d77da3fd67797fcfc8d81d02ce8ae89efca1ce3e87bb5b7a697d78d1ab125099`
and `4ead1ee72aba53e595ab381a0958d19f4a3d5478b077595008d5eb7e56dd4793`.

R19 uses fresh local stage `/tmp/roboparty-dexhand-deploy-db2da9f-r19`, fresh
remote root `/home/orangepi/roboparty_dexhand_motion_db2da9f_r19`, and motion
stage `/tmp/roboparty-dexhand-motion-1a7c820-r19`. Its controller SHA-256 is
`57aa2feefa32f512971692e774c74ef3f639a82320b34421b072986494c476fa`; the
generated driver hashes are
`76300adfa562993523ac2573e01f362f1c594f3f5df60f09187243691c64d0ae` and
`e4d1956d6e3d66cb5d0d8640296134a478a4c26cee68d147f2a534293f458201`.

R12 then completed the fresh Task 4 build/export gates, but its unique
`motion_artifact_transfer` failed closed before any motion-capable session.
The transferred controller manifest contained an absolute local `/tmp` path;
the remote `sha256sum -c` could not resolve it and returned nonzero. No
`phase-small.attempt`, SDK initialization, CAN command, or motion action was
created or sent. R12's local stage, remote root, and partial evidence leaf are
consumed and cannot be retried. The corrected manifest must contain only the
relative member name, be checked locally in the extracted bundle, and use a
new R13 boundary.

R13 uses the fresh local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r13` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r13`. Its path-bound
controller hash is
`693ae85c118c108b5f1416897f74eabee207402f4bba77e84022d457d44b263c`.

The only executable recovery suffix in this revision is r10: local stage
`/tmp/roboparty-dexhand-deploy-db2da9f-r9` and remote root
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r9`. The R9 operator session
uses the fixed live-TTY remote command `/bin/bash --noprofile --norc`, captures
a final `task4_completion` gate after all nine earlier remote gates, and must
end with explicit `exit 0`. A parameterless `exit` is forbidden.

Before the local durable dispatch marker is created for Phase A—the first
motion-capable session—any connection failure, timeout, or agent anomaly
consumes the capture label and the entire r9 suffix. The `.command` file is
created first under shell `noclobber`; the existence of any `<stem>.*` file
permanently consumes that label. INT, TERM, HUP, controller failure, or a
capture-process crash may leave a partial tuple, including a missing `.rc`.
That partial evidence must not be rerun, filled in, or treated as retry
authority, and neither path may be reused or deleted. Recovery at that boundary
requires a reviewed documentation revision with a new suffix. This rule also
applies whenever remote execution is uncertain.

After a local dispatch marker exists, the operator must never replay motion
for that phase, even if no remote movement can be proved. A postflight
connection failure preserves every artifact actually written, whether a
complete or partial tuple, and blocks every later step. Only
another reviewed documentation revision may add a new, unique, read-only
recovery label in the same evidence root to resume evidence collection. Such
recovery never authorizes motion or reuse of a failed label, and it must not
delete or abandon the physical evidence root. The remote physical attempt
marker remains part of the remote evidence, but it does not decide whether the
controller may reconnect or replay a phase. Any anomaly after Phase A dispatch
blocks Phase B; Phase B remains blocked unless all Phase A postflight and
acceptance evidence is complete and successful.

The controller creates `PHASE_SMALL_DISPATCHED` before
`motion_setup_session` and `PHASE_FULL_DISPATCHED` before
`phase_full_execution_session`. Each local marker uses an atomic `mkdir`, an
atomically installed and synced binding, and a parent-directory sync. The
binding records the production commit, frozen harness, connection label, and
boot evidence. Marker creation is the conservative no-replay boundary. The
remote physical attempt marker is still created and synced immediately before
the corresponding motion, but it is not the controller's reconnect boundary.

The transport contract has nine unique connection labels, all implemented by
one `/usr/bin/ssh` process. Each invocation runs through a checksum-bound
normal or live capture helper and an outer GNU timeout with `--foreground`,
`--preserve-status`, TERM, and a five-second kill grace. Every intermediate
wrapper uses `exec`, so SSH is the timeout's direct final command. The two
transfers use a single-process SSH tar stream: the normal helper feeds a closed
archive on stdin, and command evidence binds the stdin archive path and
SHA-256 before SSH extracts it into a new fixed remote staging directory.
Before the motion-artifact transfer reads stdin, its fixed remote command
revalidates all ten completed deployment gates and both runtime-manifest
layers, proves the motion evidence leaf absent, atomically creates it, and
copies and verifies the source and runtime manifests. Only then may it create
its fresh spool directory and read the closed tar. There is no SCP subprocess.
Bootstrap and transfer calls are bounded at 120 seconds, operator and
motion-capable sessions at 1,800 seconds, and postflight sessions at 600
seconds. `remote_operator_session` forces a live TTY but executes the fixed
remote command `/bin/bash --noprofile --norc`; `task4_completion` must pass and
the operator's final command must be `exit 0`. On normal completion the live
helper sends output live-to-TTY without transcription and leaves zero-byte
stdout/stderr sentinels, a capture-mode record, and rc. A terminal signal can
instead leave only the already-created prefix of that tuple.

Before OpenSSH starts, a checksum-bound wrapper opens `/dev/tty`, proves it is
a controlling TTY, and fails closed otherwise. Each child explicitly removes
`SSH_AUTH_SOCK`, sets `SSH_ASKPASS_REQUIRE=never`, uses the absolute OpenSSH
binary, ignores user configuration with `-F /dev/null`, selects the explicit
target `orangepi@192.168.13.1`, keeps strict known-host verification, disables
ControlMaster, proxy, forwarding, and local-command paths, and supplies
`IdentityAgent=none`,
`PreferredAuthentications=password`, `PubkeyAuthentication=no`,
`NumberOfPasswordPrompts=1`, `ConnectTimeout=10`, `ConnectionAttempts=1`,
`ServerAliveInterval=5`, and `ServerAliveCountMax=2`. Only after a real
OpenSSH password prompt appears may the operator enter the password once. No
password value may appear in an argument, environment value, or evidence file.

## Scope

The validation has two separately invoked phases:

1. a low-speed, short-travel qualification phase; and
2. the existing full motion profile after explicit operator approval.

phase_small_postflight_session is strictly read-only with respect to motion:
it may collect and aggregate Phase A postflight and record the operator's
exact acceptance, but it cannot preflight or execute Phase B.
phase_full_execution_session is the only session that may run Phase B
preflight and motion, after the full-dispatch marker has been durably created.
phase_full_postflight_session is strictly read-only with respect to motion.
Only fully accepted Phase A evidence permits Phase B dispatch.
Each local boundary uses atomic mkdir semantics before the connection opens.
The two postflight labels execute the checksum-bound postflight drivers
`phase_small_postflight_driver.sh` and `phase_full_postflight_driver.sh` via a
fixed remote command after rechecking both driver manifests. Each uses a
forced TTY only for its documented prompts and does not provide a general login
shell. The small driver contains no Phase B preflight or motion, and the full
driver contains only Phase B postflight collection and aggregation.

Both phases use only the installed AArch64 artifacts under
`/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix`. They select the
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

- root: `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9`;
- source: `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/source`;
- build: `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/build`;
- motion prefix: `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/prefix`;
- install/export gate:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/install-gate` while the
  gate is running, renamed by the gate to the final
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/install-gate-relocated`;
  and
- evidence:
  `/home/orangepi/roboparty_dexhand_motion_db2da9f_r9/evidence`, containing the
  non-motion gate bundle `deployment-db2da9f` and the disjoint motion bundle
  `motion-validation-bacf6612`.

At first deployment, both the remote root and its parent `evidence` path must
be absent; the captured bootstrap gate proves this and creates them with
atomic `mkdir` operations. By Task 5, that parent and the completed
`deployment-db2da9f` bundle must exist, while only the motion leaf
`motion-validation-bacf6612` must still be absent. The already-counted
`motion_artifact_transfer` connection performs that absence assertion and
atomic leaf creation before it accepts stdin; no additional connection label
is introduced. If a path violates the expectation for its stage, execution
stops and a new, explicitly reviewed suffix is selected. Existing evidence is
never reused, overwritten, or deleted.

Every non-motion deployment command runs under Bash with
`set -euo pipefail`, or captures and asserts each command's exit status.
Commands, selected environment values, controller timestamps, stdout, stderr,
and return codes that were actually written are stored without credentials.
Capture success is claimed only when the successful release inventory finds
the exact ten remote command labels—the nine functional deployment gates plus
`task4_completion`—and all required tuple fields with rc zero. Any
earlier failure audit begins with all existing `<stem>.*` artifacts, reports a
partial tuple and missing `.rc` as permanent failure evidence, and never
interprets absence of rc as permission to retry. Before either
motion-capable connection, the controller uses an atomic `mkdir` for the local
dispatch marker, atomically installs its binding, calls `/usr/bin/sync -f` on
the binding and parent directory, and verifies the closed bytes. The remote
phase attempts independently use atomically created marker directories plus
shell `noclobber`. A remote marker is described as persistent only after
`/usr/bin/sync -f` on the evidence directory succeeds; motion cannot start
before that verified sync, and the result/acceptance records are also synced.
A failed or interrupted attempt therefore cannot be mistaken for authorization
to replay a phase or overwrite earlier evidence. Postflight
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
`phase_complete`, zero saved rc, and all postflight gates. This work remains in
the read-only `phase_small_postflight_session`. Those files are
bound into a checksum-anchored automatic-success record that this evidence
protocol never overwrites. The only accepted gate text is exactly
`小行程正常`, read raw from `/dev/tty`; the acceptance record binds that text
to the automatic-success checksum and Phase A boot ID. Phase B revalidates
both records before local Phase B dispatch and again after its remote durable
once-only marker is created.

## Phase B: Full Motion Profile

After successful closure and local revalidation of the complete Phase A
postflight and acceptance tuple, the controller durably creates
`PHASE_FULL_DISPATCHED`. It then opens the separately labelled
`phase_full_execution_session`. Phase B uses a 60-second motion timeout and
repeats the full preflight in that session. It then
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
