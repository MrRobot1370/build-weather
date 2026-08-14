project(BW_Core VERSION 0.4.1 LANGUAGES CXX)

set(HEADERS
    "include/BW/Core/logger.h"
    "include/BW/Core/path_utils.h"
)

set(SOURCES
    "src/BW/Core/logger.cpp"
    "src/BW/Core/path_utils.cpp"
)

add_library(${PROJECT_NAME})
add_library(BW::Core ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS include
        FILES ${HEADERS}
    PRIVATE
        ${SOURCES}
)

target_link_libraries(${PROJECT_NAME} PUBLIC bw::spdlog)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "libs")

# install(EXPORT) prepends the namespace to the target name, not the alias, so
# without this the package exports BW::BW_Core.
set_property(TARGET ${PROJECT_NAME} PROPERTY EXPORT_NAME Core)
