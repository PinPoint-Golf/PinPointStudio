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

// A popup's background in the card family: the standard shell with a stronger hairline, so it
// lifts off whatever it is drawn over, and a quiet rule. For a Popup's `background:`.
//
//   ""          the card: the rule in `tone` along the top (menus, sheets, the toolbar panels)
//   plain       no rule, the large corner: a dialog's box (About, pairing, the confirm prompt)
//   plainTight  no rule, the small corner: a dropdown's list and the model editor's popovers
//
// The plain styles draw only the surface and one hairline, in `borderColor` — a confirm prompt
// rings in its tone, a picker that adds rather than picks in the accent.

import QtQuick
import PinPointStudio

Item {
    id: card

    property string style:       ""
    property color  tone:        Theme.colorText3
    property color  borderColor: Theme.colorBorderStrong

    PpCardShell {
        visible:      card.style === ""
        anchors.fill: parent
        floating:     true
        tone:         card.tone
    }
    Rectangle {
        visible:      card.style !== ""
        anchors.fill: parent
        radius:       card.style === "plainTight" ? Theme.radius : Theme.radiusLg
        color:        Theme.colorSurface
        border.width: 1
        border.color: card.borderColor
    }
}
