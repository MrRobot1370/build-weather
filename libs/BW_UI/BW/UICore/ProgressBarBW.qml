import QtQuick
import BW.UICore

Rectangle {
    id: root
    property real value: 0.0      // 0 .. 1
    property bool indeterminate: false
    property color fillColor: Style.accent

    implicitHeight: 3
    implicitWidth: 200
    radius: height / 2
    color: Style.bgWell
    clip: true

    Rectangle {
        id: fill
        height: parent.height
        radius: parent.radius
        color: root.fillColor
        width: root.indeterminate
               ? parent.width * 0.25
               : parent.width * Math.max(0, Math.min(1, root.value))
        Behavior on width {
            enabled: !root.indeterminate
            NumberAnimation { duration: Style.durMed; easing.type: Easing.OutCubic }
        }
    }

    NumberAnimation {
        target: fill
        property: "x"
        running: root.indeterminate && root.visible
        loops: Animation.Infinite
        from: -root.width * 0.25
        to: root.width
        duration: 1100
        easing.type: Easing.InOutQuad
    }
}
