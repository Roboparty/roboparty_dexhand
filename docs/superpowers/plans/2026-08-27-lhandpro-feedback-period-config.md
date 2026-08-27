# LHandPro Feedback-Period Provisioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Install a `roboparty-dexhand-config` command that safely reads, sets, verifies, and persists the LHandPro 6DOF S feedback period at 20 ms without enabling, homing, stopping, or moving the hand.

**Architecture:** Extend the private vendor-SDK adapter with its existing SDO get/set/save calls, then isolate the six-axis transaction in a deterministic helper. Add a private provisioning lifecycle to `LHandProDriver` that reuses its callback and SocketCAN ownership but skips every motion-state command, and place a tested CLI over that private interface without changing installed C++ headers or Python bindings. Keep the stored SDO base period distinct from runtime emission selection: subindex `0x14` value `200` sets a 20 ms base period, while runtime payload `00 04 50 01 5A 01` uses multiplier `0x01` to request each type every one base period.

**Tech Stack:** C++17, CMake 3.15+, Linux SocketCAN/CAN-FD, pinned LHandPro C SDK, existing fake SDK/transport tests, CTest, can-utils for physical observation only.

---

## File Structure

Create these focused files:

- `src/drivers/lhandpro/lhandpro_feedback_period.hpp`: private constants,
  result types, and the six-axis SDO transaction declaration.
- `src/drivers/lhandpro/lhandpro_feedback_period.cpp`: read/apply/rollback
  transaction implementation independent of CLI parsing and driver lifecycle.
- `src/tools/lhandpro_config_cli.hpp`: private CLI entry point and injectable
  concrete-driver factory used by tests.
- `src/tools/lhandpro_config_cli.cpp`: argument parsing, provisioning lifecycle,
  stable result labels, and exit-code mapping.
- `src/tools/roboparty_dexhand_config_main.cpp`: minimal production `main()`.
- `src/tools/CMakeLists.txt`: private CLI support target and installed executable.
- `tests/test_lhandpro_feedback_period.cpp`: deterministic transaction tests.
- `tests/test_lhandpro_config_cli.cpp`: CLI validation, lifecycle, output, and
  no-motion tests with fake SDK and transport.

Modify these existing files:

- `src/drivers/lhandpro/lhandpro_sdk.hpp`: add private virtual SDO operations.
- `src/drivers/lhandpro/lhandpro_sdk.cpp`: delegate to the vendor C functions.
- `src/drivers/lhandpro/lhandpro_driver.hpp`: add private-installed provisioning
  entry points and session-purpose state.
- `src/drivers/lhandpro/lhandpro_driver.cpp`: share initialization while making
  provisioning skip all motion commands and safety motion cleanup.
- `src/drivers/lhandpro/CMakeLists.txt`: compile the transaction source.
- `src/CMakeLists.txt`: add tools only after `dexhand` exists.
- `tests/fakes/fake_lhandpro_sdk.hpp`: model SDO values, writes, saves, and
  scripted failures.
- `tests/test_lhandpro_sdk.cpp`: cover invalid-handle SDO adapter behavior.
- `tests/test_lhandpro_driver.cpp`: prove provisioning lifecycle and state rules.
- `tests/CMakeLists.txt`: register transaction and CLI tests.
- `tests/check_install_export.cmake`: verify relocated installed CLI and RPATH.
- `package.xml`: bump the package minor version to 0.3.0.
- `README.md`: document one-time provisioning, 20 ms semantics, and boundaries.

Do not modify `include/hand_driver.hpp`, `src/pybind_module.cpp`,
`roboparty_deploy`, or any sibling repository.

### Task 1: Wrap the Existing Vendor SDO Calls

**Files:**
- Modify: `src/drivers/lhandpro/lhandpro_sdk.hpp`
- Modify: `src/drivers/lhandpro/lhandpro_sdk.cpp`
- Modify: `tests/fakes/fake_lhandpro_sdk.hpp`
- Modify: `tests/test_lhandpro_sdk.cpp`

- [ ] **Step 1: Write the failing SDK adapter test**

Add invalid-handle assertions before `sdk.create()` in
`tests/test_lhandpro_sdk.cpp` so no real CAN communication is attempted:

```cpp
unsigned int sdo_value = 0xDEADBEEFU;
CHECK_EQ(sdk.get_sdo_drive_param(0x201D, 0x14, sdo_value), -1);
CHECK_EQ(sdo_value, 0xDEADBEEFU);
CHECK_EQ(sdk.set_sdo_drive_param(0x201D, 0x14, 200U), -1);
CHECK_EQ(sdk.save_sdo_drive_param(), -1);
```

- [ ] **Step 2: Build the test to verify it fails**

