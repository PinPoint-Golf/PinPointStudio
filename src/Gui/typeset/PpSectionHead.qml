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
// It is also THE typeset crosshead every panel draws ("title + quiet sub-line / count": Charts' sections and
// ledgers, Session diagnostics, Trust, the launch monitor board). The parts past number / title / sub are off
// unless asked for, so a plain head is exactly the numbered one above:
//   count            a figure after the title (fFigS ink3: how many rows follow), centred in the title's line, or
//                    on its baseline (countOnBaseline); countFont another figure role
//   foldable/folded  a ▸ / ▾ caret before it all, for a head that folds its section
//   subFont/subColor the sub-line's role (fSmall ink3 unless set: fSmallI, fMicro, a warn ink); subGap more room
//                    before it (u)
//   subFit           "line" centred in the title's line · "grow" the same, but as tall as it wraps · "top" its
//                    own height, at the top · "baseline" on the title's baseline
//   subWrap          it wraps inside the head (the default); false: one line at its own width (a line of its own
//                    when it does not fit beside the title) · subClip: one line, cut at the head's right edge
//   note             after the sub-line, ink2 (a folded section's one-line state)
//   gap / pad / lead the room between the parts, under the head and over it (u); ruleWeight / ruleGap the top
//                    rule's weight and its room to the line (rule: true)
//   paged: false     not a pager unit (a head inside another block's atom, or on screen only): neither an atom nor
//                    a library block, so the pager walks past it as it does plain text
// It sizes itself to its parts when no width is given (a head in a Row), and its baseline is the title's.
// With no roles it draws nothing (a panel's head can exist before its typeset).
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
    property bool paged: true
    property bool atom: paged
    property bool keepNext: paged
    readonly property string section: title
    readonly property var keyKinds: paged ? [] : undefined
    property string count: ""
    property bool countOnBaseline: false
    property font countFont: _on ? roles.fFigS : Qt.font({})
    property bool foldable: false
    property bool folded: false
    property font subFont: _on ? roles.fSmall : Qt.font({})
    property color subColor: _on ? roles.ink3 : "transparent"
    property real subGap: 0
    property string subFit: "line"
    property bool subWrap: true
    property bool subClip: false
    property string note: ""
    property real gap: 10
    property real pad: 10
    property real lead: 0
    property real ruleWeight: _on ? roles.hw : 0
    property real ruleGap: 6
    readonly property bool _on: !!roles
    readonly property real _g: _on ? roles.g(gap) : 0
    // where the sub-line starts on the title's line: the parts before it, each with its gap
    readonly property real _beforeSub: (foldable ? caret.width + _g : 0) + (number.length > 0 ? num.width + _g : 0) + st.width + _g
                                       + (count !== "" ? cnt.width + _g : 0) + (subGap > 0 ? spacer.width + _g : 0)
    implicitWidth: {
        const parts = [foldable ? caret.implicitWidth : -1, number.length > 0 ? num.implicitWidth : -1, st.implicitWidth,
                       count !== "" ? cnt.implicitWidth : -1, subGap > 0 ? spacer.width : -1,
                       sub.length > 0 ? sb.implicitWidth : -1, note !== "" ? nt.implicitWidth : -1].filter(w => w >= 0)
        return parts.reduce((a, w) => a + w, 0) + _g * Math.max(0, parts.length - 1)
    }
    implicitHeight: _on ? col.y + col.height + roles.g(pad) : 0
    height: implicitHeight
    baselineOffset: col.y + fl.y + st.y + st.baselineOffset
    Column {
        id: col
        visible: sh._on
        y: sh._on ? sh.roles.g(sh.lead) : 0
        width: parent.width
        spacing: sh._on ? sh.roles.g(sh.ruleGap) : 0
        Rectangle { visible: sh.rule; width: parent.width; height: sh.ruleWeight; color: sh._on ? sh.roles.rule : "transparent" }
        Flow {
            id: fl
            width: parent.width
            spacing: sh._g
            Text { id: caret; visible: sh.foldable; text: sh.folded ? "▸" : "▾"; font: sh._on ? sh.roles.fSmall : Qt.font({}); color: sh._on ? sh.roles.ink3 : "transparent"
                   height: st.height; verticalAlignment: Text.AlignVCenter }
            Text { id: num; visible: sh.number.length > 0; text: sh.number; font: sh._on ? sh.roles.fFigS : Qt.font({}); color: sh._on ? sh.roles.ink3 : "transparent"
                   height: st.height; verticalAlignment: Text.AlignVCenter }
            Text { id: st; text: sh.title; font: sh._on ? sh.roles.fSection : Qt.font({}); color: sh._on ? sh.roles.ink : "transparent" }
            Text { id: cnt; visible: sh.count !== ""; text: sh.count; font: sh.countFont; color: sh._on ? sh.roles.ink3 : "transparent"
                   topPadding: sh.countOnBaseline ? st.baselineOffset - cfm.ascent : 0
                   height: sh.countOnBaseline ? implicitHeight : st.height
                   verticalAlignment: sh.countOnBaseline ? Text.AlignTop : Text.AlignVCenter }
            Item { id: spacer; visible: sh.subGap > 0; width: sh._on ? sh.roles.g(sh.subGap) : 0; height: 1 }
            // clipped, a hair under the rest of the line, so a rounding never sends it to a line of its own
            Text { id: sb; visible: sh.sub.length > 0; text: sh.sub; font: sh.subFont; color: sh.subColor
                   readonly property bool centred: sh.subFit === "line" || sh.subFit === "grow"
                   width: sh.subClip ? Math.max(0, col.width - sh._beforeSub - 0.01) : sh.subWrap ? Math.min(implicitWidth, col.width) : implicitWidth
                   wrapMode: sh.subWrap && !sh.subClip ? Text.WordWrap : Text.NoWrap
                   clip: sh.subClip
                   topPadding: sh.subFit === "baseline" ? st.baselineOffset - sfm.ascent : 0
                   height: sh.subFit === "line" ? st.height : sh.subFit === "grow" ? Math.max(implicitHeight, st.height) : implicitHeight
                   verticalAlignment: centred ? Text.AlignVCenter : Text.AlignTop }
            Text { id: nt; visible: sh.note !== ""; text: sh.note; font: sh._on ? sh.roles.fSmall : Qt.font({}); color: sh._on ? sh.roles.ink2 : "transparent"
                   height: st.height; verticalAlignment: Text.AlignVCenter }
        }
    }
    FontMetrics { id: cfm; font: cnt.font }
    FontMetrics { id: sfm; font: sb.font }
}
