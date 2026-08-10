# roboparty_dexhand Independent CAN-FD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `roboparty_dexhand` an independently owned, installable x86-64/AArch64 hand-control package with its own private SocketCAN transport, without modifying or depending on `roboparty_motors` internals.

**Architecture:** Keep `HandDriver` as the only installed API and route `LHandPro` through a private SDK adapter and a private CAN-FD transport. Build one installed `dexhand` shared library so C++ and Python share the same process-wide vendor callback gate; all private transport/driver libraries remain static and unexported. Use fake SDK, transport, and syscall adapters for hardware-free lifecycle and error-path tests, with vcan and native ARM checks as explicit release gates.

**Tech Stack:** C++17, Linux SocketCAN/CAN-FD, vendor `LHandProLib` C API, CMake/CTest, ament/colcon, pybind11, spdlog/fmt, Python `unittest`.

---

## Scope And Locked Contracts

Work only in `/home/sjh/leisai_hand/roboparty_dexhand`. Treat these repositories as read-only evidence throughout execution:

- `/home/sjh/leisai_hand/roboparty_motors`
- `/home/sjh/leisai_hand/roboparty_hand`
- `/home/sjh/leisai_hand/roboparty_deploy`

Do not add dexhand to deploy in this plan. Do not run `scripts/test_dexhand.py` in automated verification because it enables, homes, and moves physical hardware.

Prefix every new RoboParty-authored C/C++ file with
`// SPDX-License-Identifier: GPL-3.0` and
`// Copyright (C) 2026 Roboparty`; use `#` comments for CMake/Python files.
Do not alter or prepend RoboParty ownership to the vendor header or binaries.

One planning correction to the approved design is required before implementation. Do not hold one mutex across both SDK commands and receive decode: a command such as `initial_ex()` may wait for a response that must enter through `set_canfd_data_decode()`, which would deadlock if the receive callback waited for the same mutex. Use `sdk_call_mutex_` only to serialize public/lifecycle SDK calls. Protect handle lifetime with quiescent RX/TX callback gates, stop the monitor while RX is still available, then clear RX before closing and destroying the SDK handle.

The exact public factory contract is:

```cpp
static std::shared_ptr<HandDriver> create_hand(
    const std::string& hand_type,
    const std::string& interface_type,
    const std::string& interface,
    int hand_model = 0,
    int canfd_node_id = 1);
```

The public model values remain `0` and `1`; the driver maps them to vendor values `0` and `2`. The only supported communication value is `HandCommType::CANFD`. Package version becomes `0.2.0`.

## Final File Map

```text
include/hand_driver.hpp                       installed public API only
src/hand_driver.cpp                           validation and vendor factory
src/pybind_module.cpp                         base-class Python API
src/protocol/callback_gate.hpp                quiescent callback lease primitive
src/protocol/canfd_transport.hpp              private frame/transport contract
src/protocol/socket_canfd_transport.hpp       SocketCAN class and syscall seam
src/protocol/socket_canfd_transport.cpp       Linux SocketCAN implementation
src/drivers/lhandpro/lhandpro_sdk.hpp         mockable vendor-operation contract
src/drivers/lhandpro/lhandpro_sdk.cpp         raw C API owner/adapter
src/drivers/lhandpro/lhandpro_driver.hpp      private driver state machine
src/drivers/lhandpro/lhandpro_driver.cpp      lifecycle, callbacks, API forwarding
thirdparty/include/LHandProLib/LHandProLib.h   vendor header
thirdparty/lib/x86_64/libLHandProLib.so        x86-64 vendor binary
thirdparty/lib/aarch64/libLHandProLib.so       AArch64 vendor binary
tests/fakes/                                   hardware-free doubles
tests/installed_consumer/                      installed CMake consumer fixture
tests/*.cpp, tests/*.py, tests/*.cmake         unit/integration/export checks
```

### Task 1: Record The Existing Source Baseline And Lock The Planning Correction

**Files:**
- Create: `.gitignore`
- Modify: `docs/superpowers/specs/2026-08-10-roboparty-dexhand-independent-canfd-design.md`
- Verify only: all currently untracked source files

- [ ] **Step 1: Confirm ownership boundaries and dirty state before any source edit**

Run:

```bash
git -C /home/sjh/leisai_hand/roboparty_motors status --short
git -C /home/sjh/leisai_hand/roboparty_hand status --short
git -C /home/sjh/leisai_hand/roboparty_deploy status --short
git status --short
```

Expected: motors still shows its pre-existing modified `src/protocol/canfd/socket_canfd.cpp` and untracked `.bak`; hand still shows its pre-existing modified `src/hand_driver.cpp`; deploy is unchanged; dexhand source is untracked except the committed design specification. Record this output in the execution notes and do not stage from any read-only repository.

- [ ] **Step 2: Add the repository ignore rules**

Create `.gitignore` with exactly:

```gitignore
/build/
/install/
/log/
/CMakeFiles/
/CMakeCache.txt
/cmake_install.cmake
/Makefile
/compile_commands.json
__pycache__/
*.py[cod]
*.egg-info/
.pytest_cache/
.idea/
.vscode/
*.swp
*~
```

- [ ] **Step 3: Verify generated output is ignored but source is not**

Run:

```bash
git check-ignore build/CMakeCache.txt CMakeFiles/3.25.1/CMakeCXXCompiler.cmake
if git check-ignore include/hand_driver.hpp; then exit 1; fi
```

Expected: the first command prints both generated paths; the second prints nothing and exits zero.

- [ ] **Step 4: Amend the approved design's concurrency paragraph**

Replace the paragraph that says one SDK mutex serializes receive decode with this exact text:

```markdown
An SDK-call mutex serializes public methods and lifecycle operations. Receive
decode does not take that mutex: vendor calls may synchronously wait for CAN-FD
feedback, so blocking decode behind the caller would deadlock. Instead, the
transport's quiescent receive-callback gate guarantees that decode cannot
outlive the SDK handle. Public methods check `Ready`, acquire the SDK-call
mutex, check `Ready` again, and only then enter the vendor API.
```

Replace the cleanup sequence with:

```markdown
1. Transition to `Stopping`, rejecting new public SDK operations, and drain
   any public SDK call already holding the SDK-call mutex.
2. Stop SDK monitoring while receive and transmit callbacks are still usable.
3. Clear the receive callback and wait for in-flight decode to finish.
4. Close SDK communication and unregister the SDK transmit callback while the
   transmit context and transport remain usable.
5. Unpublish the global transmit context, close its gate, and wait for
   in-flight transmit callbacks.
6. Close the transport and join its receive thread.
7. Destroy the SDK handle, reset cached state, release the single-instance
   slot, and return to `Created`.
```

- [ ] **Step 5: Commit the untouched source baseline separately from later refactors**

Run:

```bash
git add .gitignore CMakeLists.txt CODE_REVIEW.md README.md cmake include package.xml scripts src thirdparty docs/superpowers/specs
git status --short
git commit \
  -m "Preserve the pre-refactor dexhand baseline" \
  -m "Track the existing implementation before replacing its private motors dependency so every ownership-boundary change remains reviewable." \
  -m "Constraint: roboparty_motors, roboparty_hand, and roboparty_deploy are read-only" \
  -m "Confidence: high" \
  -m "Scope-risk: narrow" \
  -m "Tested: git ignore checks and source-boundary status inspection" \
  -m "Not-tested: existing implementation build, which still depends on motors internals"
```

Expected: `build/` and `CMakeFiles/` are absent from the staged/committed file list. The commit contains current dexhand sources plus the design concurrency correction, not implementation changes.

### Task 2: Normalize Vendor SDK Layout And Architecture Selection

**Files:**
- Move: `src/drivers/lhandpro/LHandProLib.h` -> `thirdparty/include/LHandProLib/LHandProLib.h`
- Move: `thirdparty/lib/libLHandProLib.so` -> `thirdparty/lib/x86_64/libLHandProLib.so`
- Create: `thirdparty/lib/aarch64/libLHandProLib.so`
- Create: `cmake/SelectLHandProSdk.cmake`
- Create: `tests/check_sdk_artifacts.cmake`
- Create: `tests/check_arch_value.cmake`

- [ ] **Step 1: Write the failing SDK artifact check**

Create `tests/check_sdk_artifacts.cmake` with:

```cmake
cmake_minimum_required(VERSION 3.12)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(X86_LIB "${SOURCE_DIR}/thirdparty/lib/x86_64/libLHandProLib.so")
set(ARM_LIB "${SOURCE_DIR}/thirdparty/lib/aarch64/libLHandProLib.so")
set(VENDOR_HEADER "${SOURCE_DIR}/thirdparty/include/LHandProLib/LHandProLib.h")

foreach(path IN ITEMS "${X86_LIB}" "${ARM_LIB}" "${VENDOR_HEADER}")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing vendor artifact: ${path}")
  endif()
endforeach()

file(SHA256 "${X86_LIB}" X86_SHA)
file(SHA256 "${ARM_LIB}" ARM_SHA)
if(NOT X86_SHA STREQUAL "3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640")
  message(FATAL_ERROR "Unexpected x86-64 SDK hash: ${X86_SHA}")
endif()
if(NOT ARM_SHA STREQUAL "476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044")
  message(FATAL_ERROR "Unexpected AArch64 SDK hash: ${ARM_SHA}")
endif()

execute_process(COMMAND readelf -h "${X86_LIB}" OUTPUT_VARIABLE X86_ELF
                RESULT_VARIABLE X86_RC)
execute_process(COMMAND readelf -h "${ARM_LIB}" OUTPUT_VARIABLE ARM_ELF
                RESULT_VARIABLE ARM_RC)
if(NOT X86_RC EQUAL 0 OR NOT X86_ELF MATCHES "Machine:[ ]+Advanced Micro Devices X86-64")
  message(FATAL_ERROR "x86-64 SDK has the wrong ELF machine")
endif()
if(NOT ARM_RC EQUAL 0 OR NOT ARM_ELF MATCHES "Machine:[ ]+AArch64")
  message(FATAL_ERROR "AArch64 SDK has the wrong ELF machine")
endif()

foreach(case IN ITEMS "x86_64:x86_64" "amd64:x86_64"
                      "aarch64:aarch64" "arm64:aarch64")
  string(REPLACE ":" ";" pair "${case}")
  list(GET pair 0 input_arch)
  list(GET pair 1 expected_arch)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -DSOURCE_DIR=${SOURCE_DIR} -DARCH=${input_arch}
      -DEXPECTED=${expected_arch}
      -P ${SOURCE_DIR}/tests/check_arch_value.cmake
    RESULT_VARIABLE alias_rc)
  if(NOT alias_rc EQUAL 0)
    message(FATAL_ERROR "Architecture alias failed: ${case}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -DSOURCE_DIR=${SOURCE_DIR} -DARCH=riscv64 -DEXPECT_FAILURE=ON
    -P ${SOURCE_DIR}/tests/check_arch_value.cmake
  RESULT_VARIABLE unsupported_rc)
if(unsupported_rc EQUAL 0)
  message(FATAL_ERROR "Unsupported architecture was accepted")
endif()
```

- [ ] **Step 2: Run the artifact check and observe the intended failure**

Run:

```bash
cmake -DSOURCE_DIR="$PWD" -P tests/check_sdk_artifacts.cmake
```

Expected: `FATAL_ERROR` naming the missing architecture-specific artifact path.

- [ ] **Step 3: Relocate the tracked x86 files and copy the verified ARM binary**

Run:

```bash
mkdir -p thirdparty/include/LHandProLib thirdparty/lib/x86_64 thirdparty/lib/aarch64
git mv src/drivers/lhandpro/LHandProLib.h thirdparty/include/LHandProLib/LHandProLib.h
git mv thirdparty/lib/libLHandProLib.so thirdparty/lib/x86_64/libLHandProLib.so
cp /home/sjh/leisai_hand/RP_Hand/LHandProLib-API-Linux-20260727/aarch64/lib/libLHandProLib.so \
  thirdparty/lib/aarch64/libLHandProLib.so
```

Do not copy test executables, i386 binaries, debug binaries, LabView binaries, or unused headers.

- [ ] **Step 4: Implement the architecture normalizer**

Create `cmake/SelectLHandProSdk.cmake` with:

```cmake
function(roboparty_dexhand_normalize_arch input_arch output_var)
  string(TOLOWER "${input_arch}" normalized_arch)
  if(normalized_arch STREQUAL "x86_64" OR normalized_arch STREQUAL "amd64")
    set(selected_arch "x86_64")
  elseif(normalized_arch STREQUAL "aarch64" OR normalized_arch STREQUAL "arm64")
    set(selected_arch "aarch64")
  else()
    message(FATAL_ERROR
      "Unsupported roboparty_dexhand architecture '${input_arch}'. "
      "Supported values: x86_64, amd64, aarch64, arm64")
  endif()
  set(${output_var} "${selected_arch}" PARENT_SCOPE)
endfunction()
```

Create `tests/check_arch_value.cmake` with:

```cmake
cmake_minimum_required(VERSION 3.12)
foreach(required IN ITEMS SOURCE_DIR ARCH)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
include("${SOURCE_DIR}/cmake/SelectLHandProSdk.cmake")
roboparty_dexhand_normalize_arch("${ARCH}" ACTUAL)
if(DEFINED EXPECTED AND NOT ACTUAL STREQUAL EXPECTED)
  message(FATAL_ERROR "${ARCH}: expected ${EXPECTED}, got ${ACTUAL}")
endif()
```

- [ ] **Step 5: Verify hashes, ELF machines, and all accepted aliases**

Run:

```bash
cmake -DSOURCE_DIR="$PWD" -P tests/check_sdk_artifacts.cmake
```

Expected: artifact and alias checks exit zero; the nested unsupported-architecture check exits nonzero and is accepted as the expected rejection.

- [ ] **Step 6: Commit the vendor boundary**

```bash
git add cmake/SelectLHandProSdk.cmake tests/check_sdk_artifacts.cmake tests/check_arch_value.cmake thirdparty src/drivers/lhandpro
git commit \
  -m "Make controller architecture an explicit SDK choice" \
  -m "Separate vendor headers from driver sources and carry only the verified x86-64 and AArch64 runtime artifacts required by supported Linux controllers." \
  -m "Constraint: Orange Pi and RDK deployment requires AArch64 while development remains x86-64" \
  -m "Confidence: high" \
  -m "Scope-risk: narrow" \
  -m "Tested: SHA-256 and ELF machine checks for both vendor binaries" \
  -m "Not-tested: native AArch64 loading"
```

### Task 3: Add A Quiescent Callback Gate And Test Foundation

**Files:**
- Create: `src/protocol/callback_gate.hpp`
- Create: `tests/test_support.hpp`
- Create: `tests/test_callback_gate.cpp`

- [ ] **Step 1: Add always-on test assertions and the failing gate test**

Create `tests/test_support.hpp` with:

```cpp
#pragma once

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__                               \
                << ": CHECK failed: " #condition << '\n';                  \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

#define CHECK_EQ(actual, expected) CHECK((actual) == (expected))
```

Create `tests/test_callback_gate.cpp` with:

```cpp
#include "protocol/callback_gate.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <future>

using roboparty::dexhand::detail::CallbackGate;

int main() {
  CallbackGate gate;
  CHECK(!gate.try_enter().has_value());
  gate.open();

  auto lease = gate.try_enter();
  CHECK(lease.has_value());
  auto stopped = std::async(std::launch::async, [&gate] {
    gate.close_and_wait();
    return true;
  });
  CHECK(stopped.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  lease.reset();
  CHECK(stopped.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  CHECK(!gate.try_enter().has_value());

  gate.open();
  CHECK(gate.try_enter().has_value());
  gate.close_and_wait();
  return 0;
}
```

- [ ] **Step 2: Compile the test and confirm the missing-header failure**

Run:

```bash
c++ -std=c++17 -pthread -Isrc -Itests tests/test_callback_gate.cpp -o /tmp/dexhand_callback_gate_test
```

Expected: compilation fails because `protocol/callback_gate.hpp` does not exist.

