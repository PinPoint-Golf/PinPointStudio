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

// A row's hover action: a small chip of micro caps words that appears on the row under the pointer
// (or the one with focus) and does that row's one obvious thing — the swing model's "Graph"
// (ModelTable), the Sessions table's "Review" (PpSessionsTableRow). colorBg3 at rest, the accent
// and colorAccentLight under the pointer; the shortcut rides in the tooltip, so the mouse path
// teaches the keyboard one.
//
// Where it sits and when it shows are the caller's: set x/anchors and `visible`. It takes no press
// while hidden.

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Rectangle {
    id: chip

    property string text:    ""
    property string tooltip: ""
    // The pointer is on it (the caller may want to hold its row's hover while it is).
    readonly property bool hovered: press.containsMouse

    signal clicked()

    width:  label.implicitWidth + Theme.sp(14)
    height: Theme.sp(20)
    radius: Theme.radius
    color:  press.containsMouse ? Theme.colorAccentLight : Theme.colorBg3
    border.width: 1
    border.color: press.containsMouse ? Theme.colorAccent : Theme.colorBorderMid
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

    Text {
        id: label
        objectName: "rowActionText"
        anchors.centerIn: parent
        text:                chip.text
        font.family:         Theme.fontBody
        font.pixelSize:      Theme.fontSzMicro
        font.weight:         Theme.fontBodyWeight
        font.letterSpacing:  Theme.trackingMicro
        font.capitalization: Theme.capsFont
        color: press.containsMouse ? Theme.colorAccent : Theme.colorText2
    }

    ToolTip.visible: press.containsMouse && chip.tooltip !== ""
    ToolTip.text:    chip.tooltip
    ToolTip.delay:   400

    PpPressable {
        id: press
        hoverScale: 1.0
        enabled:    chip.visible
        onClicked:  chip.clicked()
    }
}
