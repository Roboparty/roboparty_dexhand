# Public Repository Cleanup And Validation Guide Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Archive internal engineering records in a verified private repository, publish a copy-paste hardware validation guide, and remove only internal process material from the public repository while preserving its automated release tests.

**Architecture:** Treat the private archive push as a hard prerequisite for public deletion. Keep product development, CI, and all registered tests in `roboparty_dexhand`; store only historical/process records in `roboparty_dexhand_internal`. Separate normal usage (`README.md`) from board acceptance (`VALIDATION.md`).

**Tech Stack:** Git/GitHub CLI, Markdown, Bash, Python 3, CMake/CTest, Linux SocketCAN

---

### Task 1: Create And Verify The Private Engineering Archive

**Source files:**
- Archive: `docs/superpowers/`
- Archive: `CODE_REVIEW.md`
- Archive: `tests/hardware/lhandpro_callback_quiescence_stress.cpp`
- Archive: `thirdparty/vendor-authorization-request.md`

**Private repository files:**
- Create: `README.md`
- Create: `ARCHIVE_MANIFEST.md`
- Preserve the archived source files at their original relative paths

- [ ] **Step 1: Resolve and record the source snapshot**

From public `main`, require a clean worktree and record:

```bash
git rev-parse HEAD
git ls-files docs/superpowers CODE_REVIEW.md \
  tests/hardware/lhandpro_callback_quiescence_stress.cpp \
  thirdparty/vendor-authorization-request.md
```

Expected: an exact commit SHA and a non-empty deterministic file list.

- [ ] **Step 2: Create the private repository**

Run with the authenticated `robygx` GitHub account:

```bash
gh repo create Roboparty/roboparty_dexhand_internal \
  --private \
  --description "Internal engineering records for roboparty_dexhand" \
  --clone
```

Expected: the repository is created under the `Roboparty` organization and a
local clone exists outside the public repository. Because the repository is
initially empty, create and use a local `main` branch before the first commit:

```bash
git symbolic-ref HEAD refs/heads/main
```

- [ ] **Step 3: Copy the exact archive snapshot**

Use `git archive` from the recorded public commit to extract only the selected
tracked paths into the private clone, preserving relative paths. Add a private
root README that states:

```markdown
# roboparty_dexhand_internal

Private engineering records archived from
`Roboparty/roboparty_dexhand`. Production source, public documentation,
automated release tests, and packaging remain in the public repository.
```

Add `ARCHIVE_MANIFEST.md` containing the UTC/archive date, source repository,
exact source commit, and complete archived file list.

- [ ] **Step 4: Commit and push the private archive**

```bash
git add README.md ARCHIVE_MANIFEST.md docs CODE_REVIEW.md tests thirdparty
git commit -m "Archive roboparty_dexhand engineering records"
git push -u origin main
```

Expected: push succeeds using the `robygx` identity.

- [ ] **Step 5: Verify before allowing deletion**

Run:

```bash
gh repo view Roboparty/roboparty_dexhand_internal \
  --json visibility,defaultBranchRef,url
git ls-tree -r --name-only origin/main
git status --short --branch
```

Expected: visibility is `PRIVATE`, default branch is `main`, every manifest
path exists at private `origin/main`, and the private clone is clean.

### Task 2: Add The Copy-Paste Hardware Validation Guide

**Files:**
- Create: `VALIDATION.md`
- Modify: `README.md`

- [ ] **Step 1: Add the validation-guide link**

Add a short link near the beginning of `README.md`:

```markdown
开发板安装后的反馈周期、Python API 和六轴运动验收，见
[`VALIDATION.md`](VALIDATION.md)。
```

- [ ] **Step 2: Write prerequisites and reusable variables**

Start `VALIDATION.md` with a physical safety checklist and these copyable
commands:

```bash
source /opt/roboparty/setup.bash
export CAN_INTERFACE=can3
export NODE_ID=1
ip -details -statistics link show "$CAN_INTERFACE"
```

State that the operator edits only `CAN_INTERFACE` when using can0, can1,
can2, or can3. The selected interface must already be UP and configured for
1 Mbps nominal / 5 Mbps data CAN-FD.

- [ ] **Step 3: Add feedback-period provisioning and verification**

Use exact installed commands:

```bash
roboparty-dexhand-config feedback-period apply \
  --interface "$CAN_INTERFACE" \
  --node-id "$NODE_ID" \
  --milliseconds 20 \
  --save
```

