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

// A coaching card: the shell, a mono title in the card's tone, an optional aside on the title's
// baseline at the right, and a column of content under them.
//
// The aside right-aligns over a column `asideWidth` wide when it is narrower than that, so a
// "SESSIONS" heading sits over the pips it heads rather than at the card's edge.

import QtQuick
import PinPointStudio

Item {
    id: card

    property color  tone:       Theme.colorAccent
    property string title:      ""
    property string aside:      ""
    property int    asideWidth: 0               // right-align the aside over a column this wide
    property int    pad:        Theme.gap(20)
    property int    itemGap:    Theme.gap(18)
    property int    bodyGap:    Theme.gap(18)
    default property alias content: body.data
    readonly property int innerWidth: width - 2 * card.pad
    // The title, for a caller to set something on its baseline.
    readonly property alias titleItem: cardTitle

    implicitHeight: body.y + body.implicitHeight + card.pad

    PpCardShell {
        anchors.fill: parent
        tone: card.tone
    }

    PpMicro {
        id: cardTitle
        x: card.pad; y: card.pad + Theme.sp(2)
        text:  card.title
        color: card.tone
    }
    PpMicro {
        visible: card.aside !== ""
        anchors.baseline: cardTitle.baseline
        x: card.width - card.pad - Math.max(implicitWidth, card.asideWidth)
           + (card.asideWidth > implicitWidth ? card.asideWidth - implicitWidth : 0)
        text: card.aside
        font.letterSpacing: Theme.trackingData
    }

    Column {
        id: body
        x: card.pad
        y: cardTitle.y + cardTitle.implicitHeight + card.bodyGap
        width: card.innerWidth
        spacing: card.itemGap
    }
}
