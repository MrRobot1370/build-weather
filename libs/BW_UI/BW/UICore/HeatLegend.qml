import QtQuick
import BW.UICore

Item {
    id: root
    property real maxMs: 0
    property real medianMs: 0
    property string caption: "compile time"

    implicitWidth: 200
    implicitHeight: 26

    function label(ms) {
        if (ms >= 1000)
            return (ms / 1000).toFixed(ms < 10000 ? 2 : 1) + " s"
        return Math.round(ms) + " ms"
    }

    Column {
        anchors.fill: parent
        spacing: 2

        Rectangle {
            id: bar
            width: parent.width
            height: 8
            radius: 2
            border.width: 1
            border.color: Style.borderSubtle

            // where the median sits on the scale
            Rectangle {
                visible: root.maxMs > 0 && root.medianMs > 0
                x: Math.min(parent.width - 1,
                            parent.width * (root.medianMs / root.maxMs))
                width: 1
                height: parent.height
                color: Style.bg0
                opacity: 0.85
            }

            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.000; color: Style.heatStops[0] }
                GradientStop { position: 0.111; color: Style.heatStops[1] }
                GradientStop { position: 0.222; color: Style.heatStops[2] }
                GradientStop { position: 0.333; color: Style.heatStops[3] }
                GradientStop { position: 0.444; color: Style.heatStops[4] }
                GradientStop { position: 0.556; color: Style.heatStops[5] }
                GradientStop { position: 0.667; color: Style.heatStops[6] }
                GradientStop { position: 0.778; color: Style.heatStops[7] }
                GradientStop { position: 0.889; color: Style.heatStops[8] }
                GradientStop { position: 1.000; color: Style.heatStops[9] }
            }
        }

        Item {
            width: parent.width
            height: 14
            TextBW {
                anchors.left: parent.left
                text: "0"
                variant: TextBW.Faint
            }
            TextBW {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.medianMs > 0
                      ? root.caption + "  (median " + root.label(root.medianMs) + ")"
                      : root.caption
                variant: TextBW.Eyebrow
            }
            TextBW {
                anchors.right: parent.right
                text: root.maxMs >= 1000
                      ? (root.maxMs / 1000).toFixed(1) + " s"
                      : Math.round(root.maxMs) + " ms"
                variant: TextBW.Faint
            }
        }
    }
}
