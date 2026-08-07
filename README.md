# Build Weather

Live and post-mortem visualization of a C++ build, as a treemap of the source
tree rather than a scrolling wall of text.

- **Map** — the build drawn as a squarified treemap. Post-mortem it is a heat
  map of compile cost; during a build files light up as they complete and
  settle to their final colour, so a parallel build reads as weather moving
  across the map.
- **Analysis** — plain and dense. Slowest translation units, the headers that
  cost the most summed across every TU (with the number of TUs including
  each one), the most expensive template instantiations, and the frontend
  versus backend split.
- **Compare** — load a baseline `.ninja_log` and get per-file deltas, sorted
  worst regression first.
- **Replay** — scrub a build's timeline and watch it happen again.

## Documentation

`docs/USER_GUIDE.md` is the user guide, with annotated screenshots of each
view. `docs/USER_GUIDE.pdf` is the printable form, regenerated with:

```bash
python tools/build-docs.py
```

## Requirements

- Windows, Visual Studio 2022 (MSVC or the bundled clang-cl)
- Qt 6.5 or newer (developed against 6.6.2, `C:/Qt/6.6.2/msvc2019_64`)
- CMake 3.27 or newer
- ninja, for live builds. Found on `PATH`, or the copy Visual Studio ships
  with its CMake integration.

## Building

```bash
cmake --preset msvc-x64 && cmake --build --preset msvc-x64-release
```

```bash
ctest --preset msvc-x64-release
```

The output lands in `build/msvc-x64/bin/Release/`. Adjust `CMAKE_PREFIX_PATH`
in `CMakePresets.json` if Qt is somewhere else.

There is a second preset, `ninja-x64`, that builds the same tree with the
Ninja generator. `tools\build-ninja.cmd` runs it inside a developer
environment. It is worth having: `build/ninja-x64` then contains a real
`.ninja_log` and `compile_commands.json`, so the app has something to look at.

## Running

```bash
build\msvc-x64\bin\Release\BuildWeather.exe build\ninja-x64
```

Or start it with no arguments and use **Open build dir**. Options:

| Option | Meaning |
| --- | --- |
| `<build-dir>` | Build directory to open on start; must contain `.ninja_log` |
| `--source <dir>` | Source root, when it cannot be inferred |
| `--traces <dir>` | Directory to scan for `-ftime-trace` documents |
| `--baseline <file>` | Baseline `.ninja_log` to compare the loaded build against |

**Live builds need a developer environment.** `cl.exe` finds neither the CRT
headers nor the import libraries unless `INCLUDE` and `LIB` are set, and a GUI
app started from Explorer does not have them. Launch Build Weather from a
Developer Command Prompt, or run `vcvars64.bat` first. The app checks for this
and says so in the Output tab rather than letting the map fill with failures.

### Producing `-ftime-trace` data

MSVC has no equivalent of `-ftime-trace`, so the header and template rankings
need a clang-cl build:

```bash
tools\make-time-traces.cmd
```

That configures `build/clangcl-x64` with `/clang:-ftime-trace` and builds it.
clang writes a trace JSON next to each object file. Point **Load -ftime-trace**
at that directory. The timings come from clang, so they describe a clang
build, not the MSVC one; the ranking of headers is what transfers.

## Headless

`bw_cli` runs the same libraries without a window. It is the post-build sanity
check and the thing to put in CI.

```bash
build\msvc-x64\bin\Release\bw_cli.exe analyze build\ninja-x64 --traces build\clangcl-x64 --top 20
```

```bash
build\msvc-x64\bin\Release\bw_cli.exe compare old.ninja_log build\ninja-x64\.ninja_log --csv deltas.csv
```

## Layout

```
libs/BW_Core      path normalization, logging
libs/BW_Build     .ninja_log, ninja progress, compile_commands.json,
                  -ftime-trace, the snapshot model, report export
libs/BW_Treemap   squarified layout
libs/BW_UI        the QML design system (BW.UICore)
apps/BuildWeather the application
tools/bw_cli      headless driver
tests/            unit tests over the Qt-free libraries, with fixtures
```

`BW_Core`, `BW_Build` and `BW_Treemap` contain no Qt. That is deliberate: the
parsers and the layout are the parts worth testing, and they are tested
without a `QGuiApplication`. The Qt layer above them is a thin adapter.

## What the numbers mean

Being precise about this matters more than the animation does.

**Durations are exact.** They come from `.ninja_log`, which records the start
and end of every edge in milliseconds. Nothing is sampled or estimated.

**A `.ninja_log` accumulates across runs.** The default scope, *All builds*,
takes each target's most recent entry, which answers "what does a full build
of this tree cost". *Last build* keeps only the most recent ninja invocation.
The header says "log covers several builds" when that distinction applies.

**Splitting a log into invocations is only approximately possible.** Ninja
does not mark invocation boundaries, `end_ms` is not monotonic within a run,
and `mtime` records the source timestamp for copy edges. What ninja does
guarantee is that an output is built at most once per invocation, so the last
invocation is the trailing run of entries with no repeated output. That is
exact unless two consecutive builds touched disjoint file sets.

**Several steps can share a file.** A generated source is written by one edge
and compiled by another; both land on the same leaf and their durations are
summed, because that is what the file costs. The same edge logged under two
output names (ninja does this for outputs that have both a relative and an
absolute name) is counted once.

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

## Not implemented

**MSVC vcperf / C++ Build Insights.** This would give MSVC builds the same
drill-down clang-cl gives through `-ftime-trace`. It is a real chunk of work,
so here is the estimate rather than a surprise:

- The C++ Build Insights SDK is a separate download, is Windows-only and
  ships as a static library with its own headers, so it becomes a build
  dependency of `BW_Build` and breaks the "no Qt, no platform" property that
  currently makes that library testable in isolation. It would need to live
  behind an interface in its own library, say `BW_Vcperf`.
- The analysis model is event-callback based (`IAnalyzer` / `IRelogger`),
  quite different in shape from parsing a file, and the ETL has to be
  produced out of band by `vcperf /start` and `vcperf /stop`, which needs
  administrator rights. That is a real workflow wart worth deciding on
  deliberately.
- Mapping its events onto the existing `TimeTraceUnit` model is
  straightforward for frontend/backend and for template instantiations, and
  awkward for include cost, which vcperf reports as a nested file-parse tree
  rather than as flat `Source` events.
- Estimate: **three to five days**, plus the ETL capture workflow and testing
  on a machine with the SDK installed. Say a week to do properly.

Ask and it gets built. The prompt treats 1a plus 1b as sufficient for v1, and
that is what is here.

**MSBuild binary logs** are not supported either. The parser is a C# library,
which is an awkward dependency in this codebase; the Ninja generator is the
better path.
