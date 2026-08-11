import QtQuick
import BW.UICore

// Implicit size comes from the single child, so put one Layout inside with
// anchors.fill: parent.
Rectangle {
    id: root
    color: Style.bgPanel
    border.color: Style.border
    border.width: 1
    radius: Style.radiusM

    default property alias content: inner.data
    property int padding: Style.space3

    readonly property var _first: inner.children.length > 0
                                  ? inner.children[0] : null
    implicitWidth:  _first && _first.implicitWidth
                    ? _first.implicitWidth + 2 * padding : 0
    implicitHeight: _first && _first.implicitHeight
                    ? _first.implicitHeight + 2 * padding : 0

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
