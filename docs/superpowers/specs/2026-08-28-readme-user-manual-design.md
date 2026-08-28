# README User Manual Design

## Goal

Turn the repository README into a Chinese-first daily-use manual for robot
developers integrating `roboparty_dexhand`. Commands, API names, identifiers,
and code remain in English.

## Audience

The primary reader is a RoboParty developer who needs to install the package,
configure an Orange Pi CAN-FD interface, provision a new hand, and control one
or two LHandPro 6DOF S hands. The reader should not need knowledge of the
library's release-validation history or vendor SDK internals.

## Scope

The README will contain, in this order:

1. Project purpose and supported hardware/software scope.
2. Package installation and source-build instructions.
3. Linux CAN-FD setup at 1 Mbps nominal and 5 Mbps data rate.
4. One-time 50 Hz feedback-period provisioning for a new, replacement, reset,
   or unknown hand.
5. A complete, directly runnable Python motion example with explicit cleanup.
6. Selecting `can0`, `can1`, `can2`, or `can3` according to physical wiring.
7. A two-hand example using separate processes and separate CAN interfaces.
8. A minimal C++ example.
9. A concise list of the main public API calls used in normal operation.
10. Required shutdown and operational safety notes.
11. Build and license information.

## Exclusions

The public manual will not contain:

- ARM64 release-gate evidence or evidence paths;
- callback-quiescence stress-test history;
- vcan release-test details;
- SDO acknowledgement investigation history;
- SDK compatibility-probe analysis;
- commit-by-commit physical validation records;
- a standalone troubleshooting or "common faults" section;
- internal superpowers plans or specifications.

Necessary constraints will be stated next to the relevant instruction instead
of being collected as fault documentation. Examples include selecting the
correct CAN interface, power-cycling the hand after persistent provisioning,
and always calling `deinit_hand()`.

## Usage Flow

The manual will make the intended workflow explicit:

```text
install package
  -> configure and raise CAN-FD interface
  -> provision and verify 20 ms base period once when required
  -> create and initialize HandDriver
  -> command/read the hand
  -> deinitialize in finally/RAII cleanup
```

The distinction between 20 ms, 50 Hz, and approximately 100 aggregate CAN-FD
frames per second will be explained briefly: two feedback frame types are each
emitted at 50 Hz.

## Examples

Examples must be copy-paste runnable and use consistent values and output
labels. The primary Python example will command all six joints to a bounded
target, read feedback, return to zero, and deinitialize the driver. It will use
an interface variable so changing physical ports requires changing one value.

The two-hand example will demonstrate one process per hand, because the vendor
SDK callback is process-global. Each process will receive its own interface and
node ID rather than implying that `roboparty_dexhand` or `roboparty_deploy`
automatically chooses a CAN port.

## Validation

After editing:

- check every documented command and API name against the current source;
- render-scan headings and fenced code blocks;
- run repository documentation-related tests, if present;
- verify the README contains no stale evidence paths or release-history
  sections;
- review the final diff for Chinese clarity and copy-paste correctness.
