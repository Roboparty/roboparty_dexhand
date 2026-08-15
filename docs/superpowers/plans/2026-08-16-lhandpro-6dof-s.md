# LHandPro 6DOF S Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing public `LHANDPRO_6DOF=0` configuration select the
vendor LHandPro 6DOF S model, then prove the change locally and on the connected
Orange Pi without homing or commanding position motion.

**Architecture:** Preserve the public C++/Python/YAML model numbers and convert
them at the existing private LHandPro boundary. Rename only the private model to
`Dof6S`, map it to vendor model `1`, retain exact `(11, 6)` validation, and leave
the existing 16DOF path untouched. Tests use fakes for lifecycle behavior and
the existing restricted SDK smoke for create/model/DOF/destroy only.

**Tech Stack:** C++17, CMake/CTest, pybind11, SocketCAN CAN-FD, LHandPro C SDK,
Python 3, SSH to native AArch64 Orange Pi.

---

### Task 1: Lock the 6DOF S Contract with Failing Tests

**Files:**
- Modify: `tests/test_factory.cpp`
- Modify: `tests/test_lhandpro_driver.cpp`
- Modify: `tests/test_lhandpro_sdk.cpp`

- [ ] **Step 1: Lock the public numeric values**

Add these checks near the start of `tests/test_factory.cpp::main()`:

```cpp
CHECK_EQ(HAND_LHANDPRO_6DOF, 0);
CHECK_EQ(HAND_LHANDPRO_16DOF, 1);
```

These checks protect the public C++ contract. The existing Python enum test
already protects the same values in `tests/test_pybind_api.py`.

- [ ] **Step 2: Make the lifecycle test require vendor model 1**

Keep the current private name temporarily so the test compiles against the old
implementation. In `check_models_and_initializing_callbacks()` change the
6DOF success expectation and ordinary-6DOF rejection case to:

```cpp
CHECK_EQ(six.sdk->hand_type, 1);

Fixture ordinary_six_is_unsupported(LHandProModel::Dof6);
ordinary_six_is_unsupported.sdk->reported_hand_type_override = 0;
CHECK(!ordinary_six_is_unsupported.driver->init_hand(false, false, 0.0F));
CHECK(ordinary_six_is_unsupported.released_once());
CHECK_EQ(ordinary_six_is_unsupported.transport->open_calls.load(), 0);
```

Retain the second-read mismatch case and exact `(11, 6)` mismatch matrix.

- [ ] **Step 3: Make the restricted adapter smoke verify model 1 readback**

Replace the initial model-0 block in `tests/test_lhandpro_sdk.cpp` with:

```cpp
int model = -1;
CHECK_EQ(sdk.set_hand_type(1), 0);
CHECK_EQ(sdk.get_hand_type(model), 0);
CHECK_EQ(model, 1);
CHECK_EQ(sdk.get_dof(total, active), 0);
CHECK_EQ(total, 11);
CHECK_EQ(active, 6);
```

Also read back and assert model `2` before the existing `(21, 16)` checks.
Do not call `initial_ex`, install callbacks, open CAN, enable, home, or move.

- [ ] **Step 4: Run the focused tests and record RED**

Configure and build a fresh temporary test tree:

```bash
DEXHAND_SOURCE=$(git rev-parse --show-toplevel)
DEXHAND_RED_ROOT=$(mktemp -d /tmp/dexhand-6dofs-red.XXXXXX)
env PATH=/usr/bin:/bin /usr/bin/cmake \
  -S "$DEXHAND_SOURCE" -B "$DEXHAND_RED_ROOT/build" \
  -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=OFF
env PATH=/usr/bin:/bin /usr/bin/cmake \
  --build "$DEXHAND_RED_ROOT/build" --parallel 2 \
  --target factory_contract lhandpro_lifecycle lhandpro_sdk
/usr/bin/ctest --test-dir "$DEXHAND_RED_ROOT/build" \
  -R '^(factory_contract|lhandpro_lifecycle|lhandpro_sdk)$' \
  --output-on-failure --timeout 30
```

Expected: `lhandpro_lifecycle` fails because the old driver writes vendor model
`0` and accepts readback `0`. `factory_contract` and `lhandpro_sdk` may already
pass because they protect unchanged API/adapter behavior.

