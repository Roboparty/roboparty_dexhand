# LHandPro Callback Quiescence Stress Design

## Goal

Empirically test the bundled AArch64 LHandPro SDK on the connected Orange Pi and
physical `can0` bus. The test must determine whether any CAN-FD transmit
callback begins after `stop_monitor()` returns or after `close()`/`destroy()`
once the active read-only traffic calls have completed.

This is runtime evidence for the pinned SDK binary.  It is not a substitute for
a written vendor callback-quiescence contract.

## Safety Boundary

- Use CAN-FD interface `can0`, node ID 1, and vendor model
  `C_LAC_DOF_6_S` (1).
- Do not call enable, home, move, target, velocity, current, clear-alarm, or
  no-home-motion APIs.
- Generate traffic only through the SDK's own monitor/query transmit callback.
  Do not use `cangen` and do not transmit arbitrary CAN identifiers.
- Use a new evidence directory.  Do not modify or reuse prior deployment or
  motion evidence.
- Compile one standalone test binary once.  Do not rebuild or redeploy the
  project inside the stress loop.

## Detector

The standalone program owns one raw CAN-FD socket. Its receive thread feeds
the four expected response identifiers for node 1 into
`lhandprolib_set_canfd_data_decode()`.  The SDK transmit callback writes only
the SDK-generated frame and records atomic entry, exit, in-flight, generation,
and lifecycle counters.

Reverse engineering and two physical diagnostic attempts established that the
pinned SDK's CAN-FD monitor thread does not invoke the CAN-FD transmit callback:
the thread only delays while communication mode is `C_LCN_CANFD`; only the
standard-CAN branch sends monitor frames. Therefore an in-flight CAN-FD monitor
callback cannot be manufactured for `stop_monitor()` to drain.

Before physical execution, a deterministic self-test still proves the counter
detector rejects an implementation that returns while a simulated callback is
blocked and accepts one that joins it. This validates the detector, but is not
reported as physical CAN-FD monitor behavior.

1. A bad implementation returns from stop while a callback is blocked and must
   be rejected.
2. A quiescent implementation waits for the callback and must be accepted.

For each physical iteration, the program creates and initializes a fresh SDK
handle and starts the monitor. During the active phase it repeatedly calls the
read-only `lhandprolib_get_can_node_id()` SDO query, verifies node ID 1, and
requires every query callback to exit. It then calls `stop_monitor()` and
requires zero in-flight callbacks and a stable entry count through a grace
period, `close()`, another grace period, `destroy()`, and a final grace period.
No SDK call is issued concurrently with lifecycle teardown; this matches the
production driver's SDK-call serialization.

## Execution And Evidence

Run for 60 seconds with a whole-process timeout.  Record the SDK/header/binary
SHA-256 values, architecture, CAN configuration and error counters before and
after, iteration count, callback entry/exit totals, maximum in-flight count,
stop-wait observations, late-callback counts, failures, and cleanup result.

The run passes only if:

- the detector self-test rejects the bad simulation and accepts the good one;
- every initialized iteration observes read-only SDO query callback traffic;
- every query returns successfully with node ID 1 and all of its callbacks exit
  before `stop_monitor()` is called;
- entry and exit totals match and in-flight is zero after every stop;
- no callback begins after stop, close, or destroy;
- every created handle is closed and destroyed exactly once; and
- CAN remains `ERROR-ACTIVE` with no increase in protocol, RX/TX error, dropped,
  or bus-off counters.

Any timeout, crash, SDK initialization error, missing traffic, cleanup failure,
late callback, counter mismatch, or CAN error delta is a failure.  The exact
attempt is not silently rerun.