- [ ] **Step 3: Implement the callback gate**

Create `src/protocol/callback_gate.hpp` with:

```cpp
#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace roboparty::dexhand::detail {

class CallbackGate {
 private:
  struct State {
    std::mutex mutex;
    std::condition_variable drained;
    bool accepting{false};
    std::size_t active{0};
  };

 public:
  class Lease {
   public:
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept : state_(std::move(other.state_)) {}
    Lease& operator=(Lease&& other) noexcept {
      if (this != &other) {
        release();
        state_ = std::move(other.state_);
      }
      return *this;
    }
    ~Lease() { release(); }

   private:
    friend class CallbackGate;
    explicit Lease(std::shared_ptr<State> state) : state_(std::move(state)) {}
    void release() noexcept {
      if (!state_) return;
      std::lock_guard<std::mutex> lock(state_->mutex);
      --state_->active;
      if (state_->active == 0) state_->drained.notify_all();
      state_.reset();
    }
    std::shared_ptr<State> state_;
  };

  CallbackGate() : state_(std::make_shared<State>()) {}
  CallbackGate(const CallbackGate&) = delete;
  CallbackGate& operator=(const CallbackGate&) = delete;
  ~CallbackGate() { close_and_wait(); }

  void open() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active == 0) state_->accepting = true;
  }

  std::optional<Lease> try_enter() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->accepting) return std::nullopt;
    ++state_->active;
    return Lease(state_);
  }

  void close_and_wait() noexcept {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->accepting = false;
    state_->drained.wait(lock, [this] { return state_->active == 0; });
  }

 private:
  std::shared_ptr<State> state_;
};

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 4: Compile and run the gate test**

Run:

```bash
c++ -std=c++17 -pthread -Isrc -Itests tests/test_callback_gate.cpp -o /tmp/dexhand_callback_gate_test
/tmp/dexhand_callback_gate_test
```

Expected: compilation and execution both exit zero in under two seconds.

- [ ] **Step 5: Commit the concurrency primitive**

```bash
git add src/protocol/callback_gate.hpp tests/test_support.hpp tests/test_callback_gate.cpp
git commit \
  -m "Make callback shutdown wait for active work" \
  -m "Use move-only callback leases so receive and vendor transmit bridges cannot outlive the resources they access." \
  -m "Rejected: clear only the callback function | a dispatch that already copied it could use freed state" \
  -m "Confidence: high" \
  -m "Scope-risk: narrow" \
  -m "Tested: callback gate close blocks until the final lease is released"
```

### Task 4: Implement The Private CAN-FD Contract And SocketCAN Transport

**Files:**
- Create: `src/protocol/canfd_transport.hpp`
- Create: `src/protocol/socket_canfd_transport.hpp`
- Create: `src/protocol/socket_canfd_transport.cpp`
- Create: `tests/fakes/fake_socket_ops.hpp`
- Create: `tests/test_canfd_transport.cpp`

- [ ] **Step 1: Write transport contract and error-path tests first**

Create `src/protocol/canfd_transport.hpp` with the contract the test will compile against:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace roboparty::dexhand::detail {

struct CanFdFrame {
  std::uint32_t id{0};
  bool extended{false};
  bool brs{false};
  std::uint8_t len{0};
  std::array<std::uint8_t, 64> data{};
};

class CanFdTransport {
 public:
  using ReceiveCallback = std::function<void(const CanFdFrame&)>;
  virtual ~CanFdTransport() = default;
  virtual bool open(const std::string& interface,
                    const std::vector<std::uint32_t>& standard_ids) = 0;
  virtual bool transmit(const CanFdFrame& frame) noexcept = 0;
  virtual void set_receive_callback(ReceiveCallback callback) = 0;
  virtual void clear_receive_callback() noexcept = 0;
  virtual void close() noexcept = 0;
};

}  // namespace roboparty::dexhand::detail
```

Create `tests/test_canfd_transport.cpp` with this complete test after adding the fake in Step 5 (the initial red compile is expected because that header does not exist yet):

```cpp
#include "fakes/fake_socket_ops.hpp"
#include "protocol/socket_canfd_transport.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

using namespace roboparty::dexhand::detail;

void check_open_failure(const std::function<void(FakeSocketOps&)>& configure,
                        int expected_closes) {
  auto ops = std::make_shared<FakeSocketOps>();
  configure(*ops);
  SocketCanFdTransport transport(ops);
  CHECK(!transport.open("can-test", {0x501}));
  CHECK_EQ(ops->close_calls, expected_closes);
}

int main() {
  auto ops = std::make_shared<FakeSocketOps>();
  SocketCanFdTransport transport(ops);
  CHECK(transport.open("can-test", {0x501, 0x481, 0x581, 0x181}));
  CHECK_EQ(ops->filters.size(), 4U);
  CHECK_EQ(ops->recv_own_msgs_calls, 0);
  const std::vector<std::uint32_t> expected_ids{0x501, 0x481, 0x581, 0x181};
  for (std::size_t index = 0; index < expected_ids.size(); ++index) {
    CHECK_EQ(ops->filters[index].can_id, expected_ids[index]);
    CHECK_EQ(ops->filters[index].can_mask,
             CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG);
  }

  CanFdFrame frame;
  frame.id = 0x601;
  frame.len = 8;
  CHECK(transport.transmit(frame));
  CHECK_EQ(ops->last_written.len, 8);
  CHECK_EQ(ops->last_written.flags & CANFD_BRS, 0);

  frame.len = 9;
  frame.brs = true;
  CHECK(transport.transmit(frame));
  CHECK((ops->last_written.flags & CANFD_BRS) != 0);

  frame.id = 0x1ABCDE;
  frame.extended = true;
  CHECK(transport.transmit(frame));
  CHECK_EQ(ops->last_written.can_id, 0x1ABCDEU | CAN_EFF_FLAG);
  frame.id = CAN_EFF_MASK + 1U;
  CHECK(!transport.transmit(frame));
  frame.extended = false;
  frame.id = CAN_SFF_MASK + 1U;
  CHECK(!transport.transmit(frame));

  ops->write_result = CANFD_MTU - 1;
  frame.id = 0x601;
  CHECK(!transport.transmit(frame));
  frame.len = 65;
  CHECK(!transport.transmit(frame));

  std::promise<void> callback_entered;
  std::promise<void> release_callback;
  auto release = release_callback.get_future().share();
  std::atomic<int> callback_count{0};
  transport.set_receive_callback([&](const CanFdFrame& received) {
    CHECK_EQ(received.id, 0x501U);
    CHECK_EQ(received.len, 3U);
    ++callback_count;
    callback_entered.set_value();
    release.wait();
  });
  canfd_frame incoming{};
  incoming.can_id = 0x501;
  incoming.len = 3;
  ops->queue(incoming);
  CHECK(callback_entered.get_future().wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  auto clearing = std::async(std::launch::async, [&transport] {
    transport.clear_receive_callback();
  });
  CHECK(clearing.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  release_callback.set_value();
  CHECK(clearing.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  ops->queue(incoming);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK_EQ(callback_count.load(), 1);

  transport.close();
  transport.close();
  CHECK(!transport.transmit(CanFdFrame{}));
  CHECK_EQ(ops->close_calls, 1);

  check_open_failure([](FakeSocketOps& fake) { fake.socket_result = -1; }, 0);
  check_open_failure([](FakeSocketOps& fake) { fake.fd_frames_result = -1; }, 1);
  check_open_failure([](FakeSocketOps& fake) { fake.filters_result = -1; }, 1);
  check_open_failure(
      [](FakeSocketOps& fake) { fake.interface_index_result = -1; }, 1);
  check_open_failure([](FakeSocketOps& fake) { fake.bind_result = -1; }, 1);
  return 0;
}
```

`FakeSocketOps` must expose deterministic results for `socket`, `setsockopt`, interface lookup, `bind`, `poll`, `read`, `write`, and `close`; it must copy installed `can_filter` values and the last written `canfd_frame` for assertions.

- [ ] **Step 2: Compile and observe the missing transport implementation failure**

Run:

```bash
c++ -std=c++17 -pthread -Isrc -Itests tests/test_canfd_transport.cpp \
  src/protocol/socket_canfd_transport.cpp \
  $(pkg-config --cflags --libs spdlog fmt) -o /tmp/dexhand_transport_test
```

Expected: compilation fails because the SocketCAN header, implementation, and fake syscall adapter do not exist.

- [ ] **Step 3: Define the injectable syscall surface and production class**

Create `src/protocol/socket_canfd_transport.hpp` with these complete public/private contracts:

```cpp
#pragma once

#include "protocol/callback_gate.hpp"
#include "protocol/canfd_transport.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace roboparty::dexhand::detail {

class SocketOps {
 public:
  virtual ~SocketOps() = default;
  virtual int socket(int domain, int type, int protocol) noexcept = 0;
  virtual int set_option(int fd, int level, int name, const void* value,
                         socklen_t size) noexcept = 0;
  virtual int interface_index(int fd, const std::string& name) noexcept = 0;
  virtual int bind(int fd, const sockaddr_can& address) noexcept = 0;
  virtual int poll_readable(int fd, int timeout_ms) noexcept = 0;
  virtual ssize_t read(int fd, canfd_frame& frame) noexcept = 0;
  virtual ssize_t write(int fd, const canfd_frame& frame) noexcept = 0;
  virtual int close(int fd) noexcept = 0;
  virtual int last_error() const noexcept = 0;
};

class LinuxSocketOps final : public SocketOps {
 public:
  int socket(int domain, int type, int protocol) noexcept override;
  int set_option(int fd, int level, int name, const void* value,
                 socklen_t size) noexcept override;
  int interface_index(int fd, const std::string& name) noexcept override;
  int bind(int fd, const sockaddr_can& address) noexcept override;
  int poll_readable(int fd, int timeout_ms) noexcept override;
  ssize_t read(int fd, canfd_frame& frame) noexcept override;
  ssize_t write(int fd, const canfd_frame& frame) noexcept override;
  int close(int fd) noexcept override;
  int last_error() const noexcept override;
};

class SocketCanFdTransport final : public CanFdTransport {
 public:
  explicit SocketCanFdTransport(
      std::shared_ptr<SocketOps> ops = std::make_shared<LinuxSocketOps>());
  ~SocketCanFdTransport() override;
  bool open(const std::string& interface,
            const std::vector<std::uint32_t>& standard_ids) override;
  bool transmit(const CanFdFrame& frame) noexcept override;
  void set_receive_callback(ReceiveCallback callback) override;
  void clear_receive_callback() noexcept override;
  void close() noexcept override;

 private:
  void receive_loop_() noexcept;
  void dispatch_(const canfd_frame& frame) noexcept;

  std::shared_ptr<SocketOps> ops_;
  int fd_{-1};
  std::atomic<bool> running_{false};
  std::thread receive_thread_;
  std::mutex transmit_mutex_;
  std::mutex callback_mutex_;
  ReceiveCallback receive_callback_;
  CallbackGate receive_gate_;
};

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 4: Implement exact SocketCAN semantics**

Create `src/protocol/socket_canfd_transport.cpp` with the complete implementation below. Keep the short log messages, because syscall failures otherwise disappear behind a `bool` API:

```cpp
#include "protocol/socket_canfd_transport.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace roboparty::dexhand::detail {

int LinuxSocketOps::socket(int domain, int type, int protocol) noexcept {
  return ::socket(domain, type, protocol);
}
int LinuxSocketOps::set_option(int fd, int level, int name, const void* value,
                               socklen_t size) noexcept {
  return ::setsockopt(fd, level, name, value, size);
}
int LinuxSocketOps::interface_index(int fd,
                                    const std::string& name) noexcept {
  if (name.empty() || name.size() >= IFNAMSIZ) {
    errno = ENAMETOOLONG;
    return -1;
  }
  ifreq request{};
  std::memcpy(request.ifr_name, name.c_str(), name.size() + 1);
  if (::ioctl(fd, SIOCGIFINDEX, &request) < 0) return -1;
  return request.ifr_ifindex;
}
int LinuxSocketOps::bind(int fd, const sockaddr_can& address) noexcept {
  return ::bind(fd, reinterpret_cast<const sockaddr*>(&address),
                sizeof(address));
}
int LinuxSocketOps::poll_readable(int fd, int timeout_ms) noexcept {
  pollfd descriptor{fd, POLLIN, 0};
  return ::poll(&descriptor, 1, timeout_ms);
}
ssize_t LinuxSocketOps::read(int fd, canfd_frame& frame) noexcept {
  return ::read(fd, &frame, CANFD_MTU);
}
ssize_t LinuxSocketOps::write(int fd, const canfd_frame& frame) noexcept {
  return ::write(fd, &frame, CANFD_MTU);
}
int LinuxSocketOps::close(int fd) noexcept { return ::close(fd); }
int LinuxSocketOps::last_error() const noexcept { return errno; }

SocketCanFdTransport::SocketCanFdTransport(std::shared_ptr<SocketOps> ops)
    : ops_(std::move(ops)) {
  if (!ops_) throw std::invalid_argument("SocketOps must not be null");
}
SocketCanFdTransport::~SocketCanFdTransport() { close(); }

bool SocketCanFdTransport::open(
    const std::string& interface,
    const std::vector<std::uint32_t>& standard_ids) {
  close();
  if (interface.empty() || standard_ids.empty() ||
      std::any_of(standard_ids.begin(), standard_ids.end(),
                  [](std::uint32_t id) { return id > CAN_SFF_MASK; })) {
    spdlog::error("Invalid CAN-FD interface or standard filter list");
    return false;
  }

  const int candidate = ops_->socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  if (candidate < 0) {
    spdlog::error("SocketCAN socket failed: errno={}", ops_->last_error());
    return false;
  }
  auto fail = [&](const char* operation) {
    spdlog::error("SocketCAN {} failed on {}: errno={}", operation, interface,
                  ops_->last_error());
    ops_->close(candidate);
    return false;
  };

  const int enabled = 1;
  if (ops_->set_option(candidate, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enabled,
                       sizeof(enabled)) < 0) {
    return fail("CAN_RAW_FD_FRAMES");
  }

  std::vector<can_filter> filters;
  filters.reserve(standard_ids.size());
  for (std::uint32_t id : standard_ids) {
    filters.push_back(
        can_filter{id, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG});
  }
  if (ops_->set_option(candidate, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                       static_cast<socklen_t>(filters.size() *
                                              sizeof(can_filter))) < 0) {
    return fail("CAN_RAW_FILTER");
  }

  const int index = ops_->interface_index(candidate, interface);
  if (index < 0) return fail("SIOCGIFINDEX");
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = index;
  if (ops_->bind(candidate, address) < 0) return fail("bind");

  fd_ = candidate;
  running_.store(true, std::memory_order_release);
  try {
    receive_thread_ = std::thread(&SocketCanFdTransport::receive_loop_, this);
  } catch (const std::exception& error) {
    spdlog::error("SocketCAN receive thread failed: {}", error.what());
    running_.store(false, std::memory_order_release);
    fd_ = -1;
    ops_->close(candidate);
    return false;
  }
  return true;
}

bool SocketCanFdTransport::transmit(const CanFdFrame& source) noexcept {
  if (source.len > source.data.size()) return false;
  if (source.extended) {
    if (source.id > CAN_EFF_MASK) return false;
  } else if (source.id > CAN_SFF_MASK) {
    return false;
  }

  canfd_frame frame{};
  frame.can_id = source.extended ? (source.id | CAN_EFF_FLAG) : source.id;
  frame.len = source.len;
  frame.flags = source.brs ? CANFD_BRS : 0;
  std::copy_n(source.data.begin(), source.len, frame.data);

  std::lock_guard<std::mutex> lock(transmit_mutex_);
  if (!running_.load(std::memory_order_acquire) || fd_ < 0) return false;
  const auto written = ops_->write(fd_, frame);
  if (written != CANFD_MTU) {
    spdlog::error("SocketCAN write failed or short: result={}, errno={}",
                  written, ops_->last_error());
    return false;
  }
  return true;
}

