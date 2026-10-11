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

// SECTIONHEAD — a numbered section head for a report or a long scroll ("1  Session patterns", with a
// quiet sub-line). It names the section the PpPager repeats as "<section>, continued" when a list runs onto
// the next page; breakBefore starts it on a new page (the detail feature always does).
Item {
    id: sh
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property string number: ""
    property string title: ""
    property string sub: ""
    property bool rule: false
    property bool breakBefore: false
    property bool atom: true
    property bool keepNext: true
    readonly property string section: title
    readonly property var keyKinds: []
    implicitHeight: col.height + roles.u(10)
    height: implicitHeight
    Column {
        id: col
        width: parent.width
        spacing: sh.roles.u(6)
        Rectangle { visible: sh.rule; width: parent.width; height: sh.roles.hw; color: sh.roles.rule }
        Flow {
            width: parent.width
            spacing: sh.roles.u(10)
            Text { visible: sh.number.length > 0; text: sh.number; font: sh.roles.fFigS; color: sh.roles.ink3; height: st.height; verticalAlignment: Text.AlignVCenter }
            Text { id: st; text: sh.title; font: sh.roles.fSection; color: sh.roles.ink }
            Text { visible: sh.sub.length > 0; text: sh.sub; font: sh.roles.fSmall; color: sh.roles.ink3; height: st.height; verticalAlignment: Text.AlignVCenter
                   width: Math.min(implicitWidth, col.width); wrapMode: Text.WordWrap }
        }
    }
}
