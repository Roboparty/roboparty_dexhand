# RP_Hand Public Branding Design

## Goal

Expose the white-label product as `RP_Hand` in public documentation and normal
consumer APIs without breaking applications already built against the existing
vendor-derived identifiers.

## Naming Boundary

The public product name is `RP_Hand`. The repository, Debian package, CMake
package, Python module, and CLI retain their established software-component
names:

- repository and CMake package: `roboparty_dexhand`;
- Debian package: `roboparty-dexhand`;
- Python module: `dexhand_py`;
- configuration CLI: `roboparty-dexhand-config`.

Public examples and product descriptions use `RP_Hand`. They do not identify
the OEM model. Vendor names remain only where they are required by the bundled
SDK, private implementation, compatibility API, license boundary, or private
engineering tests.

## Public API

The preferred factory type string becomes `"RP_Hand"`. The factory accepts
both `"RP_Hand"` and the existing `"LHandPro"` string, dispatching both to the
same private driver. An unsupported string continues to raise the existing
invalid-argument error.

The stable public model value remains numeric value `0`. New preferred aliases
are added without changing ABI values:

- C++: `HAND_RP_HAND_6DOF = 0`;
- Python: `HandModel.RP_HAND_6DOF`.

The existing C++ `HAND_LHANDPRO_6DOF` and Python
`HandModel.LHANDPRO_6DOF` names remain as compatibility aliases with value `0`.
The existing 16DOF value `1` is retained for compatibility but remains outside
the supported product scope. No `RP_Hand` 16DOF alias is introduced because
RoboParty does not offer that product.

## Documentation

The root README becomes vendor-neutral in its normal user-facing prose and
examples:

- describe the supported device as `RP_Hand 6DOF`;
- use `"RP_Hand"`, `HandModel.RP_HAND_6DOF`, and
  `HAND_RP_HAND_6DOF` in runnable examples;
- explain only that legacy names remain compatible, without printing the OEM
  identifiers in the main workflow;
- retain the existing CAN-FD, feedback-period, lifecycle, safety, and dual-hand
  instructions.

The package metadata description and public header comments use `RP_Hand`.
The manual hardware-test script also uses the preferred branded factory and
model aliases so it verifies the public path.

## Private Implementation

The following remain unchanged:

- `src/drivers/lhandpro/` names and private C++ classes;
- vendor C ABI calls and headers;
- `libLHandProLib.so` filename;
- CMake's private imported SDK target;
- third-party licensing and redistribution documentation;
- internal tests that intentionally exercise backward compatibility.

This avoids a high-risk internal rename that would not hide the immutable SDK
binary name and would provide no user-facing benefit.

## Compatibility And Errors

Both preferred and legacy factory strings must construct the same supported
driver. Both model aliases must have identical numeric values. Tests must prove
the aliases and both factory strings behave identically at the public boundary.
Error messages for unsupported type/model values should describe the supported
`RP_Hand` public contract without changing exception types.

## Validation

Implementation is complete when:

1. New factory and enum API tests fail before implementation and pass after it.
2. Existing tests continue to pass, proving legacy source compatibility.
3. Python binding tests verify `RP_HAND_6DOF == LHANDPRO_6DOF == 0`.
4. README code examples parse and reference only preferred branded identifiers.
5. Public prose and package metadata contain no OEM product name.
6. The complete registered CTest suite passes.
