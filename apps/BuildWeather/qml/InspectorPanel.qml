import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

Pane {
    id: root

    property string hoveredPath: ""
    property string pinnedPath: ""
    property int selectedIndex: -1
    property real maxMs: 0

    padding: Style.space3

    ColumnLayout {
        anchors.fill: parent
        spacing: Style.space3

        TextBW { text: "Build"; variant: TextBW.Eyebrow }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Style.space3
            rowSpacing: Style.space2

            StatTile {
                Layout.fillWidth: true
                label: "steps"
                value: AppContext.build.targetCount.toLocaleString(
                           Qt.locale(), "f", 0)
            }
            StatTile {
                Layout.fillWidth: true
                label: "total cpu"
                value: AppContext.formatDuration(AppContext.build.totalCpuMs)
                hint: "Sum of every step's duration. This is the work the "
                      + "machine did, not how long you waited."
            }
            StatTile {
                Layout.fillWidth: true
                label: "wall clock"
                value: AppContext.formatDuration(AppContext.build.wallMs)
                hint: "First start to last finish. Only meaningful for a "
                      + "single build session."
            }
            StatTile {
                Layout.fillWidth: true
                label: "peak jobs"
                value: AppContext.build.peakParallelism
                hint: "Highest number of steps in flight at once."
            }
            StatTile {
                Layout.fillWidth: true
                label: "slowest step"
                value: AppContext.formatDuration(AppContext.build.maxMs)
            }
            StatTile {
                Layout.fillWidth: true
                label: "median step"
                value: AppContext.formatDuration(AppContext.build.medianMs)
                hint: "Half the steps are faster than this. The gap between "
                      + "median and slowest is the shape of the problem."
            }
        }

        HeatLegend {
            Layout.fillWidth: true
            maxMs: root.maxMs
            medianMs: AppContext.build.medianMs
            caption: "compile time"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Style.borderSubtle
        }

        // the cell under the cursor, or the last one clicked
        TextBW { text: "Selection"; variant: TextBW.Eyebrow }

        TextBW {
            Layout.fillWidth: true
            text: root.hoveredPath !== "" ? root.hoveredPath
                  : (root.pinnedPath !== "" ? root.pinnedPath
                                            : "hover the map")
            variant: root.hoveredPath === "" && root.pinnedPath === ""
                     ? TextBW.Faint : TextBW.Mono
            font.pixelSize: Style.fontSizeS
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 4
            elide: Text.ElideMiddle
        }

        // data source provenance
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Style.borderSubtle
        }

        TextBW { text: "Data sources"; variant: TextBW.Eyebrow }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            SourceRow {
                Layout.fillWidth: true
                label: ".ninja_log"
                ok: AppContext.build.hasData
                detail: AppContext.build.hasData
                        ? AppContext.build.targetCount + " steps"
                        : "not loaded"
            }
            SourceRow {
                Layout.fillWidth: true
                label: "compile_commands.json"
                ok: AppContext.build.usingCompileDatabase
                detail: AppContext.build.usingCompileDatabase
                        ? "exact object to source join"
                        : "absent, using the CMake path convention"
            }
            SourceRow {
                Layout.fillWidth: true
                label: "-ftime-trace"
                ok: AppContext.traces.hasData
                detail: AppContext.traces.hasData
                        ? AppContext.traces.unitCount + " translation units"
                        : "not loaded"
            }
            SourceRow {
                Layout.fillWidth: true
                label: "ninja"
                ok: AppContext.runner.ninjaAvailable
                detail: AppContext.runner.ninjaAvailable
                        ? "live builds available"
                        : "not on PATH, live mode disabled"
            }
        }

        // parser diagnostics
        ColumnLayout {
            Layout.fillWidth: true
            visible: AppContext.build.diagnostics.length > 0
            spacing: 3

            TextBW { text: "Notes"; variant: TextBW.Eyebrow }
            Repeater {
                model: AppContext.build.diagnostics
                delegate: TextBW {
                    required property var modelData
                    Layout.fillWidth: true
                    text: "- " + modelData
                    variant: TextBW.Faint
                    wrapMode: Text.WordWrap
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