void SocketCanFdTransport::set_receive_callback(ReceiveCallback callback) {
  receive_gate_.close_and_wait();
  const bool enable = static_cast<bool>(callback);
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    receive_callback_ = std::move(callback);
  }
  if (enable) receive_gate_.open();
}

void SocketCanFdTransport::clear_receive_callback() noexcept {
  receive_gate_.close_and_wait();
  std::lock_guard<std::mutex> lock(callback_mutex_);
  receive_callback_ = {};
}

void SocketCanFdTransport::dispatch_(const canfd_frame& source) noexcept {
  auto lease = receive_gate_.try_enter();
  if (!lease) return;
  ReceiveCallback callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback = receive_callback_;
  }
  if (!callback) return;
  CanFdFrame frame;
  frame.extended = (source.can_id & CAN_EFF_FLAG) != 0;
  frame.id = source.can_id & (frame.extended ? CAN_EFF_MASK : CAN_SFF_MASK);
  frame.brs = (source.flags & CANFD_BRS) != 0;
  frame.len = static_cast<std::uint8_t>(
      std::min<std::size_t>(source.len, frame.data.size()));
  std::copy_n(source.data, frame.len, frame.data.begin());
  try { callback(frame); } catch (...) { }
}

void SocketCanFdTransport::receive_loop_() noexcept {
  while (running_.load(std::memory_order_acquire)) {
    const int ready = ops_->poll_readable(fd_, 50);
    if (!running_.load(std::memory_order_acquire)) break;
    if (ready == 0) continue;
    if (ready < 0) {
      if (ops_->last_error() != EINTR) {
        spdlog::error("SocketCAN poll failed: errno={}", ops_->last_error());
      }
      continue;
    }

    canfd_frame frame{};
    const auto bytes = ops_->read(fd_, frame);
    if (bytes == CANFD_MTU) {
      dispatch_(frame);
    } else if (bytes < 0 && ops_->last_error() != EAGAIN &&
               ops_->last_error() != EWOULDBLOCK) {
      spdlog::error("SocketCAN read failed: errno={}", ops_->last_error());
    }
  }
}

void SocketCanFdTransport::close() noexcept {
  clear_receive_callback();
  running_.store(false, std::memory_order_release);
  if (receive_thread_.joinable()) receive_thread_.join();
  std::lock_guard<std::mutex> lock(transmit_mutex_);
  if (fd_ >= 0) {
    ops_->close(fd_);
    fd_ = -1;
  }
}

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 5: Complete the fake syscall matrix and run the transport test**

Create `tests/fakes/fake_socket_ops.hpp` with this complete fake:

```cpp
#pragma once

#include "protocol/socket_canfd_transport.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeSocketOps final : public SocketOps {
 public:
  int socket_result{42};
  int fd_frames_result{0};
  int filters_result{0};
  int interface_index_result{7};
  int bind_result{0};
  int poll_result{0};
  ssize_t write_result{CANFD_MTU};
  int error_number{EAGAIN};
  int close_calls{0};
  int recv_own_msgs_calls{0};
  int fd_frames_calls{0};
  std::string interface_name;
  std::vector<can_filter> filters;
  canfd_frame last_written{};

  int socket(int, int, int) noexcept override { return socket_result; }
  int set_option(int, int, int name, const void* value,
                 socklen_t size) noexcept override {
    if (name == CAN_RAW_RECV_OWN_MSGS) ++recv_own_msgs_calls;
    if (name == CAN_RAW_FD_FRAMES) {
      ++fd_frames_calls;
      return fd_frames_result;
    }
    if (name == CAN_RAW_FILTER) {
      const auto* first = static_cast<const can_filter*>(value);
      filters.assign(first, first + size / sizeof(can_filter));
      return filters_result;
    }
    return 0;
  }
  int interface_index(int, const std::string& name) noexcept override {
    interface_name = name;
    return interface_index_result;
  }
  int bind(int, const sockaddr_can&) noexcept override { return bind_result; }
  int poll_readable(int, int timeout_ms) noexcept override {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (!incoming_.empty()) return 1;
    }
    if (poll_result != 0) return poll_result;
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    return 0;
  }
  ssize_t read(int, canfd_frame& frame) noexcept override {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (incoming_.empty()) {
      error_number = EAGAIN;
      return -1;
    }
    frame = incoming_.front();
    incoming_.pop_front();
    return CANFD_MTU;
  }
  ssize_t write(int, const canfd_frame& frame) noexcept override {
    last_written = frame;
    return write_result;
  }
  int close(int) noexcept override {
    ++close_calls;
    return 0;
  }
  int last_error() const noexcept override { return error_number; }
  void queue(canfd_frame frame) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    incoming_.push_back(frame);
  }

 private:
  std::mutex queue_mutex_;
  std::deque<canfd_frame> incoming_;
};

}  // namespace roboparty::dexhand::detail
```

Then run:

```bash
c++ -std=c++17 -pthread -Isrc -Itests tests/test_canfd_transport.cpp \
  src/protocol/socket_canfd_transport.cpp \
  $(pkg-config --cflags --libs spdlog fmt) -o /tmp/dexhand_transport_test
/tmp/dexhand_transport_test
```

Expected: PASS for full write, short write, BRS propagation, exact filters, no own-message option, close idempotence, and closed transmit. Extend the same test with one failure assertion for every `SocketOps` setup call and verify each failure closes exactly once.

- [ ] **Step 6: Commit the private transport**

```bash
git add src/protocol tests/fakes/fake_socket_ops.hpp tests/test_canfd_transport.cpp
git commit \
  -m "Give dexhand an independently testable CAN-FD transport" \
  -m "Bind a private nonblocking SocketCAN socket with exact feedback filters and synchronous writes whose result reaches the vendor callback." \
  -m "Rejected: reuse MotorsCANFD | it is a private target and violates module ownership" \
  -m "Confidence: high" \
  -m "Scope-risk: moderate" \
  -m "Tested: injected socket setup, filter, frame conversion, write, callback, and close paths" \
  -m "Not-tested: kernel vcan behavior, covered by the release gate"
```

### Task 5: Isolate The Vendor C API Behind An Owned SDK Adapter

**Files:**
- Create: `src/drivers/lhandpro/lhandpro_sdk.hpp`
- Create: `src/drivers/lhandpro/lhandpro_sdk.cpp`
- Create: `tests/fakes/fake_lhandpro_sdk.hpp`
- Create: `tests/test_lhandpro_sdk.cpp`

- [ ] **Step 1: Write a hardware-free ownership/model probe against the adapter**

Create `tests/test_lhandpro_sdk.cpp` with:

```cpp
#include "drivers/lhandpro/lhandpro_sdk.hpp"
#include "test_support.hpp"

using roboparty::dexhand::detail::CapiLHandProSdk;

int main() {
  CapiLHandProSdk sdk;
  CHECK(sdk.create());
  CHECK_EQ(sdk.set_hand_type(2), 0);
  int model = -1;
  CHECK_EQ(sdk.get_hand_type(model), 0);
  CHECK_EQ(model, 2);
  sdk.destroy();
  sdk.destroy();
  return 0;
}
```

- [ ] **Step 2: Compile and confirm the missing-adapter failure**

Run:

```bash
c++ -std=c++17 -Isrc -Itests -Ithirdparty/include \
  tests/test_lhandpro_sdk.cpp src/drivers/lhandpro/lhandpro_sdk.cpp \
  -Lthirdparty/lib/x86_64 -Wl,-rpath,"$PWD/thirdparty/lib/x86_64" \
  -lLHandProLib -o /tmp/dexhand_sdk_test
```

Expected: compilation fails because `lhandpro_sdk.hpp/.cpp` do not exist.

- [ ] **Step 3: Define the complete adapter surface**

Create `src/drivers/lhandpro/lhandpro_sdk.hpp` with:

```cpp
#pragma once

namespace roboparty::dexhand::detail {

class LHandProSdk {
 public:
  using TxCallback = bool (*)(unsigned int, const unsigned char*, unsigned int,
                              int);
  virtual ~LHandProSdk() = default;
  virtual bool create() noexcept = 0;
  virtual void destroy() noexcept = 0;
  virtual int set_hand_type(int type) noexcept = 0;
  virtual int get_hand_type(int& type) noexcept = 0;
  virtual void set_send_canfd_callback(TxCallback callback) noexcept = 0;
  virtual int initial_ex(int mode, int node_id) noexcept = 0;
  virtual void start_monitor() noexcept = 0;
  virtual void stop_monitor() noexcept = 0;
  virtual void close() noexcept = 0;
  virtual int decode_canfd(unsigned int id, const unsigned char* data,
                           int size) noexcept = 0;
  virtual int get_dof(int& total, int& active) noexcept = 0;
  virtual int move_motors(int id) noexcept = 0;
  virtual int stop_motors(int id) noexcept = 0;
  virtual int set_target_position(int id, int position) noexcept = 0;
  virtual int set_target_angle(int id, float angle) noexcept = 0;
  virtual int set_position_velocity(int id, int velocity) noexcept = 0;
  virtual int set_max_current(int id, int current) noexcept = 0;
  virtual int set_enable(int id, bool enable) noexcept = 0;
  virtual int home_motors(int id) noexcept = 0;
  virtual int set_move_no_home(int enable) noexcept = 0;
  virtual int get_now_position(int id, int& value) noexcept = 0;
  virtual int get_now_angle(int id, float& value) noexcept = 0;
  virtual int get_now_status(int id, int& value) noexcept = 0;
  virtual int get_now_current(int id, int& value) noexcept = 0;
  virtual int get_now_alarm(int id, int& value) noexcept = 0;
  virtual int clear_alarm(int id) noexcept = 0;
};

class CapiLHandProSdk final : public LHandProSdk {
 public:
  ~CapiLHandProSdk() override;
  bool create() noexcept override;
  void destroy() noexcept override;
  int set_hand_type(int type) noexcept override;
  int get_hand_type(int& type) noexcept override;
  void set_send_canfd_callback(TxCallback callback) noexcept override;
  int initial_ex(int mode, int node_id) noexcept override;
  void start_monitor() noexcept override;
  void stop_monitor() noexcept override;
  void close() noexcept override;
  int decode_canfd(unsigned int id, const unsigned char* data,
                   int size) noexcept override;
  int get_dof(int& total, int& active) noexcept override;
  int move_motors(int id) noexcept override;
  int stop_motors(int id) noexcept override;
  int set_target_position(int id, int position) noexcept override;
  int set_target_angle(int id, float angle) noexcept override;
  int set_position_velocity(int id, int velocity) noexcept override;
  int set_max_current(int id, int current) noexcept override;
  int set_enable(int id, bool enable) noexcept override;
  int home_motors(int id) noexcept override;
  int set_move_no_home(int enable) noexcept override;
  int get_now_position(int id, int& value) noexcept override;
  int get_now_angle(int id, float& value) noexcept override;
  int get_now_status(int id, int& value) noexcept override;
  int get_now_current(int id, int& value) noexcept override;
  int get_now_alarm(int id, int& value) noexcept override;
  int clear_alarm(int id) noexcept override;

 private:
  void* handle_{nullptr};
};

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 4: Implement direct, idempotent C API forwarding**

In `src/drivers/lhandpro/lhandpro_sdk.cpp`, include
`<LHandProLib/LHandProLib.h>`, cast `handle_` only through this helper, and use
`-1` only for the impossible invalid-handle guard:

```cpp
#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <LHandProLib/LHandProLib.h>

