# LHandPro Callback Quiescence Stress Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build once and run a 60-second, non-motion Orange Pi stress test that detects in-flight or late LHandPro CAN-FD transmit callbacks across `stop_monitor()`, `close()`, and `destroy()`.

**Architecture:** A single standalone C++17 source owns one raw CAN-FD socket and calls the vendor C API directly. Its detector is first exercised against deterministic bad and good simulated monitor shutdowns, then the same counters and barriers wrap the pinned AArch64 SDK on `can0`, node 1, model 6DOF S. The project is neither configured nor rebuilt.

**Tech Stack:** C++17, pthreads, Linux SocketCAN CAN-FD, LHandPro C API, `/usr/bin/g++`, shell evidence capture.

---

### Task 1: Add The Standalone Detector

**Files:**
- Create: `tests/hardware/lhandpro_callback_quiescence_stress.cpp`

- [ ] **Step 1: Write the detector self-test first**

Define a `CallbackCounters` object with atomic `entered`, `exited`, `inflight`,
`max_inflight`, `late_after_stop`, `late_after_close`, and
`late_after_destroy` counters. Define a `CallbackBlocker` with a mutex,
condition variables, `block_next`, `entered`, and `release` flags. Add a
simulated monitor whose bad stop returns while its callback is blocked and a
good stop that joins the callback thread.

The self-test contract is:

```cpp
CHECK(!exercise_simulated_stop(/*join_before_return=*/false));
CHECK(exercise_simulated_stop(/*join_before_return=*/true));
std::cout << "SELF_TEST PASS bad=rejected good=accepted\n";
```

- [ ] **Step 2: Compile and run the self-test to obtain RED**

Run:

```bash
/usr/bin/g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
  -Ithirdparty/include \
  tests/hardware/lhandpro_callback_quiescence_stress.cpp \
  -Lthirdparty/lib/x86_64 -Wl,-rpath,"$PWD/thirdparty/lib/x86_64" \
  -lLHandProLib -o /tmp/lhandpro_callback_quiescence_stress
/tmp/lhandpro_callback_quiescence_stress --self-test
```

Expected RED: the incomplete detector accepts the simulated bad stop, or the
new source does not compile before its implementation is present.

- [ ] **Step 3: Implement the minimal detector**

The callback records entry before any optional block and records exit in an
RAII guard. Lifecycle state is an atomic enum:

```cpp
enum class Phase { Initializing, Running, Stopping, Stopped, Closed, Destroyed };

struct CallbackExit {
  CallbackCounters& counters;
  ~CallbackExit() {
    counters.exited.fetch_add(1, std::memory_order_release);
    counters.inflight.fetch_sub(1, std::memory_order_acq_rel);
  }
};
```

The stop detector starts `stop_monitor()` on another thread, verifies it has
not returned while the selected callback is blocked, releases the callback,
joins the stop thread, asserts `inflight == 0`, snapshots `entered`, and rejects
any increase during the post-stop grace period. Apply the same stable-count
check after close and destroy.

- [ ] **Step 4: Add the physical SDK path without motion APIs**

Open `PF_CAN/SOCK_RAW/CAN_RAW`, enable `CAN_RAW_FD_FRAMES`, bind `can0`, and
install exact standard-ID filters `0x501`, `0x481`, `0x581`, and `0x181`. The
receive thread calls only:

```cpp
lhandprolib_set_canfd_data_decode(handle, frame.can_id & CAN_SFF_MASK,
                                  frame.data, frame.len);
```

Each lifecycle iteration calls only:

```cpp
handle = lhandprolib_create();
lhandprolib_set_hand_type(handle, C_LAC_DOF_6_S);
lhandprolib_set_send_canfd_callback(handle, transmit_callback);
lhandprolib_initial_ex(handle, C_LCN_CANFD, 1);
lhandprolib_start_monitor(handle);
lhandprolib_stop_monitor(handle);
lhandprolib_close(handle);
lhandprolib_destroy(handle);
```

