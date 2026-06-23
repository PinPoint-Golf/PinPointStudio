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

// The one telestrator palette — floats over the analyse-view camera tiles and
// drives the shared AnnotationTool state (active tool + ink colour) that every
// PpAnnotationLayer reads. Marks themselves live per tile; this bar is global.
// Content-sized, flat-panel chrome (no shadow), matching the toolbar idiom.

import QtQuick
import PinPointStudio

Rectangle {
    id: bar

    implicitWidth:  content.implicitWidth + Theme.sp(20)
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

    readonly property var _tools:  [ "select", "circle", "line", "rect" ]
    readonly property var _labels: [ "Select", "Circle", "Line", "Square" ]
    function _toolLabel(t) { var i = _tools.indexOf(t); return i >= 0 ? _labels[i] : "Select" }
    function _labelTool(l) { var i = _labels.indexOf(l); return i >= 0 ? _tools[i] : "select" }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.sp(10)

        // ── Tool selector ─────────────────────────────────────────────────────
        PpSegmentedControl {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.sp(208)
            options:  bar._labels
            selected: bar._toolLabel(AnnotationTool.tool)
            onActivated: (value) => { AnnotationTool.tool = bar._labelTool(value) }
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
