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

import QtQuick

// NOTES — small print: a footnote paragraph (A), optionally under a kicker and a short rule (B).
// tone 3 = ink3 (default), 2 = ink2. Held to 1.3 × the prose measure. Rich text allowed.
// keepPrev binds a footnote to the table above it when print cuts pages.
Column {
    id: nt
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property string text: ""
    property string kicker: ""
    property bool rule: false
    property int tone: 3
    property bool italic: false
    property bool small: false                 // false = micro size (A's footnotes); true = small size
    property bool atom: true
    property bool keepPrev: false
    readonly property var keyKinds: []

    spacing: roles.u(5)
    topPadding: roles.u(5)
    Rectangle { visible: nt.rule; width: nt.roles.u(28); height: nt.roles.hw; color: nt.roles.rule }
    Text {
        visible: nt.kicker.length > 0
        text: nt.kicker; font: nt.roles.fKicker; color: nt.roles.ink3
    }
    Text {
        width: Math.min(parent.width, nt.roles.flows.measure * nt.roles.em * 1.3)
        wrapMode: Text.WordWrap; textFormat: Text.RichText; lineHeight: nt.roles.r.smallLeading
        text: nt.text
        font: nt.italic ? nt.roles.fSmallI : nt.small ? nt.roles.fSmall : nt.roles.fMicro
        color: nt.tone === 2 ? nt.roles.ink2 : nt.roles.ink3
    }
}