Run:

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --target lhandpro_sdk --parallel
```

Expected: compilation fails because `CapiLHandProSdk` has no SDO methods.

- [ ] **Step 3: Add the three private virtual operations**

Add to `LHandProSdk` and matching `override` declarations to
`CapiLHandProSdk` in `src/drivers/lhandpro/lhandpro_sdk.hpp`:

```cpp
virtual int get_sdo_drive_param(unsigned int index,
                                unsigned char subindex,
                                unsigned int& value) noexcept = 0;
virtual int set_sdo_drive_param(unsigned int index,
                                unsigned char subindex,
                                unsigned int value) noexcept = 0;
virtual int save_sdo_drive_param() noexcept = 0;
```

Implement exact delegation in `src/drivers/lhandpro/lhandpro_sdk.cpp`:

```cpp
int CapiLHandProSdk::get_sdo_drive_param(
    unsigned int index, unsigned char subindex,
    unsigned int& value) noexcept {
  return handle_ ? lhandprolib_get_sdo_drive_param(
                       as_handle(handle_), index, subindex, &value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::set_sdo_drive_param(
    unsigned int index, unsigned char subindex,
    unsigned int value) noexcept {
  return handle_ ? lhandprolib_set_sdo_drive_param(
                       as_handle(handle_), index, subindex, value)
                 : kInvalidHandle;
}

int CapiLHandProSdk::save_sdo_drive_param() noexcept {
  return handle_ ? lhandprolib_save_sdo_drive_param(as_handle(handle_))
                 : kInvalidHandle;
}
```

- [ ] **Step 4: Give the fake a deterministic SDO object dictionary**

Add this public test API and overrides to `FakeLHandProSdk`:

```cpp
struct SdoAccess {
  unsigned int index;
  unsigned char subindex;
  unsigned int value;
};

FakeLHandProSdk() {
  for (const unsigned int index : {
           0x201DU, 0x205DU, 0x209DU,
           0x20DDU, 0x211DU, 0x215DU}) {
    sdo_values_.emplace(index, 200U);
  }
}

void set_sdo_value(unsigned int index, unsigned int value) {
  std::lock_guard<std::mutex> lock(mutex_);
  sdo_values_[index] = value;
}

std::vector<SdoAccess> sdo_write_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sdo_writes_;
}

int get_sdo_drive_param(unsigned int index, unsigned char subindex,
                        unsigned int& value) noexcept override {
  const int code = result_("get_sdo_drive_param");
  if (code != 0) return code;
  if (subindex != 0x14U) return failure_code;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    value = sdo_values_.at(index);
    sdo_reads_.push_back({index, subindex, value});
    return 0;
  } catch (...) {
    return failure_code;
  }
}

int set_sdo_drive_param(unsigned int index, unsigned char subindex,
                        unsigned int value) noexcept override {
  const int code = result_("set_sdo_drive_param");
  if (code != 0) return code;
  if (subindex != 0x14U) return failure_code;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    sdo_values_[index] = value;
    sdo_writes_.push_back({index, subindex, value});
    return 0;
  } catch (...) {
    return failure_code;
  }
}

int save_sdo_drive_param() noexcept override {
  return result_("save_sdo_drive_param");
}
```

Add these private members:

```cpp
std::unordered_map<unsigned int, unsigned int> sdo_values_;
std::vector<SdoAccess> sdo_reads_;
std::vector<SdoAccess> sdo_writes_;
```

- [ ] **Step 5: Run the focused SDK test**

Run:

```bash
cmake --build build --target lhandpro_sdk --parallel
ctest --test-dir build -R '^lhandpro_sdk$' --output-on-failure
```

Expected: one test passes; no socket or hand is initialized.

- [ ] **Step 6: Commit the SDK bridge**

```bash
git add src/drivers/lhandpro/lhandpro_sdk.hpp \
  src/drivers/lhandpro/lhandpro_sdk.cpp \
  tests/fakes/fake_lhandpro_sdk.hpp tests/test_lhandpro_sdk.cpp
git commit -m "Wrap LHandPro SDO parameter APIs"
```

### Task 2: Implement the Six-Axis Transaction

**Files:**
- Create: `src/drivers/lhandpro/lhandpro_feedback_period.hpp`
- Create: `src/drivers/lhandpro/lhandpro_feedback_period.cpp`
- Create: `tests/test_lhandpro_feedback_period.cpp`
- Modify: `src/drivers/lhandpro/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Define failing transaction tests**

Create `tests/test_lhandpro_feedback_period.cpp` using `test_support.hpp` and
`FakeLHandProSdk`. Define the expected constants in test data:

```cpp
constexpr std::array<unsigned int, 6> kIndexes{
    0x201D, 0x205D, 0x209D, 0x20DD, 0x211D, 0x215D};
```

