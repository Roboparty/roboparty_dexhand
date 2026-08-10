# roboparty_dexhand Independent CAN-FD Design

Status: approved in the design discussion on 2026-08-10

## Context

`roboparty_dexhand` follows the factory structure of `roboparty_motors`, but
its current LHandPro driver directly includes the private motors header
`src/protocol/canfd_iso.hpp` and links the internal `motors_canfd` target. That
coupling prevents dexhand from being an independently owned, installed package.
It also makes a future sibling-package deployment under
`roboparty_deploy/src` depend on motors implementation details.

The accepted boundary is that dexhand and motors may use separate SocketCAN
sockets bound to the same physical CAN interface. They do not need to share a
C++ singleton or a socket file descriptor.

## Scope

This design changes only `roboparty_dexhand`.

The following repositories are read-only references and must not be modified:

- `/home/sjh/leisai_hand/roboparty_motors`
- `/home/sjh/leisai_hand/roboparty_hand`
- `/home/sjh/leisai_hand/roboparty_deploy`

Adding dexhand to the deploy repository as a Git submodule is a later task.

## Goals

- Make dexhand independent of all motors source files, libraries, and package
  metadata.
- Preserve the `HandDriver` factory architecture and existing control methods.
- Add a private, testable CAN-FD transport layer owned by dexhand.
- Support native Linux builds on x86-64 development machines and AArch64
  Orange Pi/RDK controllers.
- Provide relocatable CMake and ament install exports.
- Make SDK, socket, callback, and thread cleanup explicit and idempotent.
- Reject unsupported configuration instead of silently ignoring it.
- Keep existing control/getter behavior and stable model values. Treat removal
  of the two meaningless bitrate arguments and unsupported communication enum
  values as an explicit pre-1.0 API break with migration documentation.

## Non-goals

- Sharing one socket object between motors and dexhand.
- Modifying or publishing a new API from `roboparty_motors`.
- Adding EtherCAT, RS485, or standard CAN support.
- Supporting more than one active LHandPro SDK instance in a process. The
  vendor CAN-FD transmit callback has no handle or user-data argument.
- Automatically configuring CAN bitrate or link state. Deployment tooling owns
  Linux network-interface configuration.
- Running automatic homing or motion commands during tests.
- Integrating the package into `roboparty_deploy` in this change.

## Repository Layout

```text
roboparty_dexhand/
|-- include/
|   `-- hand_driver.hpp
|-- src/
|   |-- CMakeLists.txt
|   |-- hand_driver.cpp
|   |-- pybind_module.cpp
|   |-- protocol/
|   |   |-- CMakeLists.txt
|   |   |-- canfd_transport.hpp
|   |   `-- socket_canfd_transport.{hpp,cpp}
|   `-- drivers/
|       |-- CMakeLists.txt
|       `-- lhandpro/
|           |-- CMakeLists.txt
|           |-- lhandpro_driver.{hpp,cpp}
|           `-- lhandpro_sdk.{hpp,cpp}
|-- thirdparty/
|   |-- README.md
|   |-- include/LHandProLib/LHandProLib.h
|   `-- lib/
|       |-- x86_64/libLHandProLib.so
|       `-- aarch64/libLHandProLib.so
|-- tests/
|   |-- fakes/
|   |-- test_factory.cpp
|   |-- test_lhandpro_driver.cpp
|   `-- test_socket_canfd_transport.cpp
|-- cmake/roboparty_dexhandConfig.cmake.in
|-- docs/superpowers/specs/
|-- scripts/test_dexhand.py
|-- CMakeLists.txt
|-- LICENSE
|-- .gitignore
|-- package.xml
`-- README.md
```

The root build delegates through `src`, then `src/protocol` and
`src/drivers`, and finally the vendor driver directory. Vendor headers and
binaries live under `thirdparty`; they are not mixed with RoboParty driver
sources.

## Components

### Public Hand API

`include/hand_driver.hpp` remains the only installed public source header. It
contains the generic `HandDriver` interface, factory declaration, logger, CAN
interface name, and cached degree-of-freedom state.

`get_can_name()` and `get_dof()` use virtual default implementations backed by
protected base state. Feedback getters that require SDK calls remain pure
virtual. Default arguments appear only on the public base declaration and the
Python binding, not on overrides.

### Factory

`src/hand_driver.cpp` owns vendor selection and includes the vendor driver
header. It converts the public integer model value to a strong LHandPro model
enum before constructing `LHandProDriver`.

The supported factory signature becomes:

```cpp
static std::shared_ptr<HandDriver> create_hand(
    const std::string& hand_type,
    const std::string& interface_type,
    const std::string& interface,
    int hand_model = 0,
    int canfd_node_id = 1);
