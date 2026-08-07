import QtQuick
import QtQuick.Layouts
import BW.UICore

// One number with its label. The status strip and the analysis summary are
// both rows of these, so the numbers line up and share one type ramp.
Item {
    id: root
    property string label: ""
    property string value: "-"
    property string hint: ""
    property color valueColor: Style.textPrimary

    implicitWidth: Math.max(labelText.implicitWidth, valueText.implicitWidth)
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 1

        TextBW {
            id: labelText
            text: root.label
            variant: TextBW.Eyebrow
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        TextBW {
            id: valueText
            text: root.value
            variant: TextBW.Metric
            font.pixelSize: Style.fontSizeL
            color: root.valueColor
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
    }

    ToolTipBW {
        text: root.hint
        visible: root.hint.length > 0 && hover.hovered
    }
    HoverHandler { id: hover }
}
