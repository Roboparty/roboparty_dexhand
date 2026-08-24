# LHandPro Callback Quiescence Stress Design

## Goal

Empirically test the bundled AArch64 LHandPro SDK on the connected Orange Pi and
physical `can0` bus.  The test must determine whether `stop_monitor()` waits for
an in-flight CAN-FD transmit callback and whether any callback begins after
`stop_monitor()` returns or after `close()`/`destroy()`.

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

The standalone program owns one raw CAN-FD socket.  Its receive thread feeds
the four expected response identifiers for node 1 into
`lhandprolib_set_canfd_data_decode()`.  The SDK transmit callback writes only
the SDK-generated frame and records atomic entry, exit, in-flight, generation,
and lifecycle counters.

Before physical execution, a deterministic self-test runs two simulated
monitor implementations:

1. A bad implementation returns from stop while a callback is blocked and must
   be rejected.
2. A quiescent implementation waits for the callback and must be accepted.

For each physical iteration, the program creates and initializes a fresh SDK
handle, starts the monitor, waits for callback traffic, then blocks one selected
callback.  A separate thread calls `stop_monitor()`.  The detector requires
stop to remain blocked until that callback is released.  After stop returns it
requires zero in-flight callbacks and a stable entry count through a grace
period, `close()`, another grace period, `destroy()`, and a final grace period.

## Execution And Evidence

Run for 60 seconds with a whole-process timeout.  Record the SDK/header/binary
SHA-256 values, architecture, CAN configuration and error counters before and
after, iteration count, callback entry/exit totals, maximum in-flight count,
stop-wait observations, late-callback counts, failures, and cleanup result.

The run passes only if:

- the detector self-test rejects the bad simulation and accepts the good one;
- every initialized iteration observes monitor callback traffic;
- every blocked callback exits before `stop_monitor()` returns;
- entry and exit totals match and in-flight is zero after every stop;
- no callback begins after stop, close, or destroy;
- every created handle is closed and destroyed exactly once; and
- CAN remains `ERROR-ACTIVE` with no increase in protocol, RX/TX error, dropped,
  or bus-off counters.

Any timeout, crash, SDK initialization error, missing traffic, cleanup failure,
late callback, counter mismatch, or CAN error delta is a failure.  The exact
attempt is not silently rerun.

