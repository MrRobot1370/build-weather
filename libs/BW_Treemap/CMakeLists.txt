project(BW_Treemap VERSION 0.4.0 LANGUAGES CXX)

# Pure geometry: no Qt, no rendering. The QQuickItem in apps/ turns the
# LayoutItem list into scene graph nodes.

set(HEADERS
    "include/BW/Treemap/squarify.h"
    "include/BW/Treemap/tree.h"
)

set(SOURCES
    "src/BW/Treemap/squarify.cpp"
    "src/BW/Treemap/tree.cpp"
)

add_library(${PROJECT_NAME})
add_library(BW::Treemap ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS
        BASE_DIRS include
        FILES ${HEADERS}
    PRIVATE
        ${SOURCES}
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "libs")