Add named checks for all required paths:

```cpp
void check_show_reads_all_six();
void check_show_reports_partial_read_failure();
void check_apply_already_compliant_does_not_write_or_save();
void check_apply_writes_all_six_and_saves_once();
void check_each_write_failure_rolls_back_without_save();
void check_readback_failure_rolls_back_without_save();
void check_readback_mismatch_rolls_back_without_save();
void check_failed_rollback_reports_uncertain_state();
void check_save_failure_reports_unknown_persistence_without_retry();
```

For the successful mutation, seed one index to 100 and assert six writes of
`{index, 0x14, 200}`, six successful readbacks, exactly one
`save_sdo_drive_param`, and outcome `Saved`. For the already-compliant case,
assert zero writes and zero saves.

- [ ] **Step 2: Register and run the missing-source test**

Add to `tests/CMakeLists.txt`:

```cmake
add_dexhand_test(feedback_period test_lhandpro_feedback_period.cpp)
```

Run:

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --target feedback_period --parallel
```

Expected: compilation fails because the feedback-period transaction types do
not exist.

- [ ] **Step 3: Define the private transaction contract**

Create `src/drivers/lhandpro/lhandpro_feedback_period.hpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty
#pragma once

#include "drivers/lhandpro/lhandpro_sdk.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace roboparty::dexhand::detail {

inline constexpr std::array<unsigned int, 6> kFeedbackPeriodIndexes{
    0x201D, 0x205D, 0x209D, 0x20DD, 0x211D, 0x215D};
inline constexpr unsigned char kFeedbackPeriodSubindex = 0x14;
inline constexpr unsigned int kFeedbackPeriod20msUnits = 200;

enum class FeedbackPeriodOutcome {
  Shown,
  AlreadyCompliant,
  Saved,
  ReadFailed,
  FailedRestored,
  FailedUncertain,
  SaveFailed,
};

struct FeedbackPeriodFailure {
  std::string operation;
  int code{0};
  std::size_t axis{0};
};

struct FeedbackPeriodReport {
  FeedbackPeriodOutcome outcome{FeedbackPeriodOutcome::ReadFailed};
  std::array<unsigned int, 6> before{};
  std::array<unsigned int, 6> after{};
  std::size_t before_count{0};
  std::size_t after_count{0};
  FeedbackPeriodFailure failure{};
  bool rollback_attempted{false};
  bool rollback_verified{false};
  bool save_attempted{false};

  bool success() const noexcept {
    return outcome == FeedbackPeriodOutcome::Shown ||
           outcome == FeedbackPeriodOutcome::AlreadyCompliant ||
           outcome == FeedbackPeriodOutcome::Saved;
  }
};

class LHandProFeedbackPeriod final {
 public:
  explicit LHandProFeedbackPeriod(LHandProSdk& sdk) noexcept : sdk_(sdk) {}
  FeedbackPeriodReport show();
  FeedbackPeriodReport apply_20ms();

 private:
  bool read_all_(std::array<unsigned int, 6>& values,
                 std::size_t& count, FeedbackPeriodFailure& failure);
  bool write_all_(const std::array<unsigned int, 6>& values,
                  FeedbackPeriodFailure& failure);
  bool rollback_(const std::array<unsigned int, 6>& original,
                 FeedbackPeriodReport& report);

  LHandProSdk& sdk_;
};

const char* feedback_period_outcome_name(
    FeedbackPeriodOutcome outcome) noexcept;

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 4: Implement the exact transaction order**

Create `src/drivers/lhandpro/lhandpro_feedback_period.cpp`, include its matching
header plus `<algorithm>`, and implement these rules:

```cpp
#include "drivers/lhandpro/lhandpro_feedback_period.hpp"

#include <algorithm>

FeedbackPeriodReport LHandProFeedbackPeriod::show() {
  FeedbackPeriodReport report;
  if (!read_all_(report.before, report.before_count, report.failure)) {
    report.outcome = FeedbackPeriodOutcome::ReadFailed;
    return report;
  }
  report.after = report.before;
  report.after_count = report.before_count;
  report.outcome = FeedbackPeriodOutcome::Shown;
  return report;
}

FeedbackPeriodReport LHandProFeedbackPeriod::apply_20ms() {
  FeedbackPeriodReport report = show();
  if (!report.success()) return report;
  if (std::all_of(report.before.begin(), report.before.end(),
                  [](unsigned int value) {
                    return value == kFeedbackPeriod20msUnits;
                  })) {
    report.outcome = FeedbackPeriodOutcome::AlreadyCompliant;
    return report;
  }

  std::array<unsigned int, 6> target{};
  target.fill(kFeedbackPeriod20msUnits);
  if (!write_all_(target, report.failure)) {
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }
  if (!read_all_(report.after, report.after_count, report.failure) ||
      report.after != target) {
    if (report.failure.operation.empty()) {
      report.failure = {"verify_feedback_period", -2, 0};
    }
    report.outcome = rollback_(report.before, report)
                         ? FeedbackPeriodOutcome::FailedRestored
                         : FeedbackPeriodOutcome::FailedUncertain;
    return report;
  }

  report.save_attempted = true;
  const int save_code = sdk_.save_sdo_drive_param();
  if (save_code != 0) {
    report.failure = {"save_sdo_drive_param", save_code, 0};
    report.outcome = FeedbackPeriodOutcome::SaveFailed;
    return report;
  }
  report.outcome = FeedbackPeriodOutcome::Saved;
  return report;
}
```

