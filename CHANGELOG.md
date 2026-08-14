# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/spec/v2.0.0.html).

The JSON produced by `bw_cli --json` carries its own schema version in the
`schema` field; a breaking change to that shape is called out here explicitly.

## [Unreleased]

## [0.4.1] - 2026-08-14

### Fixed

- Live builds against an MSVC build directory no longer require starting Build
  Weather from a Developer Command Prompt. When `INCLUDE` is not set, the
  `vcvarsall.bat` belonging to that build's own toolchain is run and its
  environment handed to ninja, which is what Visual Studio does. The toolchain
  comes from `CMAKE_LINKER` in the build's `CMakeCache.txt` and the
  architecture from `VSInheritEnvironments.txt`. Previously every compile
  failed with C1083 and every link with LNK1104.

## [0.4.0] - 2026-08-13

First public release.

### Added

- **Map.** The build as a squarified treemap of the source tree, with zoom,
  pan, drill-down, and colour by compile time or by delta against a baseline.
  Stable layout, on by default, orders cells by name so a file keeps its place
  between builds.
- **Analysis.** Slowest steps from `.ninja_log`; headers ranked by total cost
  summed across every translation unit, with the TU count beside each;
  template instantiations; per-unit frontend/backend split. The last three
  come from clang's `-ftime-trace` documents.
- **Compare.** Per-file deltas against a baseline `.ninja_log`, worst
  regression first, with CSV export. Also available as `--baseline` on the
  command line.
- **Replay.** Scrub any build already in the log and watch it again, with the
  in-flight job count at each moment.
- **Live builds.** Run ninja from inside the app and watch the map fill in.
  Cells flash on completion and settle to their final colour.
- **`bw_cli`.** Headless `analyze` and `compare` over the same libraries, with
  JSON and CSV output, for CI and scripts.
- Export of the analysis as JSON, and of targets, headers, templates and
  deltas as CSV.
- Light and dark themes, following the system by default.
- User guide with annotated screenshots, in Markdown and PDF.
- A self-contained Windows package, `cpack -C Release` from the build
  directory. It carries the Qt runtime, the QML modules, the Visual C++
  runtime DLLs and the docs, so it runs on a machine with neither Qt nor the
  redistributable installed. `sdk/` in the package exports a CMake package for
  `BW::Core`, `BW::Build` and `BW::Treemap`.

### Notes

- Windows only. The three core libraries carry no Qt and nothing
  Windows-specific, but no other platform has been tested.
- The header and template rankings need a clang-cl build compiled with
  `-ftime-trace`; MSVC produces no equivalent data.

[Unreleased]: https://github.com/MrRobot1370/build-weather/compare/v0.4.1...HEAD
[0.4.1]: https://github.com/MrRobot1370/build-weather/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/MrRobot1370/build-weather/releases/tag/v0.4.0
