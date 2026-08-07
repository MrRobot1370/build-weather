import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

// Plain, information dense, no animation. This is the tab that pays rent:
// it answers "which header is adding four minutes to every rebuild".
Item {
    id: page

    /// Main.qml owns the file dialog; the pages only say what to write.
    signal exportRequested(string kind)
    signal tracesFolderRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.space3
        spacing: Style.space2

        // ---- toolbar -------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Style.space2

            Segmented {
                id: which
                model: ["Slow steps", "Headers", "Templates", "Units"]
                currentIndex: 0
            }

            Item { Layout.fillWidth: true }

            TextBW {
                visible: AppContext.traces.hasData
                text: "frontend " + AppContext.formatDuration(
                          AppContext.traces.frontendUs / 1000)
                      + "   backend " + AppContext.formatDuration(
                          AppContext.traces.backendUs / 1000)
                variant: TextBW.Mono
                font.pixelSize: Style.fontSizeXS
                color: Style.textSecondary
            }

            ButtonBW {
                text: AppContext.traces.loading ? "Cancel scan"
                                                : "Load -ftime-trace"
                onClicked: {
                    if (AppContext.traces.loading)
                        AppContext.traces.cancel()
                    else if (AppContext.build.hasData)
                        AppContext.loadTracesFromBuildDirectory()
                    else
                        AppContext.showMessage(
                            "Load a build directory first.", true)
                }
                ToolTipBW {
                    visible: traceHover.hovered
                    text: "Scans the build directory for the Chrome-tracing "
                          + "JSON that clang-cl writes next to each object "
                          + "file when built with -ftime-trace."
                }
                HoverHandler { id: traceHover }
            }

            ButtonBW {
                text: "..."
                implicitWidth: 30
                enabled: !AppContext.traces.loading
                onClicked: page.tracesFolderRequested()
                ToolTipBW {
                    visible: folderHover.hovered
                    text: "Scan a different directory for trace documents."
                }
                HoverHandler { id: folderHover }
            }

            ButtonBW {
                text: "Export JSON"
                enabled: AppContext.build.hasData
                onClicked: page.exportRequested("json")
            }
            ButtonBW {
                text: "Export CSV"
                enabled: AppContext.build.hasData
                onClicked: page.exportRequested(
                    ["targets", "headers", "templates", "headers"][
                        which.currentIndex])
            }
        }

        // ---- frontend / backend split ---------------------------------------
        Pane {
            Layout.fillWidth: true
            visible: AppContext.traces.hasData
            padding: Style.space2

            RowLayout {
                anchors.fill: parent
                spacing: Style.space3

                TextBW {
                    text: "FRONTEND / BACKEND"
                    variant: TextBW.Eyebrow
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    radius: Style.radiusS
                    color: Style.bgWell
                    clip: true

                    readonly property real total:
                        Math.max(1, AppContext.traces.frontendUs
                                    + AppContext.traces.backendUs)

                    Rectangle {
                        width: parent.width * (AppContext.traces.frontendUs
                                               / parent.total)
                        height: parent.height
                        color: Style.info
                        TextBW {
                            anchors.centerIn: parent
                            visible: parent.width > 90
                            text: "frontend " + Math.round(
                                      100 * AppContext.traces.frontendUs
                                      / parent.parent.total) + "%"
                            variant: TextBW.Eyebrow
                            color: Style.textInverse
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        width: parent.width * (AppContext.traces.backendUs
                                               / parent.total)
                        height: parent.height
                        color: Style.accent
                        TextBW {
                            anchors.centerIn: parent
                            visible: parent.width > 90
                            text: "backend " + Math.round(
                                      100 * AppContext.traces.backendUs
                                      / parent.parent.total) + "%"
                            variant: TextBW.Eyebrow
                            color: Style.accentText
                        }
                    }
                }
            }
        }

        // ---- the tables ------------------------------------------------------
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: which.currentIndex

            // 0: slowest build steps, straight from .ninja_log
            DataTable {
                model: AppContext.targetsModel
                columns: [
                    { title: "#",        role: "rank",     width: 48,  align: Qt.AlignRight, mono: true },
                    { title: "path",     role: "path",     width: 0,   align: Qt.AlignLeft,  mono: true },
                    { title: "duration", role: "duration", width: 96,  align: Qt.AlignRight, mono: true },
                    { title: "kind",     role: "kind",     width: 80,  align: Qt.AlignLeft,  mono: false },
                    { title: "bucket",   role: "bucket",   width: 90,  align: Qt.AlignLeft,  mono: false }
                ]
            }

            // 1: headers ranked by aggregate include cost across every TU
            RowLayout {
                spacing: Style.space2

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Style.space1

                    TextBW {
                        text: "A 200 ms header included 400 times costs more "
                              + "than one 8 second file. Sort by total, read "
                              + "the TU count."
                        variant: TextBW.Faint
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }
                    DataTable {
                        id: headerTable
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: AppContext.headersModel
                        columns: [
                            { title: "#",       role: "rank",    width: 48,  align: Qt.AlignRight, mono: true },
                            { title: "header",  role: "path",    width: 0,   align: Qt.AlignLeft,  mono: true },
                            { title: "total",   role: "total",   width: 92,  align: Qt.AlignRight, mono: true },
                            { title: "self",    role: "self",    width: 92,  align: Qt.AlignRight, mono: true },
                            { title: "TUs",     role: "tus",     width: 62,  align: Qt.AlignRight, mono: true },
                            { title: "avg/TU",  role: "average", width: 92,  align: Qt.AlignRight, mono: true }
                        ]
                        onRowActivated: function(row) {
                            AppContext.selectHeader(row)
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 320
                    Layout.fillHeight: true
                    spacing: Style.space1

                    TextBW {
                        text: AppContext.headerUsersModel.count > 0
                              ? "Translation units including it"
                              : "Select a header to see who includes it"
                        variant: TextBW.Eyebrow
                    }
                    DataTable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: AppContext.headerUsersModel
                        columns: [
                            { title: "#",      role: "rank",   width: 40, align: Qt.AlignRight, mono: true },
                            { title: "unit",   role: "source", width: 0,  align: Qt.AlignLeft,  mono: true },
                            { title: "cost",   role: "cost",   width: 88, align: Qt.AlignRight, mono: true }
                        ]
                    }
                }
            }

            // 2: template instantiations
            DataTable {
                model: AppContext.templatesModel
                columns: [
                    { title: "#",      role: "rank",  width: 48,  align: Qt.AlignRight, mono: true },
                    { title: "entity", role: "name",  width: 0,   align: Qt.AlignLeft,  mono: true },
                    { title: "total",  role: "total", width: 92,  align: Qt.AlignRight, mono: true },
                    { title: "count",  role: "count", width: 70,  align: Qt.AlignRight, mono: true },
                    { title: "TUs",    role: "tus",   width: 62,  align: Qt.AlignRight, mono: true },
                    { title: "kind",   role: "kind",  width: 78,  align: Qt.AlignLeft,  mono: false }
                ]
            }

            // 3: per translation unit breakdown
            DataTable {
                model: AppContext.unitsModel
                columns: [
                    { title: "#",        role: "rank",     width: 48, align: Qt.AlignRight, mono: true },
                    { title: "source",   role: "source",   width: 0,  align: Qt.AlignLeft,  mono: true },
                    { title: "total",    role: "total",    width: 92, align: Qt.AlignRight, mono: true },
                    { title: "frontend", role: "frontend", width: 92, align: Qt.AlignRight, mono: true },
                    { title: "backend",  role: "backend",  width: 92, align: Qt.AlignRight, mono: true },
                    { title: "headers",  role: "headers",  width: 80, align: Qt.AlignRight, mono: true }
                ]
            }
        }

        // ---- empty state for the trace-only tables ---------------------------
        TextBW {
            Layout.fillWidth: true
            visible: which.currentIndex > 0 && !AppContext.traces.hasData
                     && !AppContext.traces.loading
            text: AppContext.traces.status
            variant: TextBW.Faint
            wrapMode: Text.WordWrap
        }
    }
}
