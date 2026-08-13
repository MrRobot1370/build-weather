## What this changes

<!-- And why. The diff already covers the what. -->

## How it was checked

<!--
Which of these you ran, and what happened. A visual change needs a
screenshot of the running app against a real .ninja_log: neither the compiler
nor ctest catches a QML binding that evaluates to undefined or a colour ramp
that has saturated.
-->

- [ ] `cmake --build --preset msvc-x64-release` is clean, no new warnings
- [ ] `ctest --preset msvc-x64-release` passes
- [ ] `bw_cli analyze build\ninja-x64` still prints sensible numbers
- [ ] Screenshot attached, if this changes anything visual
- [ ] Tests added or updated, if the change is testable
- [ ] Docs updated, if behaviour changed
- [ ] `CHANGELOG.md` updated under `Unreleased`, if a user would notice

## Anything worth a second opinion

<!-- Trade-offs you made, or parts you are unsure about. -->
