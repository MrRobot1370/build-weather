cmake_minimum_required(VERSION 3.27)

# ----------------------------------------------------------------------------
#  Build Weather
#  Live and post-mortem visualization of a C++ build as a treemap of the
#  source tree.
#
#  Layout mirrors the IOS-Various monorepo:
#    libs/       reusable libraries, headers under include/BW/<Module>/
#    apps/       the desktop application
#    tools/      headless command line drivers
#    tests/      unit tests over the Qt-free parsers, with checked-in fixtures
#    cmake/      shared CMake helpers
#    3rdparty/   header-only drops surfaced as INTERFACE targets
# ----------------------------------------------------------------------------

if(WIN32)
    # RelWithDebInfo must prefer the Release flavour of imported packages,
    # otherwise we can end up linking a binary-incompatible Debug library.
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     "MinSizeRel;Release;RelWithDebInfo;")
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;MinSizeRel;")
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE        "Release;MinSizeRel;RelWithDebInfo;")
endif()

# clangd wants compile_commands.json; Build Weather itself also reads it to
# join ninja object paths back to source files, so keep it on by default.
if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)
endif()

if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        CACHE STRING "Directory for built runtime binaries (EXE, DLL)")
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()

if(MSVC)
    add_compile_options(/utf-8 /MP /W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
endif()

set(QT_SILENCE_MISSING_DEPENDENCY_TARGET_WARNING TRUE)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

project(BuildWeather
    VERSION 0.4.0
    DESCRIPTION "Live and post-mortem visualization of a C++ build"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(BWDeployRuntime)

option(BUILD_SHARED_LIBS "Build internal libraries as shared" OFF)
option(BW_BUILD_TESTS    "Build the unit tests"               ON)
option(BW_BUILD_TOOLS    "Build the headless bw_cli driver"   ON)

# --- Qt ---------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Concurrent)

if(BW_BUILD_TESTS)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

qt_standard_project_setup(REQUIRES 6.5)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# --- Third party ------------------------------------------------------------
set(BW_3RDPARTY_DIR "${CMAKE_SOURCE_DIR}/3rdparty" CACHE PATH "Header-only drops")
add_subdirectory(3rdparty)

# --- Internal libraries -----------------------------------------------------
add_subdirectory(libs/BW_Core)
add_subdirectory(libs/BW_Build)
add_subdirectory(libs/BW_Treemap)
add_subdirectory(libs/BW_UI/BW/UICore)

# --- Application ------------------------------------------------------------
add_subdirectory(apps/BuildWeather)

# --- Tools ------------------------------------------------------------------
if(BW_BUILD_TOOLS)
    add_subdirectory(tools/bw_cli)
endif()

# --- Tests ------------------------------------------------------------------
if(BW_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# --- Packaging --------------------------------------------------------------

# vcruntime140.dll and msvcp140.dll, so the package does not require the
# Visual C++ redistributable. The UCRT ships with Windows itself.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP FALSE)
include(InstallRequiredSystemLibraries)

# The Qt-free libraries, under sdk/. Not needed to run the application.
install(TARGETS BW_Core BW_Build BW_Treemap
    EXPORT      BuildWeatherTargets
    RUNTIME     DESTINATION "."
    LIBRARY     DESTINATION "."
    ARCHIVE     DESTINATION "sdk/lib"
    FILE_SET HEADERS DESTINATION "sdk/include")
install(EXPORT BuildWeatherTargets
    FILE        BuildWeatherTargets.cmake
    NAMESPACE   BW::
    DESTINATION "sdk/lib/cmake/BuildWeather")
# BW/Core/logger.h includes spdlog, so the drop travels with the headers.
install(DIRECTORY "${BW_3RDPARTY_DIR}/spdlog/include/spdlog"
    DESTINATION "sdk/include")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BuildWeatherConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/BuildWeatherConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BuildWeatherConfig.cmake"
    INSTALL_DESTINATION "sdk/lib/cmake/BuildWeather")
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/BuildWeatherConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/BuildWeatherConfigVersion.cmake"
    DESTINATION "sdk/lib/cmake/BuildWeather")

install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        "${CMAKE_SOURCE_DIR}/CHANGELOG.md"
    DESTINATION ".")
# The repository README opens with how to clone and build, which is the wrong
# first page for someone who downloaded a binary.
install(FILES "${CMAKE_SOURCE_DIR}/cmake/PACKAGE-README.md"
    DESTINATION "." RENAME "README.md")
install(FILES
        "${CMAKE_SOURCE_DIR}/docs/USER_GUIDE.md"
        "${CMAKE_SOURCE_DIR}/docs/USER_GUIDE.pdf"
    DESTINATION "docs")

set(CPACK_PACKAGE_NAME                "BuildWeather")
set(CPACK_PACKAGE_VENDOR              "Masoud Ghasemi Dehkordi")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "BuildWeather-${PROJECT_VERSION}-windows-x64")
# Unpacking must give a named folder, never loose files in Downloads.
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_GENERATOR                   "ZIP")
set(CPACK_SOURCE_GENERATOR            "")
include(CPack)

message(STATUS "")
message(STATUS "Build Weather ${PROJECT_VERSION}")
message(STATUS "  Qt:      ${Qt6_VERSION}")
message(STATUS "  Tests:   ${BW_BUILD_TESTS}")
message(STATUS "  Tools:   ${BW_BUILD_TOOLS}")
message(STATUS "  Output:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
