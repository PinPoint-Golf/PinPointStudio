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

// PPCHSCATTER — one reading against another (ChartsRelationsPresenter SCATTER; design §2.3): a mark per shot
// (ink: a mark stands for a SHOT; warn + a tick where the facet's condition fired on it), this shot ringed,
// each axis' session box on its edge in the reading's hue, the axis titles in their hues, and the least-
// squares line: ink2 and solid when it clears chance, ink3 dashed (ch.chanceDash) inside chance. The caption
// is the presenter's ("r .62 (.17 to .86) · n 15 · inside chance"). Ranges are shared across facet panels
// (the presenter gives the same xRange / yRange to every panel). Ticks are the presenter's.
// objectName: chScatter.
Item {
    id: sc
    objectName: "chScatter"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null
    property var scatter: null
    // the readings' metric keys, for their hues (the presenter's; a caller may override)
    property string xKey: scatter && scatter.xKey ? scatter.xKey : ""
    property string yKey: scatter && scatter.yKey ? scatter.yKey : ""
    property string title: ""
    property bool interactive: false
    property bool showMarginals: true
    property bool showYLabels: true
    property bool showAxisNames: true
    property bool showCaption: true
    property real plotH: roles ? roles.u(roles.isPrint ? 150 : 170) : 170
    property bool atom: true
    readonly property var keyKinds: ["ring", "fit", "chance", "box"]
    signal shotActivated(int ordinal)
    readonly property bool ok: !!scatter && !!scatter.xRange && !!scatter.yRange
    readonly property var xR: ok ? scatter.xRange : ({ lo: 0, hi: 1 })
    readonly property var yR: ok ? scatter.yRange : ({ lo: 0, hi: 1 })
    readonly property var pts: ok ? (scatter.points || []) : []
    readonly property real gutL: roles ? roles.u(showYLabels ? 40 : 6) : 40
    readonly property real mW: showMarginals && roles ? roles.u(18) : 0
    readonly property real pTop: roles ? roles.u(title !== "" || showAxisNames ? 20 : 4) : 4
    readonly property real pw: Math.max(10, width - gutL - mW - (roles ? roles.u(showMarginals ? 8 : 2) : 8))
    readonly property real ph: plotH
    function px(v) { return gutL + (v - xR.lo) / Math.max(1e-9, xR.hi - xR.lo) * pw }
    function py(v) { return pTop + (1 - (v - yR.lo) / Math.max(1e-9, yR.hi - yR.lo)) * ph }
    readonly property var xt: ticker && ok && roles ? ticker.ticks(xR.lo, xR.hi, Math.max(2, Math.min(5, Math.floor(pw / (roles.em * 4.5)))), scatter.xUnit) : []
    readonly property var yt: ticker && ok && roles ? ticker.ticks(yR.lo, yR.hi, ph > roles.u(120) ? 4 : 3, scatter.yUnit) : []
    // the marginal boxes read the shown shots' values by ordinal (index = ordinal − 1)
    function byOrdinal(field) {
        const out = []
        for (const p of pts) out[p.ordinal - 1] = p[field]
        return out
    }
    function firedByOrdinal() { const out = []; for (const p of pts) out[p.ordinal - 1] = p.fired === true; return out }
    readonly property real capH: showCaption && roles ? capT.height + roles.u(4) : 0
    implicitHeight: pTop + ph + (showMarginals ? mW + (roles ? roles.u(4) : 4) : 0) + (roles ? roles.u(showAxisNames ? 36 : 20) : 36) + capH
    height: implicitHeight

    Text {
        visible: text !== ""
        x: sc.title !== "" ? sc.gutL : 0; width: sc.width - x; wrapMode: Text.WordWrap
        text: sc.title !== "" ? sc.title : (sc.showAxisNames && sc.ok ? sc.scatter.yLabel : "")
        font: sc.title !== "" ? (sc.roles ? sc.roles.fSmallM : Qt.font({})) : (sc.roles ? sc.roles.fLabelUnit : Qt.font({}))
        color: !sc.roles ? "black" : sc.title !== "" ? sc.roles.ink : (sc.yKey ? sc.roles.hue(sc.yKey) : sc.roles.ink3)
    }
    Rectangle { visible: !!sc.roles; x: sc.gutL; y: sc.pTop; width: sc.pw; height: sc.ph; color: "transparent"; border.width: sc.roles ? sc.roles.hw : 1; border.color: sc.roles ? sc.roles.hair : "grey" }
    Repeater {
        model: sc.roles ? sc.yt : []
        delegate: Item {
            required property var modelData
            Rectangle { x: sc.gutL; width: sc.pw; y: Math.round(sc.py(modelData.v)); height: sc.roles.hw; color: sc.roles.grid; opacity: sc.roles.gridOpacity }
            Text { visible: sc.showYLabels; x: sc.gutL - width - sc.roles.u(5); y: sc.py(modelData.v) - height / 2; text: modelData.text; font: sc.roles.fFigLabel; color: sc.roles.ink3 }
        }
    }
    Repeater {
        model: sc.roles ? sc.xt : []
        delegate: Item {
            required property var modelData
            Rectangle { y: sc.pTop; height: sc.ph; x: Math.round(sc.px(modelData.v)); width: sc.roles.hw; color: sc.roles.grid; opacity: sc.roles.gridOpacity }
            Text { x: sc.px(modelData.v) - width / 2; y: sc.pTop + sc.ph + sc.mW + sc.roles.u(sc.showMarginals ? 6 : 4); text: modelData.text; font: sc.roles.fFigLabel; color: sc.roles.ink3 }
        }
    }
    Text {
        visible: sc.showAxisNames && sc.ok
        anchors.right: parent.right; anchors.rightMargin: sc.mW + (sc.roles ? sc.roles.u(8) : 8)
        y: sc.pTop + sc.ph + sc.mW + (sc.roles ? sc.roles.u(20) : 20)
        width: Math.min(implicitWidth, sc.pw); horizontalAlignment: Text.AlignRight; wrapMode: Text.WordWrap
        text: sc.ok ? sc.scatter.xLabel : ""
        font: sc.roles ? sc.roles.fLabelUnit : Qt.font({}); color: sc.roles && sc.xKey ? sc.roles.hue(sc.xKey) : "grey"
    }
    // the least-squares line: solid ink2 clear of chance, ink3 dashed inside it
    Canvas {
        anchors.fill: parent
        readonly property var ln: sc.ok ? sc.scatter.line : null
        readonly property var deps: [ln, sc.width, sc.xR, sc.yR, sc.ph, sc.roles ? sc.roles.ink2 : "grey", sc.ok ? sc.scatter.clear : false]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            if (!sc.roles || !ln) return
            const clear = sc.scatter.clear === true
            ctx.strokeStyle = clear ? sc.roles.ink2 : sc.roles.ink3
            ctx.lineWidth = sc.roles.u(sc.roles.ch.fitLineW)
            ctx.setLineDash(clear ? [] : sc.roles.dash(sc.roles.ch.chanceDash))
            ctx.save(); ctx.beginPath(); ctx.rect(sc.gutL, sc.pTop, sc.pw, sc.ph); ctx.clip()
            ctx.beginPath(); ctx.moveTo(sc.px(ln.x0), sc.py(ln.y0)); ctx.lineTo(sc.px(ln.x1), sc.py(ln.y1)); ctx.stroke()
            ctx.restore()
        }
    }
    Repeater {
        model: sc.roles ? sc.pts : []
        delegate: Item {
            required property var modelData
            PpMark {
                id: mk
                roles: sc.roles; brush: sc.brush; interactive: sc.interactive
                shotId: modelData.ordinal; fired: modelData.fired === true
                cx: Math.round(sc.px(modelData.x) * 2) / 2; cy: Math.round(sc.py(modelData.y) * 2) / 2
                onActivated: sc.shotActivated(modelData.ordinal)
            }
            // a fired mark keeps its tick under it: warn is a mark plus a mark, never colour alone
            Rectangle {
                visible: modelData.fired === true
                width: sc.roles.u(sc.roles.ch.firedTickW); height: sc.roles.u(sc.roles.ch.firedTickH)
                x: mk.cx - width / 2; y: mk.cy + sc.roles.markFired / 2 + 1; color: sc.roles.warn
            }
        }
    }
    PpChBoxCell {
        visible: sc.showMarginals && sc.ok
        vertical: true; markScale: 0.85
        x: sc.gutL + sc.pw + (sc.roles ? sc.roles.u(8) : 8); y: sc.pTop; width: sc.mW; height: sc.ph
        lo: sc.yR.lo; hi: sc.yR.hi
        roles: sc.roles; brush: sc.brush
        box: sc.ok ? sc.scatter.yBox : null; values: sc.byOrdinal("y"); fired: sc.firedByOrdinal()
        color: sc.roles && sc.yKey ? sc.roles.hue(sc.yKey) : "grey"
    }
    PpChBoxCell {
        visible: sc.showMarginals && sc.ok
        markScale: 0.85
        x: sc.gutL; y: sc.pTop + sc.ph + (sc.roles ? sc.roles.u(3) : 3); width: sc.pw; height: sc.mW
        lo: sc.xR.lo; hi: sc.xR.hi
        roles: sc.roles; brush: sc.brush
        box: sc.ok ? sc.scatter.xBox : null; values: sc.byOrdinal("x"); fired: sc.firedByOrdinal()
        color: sc.roles && sc.xKey ? sc.roles.hue(sc.xKey) : "grey"
    }
    Text {
        id: capT
        visible: sc.showCaption && sc.ok
        y: sc.height - height
        width: sc.width; wrapMode: Text.WordWrap
        text: sc.ok ? (sc.scatter.caption || "") : ""
        font: sc.roles ? sc.roles.fSmall : Qt.font({}); color: sc.roles ? sc.roles.ink2 : "grey"
    }
}
