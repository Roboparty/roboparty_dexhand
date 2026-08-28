# README User Manual Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the release-evidence-oriented README with a Chinese-first, copy-paste-ready daily-use manual for `roboparty_dexhand`.

**Architecture:** Keep all public usage guidance in the root `README.md` and place required constraints beside the commands they affect. Remove historical validation narratives instead of moving them into another public document. Verify every documented command and method against the current CLI, Python binding, and C++ header.

**Tech Stack:** Markdown, CMake, Linux SocketCAN, Python 3, pybind11, C++17

---

### Task 1: Rewrite and Verify the User Manual

**Files:**
- Modify: `README.md`
- Reference: `src/pybind_module.cpp`
- Reference: `include/hand_driver.hpp`
- Reference: `src/tools/lhandpro_config_cli.cpp`
- Reference: `CMakeLists.txt`

- [ ] **Step 1: Replace the README structure**

Rewrite `README.md` in Chinese with these sections:

```markdown
# roboparty_dexhand
## 支持范围
## 安装
## 配置 CAN-FD
## 首次配置反馈频率
## Python 使用
## 选择 CAN 接口
## 双手使用
## C++ 使用
## 主要 API
## 注意事项
## 从源码构建
## License
```

Delete the existing release-gate, evidence-path, callback stress history,
vcan-detail, SDK-probe analysis, and migration-history sections.

- [ ] **Step 2: Add exact installation and CAN setup commands**

Document Debian installation and the externally owned CAN setup:

```bash
sudo apt install ./roboparty-dexhand_<version>_arm64.deb

sudo ip link set can0 down
sudo ip link set can0 type can \
  bitrate 1000000 sample-point 0.8 sjw 4 \
  dbitrate 5000000 dsample-point 0.75 dsjw 2 fd on
sudo ip link set can0 txqueuelen 10000
sudo ip link set can0 up
ip -details -statistics link show can0
```

State that the user replaces `can0` with the physically connected interface
and that this library does not configure Linux network interfaces.

- [ ] **Step 3: Document one-time 50 Hz provisioning**

Include the exact supported CLI flow:

```bash
roboparty-dexhand-config feedback-period apply \
  --interface can0 --node-id 1 --milliseconds 20 --save

# Power-cycle the hand, then verify:
roboparty-dexhand-config feedback-period show \
  --interface can0 --node-id 1
```

State that all six axes must show raw value `200`, and explain only the useful
relationship: 20 ms is 50 Hz for each of two feedback frame types, producing
approximately 100 aggregate frames per second.

- [ ] **Step 4: Add a complete Python example**

Use the current public binding exactly: `HandDriver.create_hand()`,
`init_hand(True, False, 0.0)`, `set_move_no_home(1)`, six joint target and
velocity setters, `move_motors(0)`, cached position reads, and mandatory
`deinit_hand()` in `finally`. Define `CAN_INTERFACE = "can0"` once so port
selection is explicit. Keep motion values bounded and label printed targets
consistently.

- [ ] **Step 5: Document dual-hand ownership**

Show two independent commands or process configurations, one for `can0` and
one for `can1`. State that one process owns one active vendor SDK instance and
its process-global callback, so the supported pattern is one process per hand.
Do not imply that `roboparty_deploy` automatically selects either interface.

- [ ] **Step 6: Add C++ and API reference sections**

Use this factory signature:

```cpp
auto hand = HandDriver::create_hand(
    "LHandPro", "canfd", "can0", HAND_LHANDPRO_6DOF, 1);
```

List only methods present in `include/hand_driver.hpp` and explain
`finger_id == 0` as broadcast where applicable. Mention that public model value
`LHANDPRO_6DOF` maps to the supported LHandPro 6DOF S hardware.

- [ ] **Step 7: Verify content mechanically**

Run:

```bash
git diff --check
rg -n "release gate|Release Gate|evidence|callback-quiescence|vcan Release|Migration From" README.md
rg -n "feedback-period (apply|show)|init_hand|deinit_hand|create_hand|move_motors" README.md
```

Expected: `git diff --check` succeeds; the stale-evidence search returns no
matches; all required user-facing commands and API names are present.

- [ ] **Step 8: Run repository tests**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: the existing build succeeds and all registered tests pass. README
examples are then manually compared with `src/pybind_module.cpp`,
`include/hand_driver.hpp`, and `src/tools/lhandpro_config_cli.cpp`.

- [ ] **Step 9: Commit the manual**

```bash
git add README.md
git commit -m "Rewrite README as Chinese user manual"
```