Implement `read_all_` in index order, count only successful reads, and report
the 1-based axis on failure. Implement `write_all_` in index order and stop on
the first failure. Implement `rollback_` as exactly one all-six write followed
by one all-six read/compare, never calling save. `feedback_period_outcome_name`
must return stable labels:

```text
shown
already-compliant
saved
read-failed
failed-restored
failed-uncertain
save-failed
```

Add `lhandpro_feedback_period.cpp` to `lhandpro_driver` in
`src/drivers/lhandpro/CMakeLists.txt`.

- [ ] **Step 5: Run transaction tests**

```bash
cmake --build build --target feedback_period --parallel
ctest --test-dir build -R '^feedback_period$' --output-on-failure
```

Expected: every transaction case passes and no transport is opened.

- [ ] **Step 6: Commit the transaction**

```bash
git add src/drivers/lhandpro/lhandpro_feedback_period.hpp \
  src/drivers/lhandpro/lhandpro_feedback_period.cpp \
  src/drivers/lhandpro/CMakeLists.txt tests/test_lhandpro_feedback_period.cpp \
  tests/CMakeLists.txt
git commit -m "Add verified feedback-period transaction"
```

### Task 3: Add a No-Motion Provisioning Lifecycle

**Files:**
- Modify: `src/drivers/lhandpro/lhandpro_driver.hpp`
- Modify: `src/drivers/lhandpro/lhandpro_driver.cpp`
- Modify: `tests/test_lhandpro_driver.cpp`

- [ ] **Step 1: Write failing provisioning lifecycle tests**

Add tests that construct `LHandProDriver` with the existing fake SDK and fake
transport, then assert:

```cpp
CHECK(fixture.driver->init_for_provisioning());
CHECK_EQ(fixture.sdk->count_set_enable(true), 0);
CHECK_EQ(fixture.sdk->count_set_enable(false), 0);
CHECK_EQ(fixture.sdk->count("home_motors"), 0);
CHECK_EQ(fixture.sdk->count("move_motors"), 0);
CHECK_EQ(fixture.sdk->count("stop_motors"), 0);
CHECK_EQ(fixture.sdk->count_set_move_no_home(1), 0);
CHECK_EQ(fixture.sdk->count_set_move_no_home(0), 0);

const auto report = fixture.driver->show_feedback_period();
CHECK(report.success());
fixture.driver->deinit_hand();

CHECK_EQ(fixture.sdk->count("stop_monitor"), 1);
CHECK_EQ(fixture.sdk->count("close"), 1);
CHECK_EQ(fixture.sdk->count("clear_tx"), 1);
CHECK_EQ(fixture.sdk->count("destroy"), 1);
CHECK_EQ(fixture.sdk->count("stop_motors"), 0);
CHECK_EQ(fixture.sdk->count_set_enable(false), 0);
CHECK_EQ(fixture.sdk->count_set_move_no_home(0), 0);
```

Also prove `show_feedback_period()` and `apply_feedback_period_20ms()` reject
Created state and 16DOF model, and prove normal `init_hand()` retains its current
enable/home/no-home and safety-cleanup behavior. During a provisioning `show`,
use the fake SDK `before_call` hook to deliver a `0x581` frame while
`decode_canfd` is scripted to fail; assert the asynchronous fault is surfaced,
the transaction does not save, and cleanup still completes.

- [ ] **Step 2: Run the lifecycle test to verify it fails**

```bash
cmake --build build --target lhandpro_lifecycle --parallel
```

Expected: compilation fails because provisioning entry points do not exist.

- [ ] **Step 3: Add private-installed lifecycle and operation entry points**

Include `lhandpro_feedback_period.hpp` from the private driver header and add:

```cpp
bool init_for_provisioning();
roboparty::dexhand::detail::FeedbackPeriodReport show_feedback_period();
roboparty::dexhand::detail::FeedbackPeriodReport
apply_feedback_period_20ms();
```

Add private session state:

