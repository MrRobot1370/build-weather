import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

Item {
    id: page

    // Main.qml owns the file dialog; the pages only say what to write.
    signal exportRequested(string kind)
    signal baselineRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.space3
        spacing: Style.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.space2

            ButtonBW {
                text: "Load baseline .ninja_log"
                primary: !AppContext.build.comparing
                enabled: AppContext.build.hasData
                onClicked: page.baselineRequested()
            }
            ButtonBW {
                text: "Clear"
                visible: AppContext.build.comparing
                onClicked: AppContext.clearBaseline()
            }
            TextBW {
                Layout.fillWidth: true
                text: AppContext.build.comparing
                      ? AppContext.build.baselineLabel
                      : "no baseline loaded"
                variant: AppContext.build.comparing ? TextBW.Mono
                                                    : TextBW.Faint
                font.pixelSize: Style.fontSizeS
                elide: Text.ElideMiddle
            }
            ButtonBW {
                text: "Export CSV"
                enabled: AppContext.build.comparing
                onClicked: page.exportRequested("compare")
            }
        }

        // headline numbers
        Pane {
            Layout.fillWidth: true
            visible: AppContext.build.comparing
            padding: Style.space3

            RowLayout {
                anchors.fill: parent
                spacing: Style.space5

                StatTile {
                    label: "baseline total"
                    value: AppContext.formatDuration(
                               AppContext.build.baselineTotalMs)
                }
                StatTile {
                    label: "current total"
                    value: AppContext.formatDuration(
                               AppContext.build.totalCpuMs)
                }
                StatTile {
                    label: "delta"
                    value: AppContext.formatDelta(
                               AppContext.build.totalCpuMs
                               - AppContext.build.baselineTotalMs)
                    valueColor: AppContext.build.totalCpuMs
                                > AppContext.build.baselineTotalMs
                                ? Style.worse : Style.better
                }
                StatTile {
                    label: "changed steps"
                    value: AppContext.deltaModel.count
                }

                Item { Layout.fillWidth: true }

                TextBW {
                    Layout.maximumWidth: 380
                    text: "Switch the map to Delta colouring to see where the "
                          + "regression lives in the tree."
                    variant: TextBW.Faint
                    wrapMode: Text.WordWrap
                }
            }
        }

        DataTable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: AppContext.deltaModel
            columns: [
                { title: "path",     role: "path",     width: 0,   align: Qt.AlignLeft,  mono: true },
                { title: "baseline", role: "baseline", width: 100, align: Qt.AlignRight, mono: true },
                { title: "current",  role: "current",  width: 100, align: Qt.AlignRight, mono: true },
                { title: "delta",    role: "delta",    width: 108, align: Qt.AlignRight, mono: true },
                { title: "state",    role: "state",    width: 88,  align: Qt.AlignLeft,  mono: false }
            ]
        }

        TextBW {
            Layout.fillWidth: true
            visible: !AppContext.build.comparing
            text: "Pick a second .ninja_log, usually one you copied aside "
                  + "before the change you want to measure. Rows are sorted "
                  + "worst regression first."
            variant: TextBW.Faint
            wrapMode: Text.WordWrap
        }
    }
}
