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

// A card for a session stage's panel: the coaching card's shell, tighter padding, and a body that
// fills the card rather than a column that sizes it — a stage panel is given its size by the
// stage and lays out whatever it shows (a chart, a video, a table) in the room it has.
//
// The heading row is the title and aside, as a coaching card's; a panel with its own heading
// (tabs, a picker) passes it as `heading` and that replaces the title. The aside stays either
// way, at the right on the first line of the row — the place a panel says "reading…" or what
// its columns are.
//
// The body CLIPS: whatever a panel draws stays inside its card, so a panel squeezed into the
// stage arrangement's side column is cut at the card's edge rather than spilling over the next.

import QtQuick
import PinPointStudio

Item {
    id: card

    property color     tone:    Theme.colorText3
    property string    title:   ""
    property string    aside:   ""
    property Component heading: null
    property int       pad:     Theme.gap(14)
    property int       bodyGap: Theme.gap(10)
    default property alias content: body.data
    readonly property int innerWidth: width - 2 * card.pad

    PpCardShell {
        anchors.fill: parent
        tone: card.tone
    }

    Item {
        id: headRow
        // Clear of the 3 px rule by the same margin the sides get.
        x: card.pad; y: card.pad
        width:  card.innerWidth
        height: Math.max(card.heading ? 0 : cardTitle.implicitHeight,
                         headingLoader.implicitHeight,
                         cardAside.visible ? cardAside.implicitHeight : 0)

        PpMicro {
            id: cardTitle
            visible: card.heading === null
            width: Math.max(0, headRow.width - (cardAside.visible ? cardAside.width + Theme.gap(12) : 0))
            elide: Text.ElideRight
            text:  card.title
            color: card.tone
        }
        Loader {
            id: headingLoader
            width: headRow.width - (cardAside.visible ? cardAside.width + Theme.gap(12) : 0)
            sourceComponent: card.heading
        }
        // Top-aligned rather than on a baseline: a heading component has none to offer, and the
        // title and a tab strip both set Micro type from the row's top, so the lines agree.
        PpMicro {
            id: cardAside
            visible: card.aside !== ""
            x: headRow.width - width
            width: Math.min(implicitWidth, headRow.width * 0.6)
            elide: Text.ElideRight
            text: card.aside
            font.letterSpacing: Theme.trackingData
        }
    }

    Item {
        id: body
        x: card.pad
        y: headRow.y + headRow.height + card.bodyGap
        width:  card.innerWidth
        height: Math.max(0, card.height - card.pad - y)
        clip: true
    }
}
