# Public Repository Cleanup And Validation Guide Design

## Goal

Keep `Roboparty/roboparty_dexhand` as a concise public product repository while
preserving internal engineering records in the private
`Roboparty/roboparty_dexhand_internal` repository. Add a copy-paste-oriented
hardware acceptance guide without weakening automated release tests.

## Repository Boundary

The public repository retains everything required to understand, build, test,
package, and use the product:

- production C++ and Python source;
- public headers and CMake package files;
- Debian packaging and GitHub Actions;
- all default automated unit, lifecycle, transport, API, artifact, and install
  tests;
- optional vcan integration tests;
- bundled SDK artifacts and their required license boundary documentation;
- `README.md` and the new `VALIDATION.md`.

The following internal or historical records are copied to the private
repository and then removed from the public repository's current branch:

- `docs/superpowers/`;
- `CODE_REVIEW.md`;
- `tests/hardware/lhandpro_callback_quiescence_stress.cpp`;
- `thirdparty/vendor-authorization-request.md`.

Removing these files from the current branch does not remove them from existing
public Git history. This cleanup does not rewrite or force-push history because
the files contain no credentials and history rewriting would create needless
consumer disruption.

## Private Archive

Create `Roboparty/roboparty_dexhand_internal` with private visibility. It is a
small engineering-record archive, not a second buildable copy of the product.
The initial snapshot preserves the original relative paths and adds a root
README describing:

- the source public repository;
- the snapshot date and source commit;
- that these records are historical/internal and are not product usage
  documentation;
- that production code and automated tests remain owned by the public
  repository.

The archive commit must be pushed and its private visibility verified before
any public file is deleted.

## Public Documentation

The root `README.md` remains the normal installation and API tutorial. Add one
short link near the beginning to `VALIDATION.md` for board/hardware acceptance.
Do not duplicate the full validation sequence in both files.

`VALIDATION.md` is a Chinese, command-first checklist for an installed ARM64
package. It contains:

1. safety and hardware prerequisites;
2. loading `/opt/roboparty/setup.bash`;
3. selecting `CAN_INTERFACE` and `NODE_ID` once;
4. inspecting the selected SocketCAN interface;
5. applying the fixed 20 ms feedback period;
6. a mandatory hand-only power-cycle boundary;
7. reading back six raw values and requiring every value to be `200`;
8. importing the installed Python module and printing
   `HandModel.RP_HAND_6DOF.value`, which must print `0`;
9. a bounded six-axis motion and position-tracking command;
10. explicit return-to-zero and `deinit_hand()` cleanup behavior;
11. a compact acceptance-result checklist.

The guide uses `CAN_INTERFACE=can3` as an editable example and references the
variable thereafter, so changing ports requires one edit. Commands use the
public `RP_Hand` API only.

## Motion Validation Safety

The motion block is directly copyable but must not move immediately without an
operator confirmation. It verifies:

- the operator types an explicit confirmation token;
- the selected interface exists and is up;
- `init_hand(True, False, 0.0)` succeeds;
- driver health is valid;
- the active DOF count is exactly six;
- every joint alarm is zero;
- the hand is already at a known reference/zero state before bypassing homing.

It then enables no-home movement, commands joints 1 through 6 to a bounded
target, waits, prints all six measured positions, returns all six to zero,
prints the final positions, restores normal homing enforcement, and always
calls `deinit_hand()` in `finally`.

## Automated Tests

The public `tests/` directory remains. These files are part of the Debian and
GitHub release gate and are not shipped in the installed binary package.
Only the unregistered manual callback stress source under `tests/hardware/` is
archived privately.

## Validation

Completion requires:

1. private repository visibility is `PRIVATE`;
2. every selected internal file exists at the pushed private HEAD;
3. those paths are absent from the public working tree after the archive is
   verified;
4. `VALIDATION.md` Bash and Python blocks parse successfully;
5. all documented public commands and API names match the installed package;
6. the public strict build and complete registered CTest suite pass;
7. public `README.md` links to `VALIDATION.md`;
8. public automated tests and GitHub workflow remain present;
9. both repositories have clean working trees after their respective commits.
