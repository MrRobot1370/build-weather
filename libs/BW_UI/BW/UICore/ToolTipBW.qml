import QtQuick
import QtQuick.Controls.Basic
import BW.UICore

ToolTip {
    id: root
    delay: 350
    font.family: Style.fontFamily
    font.pixelSize: Style.fontSizeS

    background: Rectangle {
        color: Style.bg1
        border.color: Style.border
        border.width: 1
        radius: Style.radiusS
    }
    contentItem: Text {
        text: root.text
        color: Style.textBody
        font: root.font
        renderType: Text.NativeRendering
        wrapMode: Text.WordWrap
    }
}