### Task 2: Implement the Private 6DOF S Mapping

**Files:**
- Modify: `src/drivers/lhandpro/lhandpro_driver.hpp`
- Modify: `src/drivers/lhandpro/lhandpro_driver.cpp`
- Modify: `src/hand_driver.cpp`
- Modify: `tests/test_lhandpro_driver.cpp`

- [ ] **Step 1: Make the private model name truthful**

Change the private enum in `lhandpro_driver.hpp` to:

```cpp
enum class LHandProModel { Dof6S, Dof16 };
```

Mechanically replace private `LHandProModel::Dof6` references with
`LHandProModel::Dof6S` in the factory, driver, and lifecycle tests. Do not
rename `HAND_LHANDPRO_6DOF` or Python `LHANDPRO_6DOF`.

- [ ] **Step 2: Add named private vendor model constants**

Beside `kSdkSuccess` and `kCanFdMode` in `lhandpro_driver.cpp`, add:

```cpp
constexpr int kVendorModel6DofS = 1;
constexpr int kVendorModel16Dof = 2;
```

Do not include the vendor header in the driver. The C API header remains
isolated behind `lhandpro_sdk.cpp`.

- [ ] **Step 3: Change only the private mapping behavior**

Implement the mapping and keep exact DOF validation:

```cpp
int LHandProDriver::expected_vendor_model_() const noexcept {
  return model_ == LHandProModel::Dof6S ? kVendorModel6DofS
                                        : kVendorModel16Dof;
}

ExpectedDof LHandProDriver::expected_dof_() const noexcept {
  return model_ == LHandProModel::Dof6S ? ExpectedDof{11, 6}
                                       : ExpectedDof{21, 16};
}
```

Update constructor validation to accept only `Dof6S` and `Dof16`. Keep both
pre-communication and post-communication model readback checks unchanged.

- [ ] **Step 4: Run focused tests and record GREEN**

Configure a second fresh tree against the implementation and run:

```bash
DEXHAND_SOURCE=$(git rev-parse --show-toplevel)
DEXHAND_GREEN_ROOT=$(mktemp -d /tmp/dexhand-6dofs-green.XXXXXX)
env PATH=/usr/bin:/bin /usr/bin/cmake \
  -S "$DEXHAND_SOURCE" -B "$DEXHAND_GREEN_ROOT/build" \
  -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=OFF
env PATH=/usr/bin:/bin /usr/bin/cmake \
  --build "$DEXHAND_GREEN_ROOT/build" --parallel 2 \
  --target factory_contract lhandpro_lifecycle lhandpro_sdk
/usr/bin/ctest --test-dir "$DEXHAND_GREEN_ROOT/build" \
  -R '^(factory_contract|lhandpro_lifecycle|lhandpro_sdk)$' \
  --output-on-failure --timeout 30
```

Expected: all three focused tests pass. The lifecycle trace must show model
`1` for `Dof6S`, model `2` for `Dof16`, `(11, 6)` and `(21, 16)` snapshots,
ordinary model readback `0` rejection, and transactional retry behavior.

- [ ] **Step 5: Commit the behavior change**

```bash
git add src/hand_driver.cpp \
  src/drivers/lhandpro/lhandpro_driver.hpp \
  src/drivers/lhandpro/lhandpro_driver.cpp \
  tests/test_factory.cpp tests/test_lhandpro_driver.cpp \
  tests/test_lhandpro_sdk.cpp
git commit -m "Map the public 6DOF model to LHandPro 6DOF S"
```

### Task 3: Document the Deliberate Public Meaning

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Clarify the supported model without changing examples**

Update the migration and runtime safety text to state:

```markdown
Public hand-model numeric values remain 0 for the supported LHandPro 6DOF S
model and 1 for the existing 16DOF model. The ordinary vendor 6DOF model is
not supported by this deployment.
```

Replace “the 6-DOF model” in the DOF paragraph with “the 6DOF S model”. Keep
the C++ and Python examples using the existing public `LHANDPRO_6DOF` name.

- [ ] **Step 2: Check documentation and source consistency**

Run:

```bash
rg -n 'Dof6\b|vendor model `0`|ordinary.*6DOF.*supported' \
  src tests README.md docs/superpowers
git diff --check
```

