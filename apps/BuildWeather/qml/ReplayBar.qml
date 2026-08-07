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

        // Playback rate. 64x exists because a four minute build at 16x is
        // still sixteen seconds of watching, and the speed only has any
        // visible effect while playing, which the tooltip says out loud
        // because it is not obvious from the control.
        Segmented {
            id: speed
            readonly property var factors: [1, 4, 16, 64]
            model: ["1x", "4x", "16x", "64x"]
            currentIndex: {
                var best = 0
                for (var i = 0; i < factors.length; ++i)
                    if (AppContext.replaySpeed >= factors[i]) best = i
                return best
            }
            enabled: root.usable
            onActivated: function(index) {
                AppContext.replaySpeed = factors[index]
                // Changing the rate mid-thought almost always means "and go",
                // and doing nothing here is what makes the control feel dead.
                if (!AppContext.replayPlaying)
                    AppContext.replayPlay()
            }
            ToolTipBW {
                visible: speedHover.hovered
                text: "Playback rate. Takes effect while playing: "
                      + AppContext.formatDuration(AppContext.replayDurationMs)
                      + " of build replays in about "
                      + AppContext.formatDuration(
                            AppContext.replayDurationMs
                            / speed.factors[speed.currentIndex])
                      + " at this rate."
            }
            HoverHandler { id: speedHover }
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