```cpp
enum class SessionPurpose { Motion, Provisioning };
bool init_session_(SessionPurpose purpose, bool enable_motors,
                   bool home_motors, float home_wait_time);
SessionPurpose session_purpose_{SessionPurpose::Motion};
bool safety_cleanup_required_{false};
```

- [ ] **Step 4: Refactor initialization without changing public semantics**

Make the existing methods delegate as follows:

```cpp
bool LHandProDriver::init_hand(bool enable_motors, bool home_motors,
                               float home_wait_time) {
  return init_session_(SessionPurpose::Motion, enable_motors, home_motors,
                       home_wait_time);
}

bool LHandProDriver::init_for_provisioning() {
  if (model_ != LHandProModel::Dof6S) return false;
  return init_session_(SessionPurpose::Provisioning, false, false, 0.0F);
}
```

Move the existing `init_hand()` body into `init_session_`. At the beginning of a
new attempt set:

```cpp
session_purpose_ = purpose;
safety_cleanup_required_ = purpose == SessionPurpose::Motion;
```

Preserve model, DOF, transport, callback, `initial_ex`, monitor, and Ready-state
validation for both purposes. Execute `set_enable`, `home_motors`, and
`set_move_no_home(1)` only inside:

```cpp
if (purpose == SessionPurpose::Motion) {
  // Existing enable, optional home, and set_move_no_home calls stay here.
}
```

In `cleanup_locked_`, replace the safety-command condition with:

```cpp
if (safety_cleanup_required_ && initial_ex_attempted_ &&
    !safety_cleanup_attempted_ && sdk_created_) {
  // Existing stop_motors(0), set_enable(0, false),
  // and set_move_no_home(0) sequence remains unchanged.
}
```

Reset `safety_cleanup_required_` and `session_purpose_` after cleanup. This is
the gate that guarantees provisioning never issues a motor-state command.

- [ ] **Step 5: Add the internal feedback-period methods**

Implement both methods with the established double state check and SDK lock:

```cpp
FeedbackPeriodReport LHandProDriver::show_feedback_period() {
  validate_call_state_(false, "show_feedback_period");
  if (model_ != LHandProModel::Dof6S ||
      session_purpose_ != SessionPurpose::Provisioning) {
    throw std::logic_error("LHandPro feedback provisioning requires 6DOF S provisioning session");
  }
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "show_feedback_period");
  auto report = LHandProFeedbackPeriod(*sdk_).show();
  check_health();
  return report;
}

FeedbackPeriodReport LHandProDriver::apply_feedback_period_20ms() {
  validate_call_state_(false, "apply_feedback_period_20ms");
  if (model_ != LHandProModel::Dof6S ||
      session_purpose_ != SessionPurpose::Provisioning) {
    throw std::logic_error("LHandPro feedback provisioning requires 6DOF S provisioning session");
  }
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "apply_feedback_period_20ms");
  auto report = LHandProFeedbackPeriod(*sdk_).apply_20ms();
  check_health();
  return report;
}
```

Do not add either method to `HandDriver` or pybind11.

- [ ] **Step 6: Run lifecycle and regression tests**

```bash
cmake --build build --target lhandpro_lifecycle factory_contract --parallel
ctest --test-dir build -R '^(lhandpro_lifecycle|factory_contract|feedback_period)$' \
  --output-on-failure
```

Expected: three tests pass; existing normal cleanup assertions remain intact.

- [ ] **Step 7: Commit the provisioning lifecycle**

```bash
git add src/drivers/lhandpro/lhandpro_driver.hpp \
  src/drivers/lhandpro/lhandpro_driver.cpp tests/test_lhandpro_driver.cpp
git commit -m "Add no-motion LHandPro provisioning session"
```

### Task 4: Build the Installed CLI