Expected: no stale private `Dof6` identifier, no claim that vendor model `0`
is supported, and no whitespace errors. Historical design material may mention
the old observed mismatch only when clearly marked as history.

- [ ] **Step 3: Commit the documentation change**

```bash
git add README.md
git commit -m "Document the supported LHandPro 6DOF S model"
```

### Task 4: Perform Full Local Verification

**Files:**
- Verify only; no production edits expected.

- [ ] **Step 1: Configure a fresh warning-clean build**

```bash
DEXHAND_LOCAL_ROOT=$(mktemp -d /tmp/dexhand-6dofs-local.XXXXXX)
DEXHAND_SOURCE=$(git rev-parse --show-toplevel)
env PATH=/usr/bin:/bin /usr/bin/cmake \
  -S "$DEXHAND_SOURCE" \
  -B "$DEXHAND_LOCAL_ROOT/build" \
  -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror' \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=OFF
```

Expected: configure exits `0` using system fmt/spdlog/Python dependencies.

- [ ] **Step 2: Build and run all default tests**

```bash
env PATH=/usr/bin:/bin /usr/bin/cmake \
  --build "$DEXHAND_LOCAL_ROOT/build" --parallel 2
/usr/bin/ctest --test-dir "$DEXHAND_LOCAL_ROOT/build" \
  --output-on-failure --timeout 30
```

Expected: build succeeds and all eight default tests pass. The real SDK smoke
must remain limited to create/set/get model/get DOF/destroy.

- [ ] **Step 3: Run the relocatable install consumer gate**

```bash
env PATH=/usr/bin:/bin /usr/bin/cmake \
  -DBUILD_DIR="$DEXHAND_LOCAL_ROOT/build" \
  -DSOURCE_DIR="$DEXHAND_SOURCE" \
  -DPREFIX="$DEXHAND_LOCAL_ROOT/prefix" \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P "$DEXHAND_SOURCE/tests/check_install_export.cmake"
```

Expected: installed C++ consumer, Python import/API, relocation, RPATH, private
target boundary, and exact-one-SDK checks all pass without `LD_LIBRARY_PATH`.

- [ ] **Step 4: Audit local scope**

```bash
git status --short
git diff --check main...HEAD
git diff --name-status main...HEAD
```

Expected: only the planned driver, test, README, design, and plan paths differ;
`roboparty_motors`, `roboparty_hand`, `roboparty_deploy`, and vendor artifacts
remain untouched.

### Task 5: Verify Native AArch64 and the Connected Hand Without Motion

**Files:**
- Copy the committed source to a new directory under `/home/orangepi`.
- Do not modify board deployment services or CAN configuration.

- [ ] **Step 1: Create a fresh board validation copy**

Resolve the final commit locally, create a new remote directory whose name
contains that short commit, and copy the repository without `.git` or local
build directories:

```bash
DEXHAND_COMMIT=$(git rev-parse --short HEAD)
DEXHAND_REMOTE="/home/orangepi/roboparty_dexhand_6dofs_${DEXHAND_COMMIT}"
ssh orangepi "mkdir -p '$DEXHAND_REMOTE'"
rsync -a --exclude .git --exclude build --exclude install \
  /home/sjh/leisai_hand/roboparty_dexhand/ \
  "orangepi:$DEXHAND_REMOTE/"
```

Expected: the new validation copy is isolated from earlier board builds.

- [ ] **Step 2: Verify board architecture and SDK artifact**

Run remotely:

```bash
uname -m
sha256sum "$DEXHAND_REMOTE/thirdparty/lib/aarch64/libLHandProLib.so"
cmake -DSOURCE_DIR="$DEXHAND_REMOTE" \
  -P "$DEXHAND_REMOTE/tests/check_sdk_artifacts.cmake"
```

Expected: architecture is `aarch64`; SDK SHA-256 is
`476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`;
artifact check exits `0`.

- [ ] **Step 3: Build and test natively**

Run remotely with fresh `build` and `prefix` directories:

```bash
cmake -S "$DEXHAND_REMOTE" -B "$DEXHAND_REMOTE/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror' \
  -DCMAKE_INSTALL_PREFIX="$DEXHAND_REMOTE/prefix" \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DBUILD_TESTING=ON \
  -DDEXHAND_ENABLE_VCAN_TESTS=OFF
cmake --build "$DEXHAND_REMOTE/build" --parallel 2
ctest --test-dir "$DEXHAND_REMOTE/build" \
  --output-on-failure --timeout 30
cmake --install "$DEXHAND_REMOTE/build"
```