Insert an explicit hand-only power-cycle boundary, then:

```bash
source /opt/roboparty/setup.bash
export CAN_INTERFACE=can3
export NODE_ID=1
roboparty-dexhand-config feedback-period show \
  --interface "$CAN_INTERFACE" \
  --node-id "$NODE_ID"
```

Require all six raw values to equal `200`.

- [ ] **Step 4: Add the installed Python API check**

Document:

```bash
source /opt/roboparty/setup.bash
python3 -c 'from dexhand_py import HandModel; print(HandModel.RP_HAND_6DOF.value)'
```

Expected output is exactly `0`. Do not print enum representation because its
legacy canonical name is deliberately preserved for compatibility.

- [ ] **Step 5: Add bounded six-axis motion and tracking**

Provide one complete Bash here-document using installed `python3`. It reads
`CAN_INTERFACE` and `NODE_ID` from the environment, requires the operator to
type `MOVE`, then creates:

```python
HandDriver.create_hand(
    "RP_Hand", "canfd", can_interface,
    HandModel.RP_HAND_6DOF, node_id,
)
```

The script must:

- require the hand to have a known reference/zero state;
- call `init_hand(True, False, 0.0)` and fail closed;
- call `check_health()`;
- require `active == 6` from `get_dof()`;
- require zero alarms on joints 1 through 6;
- call `set_move_no_home(1)` only after all checks;
- set all six target positions to `5000` and velocities to `3000`;
- broadcast with `move_motors(0)`, wait three seconds, and print six positions;
- return all six positions to `0`, wait three seconds, and print six positions;
- stop motors on cleanup when initialized;
- restore `set_move_no_home(0)` when it was enabled;
- always call `deinit_hand()` in `finally`.

- [ ] **Step 6: Add acceptance results and validate snippets**

Add a compact checklist for interface state, six raw `200` values, model value
`0`, target tracking near `5000`, return tracking near `0`, and clean exit.

Extract every fenced Bash block and run `bash -n`; parse every Python block
with `ast.parse`; check balanced fences and `git diff --check`.

- [ ] **Step 7: Commit public validation documentation**

```bash
git add README.md VALIDATION.md
git commit -m "Add RP_Hand hardware validation guide"
```

### Task 3: Remove Archived Internal Records From Public Main

**Files:**
- Delete: `docs/superpowers/`
- Delete: `CODE_REVIEW.md`
- Delete: `tests/hardware/lhandpro_callback_quiescence_stress.cpp`
- Delete: `thirdparty/vendor-authorization-request.md`
- Preserve: `tests/` except the archived unregistered hardware source
- Preserve: `.github/workflows/build-deb.yml`

- [ ] **Step 1: Re-verify the private archive gate**

Immediately before deletion, fetch private `origin/main`, verify repository
visibility is still `PRIVATE`, and compare every archived public path with the
private copy from the recorded source commit.

Expected: byte-for-byte equality for every archived file.

- [ ] **Step 2: Delete only the archived public paths**

Remove the four selected internal/historical path groups. Do not remove default
tests, test fakes, install-export consumers, vcan tests, CI workflows, or
third-party runtime artifacts.

- [ ] **Step 3: Add a public repository-surface guard**

Verify:

```bash
test ! -e docs/superpowers
test ! -e CODE_REVIEW.md
test ! -e tests/hardware/lhandpro_callback_quiescence_stress.cpp
test ! -e thirdparty/vendor-authorization-request.md
test -f tests/test_factory.cpp
test -f tests/test_lhandpro_feedback_period.cpp
test -f tests/check_install_export.cmake
test -f .github/workflows/build-deb.yml
```

- [ ] **Step 4: Run the complete public release gate**

Run in a clean system-toolchain environment:

```bash
cmake -S . -B build \
  -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: strict build succeeds and all 11 registered tests pass.

- [ ] **Step 5: Commit the public cleanup**

```bash
git add -A docs CODE_REVIEW.md tests/hardware \
  thirdparty/vendor-authorization-request.md
git commit -m "Move internal engineering records to private archive"
```

- [ ] **Step 6: Final cross-repository verification**

Verify both repositories are clean; the private remote contains the archive;
the public branch contains `README.md`, `VALIDATION.md`, production source,
tests, CI, and no selected internal paths. Do not push public `main` until the
user chooses the branch-integration/push option.
