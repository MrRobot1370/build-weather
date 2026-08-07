import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

// Scrub back through a build and watch it happen again.
//
// The event stream already exists: a .ninja_log carries both a start and an
// end for every target, so replay needs no extra capture. It is only honest
// for one build session, though, because timestamps from different ninja
// invocations do not share a clock; the bar says so when the scope is wrong.
Rectangle {
    id: root

    readonly property bool sessionScoped: AppContext.build.scope === 1
    readonly property bool usable: AppContext.replayAvailable
                                   && !AppContext.runner.running

    implicitHeight: 40
    color: Style.bgRail
    radius: Style.radiusM
    border.width: 1
    border.color: AppContext.replayActive ? Style.accentLine : Style.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Style.space2
        anchors.rightMargin: Style.space2
        spacing: Style.space2

        ButtonBW {
            text: AppContext.replayPlaying ? "❚❚" : "▶"
            implicitWidth: 34
            enabled: root.usable
            primary: AppContext.replayPlaying
            onClicked: AppContext.replayPlaying ? AppContext.replayPause()
                                                : AppContext.replayPlay()
            ToolTipBW {
                visible: playHover.hovered
                text: root.usable
                      ? "Replay this build session"
                      : "Load a build with timing data to replay it"
            }
            HoverHandler { id: playHover }
        }
        ButtonBW {
            text: "⟲"
            implicitWidth: 30
            enabled: root.usable
            onClicked: AppContext.replayRewind()
        }

        Slider {
            id: scrub
            Layout.fillWidth: true
            enabled: root.usable
            from: 0
            to: Math.max(1, AppContext.replayDurationMs)
            value: AppContext.replayTimeMs
            onMoved: AppContext.replayTimeMs = value

            background: Rectangle {
                x: scrub.leftPadding
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: scrub.availableWidth
                height: 4
                radius: 2
                color: Style.bgWell
                Rectangle {
                    width: scrub.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: Style.accent
                }
            }
            handle: Rectangle {
                x: scrub.leftPadding + scrub.visualPosition
                   * (scrub.availableWidth - width)
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: 12
                height: 12
                radius: 6
                color: scrub.pressed ? Style.accentHover : Style.accent
                border.color: Style.bg0
                border.width: 2
            }
        }

        TextBW {
            text: AppContext.formatDuration(AppContext.replayTimeMs) + " / "
                  + AppContext.formatDuration(AppContext.replayDurationMs)
            variant: TextBW.Mono
            font.pixelSize: Style.fontSizeXS
            color: Style.textSecondary
        }

        Segmented {
            model: ["1x", "4x", "16x"]
            currentIndex: AppContext.replaySpeed >= 16 ? 2
                          : (AppContext.replaySpeed >= 4 ? 1 : 0)
            enabled: root.usable
            onActivated: function(index) {
                AppContext.replaySpeed = [1, 4, 16][index]
            }
        }

        ButtonBW {
            text: "Exit replay"
            visible: AppContext.replayActive
            onClicked: AppContext.replayExit()
        }

        TextBW {
            visible: AppContext.replayActive && !root.sessionScoped
                     && AppContext.build.multiBuildLog
            text: "this log covers several builds, so the timings come from "
                  + "several clocks; switch to \"Last build\" for an accurate replay"
            variant: TextBW.Faint
            color: Style.warning
            elide: Text.ElideRight
            Layout.maximumWidth: 380
        }
    }
}
