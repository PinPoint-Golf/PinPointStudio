/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// The one telestrator palette — the controls only (active tool + ink colour +
// delete/clear), driving the shared AnnotationTool state every PpAnnotationLayer
// reads. The host floats it CENTRED over the camera tiles so its position reads
// as "applies to all cameras"; the open/collapse toggle lives separately in the
// LHS gutter. Content-sized, flat-panel chrome (no shadow).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: bar

    implicitWidth:  content.implicitWidth + Theme.sp(16)
    implicitHeight: Theme.sp(46)
    radius: Theme.radiusLg
    color: Qt.rgba(Theme.colorSurface.r, Theme.colorSurface.g, Theme.colorSurface.b, 0.96)
    border.width: 1
    border.color: Theme.colorBorderStrong

    // Vivid, distinct inks (theme-derived where possible; white for high contrast
    // over footage). The resolved colour is copied into each mark, so switching
    // aesthetic never recolours marks already drawn.
    readonly property var _swatches: [ Theme.colorAccent, Theme.colorGood,
                                       Theme.colorWarn, Theme.colorError, "#F5F5F5" ]

    // Tool segments: cursor / line / ellipse / hollow square.
    readonly property var _tools: [
        { tool: "select", kind: "select" },
        { tool: "line",   kind: "line"   },
        { tool: "circle", kind: "circle" },
        { tool: "rect",   kind: "rect"   }
    ]

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.sp(10)

        // ── Collapse the palette (back to the gutter open button) ─────────────
        Rectangle {
            id: collapseBtn
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.sp(30); height: Theme.sp(30)
            radius: Theme.radius
            color: collapseMa.containsMouse ? Theme.colorBg2 : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            PpAnnotationIcon {
                anchors.centerIn: parent
                width: Theme.sp(16); height: Theme.sp(16)
                kind: "chevronLeft"
                iconColor: Theme.colorText2
            }
            PpPressable { id: collapseMa; onClicked: AnnotationTool.paletteOpen = false }
        }

        // ── Tool selector pill (icons) ────────────────────────────────────────
        Rectangle {
            id: toolPill
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.sp(148); height: Theme.sp(30)
            radius: height / 2
            color: Theme.colorBg
            border.width: 1
            border.color: Theme.colorBorderMid

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.sp(3)
                spacing: Theme.sp(2)
                Repeater {
                    model: bar._tools
                    delegate: Rectangle {
                        id: seg
                        required property var modelData
                        readonly property bool active: AnnotationTool.tool === seg.modelData.tool
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: height / 2
                        color: seg.active        ? Theme.colorAccent
                             : segMa.containsMouse ? Theme.colorBg3
                             :                       "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                        PpAnnotationIcon {
                            anchors.centerIn: parent
                            width: Theme.sp(16); height: Theme.sp(16)
                            kind: seg.modelData.kind
                            iconColor: seg.active ? (Theme.dark ? Theme.colorBg : "#FFFFFF")
                                                  : Theme.colorText2
                        }
                        MouseArea {
                            id: segMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AnnotationTool.tool = seg.modelData.tool
                        }
                    }
                }
            }
        }

        Rectangle {  // separator
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(24); color: Theme.colorBorderMid
        }

        // ── Ink colour swatches ───────────────────────────────────────────────
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.sp(6)
            Repeater {
                model: bar._swatches
                delegate: Rectangle {
                    id: swatch
                    required property var modelData
                    readonly property bool active: Qt.colorEqual(AnnotationTool.strokeColor, swatch.modelData)
                    width: Theme.sp(22); height: Theme.sp(22)
                    radius: Theme.sp(5)
                    color: swatch.modelData
                    border.width: swatch.active ? 2 : 1
                    border.color: swatch.active ? Theme.colorText
                                         : Qt.rgba(Theme.colorText.r, Theme.colorText.g, Theme.colorText.b, 0.25)
                    PpPressable {
                        onClicked: {
                            AnnotationTool.strokeColor = swatch.modelData
                            AnnotationTool.recolorSelected(swatch.modelData)
                        }
                    }
                }
            }
        }

        Rectangle {  // separator
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(24); color: Theme.colorBorderMid
        }

        // ── Actions ───────────────────────────────────────────────────────────
        PpButton {
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Delete")
            destructive: true
            enabled: AnnotationTool.hasSelection
            onClicked: AnnotationTool.deleteSelected()
        }
        PpButton {
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("Clear")
            onClicked: AnnotationTool.clearAll()
        }
    }
}