**Files:**
- Create: `src/tools/lhandpro_config_cli.hpp`
- Create: `src/tools/lhandpro_config_cli.cpp`
- Create: `src/tools/roboparty_dexhand_config_main.cpp`
- Create: `src/tools/CMakeLists.txt`
- Create: `tests/test_lhandpro_config_cli.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing CLI tests with an injected driver factory**

Create tests for:

```text
--help -> 0 and no factory call
missing subcommand -> 2 and no factory call
unknown option -> 2 and no factory call
empty interface -> 2 and no factory call
node 0 or 128 -> 2 and no factory call
apply without --save -> 2 and no factory call
apply with milliseconds other than 20 -> 2 and no factory call
show success -> 0 and six values printed
already-compliant apply -> 0 and result=already-compliant
saved apply -> 0 and result=saved
transaction failure -> 1 with stable result label and failure details
initialization failure -> 1 and cleanup attempted
cleanup failure -> 1 even after successful transaction
```

For every valid command, retain raw fake SDK and transport pointers from the
factory and assert zero enable, home, move, stop, and move-no-home calls.

- [ ] **Step 2: Register the missing CLI test**

In `tests/CMakeLists.txt` add an explicit target linked to the private CLI
support library:

```cmake
add_executable(lhandpro_config_cli_test test_lhandpro_config_cli.cpp)
target_compile_features(lhandpro_config_cli_test PRIVATE cxx_std_17)
target_include_directories(lhandpro_config_cli_test PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(lhandpro_config_cli_test PRIVATE
  lhandpro_config_cli Threads::Threads)
add_test(NAME lhandpro_config_cli COMMAND lhandpro_config_cli_test)
set_tests_properties(lhandpro_config_cli PROPERTIES TIMEOUT 30)
```

Run CMake and expect configuration to fail because `lhandpro_config_cli` does
not exist.

- [ ] **Step 3: Define the internal CLI seam**

Create `src/tools/lhandpro_config_cli.hpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty
#pragma once

#include "drivers/lhandpro/lhandpro_driver.hpp"

#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace roboparty::dexhand::detail {

using ConfigDriverFactory = std::function<std::unique_ptr<LHandProDriver>(
    const std::string&, int)>;

int run_lhandpro_config_cli(int argc, const char* const argv[],
                            std::ostream& output, std::ostream& error,
                            const ConfigDriverFactory& factory);

std::unique_ptr<LHandProDriver> make_lhandpro_config_driver(
    const std::string& interface, int node_id);

}  // namespace roboparty::dexhand::detail
```

- [ ] **Step 4: Implement parsing before any factory call**

In `lhandpro_config_cli.cpp`, parse the exact grammar:

```text
roboparty-dexhand-config feedback-period show
  --interface NAME --node-id ID

roboparty-dexhand-config feedback-period apply
  --interface NAME --node-id ID --milliseconds 20 --save
```

Accept options in any order after `show` or `apply`, reject duplicates, parse
integers with full-string `std::from_chars`, and return 2 for every usage error.
Handle `--help` before validating other arguments. Only after a complete valid
parse call:

```cpp
auto driver = factory(options.interface, options.node_id);
```

The production factory is exactly:

```cpp
return std::make_unique<LHandProDriver>(
    interface, LHandProModel::Dof6S, node_id);
```

- [ ] **Step 5: Implement lifecycle, reporting, and cleanup**

Call `init_for_provisioning()`, then `show_feedback_period()` or
`apply_feedback_period_20ms()`. Print one stable `result=<label>` line plus six
`axis=N index=0xNNNN before=V after=V` lines when available. Print failure
operation, axis, and code on a failed report.

Always call `deinit_hand()` after a factory was successfully created, including
after exceptions. Preserve the first operation failure while appending cleanup
failure text. Return 0 only when the report is successful and cleanup succeeds;
return 1 for all runtime and cleanup failures.

Create the minimal production `main()`:

```cpp
#include "tools/lhandpro_config_cli.hpp"

#include <iostream>

int main(int argc, char** argv) {
  return roboparty::dexhand::detail::run_lhandpro_config_cli(
      argc, argv, std::cout, std::cerr,
      roboparty::dexhand::detail::make_lhandpro_config_driver);
}
```

- [ ] **Step 6: Add private targets and install only the executable**

Create `src/tools/CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: GPL-3.0
# Copyright (C) 2026 Roboparty

add_library(lhandpro_config_cli STATIC lhandpro_config_cli.cpp)
target_compile_features(lhandpro_config_cli PUBLIC cxx_std_17)
target_include_directories(lhandpro_config_cli PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lhandpro_config_cli PUBLIC dexhand)

add_executable(roboparty-dexhand-config
  roboparty_dexhand_config_main.cpp)
