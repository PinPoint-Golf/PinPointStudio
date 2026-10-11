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

// KEYFIGURES — A's summary row of key figures under a heavy rule: label above, figure below, hairline
// dividers. It wraps n → n/2 → n/4 per row (never a ragged 3-of-8), each figure ≥ roles.flows.keyFigure em.
// Pass `items` [{ label, figure, warn, word }] or let it build them from `detail` (+ readout).
Item {
    id: kf
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property var detail: null
    property var readout: null
    property var items: defaultItems
    property var exclude: []                    // keys to leave out of the default items: median shot pass faultLine faultOutside fired pastFault after trend
    property bool showFoot: true
    property bool atom: true
    readonly property var keyKinds: []

    readonly property var c: (detail && detail.header) || ({})
    readonly property var sp: c.spread || ({})
    readonly property int shotNo: roles.shotNo(readout)
    objectName: "sdKeyFigures"
    readonly property var defaultItems: {
        if (!detail) return []
        const out = [
            { key: "median", label: "Session median", figure: sp.medianNumber || "-" },
            { key: "shot", label: "Shot " + shotNo, figure: roles.fig(c.valueText), warn: c.thisShot === "fired" }
        ]
        roles.corridorParts(c.corridorText).forEach(q => out.push(Object.assign({ key: q.label === "Pass" ? "pass" : q.label === "Fault line" ? "faultLine" : "faultOutside" }, q)))
        out.push({ key: "fired", label: "Fired", figure: roles.fracOf(c.recurrence) })
        out.push({ key: "pastFault", label: "Past the fault line", figure: (sp.pastFault !== undefined ? sp.pastFault + " of " + sp.placed : "-") })
        if (c.firingsAfter !== undefined || !c.recencyText) out.push({ key: "after", label: "Firings after shot " + shotNo, figure: c.firingsAfter !== undefined ? String(c.firingsAfter) : "-" })
        else out.push({ key: "after", label: "Last fired", figure: roles.cap1(c.recencyText), word: true })
        out.push({ key: "trend", label: "Trend", figure: roles.trendWord(c), word: true })
        const ex = exclude || []
        return ex.length ? out.filter(it => ex.indexOf(it.key) < 0) : out
    }
    readonly property int fit: roles.columns(width, roles.flows.keyFigure, 0)
    readonly property int n: (items || []).length
    readonly property int cols: Math.max(1, fit >= n ? n : fit >= Math.ceil(n / 2) ? Math.ceil(n / 2) : Math.ceil(n / 4))
    readonly property real cw: width / cols
    readonly property real ch: roles.u(50)
    readonly property real gridH: Math.ceil(n / cols) * ch
    implicitHeight: roles.r.figureRuleTop + gridH + (showFoot ? foot.height + roles.u(6) : roles.u(4))
    height: implicitHeight

    Rectangle { width: parent.width; height: kf.roles.r.figureRuleTop; color: kf.roles.rule }
    Repeater {
        model: kf.roles ? (kf.items || []) : []
        delegate: Item {
            required property var modelData
            required property int index
            readonly property int col: index % kf.cols
            readonly property int row: Math.floor(index / kf.cols)
            x: col * kf.cw; y: kf.roles.r.figureRuleTop + row * kf.ch
            width: kf.cw; height: kf.ch
            Rectangle { visible: col > 0; width: kf.roles.hw; height: parent.height - kf.roles.u(14); y: kf.roles.u(7); color: kf.roles.hair }
            Rectangle { visible: row > 0; width: parent.width; height: kf.roles.hw; color: kf.roles.hair }
            Text { x: col > 0 ? kf.roles.u(12) : 0; y: kf.roles.u(7); width: parent.width - x - kf.roles.u(4); wrapMode: Text.WordWrap
                   text: modelData.label; font: kf.roles.fLabel; color: kf.roles.ink3 }
            Text { id: kref; visible: false; text: "0"; font: kf.roles.fFigL; anchors.bottom: parent.bottom; anchors.bottomMargin: kf.roles.u(5) }
            Text { x: col > 0 ? kf.roles.u(12) : 0; anchors.baseline: kref.baseline
                   text: modelData.figure
                   font: modelData.word ? kf.roles.fBody : kf.roles.fFigL
                   color: modelData.warn ? kf.roles.warn : kf.roles.ink }
        }
    }
    Rectangle { y: kf.roles.r.figureRuleTop + kf.gridH; width: parent.width; height: kf.roles.hw; color: kf.roles.hair }
    Text {
        id: foot
        visible: kf.showFoot
        y: kf.roles.r.figureRuleTop + kf.gridH + kf.roles.u(3)
        width: parent.width; wrapMode: Text.WordWrap
        font: kf.roles.fMicro; color: kf.roles.ink3
        text: "Figures in " + (kf.sp.unit || "-") + ".  " + kf.roles.cap1(kf.c.trendText || "") + "."
    }
}
