import QtQuick
import BW.UICore

// A bordered surface used to group readouts and form rows. Consumers put a
// single Layout inside with `anchors.fill: parent`; the Pane derives its
// implicit size from that child so it works inside a parent Layout without a
// hard-coded preferredHeight.
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
