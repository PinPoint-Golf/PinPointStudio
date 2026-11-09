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

// CARRYWATERFALL — where the yards went on one shot (shot_outcomes_design.md §5.3, §6.2): the golfer's usual,
// then each part taken off it in causal order, the residual when it is worth saying, then what the ball
// carried. Bars are drawn in the register's inks only, never warn: colour in this panel means "outside its
// corridor", and a yard lost is not that. What a part is (an estimate, cut to fit) is said in words.
// Hidden while `carry` is empty. One pager unit (`atom`) when set open on paper.
// carry: shotOutcome(shotId).carry — { usual, actual, unit, provenance, line, residual, residualShown, scaled,
//                                       parts: [{ id, name, loss, detail }] }
Item {
    id: wf
    objectName: "sdCarryWaterfall"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var carry: ({})
    property bool atom: true
    readonly property var keyKinds: []

    readonly property bool has: !!carry && carry.usual !== undefined && carry.actual !== undefined
    visible: has

    readonly property real labelW: roles.u(128)
    readonly property real valueW: roles.u(44)
    readonly property real gapW: roles.g(10)
    readonly property real trackW: Math.max(roles.u(40), width - labelW - valueW - 2 * gapW)
    readonly property real pxPerYard: has ? trackW / Math.max(1, carry.usual, carry.actual) : 0

    // the rows: each a bar from `from` to `to` on the same yardage axis, the parts walking left from the usual
    readonly property var rows: {
        if (!has) return []
        const c = carry
        const out = [{ name: "Your usual", detail: "", from: 0, to: c.usual, value: roles.figure(c.usual, 0), kind: "usual" }]
        let left = c.usual
        for (const p of (c.parts || [])) {
            const loss = Math.max(0, p.loss)
            const est = p.modelled === true || p.id === "launch_spin"
            out.push({ name: p.name + (est ? " (estimate)" : ""), detail: p.detail || "",
                       from: Math.max(0, left - loss), to: left, value: roles.figureSigned(-loss, 0), kind: "part" })
            left -= loss
        }
        if (c.residualShown === true) {
            const res = Math.max(0, c.residual)
            out.push({ name: "Not accounted for", detail: "", from: Math.max(0, left - res), to: left,
                       value: roles.figureSigned(-res, 0), kind: "residual" })
        }
        out.push({ name: "Actual", detail: "", from: 0, to: c.actual, value: roles.figure(c.actual, 0), kind: "actual" })
        return out
    }

    implicitHeight: col.height
    height: implicitHeight

    Column {
        id: col
        width: wf.width
        spacing: wf.roles.g(5)
        PpSectionHead {
            width: parent.width; roles: wf.roles; paged: false; pad: 0
            title: "Where the yards went"
            sub: wf.has ? (wf.carry.provenance || "") : ""
        }
        Repeater {
            model: wf.roles ? wf.rows : []
            delegate: Item {
                id: rw
                required property var modelData
                width: wf.width
                height: lbl.height
                Column {
                    id: lbl
                    width: wf.labelW
                    Text {
                        id: nm
                        width: parent.width; wrapMode: Text.WordWrap
                        text: rw.modelData.name
                        font: rw.modelData.kind === "part" ? wf.roles.fSmall : wf.roles.fSmallM
                        color: wf.roles.ink
                    }
                    Text {
                        visible: rw.modelData.detail !== ""
                        width: parent.width; wrapMode: Text.WordWrap
                        text: rw.modelData.detail; font: wf.roles.fMicro; color: wf.roles.ink3
                    }
                }
                Rectangle {
                    x: wf.labelW + wf.gapW + rw.modelData.from * wf.pxPerYard
                    y: (nm.height - height) / 2
                    width: Math.max(wf.roles.hw, (rw.modelData.to - rw.modelData.from) * wf.pxPerYard)
                    height: wf.roles.stripH
                    radius: wf.roles.r.barRadius
                    color: rw.modelData.kind === "usual" ? wf.roles.ink3
                         : rw.modelData.kind === "actual" ? wf.roles.ink2
                         : rw.modelData.kind === "residual" ? "transparent" : wf.roles.ink
                    border.width: rw.modelData.kind === "residual" ? wf.roles.hw : 0
                    border.color: wf.roles.ink3
                }
                Text {
                    x: wf.width - wf.valueW
                    y: nm.y + nm.baselineOffset - baselineOffset
                    width: wf.valueW
                    horizontalAlignment: Text.AlignRight
                    text: rw.modelData.value
                    font: wf.roles.fFig; color: rw.modelData.kind === "part" || rw.modelData.kind === "residual" ? wf.roles.ink2 : wf.roles.ink
                }
            }
        }
        Text {
            objectName: "sdCarryWaterfallScaled"
            visible: wf.has && wf.carry.scaled === true
            width: parent.width; wrapMode: Text.WordWrap
            text: "The parts were scaled to fit the shortfall."
            font: wf.roles.fSmall; color: wf.roles.ink3
        }
    }
}
