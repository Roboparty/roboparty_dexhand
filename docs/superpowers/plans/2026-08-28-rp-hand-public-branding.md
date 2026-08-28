# RP_Hand Public Branding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `RP_Hand` the preferred public product and API name for the supported model value `6DOF=0` while preserving all existing identifiers as compatibility aliases.

**Architecture:** Add branded aliases at the stable public factory/enum boundary and keep the private vendor adapter unchanged. Convert normal user documentation, package metadata, and the manual hardware test to the branded path. Prove both preferred and legacy paths with focused C++ and Python contract tests before running the full suite.

**Tech Stack:** C++17, pybind11, Python 3, CMake/CTest, Markdown, ROS package metadata

---

### Task 1: Add The Branded C++ Factory Contract

**Files:**
- Modify: `tests/test_factory.cpp`
- Modify: `include/hand_driver.hpp`
- Modify: `src/hand_driver.cpp`

- [ ] **Step 1: Write failing C++ contract assertions**

Add assertions to `tests/test_factory.cpp` that require:

```cpp
CHECK_EQ(HAND_RP_HAND_6DOF, 0);
CHECK_EQ(HAND_RP_HAND_6DOF, HAND_LHANDPRO_6DOF);
CHECK(HandDriver::create_hand("RP_Hand", "canfd", "can0",
                              HAND_RP_HAND_6DOF, 1) != nullptr);
```

Retain at least one successful legacy factory call to prove backward
compatibility. Keep the `HAND_LHANDPRO_16DOF == 1` assertion and do not add an
`RP_Hand` 16DOF alias.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake --build build --target factory_contract --parallel
```

Expected: compilation fails because `HAND_RP_HAND_6DOF` is not defined.

- [ ] **Step 3: Add the C++ alias and factory string**

Define the preferred value first and the legacy alias second:

```cpp
enum HandModel {
    HAND_RP_HAND_6DOF = 0,
    HAND_LHANDPRO_6DOF = HAND_RP_HAND_6DOF,
    HAND_LHANDPRO_16DOF = 1,
};
```

Change the default model and the model-0 switch label to
`HAND_RP_HAND_6DOF`. Accept both `"RP_Hand"` and the existing legacy factory
string. Update public comments and unsupported-type diagnostics to describe
`RP_Hand` as the preferred contract without renaming private driver types.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
cmake --build build --target factory_contract --parallel
ctest --test-dir build -R '^factory_contract$' --output-on-failure
```

Expected: build succeeds and the focused test passes.

- [ ] **Step 5: Commit the C++ contract**

```bash
git add tests/test_factory.cpp include/hand_driver.hpp src/hand_driver.cpp
git commit -m "Add RP_Hand public factory alias"
```

### Task 2: Export The Branded Python Model

**Files:**
- Modify: `tests/test_pybind_api.py`
- Modify: `src/pybind_module.cpp`
- Modify: `scripts/test_dexhand.py`

- [ ] **Step 1: Write failing Python binding tests**

Require the branded alias and preferred factory string:

```python
self.assertEqual(int(dexhand_py.HandModel.RP_HAND_6DOF.value), 0)
self.assertEqual(
    dexhand_py.HandModel.RP_HAND_6DOF,
    dexhand_py.HandModel.LHANDPRO_6DOF,
)
hand = dexhand_py.HandDriver.create_hand(
    'RP_Hand', 'canfd', 'can0',
    hand_model=dexhand_py.HandModel.RP_HAND_6DOF,
)
```

Retain legacy assertions and a legacy construction case. Add a manual-helper
assertion that its factory arguments use `hand_type='RP_Hand'`.

- [ ] **Step 2: Run the Python test and verify RED**

Run:

```bash
ctest --test-dir build -R '^pybind_api$' --output-on-failure
```

Expected: failure because `RP_HAND_6DOF` and/or the preferred factory string is
not yet exported.

- [ ] **Step 3: Export the alias and update the hardware helper**

Add this preferred enum export before the compatibility names:

```cpp
.value("RP_HAND_6DOF", HAND_RP_HAND_6DOF)
```

Use `HAND_RP_HAND_6DOF` as the binding's default argument. Change the manual
hardware script's user-facing text, factory type string, and selected Python
model to `RP_Hand` / `HandModel.RP_HAND_6DOF`. Do not rename the immutable SDK
library filename or private loading path.

- [ ] **Step 4: Run the Python test and verify GREEN**

Run:

```bash
cmake --build build --target dexhand_py --parallel
ctest --test-dir build -R '^pybind_api$' --output-on-failure
```

Expected: the focused Python API test passes.

- [ ] **Step 5: Commit the Python contract**

```bash
git add tests/test_pybind_api.py src/pybind_module.cpp scripts/test_dexhand.py
git commit -m "Expose RP_Hand Python model alias"
```

### Task 3: Brand The Public Manual And Metadata

**Files:**
- Modify: `README.md`
- Modify: `package.xml`

- [ ] **Step 1: Add a failing public-brand scan**

Before editing, verify that public prose still exposes the OEM product name:

```bash
rg -n "LHandPro|LHANDPRO" README.md package.xml
```

Expected: matches in product prose, examples, and package description.

- [ ] **Step 2: Update public documentation**

Use `RP_Hand 6DOF` in supported-product prose. Change runnable examples to:

```python
HandDriver.create_hand(
    "RP_Hand", "canfd", CAN_INTERFACE, HandModel.RP_HAND_6DOF, 1
)
```

```cpp
HandDriver::create_hand(
    "RP_Hand", "canfd", "can0", HAND_RP_HAND_6DOF, 1);
```

Describe the supported public value as `RP_HAND_6DOF = 0`. State briefly that
pre-branding identifiers remain compatible, but do not print the OEM product
name in the main manual. Do not add a branded 16DOF product or imply 16DOF
support. Preserve all installation, CAN, feedback, safety, dual-process, build,
and license instructions.

Change `package.xml` description to:

```xml
<description>RP_Hand dexterous hand driver for Linux CAN-FD with C++ and Python factory APIs</description>
```

- [ ] **Step 3: Verify public branding and examples**

Run:

```bash
! rg -n "LHandPro|LHANDPRO" README.md package.xml
rg -n "RP_Hand|RP_HAND_6DOF" README.md package.xml
git diff --check
```

Parse all README Python blocks with `ast.parse`, check balanced Markdown fences,
and verify the C++ example compiles with the public header.

- [ ] **Step 4: Run the complete test suite**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all registered tests pass, including `factory_contract`, `pybind_api`,
and `install_export`.

- [ ] **Step 5: Commit the branded manual**

```bash
git add README.md package.xml
git commit -m "Brand public documentation as RP_Hand"
```
