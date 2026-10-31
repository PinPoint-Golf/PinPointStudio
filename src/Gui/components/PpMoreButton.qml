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

// THE ⋯ TILE that opens a row's menu (the Sessions table's rows, the session drawer's rows): a square
// `radius` tile with a ⋯ in the data face, washed while hovered or while its menu is up, and grown by
// PpPressable (held while open). The tile is centred in the item, so a host may make the item wider
// (a table column) without moving the glyph.
//
//   PpMoreButton {
//       id: kebab
//       open: rowMenu.opened
//       onClicked: rowMenu.opened ? rowMenu.close() : rowMenu.open()
//       Popup { id: rowMenu; parent: kebab.face }    // the menu may live inside it
//   }
//
// `revealed: false` hides it at rest (a chromeless list shows it on row hover: bind it to the row's
// hover); it shows anyway while hovered or open.

import QtQuick
import PinPointStudio

Item {
    id: root

    property bool  open:     false               // its menu is up: stay washed and grown
    property bool  revealed: true                // false: hidden until hovered or open
    property real  tile:     Theme.sp(24)        // the square tile's side
    property color wash:     Theme.colorBg2      // the tile while hovered or open
    property color ink:      Theme.colorText3    // the ⋯ at rest
    property color litInk:   Theme.colorText     // the ⋯ while hovered or open
    readonly property bool containsMouse: press.containsMouse
    // The tile itself — the item PpPressable grows. A menu that opens from the tile and should move
    // with it, as it always has, takes this as its Popup parent.
    readonly property Item face: tileFace
    signal clicked()

    readonly property bool _lit: root.open || press.containsMouse

    implicitWidth:  root.tile
    implicitHeight: root.tile

    Rectangle {
        id: tileFace
        anchors.centerIn: parent
        width: root.tile; height: root.tile
        radius: Theme.radius
        color: root._lit ? root.wash : "transparent"
        opacity: (root.revealed || root._lit) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        Text {
            anchors.centerIn: parent
            text: "⋯"
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody
            color: root._lit ? root.litInk : root.ink
        }
        PpPressable {
            id: press
            held: root.open       // stay grown while the menu is up
            onClicked: root.clicked()
        }
    }
}
