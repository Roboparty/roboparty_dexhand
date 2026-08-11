# Final Review DOF And Logging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development and superpowers:verification-before-completion task-by-task. This change must remain one Lore commit because the final-review request explicitly requires one new commit.

**Goal:** Make LHandPro model probing match the vendor's real `(total, active)` semantics and keep transport/global callback diagnostics off the process default logger without weakening `noexcept` boundaries.

**Architecture:** Model expectations live as one internal pair returned from `LHandProDriver`, and the post-monitor probe must match both values exactly before publishing the snapshot. SocketCAN and process-global callback diagnostics resolve only the registered `dexhand` logger through a shared exception-swallowing helper; syscall paths snapshot their error number immediately and positive short writes do not consult it.

**Tech Stack:** C++17, CMake/CTest, spdlog 1.12, Linux SocketCAN, injected fake SDK/socket operations.

---

### Task 1: Lock vendor DOF semantics

**Files:**
- Modify: `tests/fakes/fake_lhandpro_sdk.hpp`
- Modify: `tests/test_lhandpro_driver.cpp`
- Modify: `tests/test_lhandpro_sdk.cpp`

- [ ] Change the fake's default pair to `(11, 6)` and configure the Dof16 fixture as `(21, 16)`.
- [ ] Assert successful initialization and public `get_dof()` snapshots for both exact pairs.
- [ ] Add independent total and active mismatch cases whose other field is correct.
- [ ] Restrict the real adapter smoke to `create`, `set_hand_type`, `get_dof`, and `destroy`, probing model types `0` and `2`; never call `initial_ex`.
- [ ] Build and run `lhandpro_lifecycle` and `lhandpro_sdk`; expect failures from the old total-only/range implementation.

### Task 2: Enforce exact DOF pairs

**Files:**
- Modify: `src/drivers/lhandpro/lhandpro_driver.hpp`
- Modify: `src/drivers/lhandpro/lhandpro_driver.cpp`

- [ ] Replace the total-only helper with an internal expected pair: Dof6 is `(11, 6)` and Dof16 is `(21, 16)`.
- [ ] Require both returned values to equal the pair and include expected/actual total and active values in the failure diagnostic.
- [ ] Run both targeted tests and confirm GREEN.

### Task 3: Lock named-logger and errno behavior

**Files:**
- Modify: `tests/test_canfd_transport.cpp`
- Modify: `tests/test_lhandpro_driver.cpp`
- Modify: `tests/fakes/fake_socket_ops.hpp` only if deterministic background-error synchronization requires it.

- [ ] Install distinct counting sinks for the process default logger and the registered `dexhand` logger.
- [ ] Trigger SocketCAN setup, worker, failure, and short-write diagnostics; assert the default count remains zero and the named count increases.
- [ ] Exercise a throwing named sink through `noexcept` transport paths and assert calls return failure without process termination. The LHandPro bridge has no safely injectable throwing dependency because its transport interface is already `noexcept`.
- [ ] Add a source-policy regression that rejects free `spdlog::*` and `default_logger` calls in both the SocketCAN and LHandPro global-bridge translation units.
- [ ] Assert a negative write includes the captured errno number and system message, while a positive short write omits a deliberately stale errno and its text.
- [ ] Run the targeted tests; expect old free `spdlog::error` calls to make the default count nonzero.

### Task 4: Route diagnostics safely

**Files:**
- Create: `src/logging.hpp`
- Modify: `src/protocol/socket_canfd_transport.cpp`
- Modify: `src/drivers/lhandpro/lhandpro_driver.cpp`

- [ ] Add `with_dexhand_logger(operation) noexcept`, which catches lookup, formatting, and sink exceptions and silently returns when no named logger exists.
- [ ] Replace every free SocketCAN and global callback bridge `spdlog::error` call with this helper.
- [ ] Snapshot `last_error()` immediately after every failed socket operation; format the saved code with `std::system_category().message(code)` inside the protected logging operation.
- [ ] Split failed writes from positive short writes so the latter never reads or reports `last_error()`.
- [ ] Re-run both targeted tests and repeat them to expose background timing regressions.

### Task 5: Synchronize facts and verify release gates

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-08-10-roboparty-dexhand-independent-canfd-design.md`
- Modify: `docs/superpowers/plans/2026-08-10-roboparty-dexhand-independent-canfd-implementation.md`

- [ ] Correct only DOF/logging facts and code examples derived from the old assumptions.
- [ ] Search the allowed scope for stale `6/6`, `16/16`, range-only DOF validation, free SocketCAN/bridge logging, and forbidden adapter-smoke calls.
- [ ] Run targeted Werror builds, repeated targeted tests, then a fresh pure CMake Werror build and full CTest with vcan disabled.
- [ ] Inspect `git diff --check`, exact changed paths, forbidden directories, and commit trailers.
- [ ] Request final code review, fix any High/Medium findings, create one new Lore commit, and verify the worktree is clean.
