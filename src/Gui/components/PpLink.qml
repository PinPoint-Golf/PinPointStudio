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

// A text link in the accent: words that go somewhere. It brightens under the pointer where
// `hoverTint` is on. A caller with a larger press target of its own sets `interactive: false`
// and binds `hovered` to it; `pressMargin` grows the link's own target past its words.

import QtQuick
import PinPointStudio

Text {
    id: link

    property bool interactive: true
    property bool hoverTint:   true
    property bool hovered:     link.interactive && press.containsMouse
    property int  pressMargin: 0

    signal clicked(var mouse)

    font.family:    Theme.fontBody
    font.pixelSize: Theme.fontSzBody2
    color:          link.hoverTint && link.hovered ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent

    MouseArea {
        id: press
        visible: link.interactive
        enabled: link.interactive
        anchors.fill: parent
        anchors.margins: -link.pressMargin
        hoverEnabled: link.hoverTint
        cursorShape:  Qt.PointingHandCursor
        onClicked: (mouse) => link.clicked(mouse)
    }
}