```

The nominal and data bitrate arguments are removed because dexhand does not
configure the shared Linux CAN network device. Retaining those arguments would
continue to advertise configuration that the library silently ignores.

The factory validates the vendor name, `canfd` interface type, non-empty Linux
interface name, supported model, and CANopen node ID in the inclusive range
1-127. Invalid configuration throws `std::invalid_argument` with the rejected
value in the message.

Removing the bitrate parameters is a deliberate API break and raises the
package version from `0.1.0` to `0.2.0`. Existing C++ seven-argument calls fail
at compile time. Existing Python calls using `canfd_nom_baudrate` or
`canfd_dat_baudrate` fail with `TypeError`. The migration is to remove those
arguments and configure the Linux CAN interface in deployment tooling.

### Model Compatibility

Python keeps these names:

- `HandModel.LHANDPRO_6DOF`
- `HandModel.LHANDPRO_16DOF`

The public numeric values remain stable: 6 DOF is `0` and 16 DOF is `1`.
Factory code maps them to the vendor values `C_LAC_DOF_6` (`0`) and
`C_LAC_DOF_16` (`2`). The SDK value `1` means the vendor's 6-DOF-S model and is
never used as a direct cast from the public enum.

The selected model is no longer ignored. Immediately after creating the SDK
handle and before communication initialization, the driver calls
`lhandprolib_set_hand_type` with the mapped vendor value and verifies it with
`lhandprolib_get_hand_type`. This ordering is supported by a hardware-free
probe against the supplied SDK. After communication initialization, the driver
also compares the reported total DOF with the expected model. Any failure or
mismatch performs full rollback.

### Communication Type Compatibility

`HandCommType` remains a scoped enum but exposes only `CANFD`. The current
`ETHERCAT` and `RS485` Python values are removed in version `0.2.0` because no
driver implements them. The factory continues to accept the string `canfd` to
match the motors-style factory convention.

### CAN-FD Transport

`CanFdTransport` is a dexhand-private abstract interface. It uses a private
`CanFdFrame` value type containing a raw 29-bit ID, extended-frame flag,
bitrate-switch flag, payload length, and a 64-byte payload array. It defines:

- `open(interface, standard_ids) -> bool`
- `transmit(frame) noexcept -> bool`
- `set_receive_callback(callback)`
- `clear_receive_callback() noexcept`
- `close() noexcept`

The callback receives a copied frame on the transport receive thread and must
return promptly. `clear_receive_callback()` disables new dispatch and waits for
any callback already in flight, so no callback can execute after it returns.
The interface is not installed and is not part of the supported consumer API.

`SocketCanFdTransport` provides the Linux implementation:

- Opens its own `PF_CAN/SOCK_RAW/CAN_RAW` socket for the configured interface.
- Enables `CAN_RAW_FD_FRAMES`.
- Applies kernel receive filters for `0x500 + node`, `0x480 + node`,
  `0x580 + node`, and `0x180 + node`.
- Does not enable `CAN_RAW_RECV_OWN_MSGS`; SDK decode receives device feedback,
  not the commands emitted by this socket.
- Uses non-blocking I/O.
- Owns one receive thread. Transmission is a synchronous `write` serialized by
  a mutex, so the SDK callback receives the actual kernel acceptance result.
- Serializes callback replacement and shutdown.
- Rejects transmission while closed or when `write` does not return
  `CANFD_MTU`.
- Records and logs socket, read, and write errors.
- Never changes bitrate, data bitrate, link state, or restart policy.
- Makes `close()` idempotent and joins the receive thread before returning.

Standard and extended IDs are masked explicitly. Payload length is validated
against 64 bytes. The SDK's BRS behavior is retained: frames longer than eight
bytes set BRS, while eight-byte command frames do not. Receive decode is passed
the actual `frame.len`, matching the vendor CAN-FD C example.

The transport handles only frame delivery. CANopen encoding and decoding
remain inside the vendor SDK.

### LHandPro Driver

`LHandProDriver` adapts the vendor C API to `HandDriver` and the private
transport. It owns one `LHandProSdk` adapter and one transport instance.

`LHandProSdk` is a private virtual interface for every vendor operation used by
the driver. `CapiLHandProSdk` is the production implementation and owns the raw
SDK handle. An internal constructor accepts SDK and transport instances for
hardware-free tests; the public factory always constructs the production
implementations. Neither injection point is installed as public API.

The data flow is:

```text
Application
  -> HandDriver factory/API
  -> LHandProDriver
  -> vendor SDK
  -> C transmit callback
  -> SocketCanFdTransport
  -> Linux SocketCAN interface

