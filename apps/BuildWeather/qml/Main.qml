import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import BW.UICore
import BuildWeather

ApplicationWindow {
    id: win
    width: 1500
    height: 940
    minimumWidth: 1080
    minimumHeight: 660
    visible: true
    color: Style.bg0
    title: (AppContext.build.hasData
            ? AppContext.build.buildDirectory + "  -  " : "")
           + "Build Weather  v" + AppContext.version

    palette.toolTipBase: Style.bg1
    palette.toolTipText: Style.textPrimary

    Settings {
        id: uiSettings
        category: "ui"
        property int themeMode: 2
        property string lastBuildDirectory: ""
        property bool stableOrder: true
        property bool showLabels: true
    }
    Component.onCompleted: {
        Style.themeMode = uiSettings.themeMode
        if (!AppContext.build.hasData && uiSettings.lastBuildDirectory !== "")
            AppContext.openBuildDirectory(
                AppContext.toUrl(uiSettings.lastBuildDirectory))
    }
    Connections {
        target: AppContext.build
        function onSourceChanged() {
            if (AppContext.build.buildDirectory !== "")
                uiSettings.lastBuildDirectory = AppContext.build.buildDirectory
        }
    }

    // ---- dialogs --------------------------------------------------------
    FolderDialog {
        id: buildDirDialog
        title: "Select a build directory (must contain .ninja_log)"
        onAccepted: AppContext.openBuildDirectory(selectedFolder)
    }
    FileDialog {
        id: ninjaLogDialog
        title: "Open a .ninja_log"
        nameFilters: ["ninja log (.ninja_log *.ninja_log)", "All files (*)"]
        onAccepted: AppContext.openNinjaLog(selectedFile)
    }
    FileDialog {
        id: baselineDialog
        title: "Open the baseline .ninja_log to compare against"
        nameFilters: ["ninja log (.ninja_log *.ninja_log)", "All files (*)"]
        onAccepted: AppContext.openBaseline(selectedFile)
    }
    FolderDialog {
        id: tracesDialog
        title: "Select the directory holding -ftime-trace documents"
        onAccepted: AppContext.loadTraces(selectedFolder)
    }
    FileDialog {
        id: exportDialog
        property string kind: "json"
        title: "Export analysis"
        fileMode: FileDialog.SaveFile
        defaultSuffix: kind === "json" ? "json" : "csv"
        nameFilters: kind === "json" ? ["JSON (*.json)"] : ["CSV (*.csv)"]
        onAccepted: {
            switch (kind) {
            case "json":      AppContext.exportAnalysisJson(selectedFile); break
            case "targets":   AppContext.exportTargetsCsv(selectedFile); break
            case "headers":   AppContext.exportHeadersCsv(selectedFile); break
            case "templates": AppContext.exportTemplatesCsv(selectedFile); break
            case "compare":   AppContext.exportComparisonCsv(selectedFile); break
            }
        }
    }
    function requestExport(kind) {
        exportDialog.kind = kind
        exportDialog.open()
    }

    Shortcut { sequence: StandardKey.Open; onActivated: buildDirDialog.open() }
    Shortcut { sequence: StandardKey.Refresh; onActivated: AppContext.reload() }
    Shortcut { sequence: "Ctrl+B"; onActivated: liveButton.trigger() }
    Shortcut { sequence: "Escape"; onActivated: AppContext.replayExit() }

    // ======================= layout =======================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top bar ----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Style.bgTitle

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Style.space3
                anchors.rightMargin: Style.space3
                spacing: Style.space2

                // Wordmark, with a live pip that beats while a build runs.
                Rectangle {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: AppContext.runner.running ? Style.accent
                                                     : Style.textFaint
                    SequentialAnimation on opacity {
                        running: AppContext.runner.running
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 500 }
                        NumberAnimation { to: 1.0;  duration: 500 }
                    }
                }
                TextBW {
                    text: "BUILD WEATHER"
                    variant: TextBW.Eyebrow
                    color: Style.textSecondary
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    color: Style.border
                }

                TextBW {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 520
                    text: AppContext.build.hasData
                          ? AppContext.build.buildDirectory
                          : "no build directory loaded"
                    variant: AppContext.build.hasData ? TextBW.Mono
                                                      : TextBW.Faint
                    font.pixelSize: Style.fontSizeS
                    elide: Text.ElideMiddle
                    ToolTipBW {
                        text: AppContext.build.hasData
                              ? AppContext.build.buildDirectory + "\nsource: "
                                + AppContext.build.sourceDirectory
                              : ""
                        visible: pathHover.hovered && AppContext.build.hasData
                    }
                    HoverHandler { id: pathHover }
                }

                Item { Layout.fillWidth: true }

                ButtonBW {
                    text: "Open build dir"
                    onClicked: buildDirDialog.open()
                }
                ButtonBW {
                    text: "Open .ninja_log"
                    onClicked: ninjaLogDialog.open()
                }
                ButtonBW {
                    text: "Reload"
                    enabled: AppContext.build.hasData
                    onClicked: AppContext.reload()
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 18
                    color: Style.border
                }

                ButtonBW {
                    id: liveButton
                    text: AppContext.runner.running ? "Stop build" : "Build"
                    primary: !AppContext.runner.running
                    danger: AppContext.runner.running
                    enabled: AppContext.build.hasData
                             && AppContext.runner.ninjaAvailable
                    function trigger() {
                        if (AppContext.runner.running)
                            AppContext.stopBuild()
                        else
                            AppContext.startBuild("")
                    }
                    onClicked: trigger()
                    ToolTipBW {
                        visible: liveHover.hovered
                        text: !AppContext.runner.ninjaAvailable
                              ? "ninja was not found on PATH"
                              : "Run ninja in the loaded build directory and "
                                + "watch the map (Ctrl+B)"
                    }
                    HoverHandler { id: liveHover }
                }

                Segmented {
                    model: ["System", "Light", "Dark"]
                    currentIndex: Style.themeMode
                    onActivated: function(index) {
                        Style.themeMode = index
                        uiSettings.themeMode = index
                    }
                }
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Style.border
            }
        }

        // ---- tabs --------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Style.bgRail

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Style.space3
                anchors.rightMargin: Style.space3
                spacing: Style.space2

                Repeater {
                    model: [
                        { title: "Map",      hint: "The build as a treemap of the source tree" },
                        { title: "Analysis", hint: "Slowest units, header cost, template cost" },
                        { title: "Compare",  hint: "Per-file deltas against a baseline build" },
                        { title: "Output",   hint: "Raw ninja output from the last live build" }
                    ]
                    delegate: Item {
                        required property int index
                        required property var modelData
                        Layout.preferredWidth: tabLabel.implicitWidth + 2 * Style.space2
                        Layout.fillHeight: true

                        TextBW {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: modelData.title
                            variant: TextBW.Body
                            font.pixelSize: Style.fontSizeS
                            font.weight: index === pages.currentIndex
                                         ? Font.DemiBold : Font.Normal
                            color: index === pages.currentIndex
                                   ? Style.textPrimary : Style.textSecondary
                        }
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: 2
                            color: Style.accent
                            visible: index === pages.currentIndex
                        }
                        HoverHandler { id: tabHover }
                        TapHandler { onTapped: pages.currentIndex = index }
                        ToolTipBW { text: modelData.hint; visible: tabHover.hovered }
                    }
                }

                Item { Layout.fillWidth: true }

                TextBW {
                    visible: AppContext.build.multiBuildLog
                    text: "log covers several builds"
                    variant: TextBW.Faint
                    color: Style.warning
                }
                Segmented {
                    visible: AppContext.build.hasData
                    model: ["All builds", "Last build"]
                    currentIndex: AppContext.build.scope
                    onActivated: function(index) { AppContext.build.scope = index }
                    ToolTipBW {
                        visible: scopeHover.hovered
                        text: "A .ninja_log accumulates across runs. \"All builds\" "
                              + "takes each target's most recent entry, which is what a "
                              + "full build costs. \"Last build\" keeps only the most "
                              + "recent ninja invocation, the only entries whose "
                              + "timestamps share a clock, so replay is accurate."
                    }
                    HoverHandler { id: scopeHover }
                }
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Style.border
            }
        }

        // ---- pages -------------------------------------------------------
        StackLayout {
            id: pages
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            MapPage { id: mapPage }
            AnalysisPage {
                id: analysisPage
                onExportRequested: function(kind) { win.requestExport(kind) }
                onTracesFolderRequested: tracesDialog.open()
            }
            ComparePage {
                id: comparePage
                onExportRequested: function(kind) { win.requestExport(kind) }
                onBaselineRequested: baselineDialog.open()
            }
            OutputPage { id: outputPage }
        }

        // ---- status strip -------------------------------------------------
        StatusStrip { Layout.fillWidth: true }
    }

    // Transient message, bottom right, never modal.
    Rectangle {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Style.space4
        width: Math.min(520, toastText.implicitWidth + 2 * Style.space3)
        height: toastText.implicitHeight + 2 * Style.space2
        radius: Style.radiusM
        color: Style.bg1
        border.width: 1
        border.color: AppContext.messageIsError ? Style.danger : Style.border
        opacity: 0
        visible: opacity > 0.01

        TextBW {
            id: toastText
            anchors.fill: parent
            anchors.margins: Style.space2
            text: AppContext.message
            wrapMode: Text.WordWrap
            color: AppContext.messageIsError ? Style.danger : Style.textBody
            font.pixelSize: Style.fontSizeS
        }

        SequentialAnimation {
            id: toastAnimation
            NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 140 }
            PauseAnimation { duration: 4200 }
            NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 400 }
        }
        Connections {
            target: AppContext
            function onMessageChanged() {
                if (AppContext.message !== "") toastAnimation.restart()
            }
        }
    }
}
