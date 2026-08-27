# roboparty_dexhand

Factory-style C++/Python control library for LHandPro hands over Linux CAN-FD.
The library owns a private SocketCAN socket; motors may use another socket on
the same physical CAN interface. No motors source or private target is linked.

## Supported Platforms

- Linux x86-64 (development)
- Linux AArch64/ARM64 (Orange Pi and RDK deployment)
- One active LHandPro SDK instance per process

The AArch64/ARM64 target and physical hardware path have now been exercised
for the production source commit `db2da9fb90f407bdd5e3bbd3de691e775d27abd3`.
The project owner has authorized redistribution of the two pinned SDK
artifacts. The project accepts the exact-hash empirical callback-quiescence
evidence described below; it does not present that evidence as a broader vendor
API guarantee.

## CAN Setup

Configure bitrate and link state outside this library, for example with the
deployment service responsible for `can0`. The factory does not accept bitrate
arguments and never changes the network interface.

The project requires CMake 3.15 or newer.

## Feedback-Period Provisioning

Stop every other hand-control process before reading or changing the feedback
period. Show the six stored axis values without enabling, homing, or moving the
hand:

```bash
roboparty-dexhand-config feedback-period show --interface can0 --node-id 1
```

Set each axis to 20 ms, verify the readback, and persist the verified value:

```bash
roboparty-dexhand-config feedback-period apply --interface can0 --node-id 1 --milliseconds 20 --save
```

```text
20 ms = 50 emissions/second for frame type 0x50
20 ms = 50 emissions/second for frame type 0x5A
observed aggregate = approximately 100 CAN-FD frames/second
```

These are distinct frame types, not duplicate frames. The protocol documents
`0x5A` as axis status/status2, while the supplied protocol export does not
define `0x50`.

Normal `HandDriver::init_hand()` never writes the feedback period. After an
`apply --save`, power-cycle the hand and run `feedback-period show` again before
returning it to service.

## Build

```bash
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build --parallel
cmake --install build --prefix "$PWD/install"
```

## C++

```cpp
#include <hand_driver.hpp>

auto hand = HandDriver::create_hand("LHandPro", "canfd", "can0",
                                    HAND_LHANDPRO_6DOF, 1);
```

## Python

```python
from dexhand_py import HandDriver, HandModel

hand = HandDriver.create_hand(
    hand_type="LHandPro", interface_type="canfd", interface="can0",
    hand_model=HandModel.LHANDPRO_6DOF, canfd_node_id=1)
try:
    ...
finally:
    hand.deinit_hand()
```

## Migration From 0.1.x

Version 0.2.0 removes `canfd_nom_baudrate` and `canfd_dat_baudrate`; configure
the Linux CAN interface in deployment tooling. It also removes the unimplemented
`ETHERCAT` and `RS485` enum exports. Public hand-model numeric values remain
unchanged: public value 0 now denotes the supported LHandPro 6DOF S model, and
public value 1 continues to denote the existing 16DOF model. The ordinary vendor
6DOF model is outside this deployment's support contract. Although the public
API and numeric values are unchanged, this meaning is deliberately incompatible
for users of that ordinary 6DOF hardware.

## Testing

Automatic lifecycle and transport tests use fakes and never enable, home, or
move a physical hand. The vendor ABI smoke test exercises only
create/model/destroy and does not initialize or communicate with a hand.
`scripts/test_dexhand.py --confirm-motion` is a manual hardware test only.

## ROS/Ament Build

```bash
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-select roboparty_dexhand \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3
colcon test --packages-select roboparty_dexhand
colcon test-result --verbose
```

## Installed CMake Target

Consumers use `find_package(roboparty_dexhand CONFIG REQUIRED)` and link only
`roboparty_dexhand::dexhand`. Private SDK, driver, and transport targets and
headers are not installed. `libdexhand.so` finds `libLHandProLib.so` beside it
through `$ORIGIN`; the Python extension finds the configured install library
directory through `$ORIGIN/../..`, so consumers do not need `LD_LIBRARY_PATH`.
The release gate verifies prefix-only discovery for the default install layout.
Custom install library directories such as `lib64` are not supported or verified
because their prefix search behavior depends on the host CMake platform policy.

## vcan Release Test

Configure with `-DDEXHAND_ENABLE_VCAN_TESTS=ON` only after creating and raising
`vcan-dexhand0`. The `vcan_two_socket` test is a required release gate and
proves two independent sockets, exact filters, no own-message delivery, and
bounded shutdown.

## ARM64 Release Gate

The repository carries an AArch64 SDK artifact. The Orange Pi validation for
the production commit above completed a native Debug/Werror build, exactly
8/8 CTests, install/export relocation, AArch64 artifact and RPATH checks, and
installed Python construction checks. x86 ELF inspection is not a substitute;
the board evidence is retained at
`/home/orangepi/roboparty_dexhand_can0_full_20260824/evidence`.

The same board then completed the bounded physical can0 test with CAN-FD node
ID 1 and the public 6DOF model: three 0-to-5000 and 5000-to-0 cycles, a final
open command, four cleanup steps, and a unique `phase_complete` event. The
validated vendor DOF pair was `(11, 6)`. The command returned zero; postflight
left can0 `ERROR-ACTIVE` at 1 Mbit/s nominal and 5 Mbit/s data rate with zero
bus, protocol, RX, TX, and dropped-frame errors and no active receiver.

## Runtime Safety Constraints

The driver uses the spdlog logger named `dexhand` from worker threads. If an
application externally pre-registers that logger, all attached sinks must be
thread-safe `_mt` sinks. SocketCAN worker and process-global callback-boundary
diagnostics use only that named logger, remain silent when it is absent, and
swallow logging exceptions at `noexcept` boundaries.

The vendor DOF probe reports `(total, active)` as `(11, 6)` for the 6DOF S model
and `(21, 16)` for the 16-DOF model. Initialization accepts only the exact pair
for the selected model, and `get_dof()` returns that validated snapshot.

Python users must explicitly call `deinit_hand()` before releasing the final
hand object reference. The current pybind11 2.11 binding has no
release-GIL-before-C++-destructor policy, so object destruction is not a
substitute for explicit shutdown.

Vendor callback cleanup depends on the bundled SDK's `stop_monitor quiescence`
contract: `lhandprolib_stop_monitor()` must return only after callbacks have
quiesced. Any SDK upgrade requires renewed physical-hardware and vendor
validation of that shutdown guarantee before release. The pinned binary has
project evidence from 498 complete 60-second lifecycles with zero late
callbacks in the callback counters, followed by an independent 10-second
diagnostic with 84 lifecycles, 21,434 successful decodes, and zero late
callbacks. The first run also recorded 101 decode failures; the follow-up did
not reproduce them, so that residual remains documented.

## Vendor License Boundary

RoboParty-authored source is GPL-3.0. The project owner's redistribution
authorization for the exact vendor artifacts is recorded in
`thirdparty/README.md`; it does not relicense those artifacts or imply a
vendor API guarantee.