Expected: native build succeeds and all eight default tests pass. No default
test opens physical `can0` or commands the hand.

- [ ] **Step 4: Re-run the installed consumer gate on AArch64**

```bash
cmake -DBUILD_DIR="$DEXHAND_REMOTE/build" \
  -DSOURCE_DIR="$DEXHAND_REMOTE" \
  -DPREFIX="$DEXHAND_REMOTE/relocatable" \
  -DPYTHON_EXECUTABLE=/usr/bin/python3 \
  -P "$DEXHAND_REMOTE/tests/check_install_export.cmake"
```

Expected: installed native C++ and Python consumers run without
`LD_LIBRARY_PATH`, and exactly one AArch64 SDK is installed.

- [ ] **Step 5: Perform CAN preflight without changing interface state**

Run remotely before the active probe:

```bash
ip -details -statistics link show can0
cat /proc/net/can/rcvlist_all
```

Expected: `can0` is up, CAN-FD MTU is 72, nominal bitrate is 1 Mbps, data
bitrate is 5 Mbps, state is `ERROR-ACTIVE`, and error counters are not rising.
Stop if another unexpected control process owns the hand traffic.

- [ ] **Step 6: Run the non-motion online probe**

Use the installed Python module. Do not call `home_motors`,
`set_target_position`, `set_target_angle`, or `move_motors`:

```python
import time
from dexhand_py import HandDriver, HandModel

hand = HandDriver.create_hand(
    hand_type="LHandPro",
    interface_type="canfd",
    interface="can0",
    hand_model=HandModel.LHANDPRO_6DOF,
    canfd_node_id=1,
)
initialized = False
try:
    initialized = hand.init_hand(
        enable_motors=False,
        home_motors=False,
        home_wait_time=0.0,
    )
    print("initialized:", initialized)
    if not initialized:
        raise SystemExit(1)
    hand.set_move_no_home(0)
    time.sleep(1.0)
    print("dof:", hand.get_dof())
    print("finger1 status:", hand.get_now_status(1))
    print("finger1 current:", hand.get_now_current(1))
    print("finger1 alarm:", hand.get_now_alarm(1))
finally:
    if initialized:
        hand.set_move_no_home(0)
        hand.stop_motors(0)
        hand.set_enable(0, False)
    hand.deinit_hand()
```

Expected: initialization is `True`, public DOF is `(11, 6)`, feedback calls
return, and cleanup completes. The SDK still sends initialization/monitoring
traffic and briefly executes its existing `set_move_no_home(1)` initialization
step; the probe immediately restores it to `0`. This is non-motion validation,
not a read-only bus operation.

- [ ] **Step 7: Confirm post-probe CAN health and cleanup**

Run:

```bash
ip -details -statistics link show can0
pgrep -af 'dexhand|test_dexhand|candump'
```

Expected: CAN remains `ERROR-ACTIVE`, bus error counters remain zero or
unchanged, and no validation process remains. Do not run
`scripts/test_dexhand.py --confirm-motion` in this plan.

### Task 6: Final Review and Integration

**Files:**
- Review all commits after the approved design commit.

- [ ] **Step 1: Run independent specification and quality reviews**

Dispatch one reviewer to verify exact compliance with the approved design and
one reviewer to search for regressions, false-green tests, API drift, unsafe
hardware behavior, and unintended repository changes. Resolve all Critical,
High, or Medium findings with test-first amendments.

- [ ] **Step 2: Re-run affected tests after review fixes**

At minimum rerun the focused three tests, fresh local CTest, and any reviewer-
identified regression. If production code changes after the board probe,
repeat the native AArch64 build and non-motion probe before completion.

- [ ] **Step 3: Merge locally without touching sibling repositories**

After the feature worktree is clean and reviewed, use a normal non-destructive
merge into local `main`. Verify:

```bash
git status --short
git log --oneline --decorate -5
git diff --check HEAD^..HEAD
```

Expected: local `main` contains the reviewed commits, the worktree is clean,
and no change exists in `roboparty_motors`, `roboparty_hand`, or
`roboparty_deploy`.
