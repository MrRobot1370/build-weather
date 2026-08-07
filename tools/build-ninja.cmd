@echo off
rem ---------------------------------------------------------------------------
rem  Configure and build Build Weather with the Ninja generator.
rem
rem  The MSVC preset is the primary one; this exists so the project can be
rem  built the way the app expects to *read* a build: Ninja writes the
rem  .ninja_log and compile_commands.json that Build Weather visualizes, so
rem  build/ninja-x64 doubles as the dogfood data set.
rem
rem  Usage: tools\build-ninja.cmd [Release|Debug]  (default Release)
rem ---------------------------------------------------------------------------
setlocal

rem Build preset names are lower case (ninja-x64-release), so normalise
rem whatever the caller typed.
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"
if /I "%CONFIG%"=="release" set "CONFIG=release"
if /I "%CONFIG%"=="debug"   set "CONFIG=debug"

set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022"
for %%E in (Enterprise Professional Community BuildTools) do (
    if exist "%VS_ROOT%\%%E\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=%VS_ROOT%\%%E\VC\Auxiliary\Build\vcvars64.bat"
        set "VS_NINJA=%VS_ROOT%\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    )
)

if not defined VCVARS (
    echo Could not find vcvars64.bat under "%VS_ROOT%".
    exit /b 1
)

call "%VCVARS%" >nul || exit /b 1

rem Ninja is not on PATH after vcvars; Visual Studio keeps its copy with the
rem CMake integration. Prefer a system ninja when there is one.
where ninja >nul 2>&1 || set "PATH=%VS_NINJA%;%PATH%"

pushd "%~dp0.."
cmake --preset ninja-x64 || (popd & exit /b 1)
cmake --build --preset ninja-x64-%CONFIG% || (popd & exit /b 1)
popd

endlocal
