import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BW.UICore
import BuildWeather

// The map. A treemap of the source tree on the left, a thin inspector on the
// right, and the replay transport underneath.
Item {
    id: page

    readonly property bool empty: !AppContext.build.hasData

    Shortcut { sequence: "Ctrl+0"; onActivated: map.fitToView() }
    Shortcut { sequence: StandardKey.ZoomIn;  onActivated: map.zoomBy(1.6) }
    Shortcut { sequence: StandardKey.ZoomOut; onActivated: map.zoomBy(1 / 1.6) }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Style.space3
        spacing: Style.space3

        // ================= map ============================================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Style.space1

            // ---- breadcrumb and view controls -----------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Style.space2

                ButtonBW {
                    text: "↑ up"
                    enabled: map.focusPath !== ""
                    onClicked: map.focusParent()
                }
                TextBW {
                    Layout.fillWidth: true
                    text: map.focusPath === "" ? "whole tree" : map.focusPath
                    variant: TextBW.Mono
                    font.pixelSize: Style.fontSizeS
                    color: Style.textSecondary
                    elide: Text.ElideMiddle
                }

                // ---- zoom -------------------------------------------------
                ButtonBW {
                    text: "−"
                    implicitWidth: 28
                    enabled: map.zoom > map.minZoom
                    onClicked: map.zoomBy(1 / 1.6)
                }
                TextBW {
                    text: map.zoom < 1.05 ? "fit" : map.zoom.toFixed(1) + "x"
                    variant: TextBW.Mono
                    font.pixelSize: Style.fontSizeS
                    color: map.zoom > 1.05 ? Style.accent : Style.textFaint
                    horizontalAlignment: Text.AlignHCenter
                    Layout.preferredWidth: 42
                }
                ButtonBW {
                    text: "+"
                    implicitWidth: 28
                    enabled: map.zoom < map.maxZoom
                    onClicked: map.zoomBy(1.6)
                }
                ButtonBW {
                    text: "Fit"
                    enabled: map.zoom > map.minZoom
                    onClicked: map.fitToView()
                    ToolTipBW {
                        visible: fitHover.hovered
                        text: "Show the whole tree again (Ctrl+0)"
                    }
                    HoverHandler { id: fitHover }
                }

                CheckBox {
                    id: stableCheck
                    text: "stable layout"
                    checked: true
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fontSizeS
                    contentItem: TextBW {
                        text: stableCheck.text
                        variant: TextBW.Muted
                        leftPadding: stableCheck.indicator.width + 4
                        verticalAlignment: Text.AlignVCenter
                    }
                    indicator: Rectangle {
                        implicitWidth: 14
                        implicitHeight: 14
                        y: (stableCheck.height - height) / 2
                        radius: 3
                        color: stableCheck.checked ? Style.accent : Style.bgWell
                        border.color: stableCheck.checked ? Style.accent
                                                          : Style.borderInput
                        border.width: 1
                    }
                    ToolTipBW {
                        visible: stableHover.hovered
                        text: "Order cells by name, so a file keeps its place "
                              + "between builds and only its area changes. "
                              + "Turning this off orders by duration, which "
                              + "packs squarer but moves everything whenever a "
                              + "timing moves."
                    }
                    HoverHandler { id: stableHover }
                }

                CheckBox {
                    id: labelCheck
                    text: "labels"
                    checked: true
                    contentItem: TextBW {
                        text: labelCheck.text
                        variant: TextBW.Muted
                        leftPadding: labelCheck.indicator.width + 4
                        verticalAlignment: Text.AlignVCenter
                    }
                    indicator: Rectangle {
                        implicitWidth: 14
                        implicitHeight: 14
                        y: (labelCheck.height - height) / 2
                        radius: 3
                        color: labelCheck.checked ? Style.accent : Style.bgWell
                        border.color: labelCheck.checked ? Style.accent
                                                         : Style.borderInput
                        border.width: 1
                    }
                }

                Segmented {
                    model: ["Heat", "Delta"]
                    currentIndex: map.colorMode
                    enabled: AppContext.build.comparing || map.colorMode === 0
                    onActivated: function(index) {
                        if (index === 1 && !AppContext.build.comparing) {
                            AppContext.showMessage(
                                "Load a baseline build on the Compare tab first.",
                                true)
                            return   // currentIndex is bound to map.colorMode,
                                     // which stays put, so the control does too
                        }
                        map.colorMode = index
                    }
                }
            }

            // ---- the map itself --------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Style.bgMap
                radius: Style.radiusM
                border.width: 1
                border.color: Style.border
                clip: true

                TreemapItem {
                    id: map
                    anchors.fill: parent
                    anchors.margins: 1
                    model: AppContext.build
                    stableOrder: stableCheck.checked
                    showLabels: labelCheck.checked
                    darkTheme: Style.dark

                    onLeafActivated: function(index, path) {
                        inspector.pinnedPath = path
                    }
                    onDirectoryActivated: function(path) {
                        inspector.pinnedPath = path
                    }
                }

                // Only show the grab cursor once there is somewhere to drag
                // to, so at fit zoom the pointer still says "click me".
                MouseArea {
                    anchors.fill: map
                    acceptedButtons: Qt.NoButton
                    cursorShape: map.panning ? Qt.ClosedHandCursor
                                 : (map.zoom > 1.0 ? Qt.OpenHandCursor
                                                   : Qt.ArrowCursor)
                }

                // Discoverability: nothing else tells you the map zooms.
                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Style.space2
                    visible: !page.empty && map.zoom < 1.05
                    width: hint.implicitWidth + 2 * Style.space1
                    height: hint.implicitHeight + Style.space1
                    radius: Style.radiusS
                    color: Style.overlay
                    TextBW {
                        id: hint
                        anchors.centerIn: parent
                        text: "scroll to zoom  ·  drag to pan  ·  double click to drill in"
                        variant: TextBW.Faint
                        color: Style.dark ? Style.textMuted : "#E8EAEE"
                    }
                }

                // Hover tooltip. Follows the cell rather than the cursor, so
                // it does not jitter while scanning across small files.
                Rectangle {
                    id: tip
                    visible: map.hoveredPath !== ""
                    color: Style.bg1
                    border.color: Style.borderHi
                    border.width: 1
                    radius: Style.radiusS
                    width: tipColumn.implicitWidth + 2 * Style.space2
                    height: tipColumn.implicitHeight + 2 * Style.space1
                    x: Math.max(4, Math.min(parent.width - width - 4,
                                            map.hoveredAnchor.x - width / 2))
                    y: map.hoveredAnchor.y > height + 8
                       ? map.hoveredAnchor.y - height - 6
                       : map.hoveredAnchor.y + 20
                    opacity: 0.97

                    ColumnLayout {
                        id: tipColumn
                        anchors.centerIn: parent
                        spacing: 2

                        TextBW {
                            text: map.hoveredPath
                            variant: TextBW.Mono
                            font.pixelSize: Style.fontSizeS
                            color: Style.textPrimary
                            Layout.maximumWidth: 520
                            elide: Text.ElideMiddle
                        }
                        RowLayout {
                            spacing: Style.space2
                            TextBW {
                                text: AppContext.formatDuration(
                                          map.hoveredDurationMs)
                                variant: TextBW.Metric
                                font.pixelSize: Style.fontSize
                                color: Style.accent
                            }
                            TextBW {
                                visible: map.hoveredIsDirectory
                                text: map.hoveredLeafCount + " files"
                                variant: TextBW.Faint
                            }
                            TextBW {
                                visible: !map.hoveredIsDirectory
                                         && map.hoveredRank > 0
                                text: "rank #" + map.hoveredRank + " of "
                                      + AppContext.build.targetCount
                                variant: TextBW.Faint
                            }
                            TextBW {
                                visible: AppContext.build.comparing
                                         && map.hoveredDeltaMs !== 0
                                text: AppContext.formatDelta(map.hoveredDeltaMs)
                                variant: TextBW.Faint
                                color: map.hoveredDeltaMs > 0 ? Style.worse
                                                              : Style.better
                            }
                        }
                    }
                }

                // Empty state: the app is useless without a build directory,
                // so say exactly what to point it at.
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: page.empty
                    spacing: Style.space2
                    TextBW {
                        Layout.alignment: Qt.AlignHCenter
                        text: "No build loaded"
                        variant: TextBW.Heading
                    }
                    TextBW {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 420
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "Point Build Weather at a CMake plus Ninja build "
                              + "directory. It reads .ninja_log for exact "
                              + "per-target timings and compile_commands.json "
                              + "to map objects back to sources."
                        variant: TextBW.Muted
                    }
                }
            }

            // ---- replay transport -------------------------------------------
            ReplayBar { Layout.fillWidth: true }
        }

        // ================= inspector =========================================
        InspectorPanel {
            id: inspector
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            hoveredPath: map.hoveredPath
            selectedIndex: map.selectedIndex
            maxMs: AppContext.build.maxMs
        }
    }
}
