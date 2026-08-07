project(BW_Build VERSION 0.4.0 LANGUAGES CXX)

# Deliberately free of Qt: everything here is a pure parser or model over
# plain C++ types, so the unit tests can exercise it without a QApplication
# and the UI layer stays a thin adapter.

set(HEADERS
    "include/BW/Build/build_snapshot.h"
    "include/BW/Build/build_types.h"
    "include/BW/Build/compile_commands.h"
    "include/BW/Build/ninja_log.h"
    "include/BW/Build/ninja_progress.h"
    "include/BW/Build/report.h"
    "include/BW/Build/time_trace.h"
)

set(PRIVATE_HEADERS
    "src/BW/Build/json_reader.h"
)

set(SOURCES
    "src/BW/Build/build_snapshot.cpp"
    "src/BW/Build/compile_commands.cpp"
    "src/BW/Build/ninja_log.cpp"
    "src/BW/Build/ninja_progress.cpp"
    "src/BW/Build/report.cpp"
    "src/BW/Build/time_trace.cpp"
)

add_library(${PROJECT_NAME})
add_library(BW::Build ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS include
        FILES ${HEADERS}
    PRIVATE
        ${PRIVATE_HEADERS}
        ${SOURCES}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/BW/Build)

target_link_libraries(${PROJECT_NAME} PUBLIC BW::Core)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "libs")

# The private headers are reachable for tests without exporting them.
add_library(${PROJECT_NAME}_Private INTERFACE)
add_library(BW::Build_Private ALIAS ${PROJECT_NAME}_Private)
target_include_directories(${PROJECT_NAME}_Private
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/BW/Build)
target_link_libraries(${PROJECT_NAME}_Private INTERFACE ${PROJECT_NAME})
