import QtQuick
import QtQuick.Controls.Basic
import BW.UICore

// Information-dense table over a QAbstractTableModel. Deliberately plain:
// the analysis tab is for reading numbers, so there is no animation, no
// alternating decoration beyond a faint row band, and the numeric columns
// are monospaced so digits line up.
Rectangle {
    id: root

    property alias model: view.model
    /// One entry per column: { title, width, align, mono }.
    property var columns: []
    property int rowHeight: 24
    property int currentRow: -1
    signal rowActivated(int row)

    color: Style.bgPanel
    border.color: Style.border
    border.width: 1
    radius: Style.radiusM
    clip: true

    readonly property real totalFixedWidth: {
        var sum = 0
        for (var i = 0; i < columns.length; ++i)
            sum += (columns[i].width > 0 ? columns[i].width : 0)
        return sum
    }
    readonly property int flexibleColumn: {
        for (var i = 0; i < columns.length; ++i)
            if (!(columns[i].width > 0))
                return i
        return -1
    }

    function columnWidth(index) {
        if (index === flexibleColumn)
            return Math.max(120, width - 2 - totalFixedWidth
                            - (vbar.visible ? vbar.width : 0))
        return columns[index].width
    }

    // ---- header --------------------------------------------------------
    Rectangle {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.margins: 1
        height: 26
        color: Style.bgWell

        Row {
            anchors.fill: parent
            Repeater {
                model: root.columns
                delegate: Item {
                    required property int index
                    required property var modelData
                    width: root.columnWidth(index)
                    height: header.height

                    TextBW {
                        anchors.fill: parent
                        anchors.leftMargin: Style.space1
                        anchors.rightMargin: Style.space1
                        text: modelData.title
                        variant: TextBW.Eyebrow
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: modelData.align === Qt.AlignRight
                                             ? Text.AlignRight
                                             : Text.AlignLeft
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Style.border
        }
    }

    // ---- rows ----------------------------------------------------------
    ListView {
        id: view
        anchors {
            left: parent.left; right: parent.right
            top: header.bottom; bottom: parent.bottom
        }
        anchors.margins: 1
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        cacheBuffer: 400

        ScrollBar.vertical: ScrollBar {
            id: vbar
            policy: ScrollBar.AsNeeded
            width: 8
            contentItem: Rectangle {
                radius: 4
                color: Style.borderHi
                opacity: vbar.pressed ? 0.9 : 0.5
            }
        }

        delegate: Item {
            id: rowItem
            required property int index
            required property var model
            width: view.width
            height: root.rowHeight

            Rectangle {
                anchors.fill: parent
                color: root.currentRow === rowItem.index
                       ? Style.accentSoft
                       : (rowHover.hovered ? Style.bg2
                          : (rowItem.index % 2 === 0 ? "transparent"
                                                     : Style.bgWell))
            }

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.columns
                    delegate: Item {
                        required property int index
                        required property var modelData
                        width: root.columnWidth(index)
                        height: rowItem.height

                        TextBW {
                            anchors.fill: parent
                            anchors.leftMargin: Style.space1
                            anchors.rightMargin: Style.space1
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment:
                                modelData.align === Qt.AlignRight
                                ? Text.AlignRight : Text.AlignLeft
                            elide: modelData.align === Qt.AlignRight
                                   ? Text.ElideNone : Text.ElideMiddle
                            variant: modelData.mono ? TextBW.Mono
                                                    : TextBW.Body
                            font.pixelSize: Style.fontSizeS
                            color: rowItem.model.rowColor !== undefined
                                   && index === 0
                                   ? rowItem.model.rowColor
                                   : Style.textBody
                            text: {
                                var role = modelData.role
                                var v = rowItem.model[role]
                                return v === undefined ? "" : v
                            }
                        }
                    }
                }
            }

            HoverHandler { id: rowHover }
            TapHandler {
                onTapped: {
                    root.currentRow = rowItem.index
                    root.rowActivated(rowItem.index)
                }
            }

            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Style.borderSubtle
                opacity: 0.6
            }
        }
    }

    // ---- empty state ---------------------------------------------------
    TextBW {
        anchors.centerIn: parent
        visible: view.count === 0
        text: "no data"
        variant: TextBW.Faint
    }
}
