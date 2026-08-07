import QtQuick
import QtQuick.Controls.Basic
import BW.UICore

Button {
    id: root
    property bool primary: false
    property bool danger: false

    implicitHeight: 28
    leftPadding: Style.space2
    rightPadding: Style.space2
    font.family: Style.fontFamily
    font.pixelSize: Style.fontSizeS
    font.weight: primary || danger ? Font.DemiBold : Font.Medium
    opacity: enabled ? 1.0 : 0.45

    background: Rectangle {
        radius: Style.radius
        color: {
            if (root.danger)
                return root.pressed ? Qt.darker(Style.danger, 1.3)
                     : root.hovered ? Qt.lighter(Style.danger, 1.1)
                     : Style.danger
            if (root.primary)
                return root.pressed ? Style.accentDown
                     : root.hovered ? Style.accentHover : Style.accent
            return root.pressed ? Style.bg3 : Style.bg2
        }
        border.width: 1
        border.color: root.primary || root.danger
                      ? "transparent"
                      : (root.hovered ? Style.borderHi : Style.borderInput)
        Behavior on color { ColorAnimation { duration: Style.durFast } }
    }

    contentItem: Text {
        text: root.text
        color: root.primary || root.danger ? Style.accentText
                                           : Style.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font: root.font
        renderType: Text.NativeRendering
    }
}