target_compile_features(roboparty-dexhand-config PRIVATE cxx_std_17)
target_link_libraries(roboparty-dexhand-config PRIVATE lhandpro_config_cli)
set_target_properties(roboparty-dexhand-config PROPERTIES
  INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS roboparty-dexhand-config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

Add `add_subdirectory(tools)` at the end of `src/CMakeLists.txt`, after the
`dexhand` target is fully defined.

- [ ] **Step 7: Run CLI tests and inspect public bindings**

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --target lhandpro_config_cli_test \
  roboparty-dexhand-config --parallel
ctest --test-dir build -R '^(lhandpro_config_cli|pybind_api|factory_contract)$' \
  --output-on-failure
rg -n 'feedback_period|sdo' include/hand_driver.hpp src/pybind_module.cpp
```

Expected: three tests pass; the final `rg` returns no matches.

- [ ] **Step 8: Commit the installed CLI**

```bash
git add src/tools src/CMakeLists.txt tests/test_lhandpro_config_cli.cpp \
  tests/CMakeLists.txt
git commit -m "Add LHandPro feedback provisioning CLI"
```

### Task 5: Add Install Gates, Version, and Documentation

**Files:**
- Modify: `tests/check_install_export.cmake`
- Modify: `package.xml`
- Modify: `README.md`

- [ ] **Step 1: Make the install gate require the new executable**

In `tests/check_install_export.cmake`, read and validate
`CMAKE_INSTALL_BINDIR` with the same non-empty relative-path constraints as
`CMAKE_INSTALL_LIBDIR`. After installation require exactly:

```cmake
set(CONFIG_TOOL "${PREFIX_REAL}/${INSTALL_BINDIR}/roboparty-dexhand-config")
if(NOT EXISTS "${CONFIG_TOOL}" OR IS_DIRECTORY "${CONFIG_TOOL}")
  message(FATAL_ERROR "installed feedback configuration tool is missing")
endif()
```

After relocation, run the tool without ambient library paths:

```cmake
set(RELOCATED_CONFIG_TOOL
  "${RELOCATED}/${INSTALL_BINDIR}/roboparty-dexhand-config")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LD_LIBRARY_PATH
    "${RELOCATED_CONFIG_TOOL}" --help
  WORKING_DIRECTORY "/tmp"
  RESULT_VARIABLE config_help_rc
  OUTPUT_VARIABLE config_help_out
  ERROR_VARIABLE config_help_err)
if(NOT config_help_rc EQUAL 0 OR
   NOT config_help_out MATCHES "feedback-period")
  message(FATAL_ERROR
    "relocated config tool failed: ${config_help_out}${config_help_err}")
endif()
```

This tests executable installation and runtime lookup without touching CAN.

- [ ] **Step 2: Bump minor version and update version-contract checks**

Change `package.xml` from `0.2.0` to `0.3.0`. In
`tests/check_install_export.cmake`, rename version scratch paths to
`version-0.2` and `version-0.3`, reject requested version 0.2, and accept 0.3.

- [ ] **Step 3: Document the production workflow and semantics**

Add a `Feedback-Period Provisioning` section to `README.md` containing the exact
`show` and `apply` commands. State explicitly:

```text
SDO subindex 0x14 value 200 = 20 ms base TPDO period
runtime payload 00 04 50 01 5A 01 = each type every one base period
runtime multiplier 0x14 = 20 base periods = 400 ms, not 20 ms
20 ms = 50 emissions/second for frame type 0x50
20 ms = 50 emissions/second for frame type 0x5A
observed aggregate = approximately 100 CAN-FD frames/second
```

Document that the two frame types are distinct, that `0x5A` is documented as
axis status/status2 while the supplied protocol export does not define `0x50`,
and that normal `HandDriver::init_hand()` never writes the period. Require all
other hand-control processes to be stopped, and require power-cycle plus `show`
after a saved change.

- [ ] **Step 4: Run the install/export gate**

```bash
cmake --build build --parallel
scratch_root=$(mktemp -d)
cmake -DBUILD_DIR="$PWD/build" -DSOURCE_DIR="$PWD" \
  -DPREFIX="$scratch_root/install" \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P tests/check_install_export.cmake
rm -rf "$scratch_root"
```

Expected: install, relocation, C++ consumer, version checks, Python API, and
relocated CLI help all succeed.

- [ ] **Step 5: Commit release metadata and docs**

```bash
git add package.xml README.md tests/check_install_export.cmake
git commit -m "Document feedback-period provisioning"
```

### Task 6: Run All Local Release Gates

**Files:**
- Verify only; modify a source file only when a failing gate identifies a real
  defect, and rerun the focused TDD cycle before the full suite.

- [ ] **Step 1: Run a fresh strict build**

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel
```

Expected: zero warnings promoted to errors and all targets link.

- [ ] **Step 2: Run every hardware-free CTest**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all registered tests pass, including the new transaction and CLI
tests. Record the exact pass count rather than retaining the previous 8/8
number.

- [ ] **Step 3: Run the real vcan two-socket gate**

Create `vcan-dexhand0` only if absent, configure a vcan-enabled build, run both
vcan tests, and remove only an interface created by this step:

```bash
created_vcan=0
if ! ip link show vcan-dexhand0 >/dev/null 2>&1; then
  sudo ip link add dev vcan-dexhand0 type vcan
  created_vcan=1
fi
sudo ip link set dev vcan-dexhand0 up
cmake -S . -B build-vcan -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DDEXHAND_ENABLE_VCAN_TESTS=ON
cmake --build build-vcan --parallel
ctest --test-dir build-vcan \
  -R '^(vcan_two_socket|vcan_observation_state)$' --output-on-failure
if [ "$created_vcan" -eq 1 ]; then
  sudo ip link del dev vcan-dexhand0
fi
```

