import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.space3
        spacing: Style.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.space2

            TextBW {
                text: "ninja"
                variant: TextBW.Eyebrow
            }
            TextBW {
                Layout.fillWidth: true
                text: AppContext.runner.ninjaAvailable
                      ? AppContext.runner.ninjaProgram
                      : "ninja was not found on PATH"
                variant: AppContext.runner.ninjaAvailable ? TextBW.Mono
                                                          : TextBW.Faint
                font.pixelSize: Style.fontSizeXS
                color: AppContext.runner.ninjaAvailable ? Style.textSecondary
                                                        : Style.warning
                elide: Text.ElideMiddle
            }
            CheckBox {
                id: follow
                checked: true
                contentItem: TextBW {
                    text: "follow"
                    variant: TextBW.Muted
                    leftPadding: follow.indicator.width + 4
                    verticalAlignment: Text.AlignVCenter
                }
                indicator: Rectangle {
                    implicitWidth: 14
                    implicitHeight: 14
                    y: (follow.height - height) / 2
                    radius: 3
                    color: follow.checked ? Style.accent : Style.bgWell
                    border.color: follow.checked ? Style.accent
                                                 : Style.borderInput
                    border.width: 1
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Style.bgWell
            radius: Style.radiusM
            border.width: 1
            border.color: Style.border
            clip: true

            ListView {
                id: view
                anchors.fill: parent
                anchors.margins: Style.space1
                model: AppContext.runner.output
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true

                onCountChanged: if (follow.checked) positionViewAtEnd()

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: 8
                }

                delegate: TextBW {
                    required property var modelData
                    width: view.width
                    text: modelData
                    variant: TextBW.Mono
                    font.pixelSize: Style.fontSizeXS
                    // errors and warnings findable by eye
                    color: /(^|\s)(error|FAILED)/i.test(modelData)
                           ? Style.danger
                           : (/warning/i.test(modelData) ? Style.warning
                              : (modelData.startsWith(">") ? Style.accent
                                 : Style.textBody))
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }
            }

            TextBW {
                anchors.centerIn: parent
                visible: view.count === 0
                text: "No build has been run in this session."
                variant: TextBW.Faint
            }
        }
    }
}
