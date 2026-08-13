# BW_UI/UICore - the shared QML design system.
#
# Style.qml MUST be marked as a singleton here. Without QT_QML_SINGLETON_TYPE
# the qmldir entry that qt_add_qml_module writes omits the `singleton`
# keyword, every `Style.foo` lookup silently evaluates to undefined, and the
# whole UI collapses to unstyled QtQuick.Controls.Basic defaults.
set_source_files_properties(Style.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(BW_UICORE_QML_FILES
    Style.qml
    TextBW.qml
    Pane.qml
    ButtonBW.qml
    Segmented.qml
    StatTile.qml
    ToolTipBW.qml
    ProgressBarBW.qml
    HeatLegend.qml
    DataTable.qml
)

qt_add_qml_module(BW_UICore
    URI         BW.UICore
    VERSION     1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/BW/UICore
    QML_FILES   ${BW_UICORE_QML_FILES}
)
add_library(BW::UICore ALIAS BW_UICore)

target_link_libraries(BW_UICore PRIVATE Qt6::Quick Qt6::QuickControls2)
set_property(TARGET BW_UICore PROPERTY FOLDER "libs")

# qt_add_qml_module puts qmldir at OUTPUT_DIRECTORY but the backing plugin
# DLL lands in the target's own runtime directory. The QML engine resolves
# the plugin next to qmldir, so copy it there after every build.
if(TARGET BW_UICoreplugin)
    add_custom_command(TARGET BW_UICoreplugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:BW_UICoreplugin>
                ${CMAKE_BINARY_DIR}/qml/BW/UICore/$<TARGET_FILE_NAME:BW_UICoreplugin>
        VERBATIM)
    set_property(TARGET BW_UICoreplugin PROPERTY FOLDER "libs")
endif()

# --- install ----------------------------------------------------------------
# windeployqt resolves Qt's own QML modules and knows nothing about this one.
# Without this qmldir beside the executable every `Style.foo` evaluates to
# undefined and the UI comes up unstyled.
set(BW_UICORE_QML_DEST "qml/BW/UICore")

install(TARGETS BW_UICore
    RUNTIME DESTINATION "."
    LIBRARY DESTINATION "."
    ARCHIVE DESTINATION "sdk/lib")

if(TARGET BW_UICoreplugin)
    install(TARGETS BW_UICoreplugin
        RUNTIME DESTINATION "${BW_UICORE_QML_DEST}"
        LIBRARY DESTINATION "${BW_UICORE_QML_DEST}")
endif()

install(FILES
        "${CMAKE_BINARY_DIR}/qml/BW/UICore/qmldir"
        "${CMAKE_BINARY_DIR}/qml/BW/UICore/BW_UICore.qmltypes"
    DESTINATION "${BW_UICORE_QML_DEST}")

install(FILES ${BW_UICORE_QML_FILES} DESTINATION "${BW_UICORE_QML_DEST}")