namespace roboparty::dexhand::detail {
namespace {
constexpr int kInvalidHandle = -1;
lhandprolib_handle as_handle(void* handle) noexcept {
  return static_cast<lhandprolib_handle>(handle);
}
}

CapiLHandProSdk::~CapiLHandProSdk() { destroy(); }

bool CapiLHandProSdk::create() noexcept {
  if (handle_) return true;
  handle_ = lhandprolib_create();
  return handle_ != nullptr;
}

void CapiLHandProSdk::destroy() noexcept {
  if (!handle_) return;
  lhandprolib_destroy(as_handle(handle_));
  handle_ = nullptr;
}

int CapiLHandProSdk::set_hand_type(int type) noexcept {
  return handle_ ? lhandprolib_set_hand_type(as_handle(handle_), type)
                 : kInvalidHandle;
}
int CapiLHandProSdk::get_hand_type(int& type) noexcept {
  return handle_ ? lhandprolib_get_hand_type(as_handle(handle_), &type)
                 : kInvalidHandle;
}
void CapiLHandProSdk::set_send_canfd_callback(TxCallback callback) noexcept {
  if (handle_) lhandprolib_set_send_canfd_callback(as_handle(handle_), callback);
}
int CapiLHandProSdk::initial_ex(int mode, int node_id) noexcept {
  return handle_ ? lhandprolib_initial_ex(as_handle(handle_), mode, node_id)
                 : kInvalidHandle;
}
void CapiLHandProSdk::start_monitor() noexcept {
  if (handle_) lhandprolib_start_monitor(as_handle(handle_));
}
void CapiLHandProSdk::stop_monitor() noexcept {
  if (handle_) lhandprolib_stop_monitor(as_handle(handle_));
}
void CapiLHandProSdk::close() noexcept {
  if (handle_) lhandprolib_close(as_handle(handle_));
}
int CapiLHandProSdk::decode_canfd(unsigned int id,
                                  const unsigned char* data,
                                  int size) noexcept {
  return handle_ ? lhandprolib_set_canfd_data_decode(as_handle(handle_), id,
                                                      data, size)
                 : kInvalidHandle;
}
int CapiLHandProSdk::get_dof(int& total, int& active) noexcept {
  return handle_ ? lhandprolib_get_dof(as_handle(handle_), &total, &active)
                 : kInvalidHandle;
}
```

Append these exact direct forwarders; each preserves the C return code:

```cpp
int CapiLHandProSdk::move_motors(int id) noexcept {
  return handle_ ? lhandprolib_move_motors(as_handle(handle_), id) : -1;
}
int CapiLHandProSdk::stop_motors(int id) noexcept {
  return handle_ ? lhandprolib_stop_motors(as_handle(handle_), id) : -1;
}
int CapiLHandProSdk::set_target_position(int id, int value) noexcept {
  return handle_ ? lhandprolib_set_target_position(as_handle(handle_), id, value) : -1;
}
int CapiLHandProSdk::set_target_angle(int id, float value) noexcept {
  return handle_ ? lhandprolib_set_target_angle(as_handle(handle_), id, value) : -1;
}
int CapiLHandProSdk::set_position_velocity(int id, int value) noexcept {
  return handle_ ? lhandprolib_set_position_velocity(as_handle(handle_), id, value) : -1;
}
int CapiLHandProSdk::set_max_current(int id, int value) noexcept {
  return handle_ ? lhandprolib_set_max_current(as_handle(handle_), id, value) : -1;
}
int CapiLHandProSdk::set_enable(int id, bool enable) noexcept {
  return handle_ ? lhandprolib_set_enable(as_handle(handle_), id, enable ? 1 : 0) : -1;
}
int CapiLHandProSdk::home_motors(int id) noexcept {
  return handle_ ? lhandprolib_home_motors(as_handle(handle_), id) : -1;
}
int CapiLHandProSdk::set_move_no_home(int enable) noexcept {
  return handle_ ? lhandprolib_set_move_no_home(as_handle(handle_), enable) : -1;
}
int CapiLHandProSdk::get_now_position(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_position(as_handle(handle_), id, &value) : -1;
}
int CapiLHandProSdk::get_now_angle(int id, float& value) noexcept {
  return handle_ ? lhandprolib_get_now_angle(as_handle(handle_), id, &value) : -1;
}
int CapiLHandProSdk::get_now_status(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_status(as_handle(handle_), id, &value) : -1;
}
int CapiLHandProSdk::get_now_current(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_current(as_handle(handle_), id, &value) : -1;
}
int CapiLHandProSdk::get_now_alarm(int id, int& value) noexcept {
  return handle_ ? lhandprolib_get_now_alarm(as_handle(handle_), id, &value) : -1;
}
int CapiLHandProSdk::clear_alarm(int id) noexcept {
  return handle_ ? lhandprolib_set_clear_alarm(as_handle(handle_), id) : -1;
}

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 5: Add the programmable fake SDK**

Create `tests/fakes/fake_lhandpro_sdk.hpp` with this complete programmable fake:

```cpp
#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeLHandProSdk final : public LHandProSdk {
 public:
  std::string fail_operation;
  int hand_type{0};
  int total_dof{6};
  int active_dof{6};
  TxCallback tx_callback{nullptr};
  bool created{false};
  bool monitor_started{false};
  int last_mode{-1};
  int last_node{-1};
  std::function<void(const std::string&)> before_call;
  std::function<void()> during_initial_ex;
  int reported_hand_type_override{-1};

  std::vector<std::string> event_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
  }
  int count(const std::string& operation) const {
    const auto copy = event_snapshot();
    return static_cast<int>(std::count(copy.begin(), copy.end(), operation));
  }

  bool create() noexcept override {
    record_("create");
    if (fail_operation == "create") return false;
    created = true;
    return true;
  }
  void destroy() noexcept override {
    if (!created) return;
    record_("destroy");
    created = false;
  }
  int set_hand_type(int type) noexcept override {
    const int code = result_("set_hand_type");
    if (code == 0) hand_type = type;
    return code;
  }
  int get_hand_type(int& type) noexcept override {
    const int code = result_("get_hand_type");
    if (code == 0) {
      type = reported_hand_type_override >= 0 ? reported_hand_type_override
                                              : hand_type;
    }
    return code;
  }
  void set_send_canfd_callback(TxCallback callback) noexcept override {
    record_(callback ? "install_tx" : "clear_tx");
    tx_callback = callback;
  }
  int initial_ex(int mode, int node_id) noexcept override {
    last_mode = mode;
    last_node = node_id;
    try {
      if (during_initial_ex) during_initial_ex();
    } catch (...) {
      return 7;
    }
    return result_("initial_ex");
  }
  void start_monitor() noexcept override {
    record_("start_monitor");
    monitor_started = true;
  }
  void stop_monitor() noexcept override {
    if (!monitor_started) return;
    record_("stop_monitor");
    monitor_started = false;
  }
  void close() noexcept override { record_("close"); }
  int decode_canfd(unsigned int, const unsigned char*, int size) noexcept override {
    last_decode_size = size;
    return result_("decode_canfd");
  }
  int get_dof(int& total, int& active) noexcept override {
    const int code = result_("get_dof");
    if (code == 0) {
      total = total_dof;
      active = active_dof;
    }
    return code;
  }
  int move_motors(int) noexcept override { return result_("move_motors"); }
  int stop_motors(int) noexcept override { return result_("stop_motors"); }
  int set_target_position(int, int) noexcept override {
    return result_("set_target_position");
  }
  int set_target_angle(int, float) noexcept override {
    return result_("set_target_angle");
  }
  int set_position_velocity(int, int) noexcept override {
    return result_("set_position_velocity");
  }
  int set_max_current(int, int) noexcept override {
    return result_("set_max_current");
  }
  int set_enable(int, bool) noexcept override { return result_("set_enable"); }
  int home_motors(int) noexcept override { return result_("home_motors"); }
  int set_move_no_home(int) noexcept override {
    return result_("set_move_no_home");
  }
  int get_now_position(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_position");
  }
  int get_now_angle(int, float& value) noexcept override {
    value = angle_feedback;
    return result_("get_now_angle");
  }
  int get_now_status(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_status");
  }
  int get_now_current(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_current");
  }
  int get_now_alarm(int, int& value) noexcept override {
    value = int_feedback;
    return result_("get_now_alarm");
  }
  int clear_alarm(int) noexcept override { return result_("clear_alarm"); }

  int last_decode_size{-1};
  int int_feedback{123};
  float angle_feedback{12.5F};

 private:
  int result_(const std::string& operation) noexcept {
    record_(operation);
    return fail_operation == operation ? 7 : 0;
  }
  void record_(const std::string& operation) noexcept {
    try {
      if (before_call) before_call(operation);
      std::lock_guard<std::mutex> lock(mutex_);
      events_.push_back(operation);
    } catch (...) {
    }
  }

  mutable std::mutex mutex_;
  std::vector<std::string> events_;
};

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 6: Run the adapter ownership/model probe**

Run the compile command from Step 2, then run:

```bash
/tmp/dexhand_sdk_test
```

Expected: exits zero without opening CAN, enabling, homing, or moving hardware.

- [ ] **Step 7: Commit the SDK adapter**

```bash
git add src/drivers/lhandpro/lhandpro_sdk.* tests/fakes/fake_lhandpro_sdk.hpp tests/test_lhandpro_sdk.cpp
git commit \
  -m "Make vendor ownership and return codes explicit" \
  -m "Wrap the handle-owning LHandPro C API so lifecycle rollback and command error handling can be tested without hardware." \
  -m "Constraint: vendor void functions must remain void rather than inventing success codes" \
  -m "Confidence: high" \
  -m "Scope-risk: moderate" \
  -m "Tested: hardware-free handle creation, hand-type round trip, and idempotent destroy"
```

### Task 6: Rebuild LHandProDriver As A Rollback-Safe State Machine

**Files:**
- Modify: `include/hand_driver.hpp` (base `get_dof()` default only)
- Replace: `src/drivers/lhandpro/lhandpro_driver.hpp`
- Replace: `src/drivers/lhandpro/lhandpro_driver.cpp`
- Create: `tests/fakes/fake_canfd_transport.hpp`
- Create: `tests/hand_driver_test_stub.cpp`
- Create: `tests/test_lhandpro_driver.cpp`

- [ ] **Step 1: Add a fake transport with callback quiescence**

Create `tests/fakes/fake_canfd_transport.hpp` with:

```cpp
#pragma once

#include "protocol/callback_gate.hpp"
#include "protocol/canfd_transport.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {

class FakeCanFdTransport final : public CanFdTransport {
 public:
  bool open_result{true};
  bool transmit_result{true};
  std::string open_interface;
  std::vector<std::uint32_t> open_ids;
  std::vector<CanFdFrame> sent_frames;
  int open_calls{0};
  int clear_calls{0};
  int close_calls{0};
  std::function<void()> before_transmit;

  bool open(const std::string& interface,
            const std::vector<std::uint32_t>& ids) override {
    ++open_calls;
    open_interface = interface;
    open_ids = ids;
    is_open_.store(open_result);
    return open_result;
  }
  bool transmit(const CanFdFrame& frame) noexcept override {
    if (!is_open_.load()) return false;
    try {
      if (before_transmit) before_transmit();
      std::lock_guard<std::mutex> lock(mutex_);
      sent_frames.push_back(frame);
    } catch (...) {
      return false;
    }
    return transmit_result;
  }
  void set_receive_callback(ReceiveCallback callback) override {
    gate_.close_and_wait();
    const bool enable = static_cast<bool>(callback);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback_ = std::move(callback);
      callback_active_ = enable;
    }
    if (enable) gate_.open();
  }
  void clear_receive_callback() noexcept override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!callback_active_) return;
      callback_active_ = false;
    }
    ++clear_calls;
    gate_.close_and_wait();
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = {};
  }
  void close() noexcept override {
    if (!is_open_.exchange(false)) return;
    clear_receive_callback();
    ++close_calls;
  }
  void deliver(const CanFdFrame& frame) {
    auto lease = gate_.try_enter();
    if (!lease) return;
    ReceiveCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = callback_;
    }
    if (callback) callback(frame);
  }
  bool is_open() const noexcept { return is_open_.load(); }

 private:
  std::atomic<bool> is_open_{false};
  bool callback_active_{false};
  std::mutex mutex_;
  ReceiveCallback callback_;
  CallbackGate gate_;
};

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 2: Write model, rollback, retry, and callback lifetime tests**

Create `tests/test_lhandpro_driver.cpp` with this fixture and test program:

```cpp
#include "drivers/lhandpro/lhandpro_driver.hpp"
#include "fakes/fake_canfd_transport.hpp"
#include "fakes/fake_lhandpro_sdk.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace roboparty::dexhand::detail;

struct Fixture {
  FakeLHandProSdk* sdk{nullptr};
  FakeCanFdTransport* transport{nullptr};
  std::unique_ptr<LHandProDriver> driver;

  explicit Fixture(LHandProModel model = LHandProModel::Dof6) {
    auto sdk_owner = std::make_unique<FakeLHandProSdk>();
    auto transport_owner = std::make_unique<FakeCanFdTransport>();
    sdk = sdk_owner.get();
    transport = transport_owner.get();
    if (model == LHandProModel::Dof16) {
      sdk->total_dof = 16;
      sdk->active_dof = 6;
    }
    driver = std::make_unique<LHandProDriver>(
        "can-test", model, 1, std::move(sdk_owner),
        std::move(transport_owner));
  }

  void fail(const std::string& operation) {
    if (operation == "transport.open") transport->open_result = false;
    else sdk->fail_operation = operation;
  }
  void clear_failure() {
    transport->open_result = true;
    sdk->fail_operation.clear();
  }
  bool released_once() const {
    return driver->state_for_test() == DriverState::Created && !sdk->created &&
           !transport->is_open() && sdk->count("destroy") <= 1 &&
           transport->close_calls <= 1;
  }
};

void check_failure_rollback_and_retry() {
const std::vector<std::string> failures = {
    "create", "set_hand_type", "get_hand_type", "transport.open",
    "initial_ex", "get_dof", "set_enable", "home_motors",
    "set_move_no_home"};

for (const auto& failure : failures) {
  Fixture fixture;
  fixture.fail(failure);
  CHECK(!fixture.driver->init_hand(failure == "set_enable",
                                   failure == "home_motors", 0.0F));
  CHECK(fixture.released_once());
  fixture.clear_failure();
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  fixture.driver->deinit_hand();
  fixture.driver->deinit_hand();
}
}

void check_models_and_initializing_callbacks() {
  Fixture six;
  six.sdk->during_initial_ex = [&] {
    const unsigned char command[9]{};
    CHECK(six.sdk->tx_callback != nullptr);
    CHECK(six.sdk->tx_callback(0x601, command, sizeof(command), 0));
    CanFdFrame feedback;
    feedback.id = 0x501;
    feedback.len = 5;
    six.transport->deliver(feedback);
  };
  CHECK(six.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(six.sdk->hand_type, 0);
  CHECK_EQ(six.sdk->last_mode, 1);
  CHECK_EQ(six.sdk->last_node, 1);
  CHECK_EQ(six.sdk->last_decode_size, 5);
  CHECK_EQ(six.transport->sent_frames.size(), 1U);
  CHECK(six.transport->sent_frames.front().brs);
  CHECK_EQ(six.transport->open_ids,
           (std::vector<std::uint32_t>{0x501, 0x481, 0x581, 0x181}));
  int total = 0;
  int active = 0;
  six.driver->get_dof(total, active);
  CHECK_EQ(total, 6);
  CHECK_EQ(active, 6);
  six.driver->deinit_hand();

  Fixture sixteen(LHandProModel::Dof16);
  CHECK(sixteen.driver->init_hand(false, false, 0.0F));
  CHECK_EQ(sixteen.sdk->hand_type, 2);
  sixteen.driver->get_dof(total, active);
  CHECK_EQ(total, 16);
  CHECK(active > 0 && active <= total);
  sixteen.driver->deinit_hand();

  Fixture wrong_model;
  wrong_model.sdk->reported_hand_type_override = 2;
  CHECK(!wrong_model.driver->init_hand(false, false, 0.0F));
  CHECK(wrong_model.released_once());

  Fixture wrong_dof;
  wrong_dof.sdk->total_dof = 16;
  CHECK(!wrong_dof.driver->init_hand(false, false, 0.0F));
  CHECK(wrong_dof.released_once());

  Fixture no_active_dof;
  no_active_dof.sdk->active_dof = 0;
  CHECK(!no_active_dof.driver->init_hand(false, false, 0.0F));
  CHECK(no_active_dof.released_once());
}

void check_rx_drain() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  std::promise<void> entered;
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "decode_canfd") {
      entered.set_value();
      release.wait();
    }
  };
  CanFdFrame frame;
  frame.id = 0x501;
  frame.len = 7;
  auto receive = std::async(std::launch::async,
                            [&] { fixture.transport->deliver(frame); });
  CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(cleanup.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  CHECK_EQ(fixture.sdk->count("close"), 0);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);
  release_promise.set_value();
  CHECK(receive.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  CHECK(cleanup.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  CHECK_EQ(fixture.sdk->last_decode_size, 7);
}

void check_tx_drain() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  auto callback = fixture.sdk->tx_callback;
  CHECK(callback != nullptr);
  std::promise<void> entered;
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  fixture.transport->before_transmit = [&] {
    entered.set_value();
    release.wait();
  };
  const unsigned char data[8]{};
  auto send = std::async(std::launch::async,
                         [&] { return callback(0x601, data, 8, 0); });
  CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(cleanup.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  CHECK_EQ(fixture.transport->close_calls, 0);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);
  release_promise.set_value();
  CHECK(send.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  CHECK(send.get());
  CHECK(cleanup.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  const auto sent = fixture.transport->sent_frames.size();
  CHECK(!callback(0x601, data, 8, 0));
  CHECK_EQ(fixture.transport->sent_frames.size(), sent);
}

void check_public_call_drain() {
  Fixture fixture;
  CHECK(fixture.driver->init_hand(false, false, 0.0F));
  std::promise<void> entered;
  std::promise<void> release_promise;
  auto release = release_promise.get_future().share();
  fixture.sdk->before_call = [&](const std::string& operation) {
    if (operation == "move_motors") {
      entered.set_value();
      release.wait();
    }
  };
  auto move = std::async(std::launch::async,
                         [&] { fixture.driver->move_motors(1); });
  CHECK(entered.get_future().wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  auto cleanup = std::async(std::launch::async,
                            [&] { fixture.driver->deinit_hand(); });
  CHECK(cleanup.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  CHECK_EQ(fixture.driver->state_for_test(), DriverState::Stopping);
  const int stop_calls = fixture.sdk->count("stop_motors");
  fixture.driver->stop_motors(1);
  CHECK_EQ(fixture.sdk->count("stop_motors"), stop_calls);
  CHECK_EQ(fixture.sdk->count("destroy"), 0);
  release_promise.set_value();
  CHECK(move.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  CHECK(cleanup.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
}

void check_single_active_instance() {
  Fixture first;
  Fixture second;
  std::promise<void> start_promise;
  auto start = start_promise.get_future().share();
  auto first_init = std::async(std::launch::async, [&] {
    start.wait();
    return first.driver->init_hand(false, false, 0.0F);
  });
  auto second_init = std::async(std::launch::async, [&] {
    start.wait();
    return second.driver->init_hand(false, false, 0.0F);
  });
  start_promise.set_value();
  const bool first_won = first_init.get();
  const bool second_won = second_init.get();
  CHECK(first_won != second_won);
  Fixture* winner = first_won ? &first : &second;
  Fixture* loser = first_won ? &second : &first;
  winner->driver->deinit_hand();
  CHECK(loser->driver->init_hand(false, false, 0.0F));
  loser->driver->deinit_hand();
}

int main() {
  check_failure_rollback_and_retry();
  check_models_and_initializing_callbacks();
  check_rx_drain();
  check_tx_drain();
  check_public_call_drain();
  check_single_active_instance();
  return 0;
}
```

- [ ] **Step 3: Compile the tests and observe failure against the old driver**

Create `tests/hand_driver_test_stub.cpp` for the pre-integration direct-link test only:

```cpp
#include "hand_driver.hpp"

#include <spdlog/spdlog.h>

HandDriver::HandDriver() : logger_(spdlog::default_logger()) {}
```

Change the base declaration of `get_dof()` now so the rewritten driver is concrete:

```cpp
virtual void get_dof(int& total, int& active) {
  total = dof_total_;
  active = dof_active_;
}
```

Run:

```bash
c++ -std=c++17 -pthread -Iinclude -Isrc -Itests -Ithirdparty/include \
  tests/test_lhandpro_driver.cpp src/drivers/lhandpro/lhandpro_driver.cpp \
  src/drivers/lhandpro/lhandpro_sdk.cpp src/protocol/socket_canfd_transport.cpp \
  tests/hand_driver_test_stub.cpp \
  $(pkg-config --cflags --libs spdlog fmt) \
  -Lthirdparty/lib/x86_64 -Wl,-rpath,"$PWD/thirdparty/lib/x86_64" \
  -lLHandProLib -o /tmp/dexhand_driver_test
```

Expected: compile failure because the old driver has no injection constructor, state enum, or private transport ownership.

- [ ] **Step 4: Define the driver state and ownership surface**

Replace `src/drivers/lhandpro/lhandpro_driver.hpp` with this complete internal header; override declarations deliberately do not repeat base default arguments:

```cpp
#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"
#include "hand_driver.hpp"
#include "protocol/canfd_transport.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace roboparty::dexhand::detail {
enum class LHandProModel { Dof6, Dof16 };
enum class DriverState { Created, Initializing, Ready, Stopping };
struct TxContext;
}

/**
 * @brief LHandPro CAN-FD driver with a privately owned SocketCAN transport.
 *
 * One LHandPro may be active per process because the vendor transmit callback
 * has no handle or user-data parameter. Other libraries may bind independent
 * sockets to the same Linux CAN interface.
 */
class LHandProDriver final : public HandDriver {
 public:
  LHandProDriver(std::string can_interface,
                 roboparty::dexhand::detail::LHandProModel model,
                 int canfd_node_id);
  LHandProDriver(
      std::string can_interface,
      roboparty::dexhand::detail::LHandProModel model,
      int canfd_node_id,
      std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk,
      std::unique_ptr<roboparty::dexhand::detail::CanFdTransport> transport);
  ~LHandProDriver() override;

  bool init_hand(bool enable_motors, bool home_motors,
                 float home_wait_time) override;
  void deinit_hand() override;
  void move_motors(int finger_id) override;
  void stop_motors(int finger_id) override;
  void set_target_position(int finger_id, int position) override;
  void set_target_angle(int finger_id, float angle) override;
  void set_position_velocity(int finger_id, int velocity) override;
  void set_max_current(int finger_id, int current) override;
  void set_enable(int finger_id, bool enable) override;
  void home_motors(int finger_id) override;
  void set_move_no_home(int enable) override;
  int get_now_position(int finger_id) override;
  float get_now_angle(int finger_id) override;
  int get_now_status(int finger_id) override;
  int get_now_current(int finger_id) override;
  int get_now_alarm(int finger_id) override;
  void clear_alarm(int finger_id) override;

  roboparty::dexhand::detail::DriverState state_for_test() const noexcept {
    return state_.load(std::memory_order_acquire);
  }

 private:
  void cleanup_locked_() noexcept;
  bool sdk_ok_(int code, const char* operation) const noexcept;
  bool ready_() const noexcept;
  int expected_vendor_model_() const noexcept;
  int expected_total_dof_() const noexcept;

  roboparty::dexhand::detail::LHandProModel model_;
  std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk_;
  std::unique_ptr<roboparty::dexhand::detail::CanFdTransport> transport_;
  std::atomic<roboparty::dexhand::detail::DriverState> state_;
  std::mutex lifecycle_mutex_;
  std::mutex sdk_call_mutex_;
  bool sdk_created_{false};
  bool transport_open_{false};
  bool tx_callback_installed_{false};
  bool communication_started_{false};
  bool monitor_started_{false};
  std::shared_ptr<roboparty::dexhand::detail::TxContext> tx_context_;
};
```

- [ ] **Step 5: Implement the process-wide TX context and bridge**

Start `src/drivers/lhandpro/lhandpro_driver.cpp` with the following includes, process-wide context, bridge, and constructors. This translation unit is linked only into `dexhand` SHARED:

```cpp
#include "drivers/lhandpro/lhandpro_driver.hpp"

#include "protocol/callback_gate.hpp"
#include "protocol/socket_canfd_transport.hpp"

#include <linux/can.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace roboparty::dexhand::detail {
struct TxContext {
  CallbackGate gate;
  CanFdTransport* transport{nullptr};
};
}  // namespace roboparty::dexhand::detail

namespace {
using roboparty::dexhand::detail::CallbackGate;
using roboparty::dexhand::detail::CanFdFrame;
using roboparty::dexhand::detail::CanFdTransport;
using roboparty::dexhand::detail::CapiLHandProSdk;
using roboparty::dexhand::detail::DriverState;
using roboparty::dexhand::detail::LHandProModel;
using roboparty::dexhand::detail::SocketCanFdTransport;
using roboparty::dexhand::detail::TxContext;

constexpr int kSdkSuccess = 0;
constexpr int kCanFdMode = 1;

std::mutex active_context_mutex;
std::weak_ptr<TxContext> active_context;

bool transmit_bridge(unsigned int id, const unsigned char* data,
                     unsigned int size, int is_extended) noexcept {
  std::shared_ptr<TxContext> context;
  {
    std::lock_guard<std::mutex> lock(active_context_mutex);
    context = active_context.lock();
  }
  if (!context || !context->transport || (!data && size != 0) || size > 64)
    return false;
  auto lease = context->gate.try_enter();
  if (!lease) return false;

  CanFdFrame frame;
  frame.extended = is_extended != 0;
  if ((frame.extended && id > CAN_EFF_MASK) ||
      (!frame.extended && id > CAN_SFF_MASK)) {
    return false;
  }
  frame.id = id;
  frame.len = static_cast<std::uint8_t>(size);
  frame.brs = size > 8;
  if (size > 0) std::copy_n(data, size, frame.data.begin());
  return context->transport->transmit(frame);
}

bool publish_context(const std::shared_ptr<TxContext>& context) noexcept {
  std::lock_guard<std::mutex> lock(active_context_mutex);
  if (!active_context.expired()) return false;
  context->gate.open();
  active_context = context;
  return true;
}

void unpublish_context(const std::shared_ptr<TxContext>& context) noexcept {
  if (!context) return;
  {
    std::lock_guard<std::mutex> lock(active_context_mutex);
    if (active_context.lock() == context) active_context.reset();
  }
  context->gate.close_and_wait();
}
}  // namespace

LHandProDriver::LHandProDriver(std::string can_interface,
                               LHandProModel model, int canfd_node_id)
    : LHandProDriver(std::move(can_interface), model, canfd_node_id,
                     std::make_unique<CapiLHandProSdk>(),
                     std::make_unique<SocketCanFdTransport>()) {}

LHandProDriver::LHandProDriver(
    std::string can_interface, LHandProModel model, int canfd_node_id,
    std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk,
    std::unique_ptr<CanFdTransport> transport)
    : model_(model),
      sdk_(std::move(sdk)),
      transport_(std::move(transport)),
      state_(DriverState::Created) {
  if (can_interface.empty())
    throw std::invalid_argument("CAN interface must not be empty");
  if (canfd_node_id < 1 || canfd_node_id > 127)
    throw std::invalid_argument("CAN-FD node ID must be in [1, 127]");
  if (!sdk_ || !transport_)
    throw std::invalid_argument("SDK and transport must not be null");
  can_interface_ = std::move(can_interface);
  canfd_node_id_ = canfd_node_id;
  comm_type_ = HandCommType::CANFD;
}

LHandProDriver::~LHandProDriver() { deinit_hand(); }

bool LHandProDriver::ready_() const noexcept {
  return state_.load(std::memory_order_acquire) == DriverState::Ready;
}

bool LHandProDriver::sdk_ok_(int code, const char* operation) const noexcept {
  if (code == kSdkSuccess) return true;
  try { logger_->error("LHandPro {} failed: SDK error={}", operation, code); }
  catch (...) { }
  return false;
}

int LHandProDriver::expected_vendor_model_() const noexcept {
  return model_ == LHandProModel::Dof6 ? 0 : 2;
}

int LHandProDriver::expected_total_dof_() const noexcept {
  return model_ == LHandProModel::Dof6 ? 6 : 16;
}
```

- [ ] **Step 6: Implement initialization in the exact event order**

Append the complete initialization method:

```cpp
bool LHandProDriver::init_hand(bool enable_motors, bool home_motors,
                               float home_wait_time) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  const auto current = state_.load(std::memory_order_acquire);
  if (current == DriverState::Ready) return true;
  if (current != DriverState::Created) return false;
  if (!std::isfinite(home_wait_time) || home_wait_time < 0.0F) {
    try { logger_->error("home_wait_time must be finite and nonnegative"); }
    catch (...) { }
    return false;
  }

  state_.store(DriverState::Initializing, std::memory_order_release);
  tx_context_ = std::make_shared<TxContext>();
  tx_context_->transport = transport_.get();
  if (!publish_context(tx_context_)) {
    tx_context_.reset();
    state_.store(DriverState::Created, std::memory_order_release);
    return false;
  }

  auto fail = [this]() {
    cleanup_locked_();
    return false;
  };

  try {
    if (!sdk_->create()) return fail();
    sdk_created_ = true;

    if (!sdk_ok_(sdk_->set_hand_type(expected_vendor_model_()),
                 "set_hand_type"))
      return fail();
    int reported_model = -1;
    if (!sdk_ok_(sdk_->get_hand_type(reported_model), "get_hand_type") ||
        reported_model != expected_vendor_model_()) {
      return fail();
    }

    const std::vector<std::uint32_t> response_ids{
        static_cast<std::uint32_t>(0x500 + canfd_node_id_),
        static_cast<std::uint32_t>(0x480 + canfd_node_id_),
        static_cast<std::uint32_t>(0x580 + canfd_node_id_),
        static_cast<std::uint32_t>(0x180 + canfd_node_id_)};
    if (!transport_->open(can_interface_, response_ids)) return fail();
    transport_open_ = true;

    transport_->set_receive_callback([this](const CanFdFrame& frame) {
      const auto state = state_.load(std::memory_order_acquire);
      if (state != DriverState::Initializing && state != DriverState::Ready)
        return;
      try {
        const int code = sdk_->decode_canfd(
            frame.id, frame.data.data(), static_cast<int>(frame.len));
        sdk_ok_(code, "decode_canfd");
      } catch (...) {
      }
    });
    sdk_->set_send_canfd_callback(&transmit_bridge);
    tx_callback_installed_ = true;

    communication_started_ = true;
    if (!sdk_ok_(sdk_->initial_ex(kCanFdMode, canfd_node_id_), "initial_ex"))
      return fail();
    sdk_->start_monitor();
    monitor_started_ = true;

    reported_model = -1;
    if (!sdk_ok_(sdk_->get_hand_type(reported_model), "get_hand_type") ||
        reported_model != expected_vendor_model_()) {
      return fail();
    }
    int total = 0;
    int active = 0;
    if (!sdk_ok_(sdk_->get_dof(total, active), "get_dof") ||
        total != expected_total_dof_() || active <= 0 || active > total) {
      return fail();
    }
    dof_total_ = total;
    dof_active_ = active;

    if (enable_motors) {
      if (!sdk_ok_(sdk_->set_enable(0, true), "set_enable")) return fail();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (home_motors) {
      if (!sdk_ok_(sdk_->home_motors(0), "home_motors")) return fail();
      std::this_thread::sleep_for(std::chrono::duration<float>(home_wait_time));
    }
    if (!sdk_ok_(sdk_->set_move_no_home(1), "set_move_no_home"))
      return fail();

    state_.store(DriverState::Ready, std::memory_order_release);
    return true;
  } catch (const std::exception& error) {
    try { logger_->error("LHandPro initialization exception: {}", error.what()); }
    catch (...) { }
    return fail();
  } catch (...) {
    return fail();
  }
}
```

- [ ] **Step 7: Implement idempotent cleanup in the locked order**

Append `deinit_hand()` and `cleanup_locked_()`. The latter assumes `lifecycle_mutex_` is already held:

```cpp
void LHandProDriver::deinit_hand() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (state_.load(std::memory_order_acquire) == DriverState::Created &&
      !sdk_created_ && !transport_open_ && !tx_context_) {
    return;
  }
  cleanup_locked_();
}

void LHandProDriver::cleanup_locked_() noexcept {
  state_.store(DriverState::Stopping, std::memory_order_release);
  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (monitor_started_) {
      sdk_->stop_monitor();
      monitor_started_ = false;
    }
  }

  if (transport_open_) transport_->clear_receive_callback();

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (communication_started_) {
      sdk_->close();
      communication_started_ = false;
    }
    if (tx_callback_installed_) {
      sdk_->set_send_canfd_callback(nullptr);
      tx_callback_installed_ = false;
    }
  }

  unpublish_context(tx_context_);
  if (transport_open_) {
    transport_->close();
    transport_open_ = false;
  }
  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (sdk_created_) {
      sdk_->destroy();
      sdk_created_ = false;
    }
  }
  dof_total_ = 0;
  dof_active_ = 0;
  tx_context_.reset();
  state_.store(DriverState::Created, std::memory_order_release);
}
```

The destructor calls only this non-virtual cleanup path. No cleanup or C callback propagates an exception.

- [ ] **Step 8: Implement all public wrappers with double state checks**

Append every public wrapper exactly as follows:

```cpp
void LHandProDriver::move_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->move_motors(id), "move_motors");
}
void LHandProDriver::stop_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->stop_motors(id), "stop_motors");
}
void LHandProDriver::set_target_position(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_target_position(id, value),
                       "set_target_position");
}
void LHandProDriver::set_target_angle(int id, float value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_target_angle(id, value), "set_target_angle");
}
void LHandProDriver::set_position_velocity(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_position_velocity(id, value),
                       "set_position_velocity");
}
void LHandProDriver::set_max_current(int id, int value) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_max_current(id, value), "set_max_current");
}
void LHandProDriver::set_enable(int id, bool enable) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_enable(id, enable), "set_enable");
}
void LHandProDriver::home_motors(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->home_motors(id), "home_motors");
}
void LHandProDriver::set_move_no_home(int enable) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->set_move_no_home(enable), "set_move_no_home");
}
void LHandProDriver::clear_alarm(int id) {
  if (!ready_()) return;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (ready_()) sdk_ok_(sdk_->clear_alarm(id), "clear_alarm");
}

int LHandProDriver::get_now_position(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_position(id, value), "get_now_position")
             ? value : 0;
}
float LHandProDriver::get_now_angle(int id) {
  if (!ready_()) return 0.0F;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0.0F;
  float value = 0.0F;
  return sdk_ok_(sdk_->get_now_angle(id, value), "get_now_angle")
             ? value : 0.0F;
}
int LHandProDriver::get_now_status(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_status(id, value), "get_now_status")
             ? value : 0;
}
int LHandProDriver::get_now_current(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_current(id, value), "get_now_current")
             ? value : 0;
}
int LHandProDriver::get_now_alarm(int id) {
  if (!ready_()) return 0;
  std::lock_guard<std::mutex> lock(sdk_call_mutex_);
  if (!ready_()) return 0;
  int value = 0;
  return sdk_ok_(sdk_->get_now_alarm(id, value), "get_now_alarm")
             ? value : 0;
}
```

- [ ] **Step 9: Build and run the lifecycle/concurrency tests**

Run the command from Step 3 and then:

```bash
/tmp/dexhand_driver_test
```

Expected: all failure-stage rollback, retry, model mapping, two-instance exclusion, RX/TX/public-call drain, and idempotent cleanup tests pass in under 30 seconds.

- [ ] **Step 10: Commit the driver state machine**

```bash
git add include/hand_driver.hpp src/drivers/lhandpro/lhandpro_driver.* \
  tests/fakes/fake_canfd_transport.hpp tests/hand_driver_test_stub.cpp \
  tests/test_lhandpro_driver.cpp
git commit \
  -m "Prevent LHandPro callbacks from outliving driver resources" \
  -m "Make initialization transactional and use quiescent receive/transmit gates around the vendor handle while preserving the existing public no-op and zero-return contracts." \
  -m "Rejected: serialize receive decode with SDK calls | a call waiting for feedback would deadlock its decoder" \
  -m "Confidence: high" \
  -m "Scope-risk: broad" \
  -m "Directive: keep dexhand as one shared runtime or the process-wide active slot will be duplicated" \
  -m "Tested: stage rollback, retry, callback drains, public-call drain, model validation, and single active instance" \
  -m "Not-tested: physical hand monitor shutdown behavior"
```

