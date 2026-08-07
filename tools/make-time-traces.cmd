@echo off
rem ---------------------------------------------------------------------------
rem  Build the project with clang-cl and -ftime-trace.
rem
rem  clang-cl writes a Chrome-tracing JSON next to every object file, which is
rem  what the Analysis tab reads for the header and template rankings. MSVC has
rem  no equivalent, so this second build directory exists purely to produce
rem  that data; point Build Weather's "Load -ftime-trace" at
rem  build\clangcl-x64 afterwards.
rem
rem  Usage: tools\make-time-traces.cmd [Release|Debug]  (default Release)
rem ---------------------------------------------------------------------------
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\2022"
for %%E in (Enterprise Professional Community BuildTools) do (
    if exist "%VS_ROOT%\%%E\VC\Tools\Llvm\x64\bin\clang-cl.exe" (
        set "CLANG_DIR=%VS_ROOT%\%%E\VC\Tools\Llvm\x64\bin"
    )
)
if not defined CLANG_DIR (
    echo Could not find clang-cl.exe under "%VS_ROOT%".
    echo Install the "C++ Clang tools for Windows" component.
    exit /b 1
)

pushd "%~dp0.."

rem -ftime-trace has to reach the clang driver, hence the /clang: prefix.
rem CMAKE_CXX_FLAGS *replaces* the default flags rather than adding to them,
rem so /EHsc has to be repeated here or every try block fails to compile.
cmake -S . -B build\clangcl-x64 -G "Visual Studio 17 2022" -A x64 -T ClangCL ^
    -DCMAKE_PREFIX_PATH=C:/Qt/6.6.2/msvc2019_64 ^
    -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHsc /clang:-ftime-trace" ^
    -DBW_BUILD_TESTS=OFF || (popd & exit /b 1)

cmake --build build\clangcl-x64 --config %CONFIG% || (popd & exit /b 1)

echo.
echo Time traces written under build\clangcl-x64. Load that directory from the
echo Analysis tab.
popd

endlocal
