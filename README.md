# Build Weather

**See where your C++ build time actually goes.**

Build Weather turns a build into a treemap of your source tree: every file is
a rectangle, its area and colour are what it costs to compile. Post-mortem it
is a heat map. During a build, files light up as they finish and settle to
their final colour, so a parallel build reads as weather crossing a map.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/MrRobot1370/build-weather/actions/workflows/ci.yml/badge.svg)](https://github.com/MrRobot1370/build-weather/actions/workflows/ci.yml)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-lightgrey)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![Qt 6.5+](https://img.shields.io/badge/Qt-6.5%2B-41cd52)

![The Map tab](docs/images/01-overview.png)

It reads files your build already produces. There is nothing to instrument.

---

## Contents

- [Why](#why)
- [What you get](#what-you-get)
- [Requirements](#requirements)
- [Install](#install)
- [Quick start](#quick-start)
- [Using the app](#using-the-app)
- [bw_cli, the headless tool](#bw_cli-the-headless-tool)
- [Getting header and template costs](#getting-header-and-template-costs)
- [What the numbers mean](#what-the-numbers-mean)
- [How it is put together](#how-it-is-put-together)
- [Building from source](#building-from-source)
- [Troubleshooting](#troubleshooting)
- [Not implemented](#not-implemented)
- [Contributing](#contributing)
- [Licence](#licence)

---

## Why

A slow C++ build is usually a small number of files, and a scrolling wall of
compiler output is the worst possible way to find them. Worse, the file that
costs you the most is often not slow at all: it is a 200 ms header that 400
translation units include, and no per-file timing will ever point at it.

Build Weather answers three questions:

1. **Which files cost the most?** The map, and the *Slow steps* ranking.
2. **Which header costs the most, summed over everything that includes it?**
   The *Headers* ranking, with the number of translation units alongside.
3. **What did my change just cost?** *Compare*, per-file, worst regression
   first.

## What you get

| | |
| --- | --- |
| **Map** | The build as a squarified treemap. Zoom, pan, drill into a directory. Colour by compile time, or by the delta against a baseline. |
| **Analysis** | Slowest translation units; headers ranked by total cost across every TU with the TU count beside each; the most expensive template instantiations; the frontend/backend split. |
| **Compare** | Load a baseline `.ninja_log` and get per-file deltas, worst regression first. Exportable as CSV. |
| **Replay** | Scrub a build's timeline and watch it happen again, including how many jobs were in flight at any moment. |
| **Live** | Run ninja from inside the app and watch the map fill in. |
| **`bw_cli`** | The same analysis without a window, for CI and scripts. JSON and CSV output. |

## Requirements

| | |
| --- | --- |
| **OS** | Windows 10 or 11, x64. Windows-only today; see [Not implemented](#not-implemented). |
| **Compiler** | Visual Studio 2022 (MSVC v143), or the clang-cl it ships with |
| **Qt** | 6.5 or newer. Developed and tested against 6.6.2 (`msvc2019_64`). |
| **CMake** | 3.27 or newer |
| **ninja** | Only for live builds. Found on `PATH`, or the copy Visual Studio ships with its CMake integration. |

**The project you point it at** needs to be configured with CMake and the
Ninja generator. That is the only requirement Build Weather places on your
code: it reads ninja's own log, so any Ninja-generated build works, whatever
language it compiles.

## Install

### Download a release

Grab `BuildWeather-<version>-windows-x64.zip` from the
[releases page](https://github.com/MrRobot1370/build-weather/releases), unzip
it anywhere, and run `BuildWeather.exe`.

The package is self-contained: the Qt runtime, the QML modules and the Visual
C++ runtime DLLs are all inside it. There is nothing to install, no Qt to set
up and no redistributable to chase. It also carries `bw_cli.exe`, the user
guide, and `sdk/` with the headers and libraries for the Qt-free parsing and
layout libraries.

### Or build from source

About two minutes.

```bash
git clone https://github.com/MrRobot1370/build-weather.git
cd build-weather
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
```

The preset defaults `CMAKE_PREFIX_PATH` to `C:/Qt/6.6.2/msvc2019_64`. If your
Qt is elsewhere, override it on the command line, which wins over the preset:

```bash
cmake --preset msvc-x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.7.2/msvc2019_64
```

Or put your own `CMakeUserPresets.json` beside `CMakePresets.json` inheriting
from `base` with your path. That file is gitignored, so it will not turn into
a diff every time you pull.

Binaries land in `build/msvc-x64/bin/Release/`. `windeployqt` runs as a
post-build step, so the Qt DLLs and QML plugins are placed next to the
executable and it runs without `PATH` changes. Copying that directory to
another machine also needs the Visual C++ 2015-2022 redistributable there;
the Qt side is covered, the CRT is not.

Verify the build:

```bash
ctest --preset msvc-x64-release
```

## Quick start

Configure any CMake project with the Ninja generator and build it once:

```bash
cmake -S . -B build/ninja -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ninja
```

Then point Build Weather at that directory:

```bash
BuildWeather.exe C:\path\to\your\project\build\ninja
```

Or start it with no arguments and use **Open build dir**.

That is all that is needed for the map and the slow-step ranking. Two of the
three inputs are optional:

| File | Comes from | Gives you |
| --- | --- | --- |
| `.ninja_log` | ninja, automatically | **Required.** Exact start and end of every build step |
| `compile_commands.json` | `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | Map objects back to *your* source files instead of paths under `CMakeFiles/` |
| `*.json` beside each object | clang-cl with `-ftime-trace` | Per-header and per-template cost. See [below](#getting-header-and-template-costs). |

### Command line

```
BuildWeather.exe [<build-dir>] [--source <dir>] [--traces <dir>] [--baseline <file>]
```

| Option | Meaning |
| --- | --- |
| `<build-dir>` | Build directory to open on start; must contain `.ninja_log` |
| `--source <dir>` | Source root, when it cannot be inferred from the build directory |
| `--traces <dir>` | Directory to scan for `-ftime-trace` documents |
| `--baseline <file>` | Baseline `.ninja_log` to compare the loaded build against |
| `--help`, `--version` | The usual |

## Using the app

The full walkthrough, with annotated screenshots of every view, is in
**[docs/USER_GUIDE.md](docs/USER_GUIDE.md)**. A printable version is at
[docs/USER_GUIDE.pdf](docs/USER_GUIDE.pdf).

The short version:

| Action | Gesture |
| --- | --- |
| Zoom about the cursor | Mouse wheel |
| Pan | Drag with the left button |
| Drill into a directory | Double click |
| Back up one level | Right click, or **↑ up** |
| Fit the whole tree | **Fit**, or `Ctrl+0` |

| Key | Action |
| --- | --- |
| `Ctrl+O` | Open a build directory |
| `Ctrl+B` | Start or stop a build |
| `F5` | Re-read the log |
| `Ctrl+0` | Fit the map |
| `Ctrl++` / `Ctrl+-` | Zoom in / out |
| `Esc` | Leave replay |

## bw_cli, the headless tool

`bw_cli` runs the same libraries without a window. It is the thing to put in
CI, and the quickest way to get an answer without opening anything.

```
bw_cli analyze <build-dir> [--source <dir>] [--traces <dir>]
                           [--json <file>] [--csv <file>] [--top N]
bw_cli compare <baseline.ninja_log> <current.ninja_log>
                           [--source <dir>] [--build <dir>] [--csv <file>]
```

| Option | Meaning |
| --- | --- |
| `--source <dir>` | Source root, when it cannot be inferred |
| `--build <dir>` | Build root, for resolving paths ninja recorded relative to it |
| `--traces <dir>` | Directory to scan for `-ftime-trace` documents |
| `--json <file>` | Write the full analysis as JSON |
| `--csv <file>` | Write every step (`analyze`) or every delta (`compare`) |
| `--top N` | Rows to print, and to include in `--json`. Default 20. `--csv` is never truncated. |
| `-v`, `--version` | Print the version |

Exit codes: **0** success, **1** an input could not be read or an output could
not be written, **2** the command line was wrong. Branch on these in CI.

```bash
bw_cli analyze build\ninja --traces build\clangcl --top 20
```

```
build directory : C:/proj/build/ninja
source directory: C:/proj
compile database: 183 entries
log covers      : a single build
steps           : 162
total CPU time  : 4m 10.2s
wall time       : 1m 02.3s
peak parallelism: 12
median step     : 288ms

slowest steps
    1      7.73s  apps/BuildWeather/src/main.cpp
    2      7.60s  apps/BuildWeather/src/app_context.cpp
  ...
```

Regression-gate a pull request:

```bash
bw_cli compare baseline.ninja_log build\ninja\.ninja_log --csv deltas.csv
```

The JSON carries a `"schema": "build-weather/analysis/1"` field. That schema
name is versioned: a breaking change to the shape bumps it, so a script can
check it and fail loudly rather than silently misreading.

## Getting header and template costs

The *Headers*, *Templates* and *Units* rankings need `-ftime-trace` data, and
MSVC has no equivalent. So they need a **clang-cl** build, used only to
produce the data. Install the "C++ Clang tools for Windows" component of
Visual Studio, then configure a second build directory with the `ClangCL`
toolset:

```bash
cmake -S . -B build/clangcl -G "Visual Studio 17 2022" -A x64 -T ClangCL ^
      -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /clang:-ftime-trace"
cmake --build build/clangcl --config Release
```

Two things that are easy to get wrong. `-T ClangCL` selects a *toolset*, which
the Ninja generator does not support, so this has to use the Visual Studio
generator. And `CMAKE_CXX_FLAGS` **replaces** CMake's defaults rather than
adding to them, which is why `/DWIN32 /D_WINDOWS /EHsc` are repeated; drop
`/EHsc` and every `try` block fails to compile.

clang writes a trace JSON next to each object file. Point **Load -ftime-trace**
(or `--traces`) at that directory.

For this repository specifically, `tools\make-time-traces.cmd` does all of the
above into `build/clangcl-x64`. Set `BW_QT_PREFIX` first if your Qt is not at
the default location.

The timings then describe a *clang* build, not your MSVC one, so the absolute
numbers will differ. **The ranking of headers is what transfers**, and the
ranking is the part you act on.

## What the numbers mean

Being precise about this matters more than the animation does. A treemap
invites more trust than it earns, so here is exactly what is measured.

**Durations are exact.** They come from `.ninja_log`, which records the start
and end of every edge in milliseconds. Nothing is sampled or estimated.

**A `.ninja_log` accumulates across runs.** The default scope, *All builds*,
takes each target's most recent entry, which answers "what does a full build
of this tree cost". *Last build* keeps only the most recent ninja invocation.
The header says "log covers several builds" when that distinction applies, and
`bw_cli` prints the same note.

**Across several invocations, wall time and parallelism mean little.**
Timestamps are relative to the start of their own ninja run, so overlaying
several runs mixes clocks. Each individual duration stays exact; the *timeline*
does not. Switch to *Last build* before replaying anything.

**Splitting a log into invocations is only approximately possible.** Ninja
does not mark invocation boundaries, `end_ms` is not monotonic within a run,
and `mtime` records the source timestamp for copy edges. What ninja does
guarantee is that an output is built at most once per invocation, so the last
invocation is the trailing run of entries with no repeated output. That is
exact unless two consecutive builds touched disjoint file sets.

**Several steps can share a file.** A generated source is written by one edge
and compiled by another; both land on the same leaf and their durations are
summed, because that is what the file costs. The same edge logged under two
output names (ninja does this for outputs with both a relative and an absolute
name) is counted once.

**Zero-duration steps are still drawn.** They get a minimum area, so a fast
file does not silently vanish from the tree.

**Live mode animates on completion, not on start.** Ninja only prints a status
line when an edge *starts* if it believes stdout is a terminal, and a child
process never gets one. Behind a pipe it prints one line per finished edge.
The in-flight count comes from `%r`, which is ninja's own number.

**Paths from different sources are joined on one key.** Ninja gives paths
relative to the build directory, `-ftime-trace` gives absolute ones, and on
Windows both mix separators and casing. Everything goes through
`BW::Core::pathKey` and nothing joins on a raw string.

**Files outside the source tree get their own buckets.** `[generated]`,
`[external]` and `[system]` keep build output, vendored code and toolchain
headers from distorting the map.

**Layout is stable by default.** Cells are ordered by name, not by duration,
so a file keeps its place between builds and only its area changes. Turning
off *stable layout* orders by duration, which packs squarer but moves
everything whenever a timing moves.

## How it is put together

```
libs/BW_Core      path normalization, logging
libs/BW_Build     .ninja_log, ninja progress, compile_commands.json,
                  -ftime-trace, the snapshot model, report export
libs/BW_Treemap   squarified layout
libs/BW_UI        the QML design system (BW.UICore)
apps/BuildWeather the application
tools/bw_cli      headless driver
tools/docs        scripts that regenerate the user guide figures
tests/            unit tests over the Qt-free libraries, with fixtures
3rdparty/         header-only drops as INTERFACE targets
```

`BW_Core`, `BW_Build` and `BW_Treemap` contain **no Qt**. That is deliberate:
the parsers and the layout are the parts worth testing, and they are tested
without a `QGuiApplication`. The Qt layer above them is a thin adapter, and
`bw_cli` links the same libraries with no Qt at all.

55 test cases across four binaries cover the parsers, the path handling and
the layout invariants, with checked-in fixtures under `tests/data/`.

## Building from source

```bash
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release
```

| CMake option | Default | Meaning |
| --- | --- | --- |
| `BW_BUILD_TESTS` | `ON` | Build the unit tests |
| `BW_BUILD_TOOLS` | `ON` | Build `bw_cli` |
| `BUILD_SHARED_LIBS` | `OFF` | Build the internal libraries as shared |

Presets:

| Preset | Generator | Notes |
| --- | --- | --- |
| `msvc-x64` | Visual Studio 17 2022 | The normal one |
| `ninja-x64` | Ninja Multi-Config | Must be configured from a developer environment. `tools\build-ninja.cmd` does that for you, and gives you a real `.ninja_log` for the app to look at. |

Build presets are `<configure-preset>-<config>`, for example
`msvc-x64-release`, `msvc-x64-debug`, `msvc-x64-relwithdebinfo`.

### Building the release package

```bash
cmake --build --preset msvc-x64-release
cd build/msvc-x64 && cpack -C Release
```

That produces `BuildWeather-<version>-windows-x64.zip`: the executables, the
Qt runtime and QML modules from `windeployqt`, the Visual C++ runtime DLLs,
the docs, and `sdk/`. Unpacking it gives one named folder that runs on a
machine with no Qt and no redistributable installed.

The `sdk/` directory exports a CMake package, so the Qt-free libraries can be
consumed directly:

```cmake
find_package(BuildWeather REQUIRED)   # -DCMAKE_PREFIX_PATH=<package>/sdk
target_link_libraries(you PRIVATE BW::Build BW::Treemap)
```

## Troubleshooting

**"ninja was not found."** Live mode needs `ninja.exe` on `PATH`. Starting
Build Weather from a Developer Command Prompt is the easiest fix; that
environment includes the copy Visual Studio ships with its CMake integration.
Everything except live builds works without ninja.

**Every compile fails with C1083 during a live build.** `cl.exe` finds neither
the CRT headers nor the import libraries unless `INCLUDE` and `LIB` are set,
and a GUI application started from Explorer does not have them. Launch Build
Weather from a Developer Command Prompt, or run `vcvars64.bat` first. The app
checks for this and says so on the Output tab rather than letting the map fill
with failures.

**The map shows paths under `CMakeFiles/` instead of my source files.**
`compile_commands.json` is missing. Reconfigure with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

**The Headers and Templates tabs are empty.** They need `-ftime-trace` data
from a clang-cl build; see [above](#getting-header-and-template-costs). MSVC
does not produce it.

**"log covers several builds" and the wall time looks wrong.** It is: see
[What the numbers mean](#what-the-numbers-mean). Switch the scope to *Last
build*.

**CMake cannot find Qt.** Pass `-DCMAKE_PREFIX_PATH=<your Qt msvc dir>` on the
configure line, as shown in [Install](#install).

## Not implemented

**MSVC vcperf / C++ Build Insights.** This would give MSVC builds the same
drill-down clang-cl gives through `-ftime-trace`. It is a real chunk of work,
so here is the estimate rather than a surprise:

- The C++ Build Insights SDK is a separate download, is Windows-only and ships
  as a static library with its own headers, so it becomes a build dependency
  of `BW_Build` and breaks the "no Qt, no platform" property that currently
  makes that library testable in isolation. It would need to live behind an
  interface in its own library, say `BW_Vcperf`.
- The analysis model is event-callback based (`IAnalyzer` / `IRelogger`),
  quite different in shape from parsing a file, and the ETL has to be produced
  out of band by `vcperf /start` and `vcperf /stop`, which needs administrator
  rights. That is a real workflow wart worth deciding on deliberately.
- Mapping its events onto the existing `TimeTraceUnit` model is
  straightforward for frontend/backend and for template instantiations, and
  awkward for include cost, which vcperf reports as a nested file-parse tree
  rather than as flat `Source` events.
- Estimate: **three to five days**, plus the ETL capture workflow and testing
  on a machine with the SDK installed. Say a week to do properly.

**MSBuild binary logs.** The parser is a C# library, which is an awkward
dependency here; the Ninja generator is the better path.

**Linux and macOS.** Nothing in `BW_Core`, `BW_Build` or `BW_Treemap` is
Windows-specific, and the Qt layer is portable Qt Quick. What is Windows-only
is the toolchain assumption, the ninja discovery fallback, the developer
environment check, and the fact that nothing else has been tested. Porting is
plausible rather than done; treat it as unsupported until someone runs it.

## Contributing

Issues and pull requests are welcome. See
**[CONTRIBUTING.md](CONTRIBUTING.md)** for the build, the house style, and
what a change is expected to come with.

Security reports: **[SECURITY.md](SECURITY.md)**.

## Licence

MIT. See [LICENSE](LICENSE).

Third-party components and their terms, including the Qt licensing note, are
listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
