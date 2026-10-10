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

// Something set into a card: a faintly darker panel with a hairline, for the part a reader acts
// on (a drill, what to try). With `bar`, a short bar in the tone down its left edge marks it as
// the card's own — the focus card's HOW TO PRACTISE.
//
// The content is a column inset by padX / padY; the panel's natural height follows it.

import QtQuick
import PinPointStudio

Item {
    id: inset

    property color tone:    Theme.colorAccent
    property bool  bar:     false
    property int   padX:    Theme.gap(14)
    property int   padY:    Theme.gap(12)
    property int   spacing: Theme.gap(4)
    default property alias content: col.data
    readonly property real contentHeight: col.implicitHeight

    implicitHeight: col.implicitHeight + 2 * inset.padY

    Rectangle {
        anchors.fill: parent
        radius:  Theme.radius
        color:   Qt.alpha(Theme.colorText, Theme.dark ? 0.035 : 0.04)
        border.width: 1
        border.color: Theme.colorBorder
    }
    Rectangle {
        visible: inset.bar
        x: 0; y: Theme.sp(14)
        width: 3; height: parent.height - 2 * Theme.sp(14)
        radius: 1.5
        color: inset.tone
    }
    Column {
        id: col
        x: inset.padX; y: inset.padY
        width: parent.width - 2 * inset.padX
        spacing: inset.spacing
    }
}
