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

// A selectable tile: a card or row the user picks one of (a layout card, a preset, a club, an input
// device, an athlete). Selected is the accent wash inside an accent border; hover eases the border to
// accent-mid (Theme.choiceFill / choiceBorder). Give it a `title` and a line of `text`, or put your
// own content in it; it sizes to nothing of its own, so set its height.
//
//   style  (rest → hover)
//     ""      clear, mid border → a faint bg2 fading in                (cards, preset and club rows)
//     card    the surface, strong border → half bg2; no hover grow      (the microphone's device rows)
//     tint    the surface → the accent wash laid over it (opaque both ends), mid border, large
//             radius; selected looks hovered, with the accent border   (the athlete cards)

import QtQuick
import PinPointStudio

Rectangle {
    id: tile

    property bool   selected: false
    property string style:    ""
    property string title:    ""
    property string text:     ""
    property alias  hoverScale: press.hoverScale
    property alias  moves:      press.moves
    property alias  held:       press.held
    readonly property bool hovered: press.containsMouse

    signal clicked()
    signal doubleClicked()

    radius: style === "tint" ? Theme.radiusLg : Theme.radius
    color:  style === "tint" ? ((selected || hovered) ? Qt.tint(Theme.colorSurface, Theme.colorAccentLight)
                                                      : Theme.colorSurface)
          : Theme.choiceFill(selected, hovered, style === "card" ? "card" : "clear")
    border.width: 1
    border.color: Theme.choiceBorder(selected, hovered,
                                     style === "card" ? Theme.colorBorderStrong : Theme.colorBorderMid)
    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

    Column {
        visible: tile.title !== ""
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                  leftMargin: Theme.gap(12); rightMargin: Theme.gap(12) }
        spacing: Theme.gap(2)
        Text {
            width: parent.width
            text: tile.title; elide: Text.ElideRight
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            color: tile.selected ? Theme.colorAccent : Theme.colorText
        }
        Text {
            width: parent.width
            text: tile.text; elide: Text.ElideRight
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }
    }

    PpPressable {
        id: press
        z: 1                       // over whatever the caller puts in the tile
        hoverScale: tile.style === "card" ? 1.0 : 1.02
        onClicked:  tile.clicked()
        onDoubleClicked: tile.doubleClicked()
    }
}
