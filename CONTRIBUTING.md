# Contributing

Thanks for taking an interest. Issues and pull requests are both welcome.

## Before a large change

Open an issue first. A treemap tool has a lot of plausible-sounding features
that would make it worse, and it is better to disagree about a paragraph than
about a thousand lines. Small fixes need no ceremony: send the pull request.

## Getting set up

```bash
git clone https://github.com/MrRobot1370/build-weather.git
cd build-weather
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release
ctest --preset msvc-x64-release
```

If Qt is not at `C:/Qt/6.6.2/msvc2019_64`, add
`-DCMAKE_PREFIX_PATH=<your Qt msvc dir>` to the configure line, or create a
`CMakeUserPresets.json` (gitignored) inheriting from `base`.

Please do not commit a change to the Qt path in `CMakePresets.json`; that
file's default is deliberately one person's path, and everyone else overrides
it locally.

To get data for the app to display, build the tree with the Ninja generator:

```bash
tools\build-ninja.cmd
```

That produces `build/ninja-x64/.ninja_log` and `compile_commands.json`, which
is a real build you can open in the app. `tools\make-time-traces.cmd` adds a
clang-cl build under `build/clangcl-x64` for the Analysis tab.

## What a change should come with

**Tests, when the change is testable.** The three libraries under `libs/` are
Qt-free precisely so that they can be tested without a `QGuiApplication`. A
parser fix or a layout fix belongs in `tests/`, with a fixture under
`tests/data/` if it needs one. Test the *invariant*, not the symptom: "no
rectangle escapes its parent's content area" survives a refactor, "this cell
is at x=412" does not.

**A screenshot, when the change is visual.** Neither the compiler nor `ctest`
catches a QML binding that silently evaluates to `undefined`, a scene-graph
node drawn in the wrong order, or a colour ramp that has saturated. If you
changed QML or the treemap renderer, run the app against a real `.ninja_log`
and attach what you see.

**A note in `CHANGELOG.md`** under `Unreleased`, if a user would notice.

## House style

**Formatting** is `.clang-format`, and `.editorconfig` covers the rest. Run
clang-format on what you touch; do not reformat files you are not changing.

**Line endings are CRLF** for `.h`, `.hpp`, `.cpp`, `.py`, `.qml`, `.json`,
`.md`, `.txt`, `.cmake` and `.ps1`. `.gitattributes` enforces this, so it
should happen without you thinking about it. `tests/data/*.ninja_log` is
marked binary on purpose: it is a byte-exact reproduction of ninja's output
and git must never rewrite it.

**Comments state a constraint, never a story.** A comment earns its place by
saying something the code cannot: a unit, a library quirk, an invariant, a
non-obvious choice between two valid options. It does not narrate how the code
came to be that way, and it does not restate the line below it.

```cpp
// Ninja only prints a status line when an edge *starts* if it believes
// stdout is a terminal, and a child process never gets one.
```

Headers carry `///` one-liners on the API surface and are legitimately denser
than sources; a `.cpp` full of plumbing correctly has almost no comments. QML
files usually have none.

**Naming and layout** mirror what is already there: headers under
`libs/<Module>/include/BW/<Module>/`, sources under
`libs/<Module>/src/BW/<Module>/`.

## Things to be careful about

**Never join paths on a raw string.** Ninja writes paths relative to the build
directory, `-ftime-trace` writes absolute ones, and Windows mixes separators
and casing throughout. Everything goes through `BW::Core::normalizePath` for
display and `BW::Core::pathKey` for joining. A bug here shows up as a file
mysteriously appearing twice on the map.

**Keep `BW_Core`, `BW_Build` and `BW_Treemap` free of Qt.** That property is
what makes them testable and what lets `bw_cli` exist. If a change seems to
need Qt in one of them, it probably belongs in the adapter layer under
`apps/BuildWeather/src/`.

**Be careful about what the numbers claim.** The "What the numbers mean"
section of the README is a promise. If a change makes one of those statements
untrue, the statement has to change in the same commit.

## Commits and pull requests

Write the subject in the imperative and under about 72 characters, with a
prefix where one fits: `map:`, `docs:`, `fix:`, `build:`, `tests:`. Put the
*why* in the body; the diff already covers the what.

Keep a pull request to one concern. Confirm before you open it:

- [ ] `cmake --build --preset msvc-x64-release` is clean, with no new warnings
- [ ] `ctest --preset msvc-x64-release` passes
- [ ] `bw_cli analyze build\ninja-x64` still prints sensible numbers
- [ ] docs updated if behaviour changed

## Reporting a bug

Please include the Build Weather version, your Qt and Visual Studio versions,
and what you pointed it at. If you can, attach the `.ninja_log` that triggers
it, or the smallest part of one that still does: almost every parser bug is
reproducible from the log alone.

Security issues go through [SECURITY.md](SECURITY.md) instead.
