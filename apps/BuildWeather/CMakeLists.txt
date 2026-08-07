qt_add_executable(BuildWeather
    src/main.cpp
    src/app_context.cpp
    src/build_model.cpp
    src/build_runner.cpp
    src/row_model.cpp
    src/trace_controller.cpp
    src/treemap_item.cpp
)

# The headers go through SOURCES (not just the .cpp files) so qmltyperegistrar
# sees the QML_ELEMENT / QML_SINGLETON macros and generates the registrations.
qt_add_qml_module(BuildWeather
    URI     BuildWeather
    VERSION 1.0
    SOURCES
        src/app_context.h
        src/build_model.h
        src/build_runner.h
        src/color_ramp.h
        src/format.h
        src/row_model.h
        src/trace_controller.h
        src/treemap_item.h
    QML_FILES
        qml/Main.qml
        qml/MapPage.qml
        qml/AnalysisPage.qml
        qml/ComparePage.qml
        qml/OutputPage.qml
        qml/InspectorPanel.qml
        qml/SourceRow.qml
        qml/ReplayBar.qml
        qml/StatusStrip.qml
)

target_include_directories(BuildWeather PRIVATE src)

target_link_libraries(BuildWeather PRIVATE
    BW::Core BW::Build BW::Treemap BW::UICore
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)

target_compile_features(BuildWeather PRIVATE cxx_std_20)

target_compile_definitions(BuildWeather PRIVATE
    BW_VERSION="${PROJECT_VERSION}"
    BW_NAME="Build Weather"
    BW_QML_IMPORT_PATH="${CMAKE_BINARY_DIR}/qml")

set_property(TARGET BuildWeather PROPERTY FOLDER "apps")

# Windows GUI subsystem only for Release: during development a console makes
# the spdlog output visible without opening the log file.
set_target_properties(BuildWeather PROPERTIES
    WIN32_EXECUTABLE $<AND:$<CONFIG:Release>,$<BOOL:${WIN32}>>)

bw_deploy_runtime(BuildWeather)