### Task 7: Align The Public Base Class And Factory With The Approved Contract

**Files:**
- Modify: `include/hand_driver.hpp`
- Modify: `src/hand_driver.cpp`
- Create: `tests/test_factory.cpp`

- [ ] **Step 1: Write factory signature and validation tests**

Create `tests/test_factory.cpp` with:

```cpp
#include "hand_driver.hpp"
#include "test_support.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

void check_invalid(const std::string& hand_type,
                   const std::string& interface_type,
                   const std::string& interface, int model, int node,
                   const std::string& expected_text) {
  try {
    (void)HandDriver::create_hand(hand_type, interface_type, interface, model,
                                  node);
    CHECK(false);
  } catch (const std::invalid_argument& error) {
    CHECK(std::string(error.what()).find(expected_text) != std::string::npos);
  }
}

int main() {
using Factory = std::shared_ptr<HandDriver> (*)(
    const std::string&, const std::string&, const std::string&, int, int);
Factory factory = &HandDriver::create_hand;
CHECK(factory != nullptr);

CHECK(HandDriver::create_hand("LHandPro", "canfd", "can0") != nullptr);
check_invalid("Unknown", "canfd", "can0", 0, 1, "Unknown");
check_invalid("LHandPro", "ethercanfd", "can0", 0, 1, "ethercanfd");
check_invalid("LHandPro", "canfd", "", 0, 1, "empty");
check_invalid("LHandPro", "canfd", "can0", -1, 1, "-1");
check_invalid("LHandPro", "canfd", "can0", 2, 1, "2");
check_invalid("LHandPro", "canfd", "can0", 0, 0, "0");
check_invalid("LHandPro", "canfd", "can0", 0, 128, "128");
return 0;
}
```

Construction must not create an SDK handle or open a socket.

- [ ] **Step 2: Compile and verify the old seven-argument signature fails**

Run this command; do not include the Task 6 test stub because the production base constructor now comes from `src/hand_driver.cpp`:

```bash
c++ -std=c++17 -pthread -Iinclude -Isrc -Itests -Ithirdparty/include \
  tests/test_factory.cpp src/hand_driver.cpp \
  src/drivers/lhandpro/lhandpro_driver.cpp \
  src/drivers/lhandpro/lhandpro_sdk.cpp src/protocol/socket_canfd_transport.cpp \
  $(pkg-config --cflags --libs spdlog fmt) \
  -Lthirdparty/lib/x86_64 -Wl,-rpath,"$PWD/thirdparty/lib/x86_64" \
  -lLHandProLib -o /tmp/dexhand_factory_test
```

Expected: compile failure at the `Factory` assignment because the current API has seven parameters.

- [ ] **Step 3: Make the public header contract exact**

Apply these exact changes in `include/hand_driver.hpp`:

```cpp
enum class HandCommType { CANFD = 0 };

enum HandModel {
  HAND_LHANDPRO_6DOF = 0,
  HAND_LHANDPRO_16DOF = 1,
};

static std::shared_ptr<HandDriver> create_hand(
    const std::string& hand_type,
    const std::string& interface_type,
    const std::string& interface,
    int hand_model = HAND_LHANDPRO_6DOF,
    int canfd_node_id = 1);

virtual void get_dof(int& total, int& active) {
  total = dof_total_;
  active = dof_active_;
}
virtual std::string get_can_name() { return can_interface_; }
```

Use this Doxygen block directly above the factory declaration:

```cpp
/**
 * @brief Create a supported dexterous-hand driver.
 * @param hand_type Exact vendor name, currently `LHandPro`.
 * @param interface_type Exact transport name, currently `canfd`.
 * @param interface Non-empty Linux SocketCAN interface such as `can0`.
 * @param hand_model Stable public HandModel numeric value.
 * @param canfd_node_id CANopen node ID in the inclusive range 1-127.
 * @return Driver object; construction performs no communication I/O.
 * @throws std::invalid_argument if any configuration value is unsupported.
 */
```

Keep feedback getters pure virtual. Keep default arguments only in this base declaration; the complete derived declarations in Task 6 contain none.

- [ ] **Step 4: Implement strict factory validation and model mapping**

Replace the factory body with:

```cpp
if (hand_type != "LHandPro") {
  throw std::invalid_argument("Unsupported hand_type: " + hand_type);
}
if (interface_type != "canfd") {
  throw std::invalid_argument("Unsupported interface_type: " + interface_type);
}
if (interface.empty()) {
  throw std::invalid_argument("interface must not be empty");
}
if (canfd_node_id < 1 || canfd_node_id > 127) {
  throw std::invalid_argument("canfd_node_id must be in [1, 127], got " +
                              std::to_string(canfd_node_id));
}

using roboparty::dexhand::detail::LHandProModel;
LHandProModel model;
switch (hand_model) {
  case HAND_LHANDPRO_6DOF: model = LHandProModel::Dof6; break;
  case HAND_LHANDPRO_16DOF: model = LHandProModel::Dof16; break;
  default:
    throw std::invalid_argument("Unsupported hand_model: " +
                                std::to_string(hand_model));
}
return std::make_shared<LHandProDriver>(interface, model, canfd_node_id);
```

Include `<stdexcept>` and keep the vendor driver include private to `src/hand_driver.cpp`.

- [ ] **Step 5: Build and run factory tests**

Run the Step 2 compile command and then:

```bash
/tmp/dexhand_factory_test
```

Expected: signature and all invalid-value checks pass; no socket or physical hand access occurs.

- [ ] **Step 6: Commit the public API contract**

```bash
git add include/hand_driver.hpp src/hand_driver.cpp tests/test_factory.cpp
git commit \
  -m "Reject hand configuration the library cannot honor" \
  -m "Expose only CAN-FD and remove ignored bitrate parameters while preserving stable public model values and base getter defaults." \
  -m "Constraint: Linux deployment tooling owns CAN bitrate configuration" \
  -m "Confidence: high" \
  -m "Scope-risk: moderate" \
  -m "Tested: exact factory type and invalid vendor, interface, model, and node values"
```

### Task 8: Make The Python Binding Match The C++ API Exactly

**Files:**
- Replace: `src/pybind_module.cpp`
- Create: `tests/test_pybind_api.py`

- [ ] **Step 1: Write Python API contract tests**

Create `tests/test_pybind_api.py` with `unittest` cases that assert:

```python
import unittest
import dexhand_py


class DexhandApiTest(unittest.TestCase):
    def test_enums_are_exact(self):
        self.assertEqual(int(dexhand_py.HandCommType.CANFD.value), 0)
        self.assertFalse(hasattr(dexhand_py.HandCommType, "ETHERCAT"))
        self.assertFalse(hasattr(dexhand_py.HandCommType, "RS485"))
        self.assertEqual(int(dexhand_py.HandModel.LHANDPRO_6DOF.value), 0)
        self.assertEqual(int(dexhand_py.HandModel.LHANDPRO_16DOF.value), 1)

    def test_factory_defaults_and_removed_keywords(self):
        hand = dexhand_py.HandDriver.create_hand("LHandPro", "canfd", "can0")
        self.assertEqual(hand.get_can_name(), "can0")
        with self.assertRaises(TypeError):
            dexhand_py.HandDriver.create_hand(
                "LHandPro", "canfd", "can0", canfd_nom_baudrate=1000000)

    def test_complete_base_api(self):
        expected = {
            "create_hand", "init_hand", "deinit_hand", "move_motors",
            "stop_motors", "set_target_position", "set_target_angle",
            "set_position_velocity", "set_max_current", "set_enable",
            "home_motors", "set_move_no_home", "get_now_position",
            "get_now_angle", "get_now_status", "get_now_current",
            "get_now_alarm", "clear_alarm", "get_dof", "get_can_name",
        }
        self.assertTrue(expected.issubset(set(dir(dexhand_py.HandDriver))))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Replace bindings with the five-argument contract**

Replace `src/pybind_module.cpp` with:

```cpp
#include <pybind11/pybind11.h>

#include "hand_driver.hpp"

namespace py = pybind11;

PYBIND11_MODULE(dexhand_py, module) {
  module.doc() = "Dexterous Hand Driver Python SDK (roboparty_dexhand)";

  py::enum_<HandCommType>(module, "HandCommType")
      .value("CANFD", HandCommType::CANFD)
      .export_values();
  py::enum_<HandModel>(module, "HandModel")
      .value("LHANDPRO_6DOF", HAND_LHANDPRO_6DOF)
      .value("LHANDPRO_16DOF", HAND_LHANDPRO_16DOF)
      .export_values();

  py::class_<HandDriver, std::shared_ptr<HandDriver>>(module, "HandDriver")
      .def_static("create_hand", &HandDriver::create_hand,
                  py::arg("hand_type"), py::arg("interface_type"),
                  py::arg("interface"),
                  py::arg("hand_model") = HAND_LHANDPRO_6DOF,
                  py::arg("canfd_node_id") = 1)
      .def("init_hand", &HandDriver::init_hand,
           py::arg("enable_motors") = true,
           py::arg("home_motors") = true,
           py::arg("home_wait_time") = 5.0F,
           py::call_guard<py::gil_scoped_release>())
      .def("deinit_hand", &HandDriver::deinit_hand,
           py::call_guard<py::gil_scoped_release>())
      .def("move_motors", &HandDriver::move_motors,
           py::arg("finger_id") = 0)
      .def("stop_motors", &HandDriver::stop_motors,
           py::arg("finger_id") = 0)
      .def("set_target_position", &HandDriver::set_target_position,
           py::arg("finger_id"), py::arg("position"))
      .def("set_target_angle", &HandDriver::set_target_angle,
           py::arg("finger_id"), py::arg("angle"))
      .def("set_position_velocity", &HandDriver::set_position_velocity,
           py::arg("finger_id"), py::arg("velocity"))
      .def("set_max_current", &HandDriver::set_max_current,
           py::arg("finger_id"), py::arg("current"))
      .def("set_enable", &HandDriver::set_enable,
           py::arg("finger_id"), py::arg("enable"))
      .def("home_motors", &HandDriver::home_motors,
           py::arg("finger_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("set_move_no_home", &HandDriver::set_move_no_home,
           py::arg("enable"))
      .def("get_now_position", &HandDriver::get_now_position,
           py::arg("finger_id"))
      .def("get_now_angle", &HandDriver::get_now_angle,
           py::arg("finger_id"))
      .def("get_now_status", &HandDriver::get_now_status,
           py::arg("finger_id"))
      .def("get_now_current", &HandDriver::get_now_current,
           py::arg("finger_id"))
      .def("get_now_alarm", &HandDriver::get_now_alarm,
           py::arg("finger_id"))
      .def("clear_alarm", &HandDriver::clear_alarm,
           py::arg("finger_id") = 0)
      .def("get_dof", [](HandDriver& self) {
        int total = 0;
        int active = 0;
        self.get_dof(total, active);
        return py::make_tuple(total, active);
      })
      .def("get_can_name", &HandDriver::get_can_name);
}
```

- [ ] **Step 3: Defer execution until the integrated CMake build exists**

The source-level red check is:

```bash
rg -n 'ETHERCAT|RS485|canfd_nom_baudrate|canfd_dat_baudrate' src/pybind_module.cpp
```

Expected after the edit: no output. The import test is run after install in Task 10 because loading the extension directly from a partial build would hide RPATH/export mistakes.

- [ ] **Step 4: Commit the Python contract**

```bash
git add src/pybind_module.cpp tests/test_pybind_api.py
git commit \
  -m "Keep Python configuration aligned with the C++ factory" \
  -m "Remove unsupported enum and bitrate surfaces while retaining every base operation and releasing the GIL around blocking lifecycle calls." \
  -m "Confidence: high" \
  -m "Scope-risk: moderate" \
  -m "Tested: source binding inventory" \
  -m "Not-tested: installed extension import, covered after CMake integration"
```

### Task 9: Replace Motors Coupling With A Relocatable Dual-Mode Build

**Files:**
- Replace: `CMakeLists.txt`
- Replace: `src/CMakeLists.txt`
- Create: `src/protocol/CMakeLists.txt`
- Create: `src/drivers/CMakeLists.txt`
- Replace: `src/drivers/lhandpro/CMakeLists.txt`
- Replace: `cmake/roboparty_dexhandConfig.cmake.in`
- Create: `tests/CMakeLists.txt`
- Create: `tests/installed_consumer/CMakeLists.txt`
- Create: `tests/installed_consumer/main.cpp`
- Create: `tests/check_install_export.cmake`
- Create: `tests/test_vcan_two_socket.cpp`

- [ ] **Step 1: Write the installed-consumer fixture before changing targets**

Create `tests/installed_consumer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.12)
project(dexhand_installed_consumer LANGUAGES CXX)
find_package(roboparty_dexhand CONFIG REQUIRED)
add_executable(dexhand_consumer main.cpp)
target_compile_features(dexhand_consumer PRIVATE cxx_std_17)
target_link_libraries(dexhand_consumer PRIVATE roboparty_dexhand::dexhand)
```

Create `tests/installed_consumer/main.cpp`:

```cpp
#include <hand_driver.hpp>

int main() {
  auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0");
  return hand && hand->get_can_name() == "can0" ? 0 : 1;
}
```

This fixture constructs only; it never calls `init_hand()`.

- [ ] **Step 2: Configure the old build without motors and observe the intended failure**

Run:

```bash
VERIFY_ROOT=$(mktemp -d /tmp/dexhand-cmake-red.XXXXXX)
cmake -S . -B "$VERIFY_ROOT/build" -G Ninja \
  -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
```

Expected: configuration fails with the old `roboparty_motors not found` path or an unavailable private motors target.

- [ ] **Step 3: Replace the root build with target-scoped configuration**

Use this complete structure in `CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty
cmake_minimum_required(VERSION 3.12)

file(READ "package.xml" PACKAGE_XML_CONTENT)
string(REGEX MATCH "<version>([0-9\\.]+)</version>" _ "${PACKAGE_XML_CONTENT}")
project(roboparty_dexhand VERSION ${CMAKE_MATCH_1} LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)
include(cmake/SelectLHandProSdk.cmake)

find_package(ament_cmake QUIET)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

