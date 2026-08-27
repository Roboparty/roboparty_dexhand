# LHandPro Feedback-Period Provisioning Design

**Date:** 2026-08-27

**Status:** Approved for implementation planning

## Objective

Add a production-provisioning command to `roboparty_dexhand` that sets the
LHandPro 6DOF S CAN TPDO period to 20 ms through the vendor SDK's existing SDO
APIs. The command must read, verify, and persist the six per-axis values without
enabling, homing, or moving the hand.

The 20 ms value means that each enabled real-time feedback type is emitted at
50 Hz. The tested hand currently emits both frame type `0x50` and frame type
`0x5A` on CAN ID `0x480 + node_id`, so the observed aggregate is approximately
100 CAN-FD frames per second. These are two distinct feedback types scheduled
in the same period, not two fragments that the project may merge or suppress.

## Scope

This change is wholly owned by the `roboparty_dexhand` repository. It adds:

- an installed `roboparty-dexhand-config` executable;
- a narrow internal wrapper over the vendor SDK SDO get, set, and save calls;
- fixed 20 ms feedback-period provisioning for LHandPro 6DOF S;
- unit, integration, install, and physical-hardware validation;
- user-facing provisioning documentation.

It does not:

- modify `roboparty_deploy` or any sibling repository;
- change Linux CAN bitrate, data bitrate, link state, or queue length;
- change the public `HandDriver` C++ API or `dexhand_py` API;
- run automatically from `init_hand()` or a normal robot startup path;
- enable, home, stop, or move motors;
- expose arbitrary SDO object-dictionary reads or writes;
- change which real-time feedback types the hand emits;
- support the ordinary vendor 6DOF model or the 16DOF model in this first
  provisioning command;
- accept a period other than the physically validated 20 ms value.

Production staff or system integrators must invoke the installed command as a
manufacturing/deployment gate for every new, replacement, factory-reset, or
unknown-configuration hand. Before the first normal `init_hand()` or robot
startup, they must stop all control processes, run `apply --save`, power-cycle
the hand itself, then run `show` and require all six raw values to equal 200. Any
failure blocks normal startup. This responsibility belongs to the
`roboparty_dexhand` CLI and the manufacturing/deployment process; this design
does not automatically modify `roboparty_deploy` or make normal initialization
rewrite persistent parameters.

## Existing Boundaries

The bundled vendor header already declares:

- `lhandprolib_get_sdo_drive_param()`;
- `lhandprolib_set_sdo_drive_param()`;
- `lhandprolib_save_sdo_drive_param()`.

The current `LHandProSdk` abstraction does not wrap those functions, and the
current driver, Python module, and scripts do not expose them. Previous board
experiments used raw `cansend` and `candump` requests and therefore bypassed
`roboparty_dexhand`.

The implementation must use the vendor SDK calls. It must not add a second
hand-written CANopen SDO protocol implementation or shell out to `cansend` and
`candump`.

## Installed Command

The read-only operation is:

```sh
roboparty-dexhand-config feedback-period show \
  --interface can1 \
  --node-id 1
```

The mutating operation is:

```sh
roboparty-dexhand-config feedback-period apply \
  --interface can1 \
  --node-id 1 \
  --milliseconds 20 \
  --save
```

`--interface` must be non-empty. `--node-id` must be in the inclusive range
1-127. In this release, `apply` accepts exactly `--milliseconds 20` and requires
the explicit `--save` acknowledgement. Unsupported values and missing options
are usage errors and cause no device I/O.

The tool uses period units in its interface instead of a `--frequency` option.
This avoids ambiguity between per-type update frequency and aggregate CAN frame
rate.

The command prints:

- the interface and node ID;
- every axis object index and its value before the operation;
- whether the operation was already compliant or changed the device;
- every axis value read back after a write;
- whether persistent save was acknowledged;
- a reminder that a power-cycle followed by `show` is the persistence check.

Machine-readable output is outside this first release.

## Internal Architecture

`LHandProSdk` gains narrow virtual methods for SDO parameter get, set, and
save. `CapiLHandProSdk` delegates those methods directly to the three existing
vendor C functions. The fake SDK records calls and provides scripted results
for deterministic tests.

Provisioning logic remains private to the LHandPro implementation. It may be
implemented as private concrete-driver methods or a focused internal component,
but it must reuse the established SDK handle, callback bridge, filtered private
SocketCAN transport, receive decoding, health checks, and cleanup ordering. The
installed CLI links to private implementation targets; no new installed C++
header or Python binding is introduced.