Linux SocketCAN interface
  -> SocketCanFdTransport receive thread/filter
  -> vendor SDK decode function
  -> cached feedback getters
  -> Application
```

Motors may bind another socket to the same Linux CAN interface. Kernel CAN
arbitration and filters isolate the independent consumers.

## Lifecycle and Concurrency

Construction stores and validates configuration but performs no I/O. Driver
lifecycle is an explicit state machine: `Created`, `Initializing`, `Ready`, and
`Stopping`. A lifecycle mutex serializes initialization and deinitialization.
An SDK-call mutex serializes public methods and lifecycle operations. Receive
decode does not take that mutex: vendor calls may synchronously wait for CAN-FD
feedback, so blocking decode behind the caller would deadlock. Instead, the
transport's quiescent receive-callback gate guarantees that decode cannot
outlive the SDK handle. Public methods check `Ready`, acquire the SDK-call
mutex, check `Ready` again, and only then enter the vendor API.

`init_hand()` performs these stages:

1. Transition from `Created` to `Initializing` and claim the active LHandPro
   slot from the shared dexhand runtime.
2. Create the SDK handle, set the mapped hand type, and read it back.
3. Create and open the private CAN-FD transport with response-ID filters.
4. Install the SDK transmit callback and transport receive callback.
5. Call `lhandprolib_initial_ex` for CAN-FD and the selected node.
6. Start SDK monitoring and read DOF information.
7. Verify reported hand type and DOF against the requested model.
8. Optionally enable and home motors.
9. Enable movement without completed homing, matching current behavior.
10. Transition to `Ready`.

Each completed stage has a matching rollback action. Failure returns `false`
after rollback and leaves the object reusable for a later initialization
attempt.

The SDK transmit callback cannot capture an object and receives no SDK handle.
A mutex-protected active-instance pointer in the shared `dexhand` runtime
therefore routes transmissions. A callback gate counts in-flight transmit
callbacks and can reject new entries. A second simultaneous LHandPro
initialization fails explicitly.

`deinit_hand()` and the destructor use one private, non-virtual `cleanup_()`
path. Cleanup is idempotent and does not throw. Under the lifecycle lock it:

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

Callbacks catch all exceptions at the C boundary. Receive callbacks may decode
during `Initializing`, `Ready`, and the early `Stopping` phase, until
`clear_receive_callback()` closes the receive gate. The gate's quiescence wait
drains all in-flight decode before the SDK handle can be destroyed.

## Error Handling

Every vendor function that returns an SDK error code is checked.

- Initialization failures log the operation and numeric SDK code, roll back,
  and return `false`.
- Existing `void` control methods retain their signature and log SDK failures.
- Getters log SDK failures and return a type-safe zero value.
- Destructor and callback paths never propagate exceptions.
- Socket setup errors include the interface and system error text.
- Transmit callback success means that the serialized SocketCAN `write` was
  accepted by the kernel; short write, closed socket, and system errors return
  `false`.

Python bindings release the GIL for blocking initialization, deinitialization,
and homing calls. Python-visible exceptions are used only for invalid factory
configuration; runtime SDK failures retain the C++ return-value contract.

`dexhand_py` binds every public `HandDriver` method and no vendor driver class.
It exports `HandCommType.CANFD`, both stable `HandModel` names, the five-argument
factory with matching keyword names/defaults, lifecycle methods, all control
and feedback methods, `get_dof()` as a two-element tuple, and `get_can_name()`.

## Build and Install Design

The build retains the dual ament/pure-CMake structure and defines:

- `dexhand_canfd` as a static transport library.
- `lhandpro_driver` as a static vendor driver library.
- `dexhand` as the shared aggregate runtime and factory library.
- `dexhand_py` as the pybind11 module.

The exact link graph is:

```text
dexhand_py
  -> dexhand (SHARED, installed/exported)
       -> lhandpro_driver (STATIC, private)
            -> dexhand_canfd (STATIC, private)
            -> roboparty_dexhand::lhandpro_sdk (IMPORTED, private)
