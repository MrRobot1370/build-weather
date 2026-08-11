import QtQuick
import QtQuick.Controls.Basic
import BW.UICore

// Controlled: tapping only emits activated(), it never writes its own
// currentIndex. Bind currentIndex to the real state and change it in
// onActivated.
Rectangle {
    id: root

    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    implicitHeight: 26
    implicitWidth: row.implicitWidth + 4
    radius: Style.radiusS
    color: Style.bgWell
    border.width: 1
    border.color: Style.borderInput

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: root.model
            delegate: Item {
                required property int index
                required property var modelData

                width: Math.max(56, label.implicitWidth + 2 * Style.space2)
                height: root.height - 4

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Style.radiusS - 1
                    color: index === root.currentIndex
                           ? Style.accentSoft
                           : (hover.hovered ? Style.bg3 : "transparent")
                    border.width: index === root.currentIndex ? 1 : 0
                    border.color: Style.accentLine
                    Behavior on color { ColorAnimation { duration: Style.durFast } }
                }

                TextBW {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    variant: TextBW.Muted
                    color: index === root.currentIndex ? Style.accent
                                                       : Style.textSecondary
                    font.weight: index === root.currentIndex ? Font.DemiBold
                                                             : Font.Normal
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: root.activated(index)
                }
            }
        }
    }
}
