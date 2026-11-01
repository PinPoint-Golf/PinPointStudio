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

// PpScopedAction — one tile of a device panel's scoped action row (Scan /
// Connect / Calibrate): a symbol glyph over a short label, outlined at rest and
// attention-framed when `primary` (work to do). Was an inline `component
// ScopedAction` copied into PpCameraPanel and PpImuPanel; extracted so the two
// panels' rows cannot drift.
//
// The caller sets `width` (the panels split their row into three equal cells);
// the tile does not read its host's size.

import QtQuick
import PinPointStudio

Rectangle {
    id: root
    property string glyph:   ""
    property string label:   ""
    property bool   primary: false
    // Drives the traveling-light frame while a connect attempt is in flight.
    property bool   connecting: false
    signal triggered()
    height: Theme.sp(50); radius: Theme.radius
    // Hover brighten + scale/dip — the PpButton language. Primary (amber)
    // lightens; the outline variant fades a faint fill in (alpha-ramped rest
    // so no colour flash).
    color:        primary ? (saMa.containsMouse ? Qt.lighter(Theme.colorAttentionLight, 1.08)
                                                : Theme.colorAttentionLight)
                          : Theme.hoverFill(saMa.containsMouse, Theme.colorBg2)
    border.width: 1
    border.color: primary ? Theme.colorAttention : Theme.colorBorderStrong
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    Column {
        anchors.centerIn: parent; spacing: Theme.gap(4)
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: root.glyph
            font.family: Theme.fontSymbol; font.pixelSize: Theme.sp(16)
            color: root.primary ? Theme.colorAttention : Theme.colorText2
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter; text: root.label
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: root.primary ? Theme.colorAttention : Theme.colorText
        }
    }
    PpPressable { id: saMa; onClicked: root.triggered() }
    PpConnectingFrame { anchors.fill: parent; radius: parent.radius; running: root.connecting }
}