The bundled vendor set and save calls return after transmitting their SDO
requests; a zero return is not an acknowledgement from the device. The driver
therefore arms a private acknowledgement gate before each call and waits up to
100 ms for the exact standard-ID `0x580 + node`, command `0x60`, index, and
subindex response. An exact `0x80` response fails the active request. A response
that arrives before the vendor call returns is retained, but it releases the
caller only after that receive callback has finished. Cleanup cancels and wakes
any pending wait.

All `0x60` SDO write acknowledgements are still passed to the vendor decoder so
it can clear internal pending state, but the raw frame is authoritative. The
bundled AArch64 SDK consistently returns decoder code 3 for these valid frames,
so that return is ignored only for `0x60` write acknowledgements. Other frames,
including nonmatching `0x80` aborts and normal feedback, retain the existing
fail-closed decoder handling.

The pinned AArch64 SDK's save call was also observed to perform a two-stage
exchange. It first sends `00 10 10 00 00 00 00 00` and receives
`00 10 10 00 20 00 00 00`; about 5 ms later it sends the documented
`2F 10 10 01 73 61 76 65` save request and receives
`60 10 10 01 00 00 00 00`. The first response also produces decoder code 3.
Only while the acknowledgement gate is pending for save `0x1010:0x01`, the
driver recognizes that exact first response, passes it to the decoder without
recording code 3 as a fault, and continues waiting. It never completes the save
wait. The complete payload, including observed version byte `0x20`, is pinned;
another value fails closed and requires renewed SDK/firmware validation.

The CLI establishes a normal communication session for the 6DOF S model with
motor enable and homing disabled. The same process-global callback ownership
gate and callback-quiescence cleanup contract used by `LHandProDriver` apply.

The operator must stop any other process controlling the same hand before
running the command. Cross-process ownership detection is outside this change;
the command documents this precondition and does not claim to enforce it.

## Device Objects

The six CAN TPDO period objects use subindex `0x14`:

| Axis | Index |
| ---: | ----: |
| 1 | `0x201D` |
| 2 | `0x205D` |
| 3 | `0x209D` |
| 4 | `0x20DD` |
| 5 | `0x211D` |
| 6 | `0x215D` |

The documented unit is 0.1 ms. The only accepted target is therefore:

```text
20 ms / 0.1 ms = 200 = 0x00C8
```

The object subindex `0x14` identifies the stored base-period field; it is not a
runtime period value. After `initial_ex`, the separate runtime command on CAN ID
`0x500 + node_id` uses payload `00 04 50 01 5A 01`. The bytes following frame
types `0x50` and `0x5A` are multipliers of the current stored base period, so
`0x01` schedules each type every 20 ms. A runtime multiplier of `0x14` would
schedule each type every 20 base periods, or 400 ms with these SDO values.

## Show Transaction

`show` performs these steps:

1. Validate every CLI argument without device I/O.
2. Create a 6DOF S SDK handle and open the private CAN-FD transport.
3. Initialize communication without enabling, homing, or moving.
4. Read subindex `0x14` from all six indexes.
5. Report all six raw values and their period interpretation.
6. Clean up through the established stop-monitor, close-transport, close-SDK,
   callback-unpublish, and destroy sequence.

All six reads must succeed for exit status zero. A partial snapshot is printed
for diagnostics but is not reported as a successful configuration.

## Apply Transaction

`apply` performs these steps:

1. Complete the same argument validation and six-axis snapshot as `show`.
2. If all six values already equal 200, perform no writes and no nonvolatile
   save, report `already-compliant`, clean up, and exit successfully.
3. Otherwise retain the complete original six-axis snapshot in memory.
4. Write target value 200 to all six indexes. Each write must receive its exact
   SDO acknowledgement before the next write begins. The vendor protocol
   explicitly requires the six indexes to be written together; the
   implementation must not update only the mismatching axes.
5. Read all six indexes again.
6. Only when every read succeeds and every value equals 200, invoke the vendor
   save call exactly once and require the exact `0x1010:0x01` acknowledgement.
7. Report the final values and save acknowledgement, then clean up.

No persistent save occurs before full six-axis verification. A successful save
acknowledgement proves that the save request completed; persistence itself is
accepted only after a power-cycle and a new successful `show` operation.

## Failure and Rollback

Argument, initialization, read, write, readback, save, and cleanup failures are
reported with the failed operation and vendor error code where available. The
command exits nonzero on every such failure.