The transmit callback writes the SDK-provided ID, payload, length, and frame
type unchanged to the raw socket. The source must contain no references to
`set_enable`, `home`, `move`, `target`, `velocity`, `current`, `clear_alarm`, or
`set_move_no_home`.

- [ ] **Step 5: Run self-test GREEN and static safety checks**

Run:

```bash
/tmp/lhandpro_callback_quiescence_stress --self-test
rg -n 'set_enable|home|move_|move\(|target|velocity|current|clear_alarm|set_move_no_home|cangen' \
  tests/hardware/lhandpro_callback_quiescence_stress.cpp
```

Expected: self-test prints exactly one PASS line and exits 0; `rg` exits 1 with
no matches.

- [ ] **Step 6: Commit the detector**

```bash
git add tests/hardware/lhandpro_callback_quiescence_stress.cpp
git commit -m "Add LHandPro callback quiescence stress probe"
```

### Task 2: Build Once On The Orange Pi

**Files:**
- Create remotely: `/home/orangepi/lhandpro_callback_quiescence_20260824/`
- Do not modify: prior deployment and motion evidence directories

- [ ] **Step 1: Capture a read-only preflight**

Verify the board is AArch64, `can0` is `UP`, `LOWER_UP`, `ERROR-ACTIVE`, CAN-FD
1,000,000/5,000,000, and has zero protocol/error/drop counters. Verify no
dexhand/controller process and no SocketCAN receiver owns `can0`. Record the
boot ID and before snapshot in the new evidence directory. Stop before any SDK
call if a condition fails.

- [ ] **Step 2: Transfer only the source, header, and pinned SDK**

Create a closed tar stream containing:

```text
tests/hardware/lhandpro_callback_quiescence_stress.cpp
thirdparty/include/LHandProLib/LHandProLib.h
thirdparty/lib/aarch64/libLHandProLib.so
```

On the board, verify the SDK SHA-256 is exactly
`476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`
and `file` reports AArch64 before compiling.

- [ ] **Step 3: Compile exactly once**

Run remotely:

```bash
/usr/bin/g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread \
  -Iinclude tests/hardware/lhandpro_callback_quiescence_stress.cpp \
  -Llib -Wl,-rpath,'$ORIGIN/lib' -lLHandProLib \
  -o lhandpro_callback_quiescence_stress
```

Record the source, header, SDK, and executable SHA-256 values. Run the binary's
`--self-test` mode and require exit 0 before physical mode.

### Task 3: Run One 60-Second Physical Stress Attempt

**Files:**
- Write remotely: new evidence files beneath the dedicated stress directory

- [ ] **Step 1: Run with a whole-process timeout**

Run the already-built binary once:

```bash
timeout --preserve-status --signal=INT --kill-after=5s 75s \
  ./lhandpro_callback_quiescence_stress \
  --interface can0 --node-id 1 --model 1 --duration-seconds 60
```

Expected successful summary fields:

```text
result=PASS
duration_seconds=60
iterations=>0
entered=exited
inflight=0
stop_returned_while_blocked=0
late_after_stop=0
late_after_close=0
late_after_destroy=0
created=closed=destroyed
```

- [ ] **Step 2: Capture postflight regardless of program result**

Capture `ip -details -statistics link show can0`,
`/sys/class/net/can0/statistics/*`, SocketCAN receiver lists, and relevant
processes. Compare pre/post boot ID, CAN state, protocol error counters, RX/TX
errors, dropped packets, and bus-off. Traffic packet/byte counters may increase;
error/drop counters may not.

- [ ] **Step 3: Classify the evidence**

Report PASS only when the binary exits 0, all detector counters meet the
contract, and CAN postflight is clean. A PASS supports only this exact SDK hash,
board, interface, and test duration. It does not convert observed behavior into
a vendor guarantee. Preserve a failure exactly as observed and do not silently
rerun it.

