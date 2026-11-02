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
import PinPointStudio

// PPTRUSTFOLDTOGGLE — the ONE "▸ How far can I trust this?" line that opens a trust fold: the panel's summary
// (PpTrustSummaryFold) and a Charts section's own (PpChTrustFold). A link-ink caret and question; while closed, an
// optional `tail` after it in ink3 (what the fold holds, in a few words). Printed: no caret, ink, nothing to click.
// With no width it is as wide as its words (a fold's line); `hitMargin` lets the press reach past it.
// The user names it (trustSummaryToggle, <prefix>TrustToggle).
Item {
    id: tg
    property QtObject roles: null
    property bool open: false
    property bool printed: false
    property string tail: ""
    property real hitMargin: 0
    signal toggled()
    readonly property bool _tailShown: !open && !printed && tail !== ""
    readonly property real _gap: roles ? roles.g(7) : Theme.gap(7)
    implicitWidth: q.implicitWidth + (_tailShown ? _gap + tl.implicitWidth : 0)
    height: row.height
    Flow {
        id: row
        width: parent.width
        spacing: tg._gap
        Text { id: q; text: tg.printed ? "How far can I trust this?" : (tg.open ? "▾" : "▸") + " How far can I trust this?"
               font: tg.roles ? (tg.printed ? tg.roles.fSmallM : tg.roles.fSmallL) : Qt.application.font
               color: tg.roles ? (tg.printed ? tg.roles.ink : tg.roles.link) : "grey" }
        Text { id: tl; visible: tg._tailShown
               text: tg.tail
               font: tg.roles ? tg.roles.fSmall : Qt.application.font; color: tg.roles ? tg.roles.ink3 : "grey" }
    }
    MouseArea { anchors.fill: parent; anchors.margins: -tg.hitMargin; enabled: !tg.printed; cursorShape: Qt.PointingHandCursor; onClicked: tg.toggled() }
}
