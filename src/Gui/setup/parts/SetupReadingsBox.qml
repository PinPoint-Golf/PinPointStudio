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

// The readings box a setup Check page lays over its live 3-D figure, top right: a translucent
// panel with a hairline border, sized to its rows plus padding. The rows are its children (the
// default property); CheckArmPage puts its live-wrist rows here, CheckTrunkPage its one line of
// trunk numbers and the address hint.
import QtQuick
import PinPointStudio

Rectangle {
    id: box

    default property alias rows: col.data
    property real padX:        Theme.sp(20)     // added to the rows' width
    property real padY:        Theme.sp(14)     // added to the rows' height
    property real rowSpacing:  Theme.gap(6)
    // The rows' width. A row that fills the box (width: parent.width) needs this set, or the
    // column would size itself from that row.
    property real rowsWidth:   col.implicitWidth

    anchors { top: parent.top; right: parent.right; margins: Theme.gap(10) }
    width:   col.width  + box.padX
    height:  col.height + box.padY
    radius:  Theme.radius
    color:   Qt.alpha(Theme.colorBg2, 0.85)
    border.width: 1
    border.color: Theme.colorBorderMid

    Column {
        id: col
        anchors.centerIn: parent
        width:   box.rowsWidth
        spacing: box.rowSpacing
    }
}