If a write or post-write readback fails after any target write was attempted,
the tool makes one best-effort rollback transaction:

1. Write the original snapshot back to all six indexes, waiting for each exact
   acknowledgement. Continue best-effort across all six axes after an
   individual rollback failure.
2. Read all six values and compare them with the original snapshot.
3. Do not invoke persistent save.

A verified rollback is reported as restored-but-not-applied. A failed rollback
is reported prominently as an uncertain transient device state with an
instruction to power-cycle the hand before further operation. Because the
failed transaction never calls save, the last persisted configuration is
expected to return after power-cycle, but the tool does not claim this until a
fresh `show` succeeds.

If the save call itself returns failure after a verified target readback, the
tool reports that the runtime values are 200 but persistence is unknown. It
does not try to save again automatically.

Cleanup always runs, including after argument-independent runtime failures.
Cleanup faults remain visible and make the command fail even when the parameter
operation itself succeeded.

## Exit Contract

- `0`: complete six-axis read succeeded, and for `apply` the device was either
  already compliant or the verified values were saved successfully;
- `2`: invalid command-line usage; no device I/O occurred;
- `1`: initialization, communication, parameter, verification, save, rollback,
  or cleanup failure.

Scripts must rely on the exit status and stable result labels, not parse prose
error text.

## Tests

Hardware-free tests use the existing fake SDK and fake transport. They cover:

- C API wrapper delegation for get, set, and save;
- CLI validation before I/O;
- successful six-axis `show`;
- one failed read producing a nonzero result;
- already-compliant `apply` producing zero writes and zero saves;
- mismatched `apply` writing all six indexes, verifying all six, and saving
  exactly once;
- each possible write failure preventing save and attempting rollback;
- post-write read failure and value mismatch preventing save;
- verified and failed rollback reporting;
- save failure reporting without automatic retry;
- SDK exceptions and asynchronous decode faults;
- acknowledgement-before-return, exact acknowledgement matching, SDO abort,
  timeout, per-axis serialization, save acknowledgement, and cleanup
  cancellation;
- cleanup on every success and failure path;
- installed executable presence and runtime lookup of the pinned vendor SDK;
- no change to the public C++ or Python API surface.

Existing build, CTest, Python binding, vcan two-socket, install/export, RPATH,
SDK artifact, and callback-quiescence gates remain required.

## ARM64 Physical Acceptance

Physical validation runs on an Orange Pi with the hand at node ID 1 and an
already configured 1 Mbit/s nominal, 5 Mbit/s data-rate CAN-FD interface. The
test must not reconfigure the Linux CAN interface and must not command motion.
Provisioning still initializes communication and can emit transient runtime
configuration traffic; the no-motion requirement does not mean zero CAN
traffic.

Before any normal robot startup, the device-level acceptance gate is mandatory:
stop all control processes, run `apply --save`, power-cycle only the hand, and
run `show` to prove all six raw values are 200. A new, replacement,
factory-reset, or unknown-configuration hand is rejected from service if any
step fails. Runtime multiplier 1 yields 50 Hz only with this verified 20 ms base
period; an unprovisioned base period can yield unexpectedly high feedback rates.

Acceptance requires:

1. Stop all other dexhand control processes.
2. Run `show` and capture all six current values.
3. Exercise the already-compliant `apply` path and prove that it performs no
   device writes or save.
4. Under an explicitly authorized hardware test, create a transient six-axis
   mismatch without persisting it, run `apply`, and prove that the CLI restores
   all six values to 200 and receives a successful save acknowledgement.
5. Power-cycle the hand.
6. Run `show` and prove all six values remain 200.
7. Passively capture five seconds of traffic and observe approximately 250
   frame-type `0x50` messages and 250 frame-type `0x5A` messages on CAN ID
   `0x480 + node_id`.
8. Observe no new RX drops, CAN protocol errors, bus errors, error-passive
   transition, or bus-off transition during the acceptance window.
9. Prove cleanup leaves no configuration process and no active receive worker.

The frame-rate observation validates the approved 20 ms behavior. It does not
redefine `0x50` and `0x5A` as fragments of one message: they remain two distinct
configured real-time feedback types.

## Release Impact

The change adds installed functionality without changing existing motion API
semantics, so it is released as a new minor version. Release documentation and
manufacturing instructions must make provisioning a mandatory device gate, not
optional tuning. The gate is implemented by `roboparty-dexhand-config`; it does
not automatically change `roboparty_deploy`, and normal `HandDriver`
initialization never rewrites device parameters.