Expected: both vcan tests pass and no unrelated network interface changes.

- [ ] **Step 4: Repeat install, ABI-boundary, and source checks**

```bash
scratch_root=$(mktemp -d)
cmake -DBUILD_DIR="$PWD/build" -DSOURCE_DIR="$PWD" \
  -DPREFIX="$scratch_root/install" \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P tests/check_install_export.cmake
rm -rf "$scratch_root"
git diff --check
rg -n 'feedback_period|sdo' include/hand_driver.hpp src/pybind_module.cpp
git status --short
```

Expected: install gate succeeds, diff check is clean, public API search has no
matches, and only intentionally uncommitted evidence/documentation remains.

### Task 7: Run ARM64 No-Motion Physical Acceptance

**Files:**
- Modify after evidence: `README.md`
- Do not add board credentials, raw private logs, or mutable device identifiers
  to the repository.

- [ ] **Step 1: Deploy the exact source commit through the GitHub-style flow**

On the Orange Pi, clone or fetch the exact implementation commit into a fresh
directory, verify `git rev-parse HEAD`, then configure a native strict build:

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-Werror
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: AArch64 native build succeeds and every hardware-free test passes.

- [ ] **Step 2: Install to a fresh prefix and verify the relocated CLI**

```bash
cmake --install build --prefix "$PWD/install"
env -u LD_LIBRARY_PATH "$PWD/install/bin/roboparty-dexhand-config" --help
```

Expected: help succeeds through installed RPATH without changing CAN.

- [ ] **Step 3: Capture a read-only preflight**

Stop other hand-control processes. Record `ip -details -statistics link show
can1`, RX drops, CAN state/error counters, and process inventory. Then run:

```bash
env -u LD_LIBRARY_PATH "$PWD/install/bin/roboparty-dexhand-config" \
  feedback-period show --interface can1 --node-id 1
```

Expected: six indexes read as 200, no enable/home/move frames are emitted, and
the process exits zero with `result=shown`.

- [ ] **Step 4: Prove the already-compliant no-write path**

Run the installed `apply` command:

```bash
env -u LD_LIBRARY_PATH "$PWD/install/bin/roboparty-dexhand-config" \
  feedback-period apply --interface can1 --node-id 1 \
  --milliseconds 20 --save
```

Expected: `result=already-compliant`, zero parameter writes, zero save request,
zero motion-state commands, and clean shutdown.

- [ ] **Step 5: Exercise the real write path only with explicit authorization**

Set all six period objects to the transient value 201 (`0x00C9`) with these
exact CAN-FD SDO writes. Do not send the `0x1010:01` save object:

```bash
cansend can1 601##42B1D2014C9000000
cansend can1 601##42B5D2014C9000000
cansend can1 601##42B9D2014C9000000
cansend can1 601##42BDD2014C9000000
cansend can1 601##42B1D2114C9000000
cansend can1 601##42B5D2114C9000000
```

For each write require the matching `0x581` SDO acknowledgement beginning with
`60` and the same index/subindex. Read all six back through the installed
`show` command and require value 201, then immediately run the installed
`apply` command from Step 4.

Expected: the production command writes all six values to 200, reads all six
back as 200, receives one save acknowledgement, emits no motor-state command,
and reports `result=saved`. If any write or verification fails, stop the test,
follow the command's rollback/power-cycle instruction, and do not improvise a
second mutation.

- [ ] **Step 6: Power-cycle and prove persistence**

After the operator power-cycles the hand, rerun `show`.

Expected: every index remains 200. A successful pre-power-cycle save alone is
not accepted as persistence evidence.

- [ ] **Step 7: Measure both real-time feedback types for five seconds**

Capture CAN ID `0x481` and count the first payload byte:

```bash
timeout 5 candump -L 'can1,481:7FF' | awk '
  $3 == "481" && $5 == "50" {type50++}
  $3 == "481" && $5 == "5A" {type5a++}
  END {printf "type50=%d type5A=%d total=%d\n",
              type50, type5a, type50 + type5a}'
```

Expected: approximately 250 of type `0x50`, approximately 250 of type `0x5A`,
and approximately 500 aggregate frames. Compare fresh pre/post counters and
require no new drops, bus errors, protocol errors, error-passive transition, or
bus-off transition.

- [ ] **Step 8: Record verified release evidence and commit it**

Update the README ARM64 release-gate section with the exact source commit,
native test count, installed command result labels, six readback values,
observed frame counts, CAN error deltas, and evidence location on the board.
Do not claim `0x50` field semantics that the supplied protocol does not define.

```bash
git add README.md
git commit -m "Record ARM64 feedback provisioning validation"
git status --short
```

Expected: README evidence commit succeeds and the working tree is clean.