```

Only `dexhand` is installed and exported as
`roboparty_dexhand::dexhand`. Both C++ consumers and `dexhand_py` therefore use
one loaded shared runtime and one active-instance guard. Private component
targets and headers are not installed.

The vendor SDK is represented in the build by the exact imported target
`roboparty_dexhand::lhandpro_sdk`. It points to the selected source binary and
is linked privately into the shared runtime. The installed runtime records only
the SDK SONAME, not a build-tree absolute path. No SDK imported target needs to
be exposed to consumers.

Architecture selection normalizes these values:

- `x86_64` and `amd64` select `thirdparty/lib/x86_64`.
- `aarch64` and `arm64` select `thirdparty/lib/aarch64`.
- Other processors fail during CMake configuration with a supported-platform
  list.

Only the selected binary is installed. `dexhand` uses install RPATH `$ORIGIN`
to find the SDK beside it. `dexhand_py`, installed under Python site-packages,
uses `$ORIGIN/../..` to find `libdexhand.so` in the prefix's `lib` directory.

All compile settings are target-scoped. CMake requires C++17 through
`target_compile_features`; it does not overwrite `CMAKE_BUILD_TYPE`, global
compiler flags, or the caller's architecture settings.

The installed config loads the public fmt/spdlog dependencies and then the
single exported target file. Pure CMake and ament modes install the same header,
shared library, SDK binary, package config, version config, and Python module.

## Package and License Metadata

`package.xml` removes `roboparty_motors`, retains the actual fmt/spdlog and
Python build dependencies, declares the Python runtime dependency, and adds the
ament lint dependencies used under `BUILD_TESTING`. Version becomes `0.2.0`
because the factory signature and unsupported enum exports change. The
description names the currently implemented LHandPro CAN-FD scope rather than
future transports.

The repository gains:

- A root GPLv3 `LICENSE` for RoboParty-authored source.
- A `.gitignore` covering CMake, colcon, Python, editor, and generated output.
- `thirdparty/README.md` recording the vendor SDK distribution name,
  architectures, binary hashes, and license boundary.

No standalone vendor license file was found in the supplied
`LHandProLib-API-Linux-20260727` distribution. The repository GPL declaration
must not claim to relicense the binary. Public redistribution remains subject
to confirmation of the vendor's terms. Any SocketCAN source adapted from
`roboparty_hand` retains its original copyright attribution and GPL identifier.

## Documentation

README and Doxygen updates will:

- Describe independent sockets on a shared physical CAN bus.
- Remove the claim that motors and dexhand share one singleton/socket.
- State the one-active-LHandPro-per-process constraint.
- Document supported x86-64 and AArch64 Linux platforms.
- Remove bitrate arguments from C++ and Python examples.
- Include a `0.1.x` to `0.2.0` migration note for removed bitrate arguments and
  unsupported communication enum values.
- Keep method names and defaults synchronized with bindings.
- Use consistent SPDX and Doxygen style across RoboParty-authored files.

## Test Strategy

Tests are written before each implementation slice and observed failing for the
intended reason.

CTest drives small self-contained C++ test executables; no test framework is
added. Python binding checks use the Python standard library. When ament is
available, `ament_lint_auto` registers the declared lint dependencies. vcan
setup relies on standard Linux `iproute2`/kernel support and is kept outside
package runtime dependencies.

Hardware-free tests cover:

1. No motors header, CMake target, package dependency, or source path remains.
2. Factory validation, model mapping, default arguments, and unsupported types.
3. CAN-FD frame construction, ID filtering, serialized write failures, callback
   quiescence, and repeated transport shutdown.
4. `FakeLHandProSdk` and `FakeCanFdTransport` are injected through the private
   driver constructor. They exercise each initialization failure stage,
   hand-type mapping, rollback, repeated deinitialization, DOF mismatch,
   callback blocking during deinit, concurrent init rejection, successful
   retry after failure, and the single-active-instance guard.
5. Python exports, method coverage, enum names, defaults, and module import.
6. Pure-CMake configure/build/install and an isolated downstream
   `find_package(roboparty_dexhand)` consumer that links and runs with a clean
   environment using only the installed prefix.
7. Ament/colcon build and installed Python import.
8. Export inspection rejects source-tree absolute paths, bare dependency
   targets, and motors references.
9. XML validation, compiler warnings, and cppcheck.
10. ELF inspection verifies that each packaged SDK binary matches its source
    architecture and that CMake selects the expected path.

A vcan integration test verifies that two independent sockets can bind the same
virtual interface, transmit from both sockets, filter unrelated IDs, and shut
down without deadlock. It has a bounded timeout and is a release gate. It may be
skipped on an unprivileged development host, but a release cannot be declared
complete until it passes in CI or a controlled Linux test environment.

No automatic test enables, homes, or moves a real hand. The existing hardware
script remains a manual acceptance test.

The current development host has no AArch64 cross-compiler. Local verification
can prove SDK selection and ELF architecture but cannot prove an AArch64 native
link or runtime. A native build, installed C++ smoke executable, and Python
import on Orange Pi/RDK are release gates. Until they pass, the implementation
is AArch64-ready but not claimed as deployment-verified.

## Acceptance Criteria

- Only files under `roboparty_dexhand` change.
- `rg` finds no `roboparty_motors`, `MotorsCANFD`, or motors private include in
  build metadata or source code.
- Existing public control/getter method names and return types remain stable.
- Public model values remain `0` for 6 DOF and `1` for 16 DOF; tests verify the
  internal mappings to SDK values `0` and `2`.
- The two unused bitrate factory arguments are removed consistently from C++,
  Python, tests, and documentation, with a `0.2.0` migration note.
- x86-64 pure CMake and colcon builds pass without hardware.
- An isolated installed consumer configures and links successfully.
- `dexhand_py` imports from the installed prefix.
- Install exports are relocatable and use namespaced targets.
- The AArch64 SDK is present, identified as AArch64 ELF, and selected for an
  AArch64 CMake target processor.
- Shutdown, failed initialization, and repeated deinitialization are covered by
  hardware-free tests.
- Installed C++ and Python consumers both resolve the same shared dexhand
  runtime; no supported static runtime can duplicate the active-instance guard.
- The vcan two-socket/filter/shutdown release test passes before release.
- Native AArch64 build, installed C++ smoke, and Python import pass before an
  ARM deployment release.
- Vendor redistribution authorization is recorded before public release.
- No automatic test sends motion commands to physical hardware.

## Rejected Alternatives

### Publish or modify the motors CAN-FD layer

Rejected because the motors repository belongs to another module owner and a
dexhand change must not require edits to it.

### Depend on roboparty_hand transport internals

Rejected because `roboparty_hand` is a concrete hand-control package, not a
stable transport package. That dependency would replace one private coupling
with another.

### Put SocketCAN directly in LHandProDriver

Rejected because it mixes transport threads, socket lifetime, and vendor SDK
adaptation in one class and prevents isolated transport testing.

### Create a new shared transport repository now

Rejected because only dexhand requires ownership changes in this task. A
neutral transport package can be considered later if multiple independently
owned packages need a supported common API.

## Residual Risks

- Vendor binary redistribution rights are not documented in the supplied SDK
  archive. Missing authorization blocks public release.
- Native AArch64 link and runtime behavior cannot be verified on the current
  x86-64 host without a cross-toolchain or target board. Missing native build,
  C++ smoke, or Python import evidence blocks an ARM deployment release.
- Separate sockets do not provide one application-level scheduler across arm
  motors and dexhand. Kernel CAN arbitration remains authoritative under bus
  contention.
- The one-active-LHandPro limitation remains until the vendor SDK provides a
  callback with handle or user-data context.
