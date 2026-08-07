import QtQuick
import QtQuick.Layouts
import BW.UICore

// One line of provenance: which data source is present and what it bought us.
// Worth the space, because half the questions about a treemap are really
// questions about where the numbers came from.
RowLayout {
    id: root
    property string label: ""
    property string detail: ""
    property bool ok: false

    spacing: Style.space1

    Rectangle {
        Layout.preferredWidth: 6
        Layout.preferredHeight: 6
        Layout.alignment: Qt.AlignVCenter
        radius: 3
        color: root.ok ? Style.success : Style.textFaint
    }
    TextBW {
        text: root.label
        variant: TextBW.Mono
        font.pixelSize: Style.fontSizeXS
        color: root.ok ? Style.textBody : Style.textFaint
    }
    TextBW {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
        text: root.detail
        variant: TextBW.Faint
        elide: Text.ElideMiddle
    }
}