roboparty_dexhand_normalize_arch("${CMAKE_SYSTEM_PROCESSOR}" DEXHAND_SDK_ARCH)
set(DEXHAND_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lib/${DEXHAND_SDK_ARCH}")
set(DEXHAND_SDK_LIBRARY "${DEXHAND_SDK_DIR}/libLHandProLib.so")
if(NOT EXISTS "${DEXHAND_SDK_LIBRARY}")
  message(FATAL_ERROR "Missing ${DEXHAND_SDK_ARCH} LHandPro SDK: ${DEXHAND_SDK_LIBRARY}")
endif()

add_library(roboparty_dexhand::lhandpro_sdk SHARED IMPORTED GLOBAL)
set_target_properties(roboparty_dexhand::lhandpro_sdk PROPERTIES
  IMPORTED_LOCATION "${DEXHAND_SDK_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/include")

add_subdirectory(src)

pybind11_add_module(dexhand_py src/pybind_module.cpp)
target_compile_features(dexhand_py PRIVATE cxx_std_17)
target_link_libraries(dexhand_py PRIVATE dexhand)
set_target_properties(dexhand_py PROPERTIES INSTALL_RPATH "\$ORIGIN/../..")

set(PYTHON_INSTALL_DIR
  "${CMAKE_INSTALL_LIBDIR}/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES "${DEXHAND_SDK_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS dexhand
  EXPORT roboparty_dexhand-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS dexhand_py
  LIBRARY DESTINATION ${PYTHON_INSTALL_DIR}
  RUNTIME DESTINATION ${PYTHON_INSTALL_DIR})
install(EXPORT roboparty_dexhand-targets
  FILE roboparty_dexhandTargets.cmake
  NAMESPACE roboparty_dexhand::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/roboparty_dexhandConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/roboparty_dexhandConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/roboparty_dexhand)

option(DEXHAND_ENABLE_VCAN_TESTS "Enable privileged vcan integration test" OFF)
if(BUILD_TESTING)
  add_subdirectory(tests)
  if(ament_cmake_FOUND)
    find_package(ament_lint_auto REQUIRED)
    set(AMENT_LINT_AUTO_FILE_EXCLUDE thirdparty)
    ament_lint_auto_find_test_dependencies()
  endif()
endif()

if(ament_cmake_FOUND)
  ament_export_targets(roboparty_dexhand-targets HAS_LIBRARY_TARGET)
  ament_export_dependencies(fmt spdlog)
  ament_export_include_directories(include)
  ament_package()
endif()
```

Do not set `CMAKE_BUILD_TYPE`, `CMAKE_CXX_FLAGS`, `CMAKE_CXX_STANDARD`, `-O3`, `-march=native`, compiler launchers, or global PIC. Respect the caller/toolchain.

- [ ] **Step 4: Implement the exact private target graph**

Set `src/CMakeLists.txt` to:

```cmake
add_subdirectory(protocol)
add_subdirectory(drivers)

add_library(dexhand SHARED hand_driver.cpp)
target_compile_features(dexhand PUBLIC cxx_std_17)
target_include_directories(dexhand
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dexhand
  PUBLIC fmt::fmt spdlog::spdlog
  PRIVATE lhandpro_driver Threads::Threads)
set_target_properties(dexhand PROPERTIES
  BUILD_RPATH "${DEXHAND_SDK_DIR}"
  INSTALL_RPATH "\$ORIGIN")
```

Set `src/protocol/CMakeLists.txt` to:

```cmake
add_library(dexhand_canfd STATIC socket_canfd_transport.cpp)
target_compile_features(dexhand_canfd PUBLIC cxx_std_17)
set_target_properties(dexhand_canfd PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(dexhand_canfd PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dexhand_canfd PUBLIC Threads::Threads spdlog::spdlog)
```

Set `src/drivers/CMakeLists.txt` to:

```cmake
add_subdirectory(lhandpro)
```

Set `src/drivers/lhandpro/CMakeLists.txt` to:

```cmake
add_library(lhandpro_driver STATIC lhandpro_driver.cpp lhandpro_sdk.cpp)
target_compile_features(lhandpro_driver PUBLIC cxx_std_17)
set_target_properties(lhandpro_driver PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(lhandpro_driver
  PUBLIC ${PROJECT_SOURCE_DIR}/src
  PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(lhandpro_driver
  PRIVATE dexhand_canfd roboparty_dexhand::lhandpro_sdk
          fmt::fmt spdlog::spdlog Threads::Threads)
```

Only `dexhand` is installed/exported. The private static targets are linked into that shared runtime, not directly into `dexhand_py` and not exposed to consumers.

- [ ] **Step 5: Replace the installed config with one public target**

Set `cmake/roboparty_dexhandConfig.cmake.in` to:

```cmake
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(fmt)
find_dependency(spdlog)
include("${CMAKE_CURRENT_LIST_DIR}/roboparty_dexhandTargets.cmake")
check_required_components(roboparty_dexhand)
```

- [ ] **Step 6: Register every hardware-free test target**

Create `tests/CMakeLists.txt` with:

```cmake
function(add_dexhand_test name source)
  add_executable(${name} ${source})
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_include_directories(${name} PRIVATE
    ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/thirdparty/include)
  target_link_libraries(${name} PRIVATE dexhand Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

add_dexhand_test(callback_gate test_callback_gate.cpp)
add_dexhand_test(factory_contract test_factory.cpp)
add_dexhand_test(lhandpro_lifecycle test_lhandpro_driver.cpp)

add_executable(canfd_transport test_canfd_transport.cpp)
target_compile_features(canfd_transport PRIVATE cxx_std_17)
target_include_directories(canfd_transport PRIVATE
  ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(canfd_transport PRIVATE dexhand_canfd Threads::Threads)
add_test(NAME canfd_transport COMMAND canfd_transport)
set_tests_properties(canfd_transport PROPERTIES TIMEOUT 30)

add_executable(lhandpro_sdk test_lhandpro_sdk.cpp)
target_compile_features(lhandpro_sdk PRIVATE cxx_std_17)
target_include_directories(lhandpro_sdk PRIVATE
  ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(lhandpro_sdk PRIVATE lhandpro_driver)
add_test(NAME lhandpro_sdk COMMAND lhandpro_sdk)

add_test(NAME pybind_api
  COMMAND ${CMAKE_COMMAND} -E env
    "PYTHONPATH=$<TARGET_FILE_DIR:dexhand_py>"
    "PYTHONNOUSERSITE=1"
    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_pybind_api.py)
set_tests_properties(pybind_api PROPERTIES TIMEOUT 30)

add_test(NAME sdk_artifacts
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_sdk_artifacts.cmake)

if(DEXHAND_ENABLE_VCAN_TESTS)
  add_executable(vcan_two_socket test_vcan_two_socket.cpp)
  target_compile_features(vcan_two_socket PRIVATE cxx_std_17)
  target_include_directories(vcan_two_socket PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(vcan_two_socket PRIVATE dexhand_canfd Threads::Threads)
  add_test(NAME vcan_two_socket COMMAND vcan_two_socket vcan-dexhand0)
  set_tests_properties(vcan_two_socket PROPERTIES TIMEOUT 15)
endif()
```

The installed export fixture is intentionally run after `cmake --install` in Task 10, so it validates the exact requested prefix and relocation rather than mutating the build during unit CTest execution.

- [ ] **Step 7: Add the opt-in two-socket kernel test**

Create `tests/test_vcan_two_socket.cpp` with:

```cpp
#include "protocol/socket_canfd_transport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace roboparty::dexhand::detail;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: vcan_two_socket <interface>\n";
    return 2;
  }
  const std::string interface = argv[1];
  SocketCanFdTransport first;
  SocketCanFdTransport second;
  std::mutex mutex;
  std::condition_variable received;
  std::atomic<int> first_saw_first{0};
  std::atomic<int> first_saw_second{0};
  std::atomic<int> second_saw_first{0};
  std::atomic<int> second_saw_second{0};

  if (!first.open(interface, {0x321}) || !second.open(interface, {0x321})) {
    std::cerr << "failed to open both SocketCAN transports\n";
    return 1;
  }
  first.set_receive_callback([&](const CanFdFrame& frame) {
    if (frame.len > 0 && frame.data[0] == 0xA1) ++first_saw_first;
    if (frame.len > 0 && frame.data[0] == 0xB2) ++first_saw_second;
    received.notify_all();
  });
  second.set_receive_callback([&](const CanFdFrame& frame) {
    if (frame.len > 0 && frame.data[0] == 0xA1) ++second_saw_first;
    if (frame.len > 0 && frame.data[0] == 0xB2) ++second_saw_second;
    received.notify_all();
  });

  CanFdFrame frame;
  frame.id = 0x321;
  frame.len = 8;
  frame.data[0] = 0xA1;
  if (!first.transmit(frame)) return 1;
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!received.wait_for(lock, std::chrono::seconds(1),
                           [&] { return second_saw_first.load() == 1; }))
      return 1;
  }
  if (first_saw_first.load() != 0) return 1;

  frame.id = 0x322;
  frame.data[0] = 0xCC;
  if (!first.transmit(frame)) return 1;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if (first_saw_first.load() != 0 || second_saw_first.load() != 1 ||
      first_saw_second.load() != 0 || second_saw_second.load() != 0) {
    return 1;
  }

  frame.id = 0x321;
  frame.data[0] = 0xB2;
  if (!second.transmit(frame)) return 1;
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (!received.wait_for(lock, std::chrono::seconds(1),
                           [&] { return first_saw_second.load() == 1; }))
      return 1;
  }
  if (second_saw_second.load() != 0 || second_saw_first.load() != 1 ||
      first_saw_second.load() != 1) {
    return 1;
  }
  first.close();
  second.close();
  return 0;
}
```

The executable does not create/delete the interface and never silently skips when the option is ON.

- [ ] **Step 8: Configure, build, and run the hardware-free test suite**

Run:

```bash
VERIFY_ROOT=$(mktemp -d /tmp/dexhand-cmake-green.XXXXXX)
cmake -S . -B "$VERIFY_ROOT/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON -DDEXHAND_ENABLE_VCAN_TESTS=OFF
cmake --build "$VERIFY_ROOT/build" --parallel
ctest --test-dir "$VERIFY_ROOT/build" --output-on-failure --timeout 30
```

Expected: configure/build succeed without discovering motors; all registered hardware-free C++/Python/artifact tests pass; `vcan_two_socket` is not registered.

- [ ] **Step 9: Prove the target graph contains one shared runtime**

Run:

```bash
file "$VERIFY_ROOT/build/src/libdexhand.so"
find "$VERIFY_ROOT/build" -name 'liblhandpro_driver.a' -o -name 'libdexhand_canfd.a'
cmake --build "$VERIFY_ROOT/build" --target help | rg 'dexhand|lhandpro'
```

Expected: `libdexhand.so` is shared; both private `.a` files exist only as build artifacts; there is one `dexhand_py` target and no motors targets.

- [ ] **Step 10: Commit the independent build graph**

```bash
git add CMakeLists.txt cmake src tests
git commit \
  -m "Keep one dexhand runtime across C++ and Python consumers" \
  -m "Export only the shared factory library while folding private LHandPro and SocketCAN static components into it and selecting the vendor SDK by target architecture." \
  -m "Constraint: pure CMake and ament installs must expose the same target" \
  -m "Rejected: static aggregate | C++ and pybind could duplicate the vendor callback singleton" \
  -m "Confidence: high" \
  -m "Scope-risk: broad" \
  -m "Directive: do not export private transport or vendor targets" \
  -m "Tested: pure CMake configure, build, CTest, target and artifact inspection"
```

### Task 10: Complete Install, Package, License, And Migration Metadata

**Files:**
- Modify: `package.xml`
- Create: `LICENSE`
- Create: `thirdparty/README.md`
- Replace: `README.md`
- Modify: `CODE_REVIEW.md`
- Modify: `scripts/test_dexhand.py`
- Complete: `tests/check_install_export.cmake`

- [ ] **Step 1: Update package metadata exactly**

Replace `package.xml` with:

```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  <name>roboparty_dexhand</name>
  <version>0.2.0</version>
  <description>LHandPro dexterous hand driver for Linux CAN-FD with C++ and Python factory APIs</description>
  <maintainer email="dev@roboparty.com">Roboparty</maintainer>
  <license>GPL-3.0</license>

  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>fmt</depend>
  <depend>spdlog</depend>
  <build_depend>pybind11_vendor</build_depend>
  <build_depend>python3-dev</build_depend>
  <exec_depend>python3</exec_depend>

  <test_depend>ament_lint_auto</test_depend>
  <test_depend>ament_lint_common</test_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>
```

There is no `roboparty_motors` dependency and no runtime dependency for vcan tooling.

- [ ] **Step 2: Add source and vendor license boundaries**

Copy the canonical GPLv3 text already used by the sibling standard repository without modifying that repository, and verify the copy:

```bash
cp /home/sjh/leisai_hand/roboparty_motors/LICENSE LICENSE
cmp /home/sjh/leisai_hand/roboparty_motors/LICENSE LICENSE
sha256sum LICENSE
```

Expected SHA-256: `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`.

Create `thirdparty/README.md` with:

```markdown
# LHandPro Vendor SDK

This directory contains the binary-only LHandProLib runtime and C header from
`LHandProLib-API-Linux-20260727` supplied for RoboParty hardware integration.

| Architecture | SHA-256 |
| --- | --- |
| x86-64 | `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640` |
| AArch64 | `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044` |

The repository GPL-3.0 declaration applies to RoboParty-authored source;
it does not relicense these vendor artifacts. No standalone vendor license was
present in the supplied distribution. Public redistribution is blocked until
the vendor's redistribution terms are confirmed and recorded here.
```

- [ ] **Step 3: Replace stale architecture and migration documentation**

Rewrite `README.md` with these concrete sections and commands:

```markdown
# roboparty_dexhand

Factory-style C++/Python control library for LHandPro hands over Linux CAN-FD.
The library owns a private SocketCAN socket; motors may use another socket on
the same physical CAN interface. No motors source or private target is linked.

## Supported Platforms

- Linux x86-64 (development)
- Linux AArch64/ARM64 (Orange Pi and RDK deployment)
- One active LHandPro SDK instance per process

## CAN Setup

Configure bitrate and link state outside this library, for example with the
deployment service responsible for `can0`. The factory does not accept bitrate
arguments and never changes the network interface.

## Build

cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --parallel
cmake --install build --prefix "$PWD/install"

## C++

auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0",
                                    HAND_LHANDPRO_6DOF, 1);

## Python

hand = HandDriver.create_hand(
    hand_type="LHandPro", interface_type="canfd", interface="can0",
    hand_model=HandModel.LHANDPRO_6DOF, canfd_node_id=1)

## Migration From 0.1.x

Version 0.2.0 removes `canfd_nom_baudrate` and `canfd_dat_baudrate`; configure
the Linux CAN interface in deployment tooling. It also removes the unimplemented
`ETHERCAT` and `RS485` enum exports. Public hand-model numeric values remain 0
for 6-DOF and 1 for 16-DOF.

## Testing

Automatic tests use fakes and never enable, home, or move a physical hand.
`scripts/test_dexhand.py --confirm-motion` is a manual hardware test only.
```

Append these exact operational sections to the same README:

```markdown
## ROS/Ament Build

source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-select roboparty_dexhand \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
colcon test --packages-select roboparty_dexhand
colcon test-result --verbose

## Installed CMake Target

Consumers use `find_package(roboparty_dexhand CONFIG REQUIRED)` and link only
`roboparty_dexhand::dexhand`. Private SDK, driver, and transport targets and
headers are not installed. `libdexhand.so` finds `libLHandProLib.so` beside it
through `$ORIGIN`; the Python extension finds the prefix `lib` directory
through `$ORIGIN/../..`, so consumers do not need `LD_LIBRARY_PATH`.

## vcan Release Test

Configure with `-DDEXHAND_ENABLE_VCAN_TESTS=ON` only after creating and raising
`vcan-dexhand0`. The `vcan_two_socket` test is a required release gate and
proves two independent sockets, exact filters, no own-message delivery, and
bounded shutdown.

## ARM64 Release Gate

The repository carries an AArch64 SDK artifact, but an Orange Pi/RDK release
also requires a native build, CTest, installed C++ consumer, and installed
Python import on that board. x86 ELF inspection is not a substitute.

## Vendor License Boundary

RoboParty-authored source is GPL-3.0. The supplied vendor binary has no
standalone license in its distribution; public redistribution is blocked until
vendor authorization is recorded in `thirdparty/README.md`.
```

- [ ] **Step 4: Mark the old review's conflicting decisions as superseded**

At the top of `CODE_REVIEW.md`, add:

```markdown
> Status: historical review, superseded by the approved independent-CAN-FD
> design in
> `docs/superpowers/specs/2026-08-10-roboparty-dexhand-independent-canfd-design.md`.
> In particular, recommendations to modify `roboparty_motors` or share its
> private `MotorsCANFD` target must not be implemented.
```

Immediately before historical issue 8, add:

```markdown
> Historical diagnosis only: the receive-own-message change below was explored
> against the old shared-socket implementation. It is not part of the accepted
> fix and must not be applied to `roboparty_motors`; dexhand now owns a separate
> socket and leaves `CAN_RAW_RECV_OWN_MSGS` disabled.
```

Replace the architecture table row `CAN 总线复用单例` with:

```markdown
| CAN transport ownership | motors owns its transport | dexhand owns its private transport | consistent module boundary; separate sockets may bind the same interface |
```

Retain the rest as explicitly historical evidence rather than deleting it.

- [ ] **Step 5: Make the physical-hand script require explicit motion consent**

In `scripts/test_dexhand.py`, import `argparse` and `platform`, then place this block before `ctypes.CDLL` and before importing `dexhand_py`:

```python
parser = argparse.ArgumentParser(description="Manual LHandPro motion test")
parser.add_argument(
    "--confirm-motion",
    action="store_true",
    help="acknowledge that this script enables, homes, and moves real hardware",
)
args = parser.parse_args()
if not args.confirm_motion:
    parser.error("refusing to move hardware without --confirm-motion")

machine = platform.machine().lower()
sdk_arches = {
    "x86_64": "x86_64",
    "amd64": "x86_64",
    "aarch64": "aarch64",
    "arm64": "aarch64",
}
try:
    sdk_arch = sdk_arches[machine]
except KeyError:
    parser.error(f"unsupported machine architecture: {machine}")
so_path = (
    Path(__file__).resolve().parent.parent
    / "thirdparty"
    / "lib"
    / sdk_arch
    / "libLHandProLib.so"
)
```

Delete the old flat `thirdparty/lib/libLHandProLib.so` assignment and use:

```python
hand = HandDriver.create_hand(
    hand_type="LHandPro",
    interface_type="canfd",
    interface="can0",
    hand_model=HandModel.LHANDPRO_6DOF,
    canfd_node_id=1,
)
```

Update the usage text to `scripts/test_dexhand.py --confirm-motion`. Do not register this script with CTest or ament.

- [ ] **Step 6: Implement the relocated install/export check**

Create `tests/check_install_export.cmake` with:

```cmake
cmake_minimum_required(VERSION 3.12)
foreach(required IN ITEMS BUILD_DIR SOURCE_DIR PREFIX PYTHON_EXECUTABLE)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
                --prefix "${PREFIX}" RESULT_VARIABLE install_rc)
if(NOT install_rc EQUAL 0)
  message(FATAL_ERROR "install failed: ${install_rc}")
endif()

file(GLOB_RECURSE configs "${PREFIX}/lib/cmake/roboparty_dexhand/*")
if(NOT configs)
  message(FATAL_ERROR "installed CMake package files are missing")
endif()
foreach(config IN LISTS configs)
  file(READ "${config}" content)
  if(content MATCHES "${SOURCE_DIR}|roboparty_motors|MotorsCANFD|motors_canfd")
    message(FATAL_ERROR "private/source path leaked through ${config}")
  endif()
endforeach()
if(EXISTS "${PREFIX}/include/LHandProLib/LHandProLib.h" OR
   EXISTS "${PREFIX}/include/protocol/canfd_transport.hpp")
  message(FATAL_ERROR "private header was installed")
endif()
if(NOT EXISTS "${PREFIX}/lib/libdexhand.so" OR
   NOT EXISTS "${PREFIX}/lib/libLHandProLib.so")
  message(FATAL_ERROR "required shared runtime missing")
endif()

file(GLOB sdk_copies "${PREFIX}/lib/libLHandProLib.so")
list(LENGTH sdk_copies sdk_count)
if(NOT sdk_count EQUAL 1)
  message(FATAL_ERROR "expected exactly one selected SDK, got ${sdk_count}")
endif()

set(target_file "${PREFIX}/lib/cmake/roboparty_dexhand/roboparty_dexhandTargets.cmake")
file(READ "${target_file}" target_content)
if(NOT target_content MATCHES "roboparty_dexhand::dexhand")
  message(FATAL_ERROR "public dexhand target missing")
endif()
if(target_content MATCHES "lhandpro_driver|dexhand_canfd|lhandpro_sdk")
  message(FATAL_ERROR "private target leaked into installed export")
endif()

set(RELOCATED "${PREFIX}-relocated")
if(EXISTS "${RELOCATED}")
  message(FATAL_ERROR "relocation destination already exists: ${RELOCATED}")
endif()
file(RENAME "${PREFIX}" "${RELOCATED}")

set(CONSUMER_BUILD "${BUILD_DIR}/installed-consumer-check")
file(REMOVE_RECURSE "${CONSUMER_BUILD}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR}/tests/installed_consumer"
    -B "${CONSUMER_BUILD}"
    -DCMAKE_PREFIX_PATH=${RELOCATED}
  RESULT_VARIABLE configure_rc OUTPUT_VARIABLE configure_out
  ERROR_VARIABLE configure_err)
if(NOT configure_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer configure failed:\n${configure_out}\n${configure_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${CONSUMER_BUILD}"
  RESULT_VARIABLE build_rc OUTPUT_VARIABLE build_out ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer build failed:\n${build_out}\n${build_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    "${CONSUMER_BUILD}/dexhand_consumer"
  RESULT_VARIABLE consumer_rc OUTPUT_VARIABLE consumer_out
  ERROR_VARIABLE consumer_err)
if(NOT consumer_rc EQUAL 0)
  message(FATAL_ERROR
    "installed consumer run failed:\n${consumer_out}\n${consumer_err}")
endif()

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" -c
    "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')"
  RESULT_VARIABLE py_version_rc OUTPUT_VARIABLE PY_VERSION
  ERROR_VARIABLE py_version_err OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT py_version_rc EQUAL 0)
  message(FATAL_ERROR "Python version lookup failed: ${py_version_err}")
endif()
set(PYTHON_SITE "${RELOCATED}/lib/python${PY_VERSION}/site-packages")
file(GLOB PYTHON_MODULE "${PYTHON_SITE}/dexhand_py*.so")
list(LENGTH PYTHON_MODULE module_count)
if(NOT module_count EQUAL 1)
  message(FATAL_ERROR "expected one installed dexhand_py module, got ${module_count}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    PYTHONNOUSERSITE=1 PYTHONPATH=${PYTHON_SITE}
    "${PYTHON_EXECUTABLE}" "${SOURCE_DIR}/tests/test_pybind_api.py"
  WORKING_DIRECTORY "/tmp"
  RESULT_VARIABLE python_rc OUTPUT_VARIABLE python_out ERROR_VARIABLE python_err)
if(NOT python_rc EQUAL 0)
  message(FATAL_ERROR
    "installed Python API failed:\n${python_out}\n${python_err}")
endif()
```

- [ ] **Step 7: Build, install, relocate, and validate without `LD_LIBRARY_PATH`**

Run:

```bash
VERIFY_ROOT=$(mktemp -d /tmp/dexhand-install.XXXXXX)
cmake -S . -B "$VERIFY_ROOT/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON
cmake --build "$VERIFY_ROOT/build" --parallel
cmake -DBUILD_DIR="$VERIFY_ROOT/build" -DSOURCE_DIR="$PWD" \
  -DPREFIX="$VERIFY_ROOT/prefix" -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P tests/check_install_export.cmake
```

Expected: install/rename/consumer/Python checks all exit zero. The consumer constructs but does not initialize hardware.

- [ ] **Step 8: Inspect the installed ABI and runtime search paths**

Run against the relocated prefix:

```bash
readelf -d "$VERIFY_ROOT/prefix-relocated/lib/libdexhand.so" | rg 'RUNPATH.*\$ORIGIN'
PYMOD=$(find "$VERIFY_ROOT/prefix-relocated/lib" -name 'dexhand_py*.so' -print -quit)
readelf -d "$PYMOD" | rg 'RUNPATH.*\$ORIGIN/\.\./\.\.'
if ldd "$PYMOD" | rg 'not found'; then exit 1; fi
find "$VERIFY_ROOT/prefix-relocated" -name 'libLHandProLib.so' -print
```

Expected: both RPATH patterns match, `ldd` has no missing dependency, and exactly one selected x86-64 vendor library is installed.

- [ ] **Step 9: Commit metadata and install evidence**

```bash
git add package.xml LICENSE thirdparty/README.md README.md CODE_REVIEW.md scripts/test_dexhand.py tests/check_install_export.cmake
git commit \
  -m "Make the dexhand package honest about support and redistribution" \
  -m "Document the independent socket boundary, the 0.2 API migration, explicit physical-motion consent, verified SDK artifacts, and the unresolved vendor license gate." \
  -m "Constraint: vendor redistribution terms are absent from the supplied SDK" \
  -m "Confidence: high" \
  -m "Scope-risk: moderate" \
  -m "Directive: do not publish vendor binaries until redistribution authorization is recorded" \
  -m "Tested: relocated install, downstream C++ build/run, Python import, RPATH and package metadata"
```

### Task 11: Run Full Quality Gates And Record Platform-Limited Risks

**Files:**
- Modify only if verification exposes a defect: files owned by the failing test
- Verify: all dexhand source, tests, metadata, install outputs

- [ ] **Step 1: Prove there is no motors ownership leak**

Run:

```bash
if rg -n 'roboparty_motors|MotorsCANFD|motors_canfd|ROBOPARTY_MOTORS|MOTORS_' \
  CMakeLists.txt cmake include src package.xml README.md; then exit 1; fi
```

Expected: no output and exit zero. Historical mentions in `CODE_REVIEW.md` are allowed only inside the explicit superseded warning/evidence.

- [ ] **Step 2: Run a fresh pure-CMake Debug build and every hardware-free test**

```bash
DEXHAND_ROOT="$PWD"
DEXHAND_VERIFY=$(mktemp -d /tmp/dexhand-final.XXXXXX)
DEXHAND_BUILD="$DEXHAND_VERIFY/build"
DEXHAND_PREFIX="$DEXHAND_VERIFY/prefix"

cmake -S "$DEXHAND_ROOT" -B "$DEXHAND_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="$DEXHAND_PREFIX" \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON -DDEXHAND_ENABLE_VCAN_TESTS=OFF
cmake --build "$DEXHAND_BUILD" --parallel
ctest --test-dir "$DEXHAND_BUILD" --output-on-failure --timeout 30
cmake --install "$DEXHAND_BUILD"
```

Expected: zero configure/build/test/install failures and no real CAN access.

- [ ] **Step 3: Run relocated installed C++ and Python checks**

```bash
cmake -DBUILD_DIR="$DEXHAND_BUILD" -DSOURCE_DIR="$DEXHAND_ROOT" \
  -DPREFIX="$DEXHAND_VERIFY/relocatable" \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P "$DEXHAND_ROOT/tests/check_install_export.cmake"
```

Expected: C++ consumer and Python API pass with `LD_LIBRARY_PATH` unset and no build/source-tree dependency.

- [ ] **Step 4: Run metadata and static analysis**

```bash
xmllint --noout package.xml
cppcheck --enable=warning,performance,portability --std=c++17 \
  --error-exitcode=1 include src tests
```

Expected: both commands exit zero. Fix only verified dexhand findings; do not suppress warnings globally and do not edit reference repositories.

- [ ] **Step 5: Run isolated ament/colcon mode**

```bash
source /opt/ros/jazzy/setup.bash
COLCON_VERIFY=$(mktemp -d /tmp/dexhand-colcon.XXXXXX)
mkdir -p "$COLCON_VERIFY/src"
ln -s "$DEXHAND_ROOT" "$COLCON_VERIFY/src/roboparty_dexhand"
cd "$COLCON_VERIFY"
colcon build --symlink-install --packages-select roboparty_dexhand \
  --cmake-args -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
colcon test --packages-select roboparty_dexhand --event-handlers console_direct+
colcon test-result --verbose
```

Expected: only dexhand is selected; zero test failures; no motors source is discovered or built.

- [ ] **Step 6: Run the vcan gate only in a privileged local/CI environment**

```bash
VCAN_NAME=vcan-dexhand0
sudo modprobe vcan
sudo ip link add "$VCAN_NAME" type vcan
sudo ip link set "$VCAN_NAME" up
cmake -S "$DEXHAND_ROOT" -B "$DEXHAND_VERIFY/vcan-build" -G Ninja \
  -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=ON
cmake --build "$DEXHAND_VERIFY/vcan-build" --parallel
ctest --test-dir "$DEXHAND_VERIFY/vcan-build" -R '^vcan_two_socket$' \
  --output-on-failure --timeout 15
sudo ip link del "$VCAN_NAME"
```

Expected release result: PASS, never SKIP. The current unprivileged x86 host has no vcan interface, so report this gate as not run if privilege is unavailable; do not claim it passed and do not leave a created interface behind.

- [ ] **Step 7: Verify both packaged SDK artifacts on x86**

```bash
readelf -h thirdparty/lib/x86_64/libLHandProLib.so | rg 'Machine:.*X86-64'
readelf -h thirdparty/lib/aarch64/libLHandProLib.so | rg 'Machine:.*AArch64'
sha256sum thirdparty/lib/*/libLHandProLib.so
cmake -DSOURCE_DIR="$DEXHAND_ROOT" -P tests/check_sdk_artifacts.cmake
```

Expected: exact hashes from Task 2 and correct ELF machines. This is artifact evidence, not an ARM runtime claim.

- [ ] **Step 8: Run the native ARM release gate on Orange Pi/RDK**

On AArch64 hardware, repeat Steps 2-5, then verify:

```bash
file "$DEXHAND_PREFIX/lib/libdexhand.so" "$DEXHAND_PREFIX/lib/libLHandProLib.so"
ldd "$DEXHAND_PREFIX/lib/libdexhand.so"
cmake -E env --unset=LD_LIBRARY_PATH \
  PYTHONNOUSERSITE=1 \
  PYTHONPATH="$DEXHAND_PREFIX/lib/python3.12/site-packages" \
  /usr/bin/python3 -c 'import dexhand_py; print(dexhand_py.HandModel.LHANDPRO_16DOF)'
```

Expected release result: both shared objects are AArch64, `ldd` has no missing libraries, installed C++ consumer and Python import pass. The current x86 machine has no AArch64 compiler and cannot satisfy this gate.

- [ ] **Step 9: Review the final diff and ownership boundary**

Run:

```bash
git status --short
git diff --check
git log --oneline --decorate -12
git -C /home/sjh/leisai_hand/roboparty_motors status --short
git -C /home/sjh/leisai_hand/roboparty_hand status --short
git -C /home/sjh/leisai_hand/roboparty_deploy status --short
```

Expected: dexhand contains only intended changes and no generated output; `git diff --check` is clean; all implementation commits follow Lore trailers; all three read-only repository states match Step 1's baseline.

- [ ] **Step 10: Record honest completion evidence**

The final report must list changed files by component, simplifications (motors dependency removed, unsupported API removed, private targets unexported), exact passing test/build/install commands, and remaining release risks. Until separately proven, state these as open gates:

```text
vcan two-socket kernel integration: not run without privilege
AArch64 native build/install/C++ smoke/Python import: not run on x86 host
Vendor binary public redistribution: blocked pending vendor authorization
Physical hand monitor shutdown and motion: manual hardware acceptance only
```

Do not make an extra commit solely for test output. If verification required a code fix, add a focused regression test first, make the minimal fix, rerun the affected and full suites, and commit with a Lore `Tested:` trailer.

## Self-Review Result

The plan covers every approved specification section:

| Specification area | Implementing tasks |
| --- | --- |
| Ownership, repository layout, protocol boundary | 1, 2, 4, 9 |
| Base API, factory, models, enums, Doxygen | 7, 8 |
| Vendor SDK adapter and checked return codes | 5, 6 |
| SocketCAN filters, frame conversion, callbacks, cleanup | 3, 4, 6 |
| Lifecycle, rollback, concurrency, one-active-instance rule | 3, 6 |
| Shared runtime, pure CMake, ament, install/export/RPATH | 9, 10 |
| x86-64/AArch64 artifact selection and release gates | 2, 9, 11 |
| Package metadata, licenses, migration and manual safety | 10 |
| Unit, Python, installed consumer, vcan, static checks | 3-11 |

Placeholder scan: no `TBD`, `TODO`, deferred implementation, or undefined
method/type names remain. Type review: public model `0/1`, vendor model `0/2`,
`DriverState`, `TxContext`, SDK adapter methods, transport frame fields, target
names, and Python keyword names are consistent across declarations, tests,
implementation snippets, install checks, and documentation. The only deliberate
delta from the approved spec is the documented SDK-lock refinement at the top
of this plan and in Task 1; it prevents receive-dependent SDK calls from
deadlocking while preserving handle lifetime through callback drains.
