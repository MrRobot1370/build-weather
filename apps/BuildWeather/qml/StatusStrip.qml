import QtQuick
import QtQuick.Layouts
import BW.UICore
import BuildWeather

// Unobtrusive strip along the bottom: progress and elapsed time while a build
// runs, and the parallelism readout. Watching parallelism collapse at the end
// of a build is the diagnostic that pays for the whole live view.
Rectangle {
    id: root
    implicitHeight: 26
    color: Style.bgRail

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Style.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.space3
        anchors.rightMargin: Style.space3
        spacing: Style.space3

        TextBW {
            text: AppContext.runner.running
                  ? AppContext.runner.finishedEdges + " / "
                    + AppContext.runner.totalEdges
                  : AppContext.build.status
            variant: AppContext.runner.running ? TextBW.Mono : TextBW.Muted
            font.pixelSize: Style.fontSizeXS
            elide: Text.ElideRight
            Layout.maximumWidth: 420
        }

        ProgressBarBW {
            Layout.preferredWidth: 220
            visible: AppContext.runner.running
            value: AppContext.runner.progress
        }

        TextBW {
            visible: AppContext.runner.running
            text: AppContext.runner.currentStep
            variant: TextBW.Faint
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            Layout.maximumWidth: 520
        }

        Item { Layout.fillWidth: true }

        // Parallelism, as a tiny bar chart of slots so a collapse is visible
        // at a glance rather than as a number you have to read.
        RowLayout {
            spacing: 2
            visible: AppContext.runner.running || AppContext.replayActive

            TextBW {
                text: "jobs"
                variant: TextBW.Eyebrow
            }
            Repeater {
                model: 16
                delegate: Rectangle {
                    required property int index
                    readonly property int active:
                        AppContext.runner.running
                        ? AppContext.runner.activeJobs
                        : AppContext.replayParallelism
                    width: 4
                    height: 12
                    radius: 1
                    color: index < active ? Style.accent : Style.bgWell
                    Behavior on color { ColorAnimation { duration: 90 } }
                }
            }
            TextBW {
                text: (AppContext.runner.running
                       ? AppContext.runner.activeJobs
                       : AppContext.replayParallelism)
                      + (AppContext.runner.running
                         ? " (peak " + AppContext.runner.peakJobs + ")" : "")
                variant: TextBW.Mono
                font.pixelSize: Style.fontSizeXS
                color: Style.textSecondary
            }
        }

        TextBW {
            visible: AppContext.runner.running
            text: AppContext.formatDuration(AppContext.runner.elapsedMs)
            variant: TextBW.Mono
            font.pixelSize: Style.fontSizeXS
            color: Style.accent
        }

        TextBW {
            visible: !AppContext.runner.running
                     && (AppContext.traces.loading || AppContext.traces.hasData)
            text: AppContext.traces.status
            variant: TextBW.Faint
            font.pixelSize: Style.fontSizeXS
            elide: Text.ElideMiddle
            Layout.maximumWidth: 360
        }

        ProgressBarBW {
            Layout.preferredWidth: 120
            visible: AppContext.traces.loading
            value: AppContext.traces.progress
            fillColor: Style.info
        }
    }
}
