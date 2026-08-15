# LHandPro 6DOF S Support Design

## Context

The Orange Pi hardware probe used `can0`, CAN-FD node ID 1, and the public
`LHANDPRO_6DOF` model. Communication reached the physical hand without CAN
errors, but initialization rolled back because the post-communication model
readback was vendor value `1` (`C_LAC_DOF_6_S`) rather than value `0`
(`C_LAC_DOF_6`). The hand reports the expected `(total, active)` DOF pair
`(11, 6)`.

Only the 6DOF S hand is required by the current deployment. Supporting both
6DOF variants through separate public models would add configuration surface
that the deployment cannot currently exercise.

## Decision

Keep the existing public API and numeric values unchanged:

- `HAND_LHANDPRO_6DOF = 0`
- `HAND_LHANDPRO_16DOF = 1`

Change the private LHandPro mapping for `HAND_LHANDPRO_6DOF` from vendor model
`C_LAC_DOF_6` (`0`) to `C_LAC_DOF_6_S` (`1`). Continue to require the exact
DOF pair `(11, 6)`. Leave the existing 16DOF mapping and `(21, 16)` validation
unchanged.

This deliberately makes the current `LHANDPRO_6DOF` configuration mean the
6DOF S hardware used by RoboParty. The ordinary vendor 6DOF model is not part
of the supported deployment contract.

## Error Handling

Initialization must continue to fail transactionally if either condition is
not met after communication starts:

- the SDK model readback is not `C_LAC_DOF_6_S`; or
- the reported DOF pair is not exactly `(11, 6)`.

No validation bypass, fallback, or automatic model detection is added.

## Verification

Automated coverage will prove that:

- the existing public 6DOF factory value selects vendor model `1`;
- model readback `1` with DOF `(11, 6)` initializes successfully;
- other model readbacks and incorrect DOF pairs still roll back cleanly;
- the SDK adapter smoke verifies set/get of vendor model `1` and DOF
  `(11, 6)`; and
- the existing 16DOF path remains unchanged.

After native AArch64 build and automated tests pass, the Orange Pi probe will
repeat initialization with enable and home disabled. It will immediately set
`move_no_home` back to `0`, read DOF/status feedback, then stop, disable, and
deinitialize. Homing and position motion remain outside this change.

## Compatibility

The change preserves the C++ and Python factory signatures, public enum
values, target names, and ABI. It is a deliberate semantic compatibility
break only for users of the unsupported ordinary 6DOF hardware.
