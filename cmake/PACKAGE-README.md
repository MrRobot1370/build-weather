# Build Weather

**See where your C++ build time actually goes.** Every file in your source
tree is a rectangle; its area and colour are what it costs to compile.

This is a self-contained build. There is nothing to install, no Qt to set up
and no redistributable to chase. Unzip it anywhere and run `BuildWeather.exe`.

## Start here

1. Configure the project you want to measure with **CMake and the Ninja
   generator**, and build it once:

   ```
   cmake -S . -B build/ninja -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build/ninja
   ```

2. Run `BuildWeather.exe` and press **Open build dir**, or pass the directory
   on the command line:

   ```
   BuildWeather.exe C:\path\to\your\project\build\ninja
   ```

That is the whole setup. Build Weather reads `.ninja_log`, which ninja writes
on its own, so there is nothing to instrument.

`docs\USER_GUIDE.pdf` walks through every view with annotated screenshots.

## What is in here

| | |
| --- | --- |
| `BuildWeather.exe` | The application |
| `bw_cli.exe` | The same analysis without a window, for CI and scripts. Run `bw_cli --help`. |
| `docs\` | User guide, in Markdown and PDF |
| `sdk\` | Headers and libraries for the Qt-free parsing and layout libraries. Not needed to run anything. |
| everything else | Qt runtime, QML modules and the C runtime |

## Requirements

Windows 10 or 11, x64. The project you point it at must be built with the
Ninja generator; any language ninja compiles will work.

Two optional extras, each unlocking more of the analysis:

- `compile_commands.json` (from `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) makes
  the map show your source files rather than paths under `CMakeFiles/`.
- clang-cl `-ftime-trace` documents unlock the per-header and per-template
  rankings. MSVC produces no equivalent; see the user guide.

**Running builds from inside the app** additionally needs `ninja.exe` on
`PATH`, and a developer environment: `cl.exe` finds neither the CRT headers
nor the import libraries unless `INCLUDE` and `LIB` are set. Start Build
Weather from a Developer Command Prompt for that. Everything else works
without it.

## Licence

MIT; see `LICENSE`. Third-party components and their terms, including the Qt
licensing note, are in `THIRD-PARTY-NOTICES.md`.

Source, issues and newer releases:
https://github.com/MrRobot1370/build-weather
