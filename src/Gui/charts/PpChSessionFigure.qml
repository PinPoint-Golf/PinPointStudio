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

// PPCHSESSIONFIGURE — Session's figure (design §2.2, §4): every shot's curve for each metric of the preset,
// P-ALIGNED (D9: the session's P-positions, no clock-time view), as shared-axis small multiples. Drawn from
// ChartsTracesPresenter.session only; QML maps values to pixels.
//   • band "middle": the middle half as the metric's hue at envAlpha and the median (weight, not a 2nd hue);
//     "every": each other shot the same hue at otherShotAlpha (hover one and it lights in every row);
//     "fit" (D2): the group's curve (long dash) with ± 1 sd as the wash, blank where < 3 shots contribute,
//     refused (with the presenter's reason) for a group < 3, and under the plot the coverage strip: one
//     hairline per shot where it was measured, this shot first in ink;
//   • this shot at full strength in its own group's panel, with its honesty runs;
//   • where the curves part (the presenter's part.canonMs) as a dotted rule;
//   • facets side by side while a panel keeps 22 em (ch.facetEm), else OVERLAID: this shot's group, the
//     other group's median / fit dashed (panel.alt) — a diagram below its minimum changes form (rule 8);
//   • the P tags along the top and, once under the figure, the middle half of WHEN each P-position happened
//     across the shots in ms from impact (ticks[].when: the timing the warp removed, kept quietly);
//   • the playhead at the picked shot's canonical time (now.canonMs); click or drag seeks the picked shot;
//     over a shot's curve (every shot) the pointer lights that shot, and a click puts it in the carousel.
// objectNames: chSessionFigure, chEnvelope:<key>, chFit:<key>, chCoverage:<key>, chPartAt.
Item {
    id: sf
    objectName: "chSessionFigure"
    property QtObject roles: null
    property var brush: null
    property QtObject presenter: null           // ChartsTracesPresenter: seekUsFor, shotAt
    property bool interactive: true
    property var session: null                  // tracesP.session
    property var nowRows: []                    // now.rows (Replay: now against typical)
    property real playCanon: NaN                // now.canonMs
    property string figMode: "figures"          // "figures" | "now"
    property int pickedOrdinal: 0
    property real plotH: roles ? roles.u(70) : 70
    readonly property var keyKinds: []
    property bool atom: true
    signal shotActivated(int ordinal)

    readonly property var rows: session && session.rows ? session.rows : []
    readonly property var groups: session && session.groups ? session.groups : []
    readonly property string band: session ? (session.band || "middle") : "middle"
    readonly property real x0: session ? session.t0 : 0
    readonly property real x1: session ? session.t1 : 1
    readonly property var pTicks: session && session.ticks ? session.ticks : []
    readonly property real em: roles ? roles.em : 14
    readonly property real gap: roles ? roles.gutter(width) : 14
    readonly property bool figSide: roles ? roles.shows("figColumn", width) : true
    readonly property real figW: figSide ? Math.round((roles.atLeast(width, "medium") ? roles.flows.figColWide : roles.flows.figCol) * em) : 0
    readonly property real areaW: Math.max(40, width - (figSide ? figW + gap : 0))
    readonly property real yGut: Math.round(3.4 * em)
    readonly property int nPanels: groups.length
    // side by side while each panel keeps its tile minimum; else overlaid on this shot's group
    readonly property bool side: nPanels > 1 && roles && (areaW - (nPanels - 1) * gap) / nPanels >= roles.flows.facet * em
    readonly property int cols: side ? nPanels : 1
    readonly property real panelW: (areaW - (cols - 1) * gap) / Math.max(1, cols)
    readonly property real innerW: Math.max(20, panelW - yGut)
    readonly property real nameBand: roles ? roles.u(17) : 17
    readonly property bool hasHeads: nPanels > 1
    readonly property real topH: roles ? (hasHeads ? roles.u(18) : 0) + roles.u(16) : 0
    readonly property real coverPitch: roles ? roles.ch.coverPitch : 1.5
    function covH(r) { return band === "fit" && r.coverage && r.coverage.length ? r.coverage.length * coverPitch + roles.u(6) : 0 }
    function ownIndex() { for (let i = 0; i < groups.length; ++i) if (groups[i].own) return i; return 0 }
    function byKey(list, key) { for (let i = 0; i < (list || []).length; ++i) if (list[i].key === key) return list[i]; return null }
    function panelOf(r, group) { const ps = r.panels || []; for (let i = 0; i < ps.length; ++i) if (ps[i].group === group) return ps[i]; return null }
    readonly property var pLines: pTicks.map(function (p) { return { t: p.canonMs, strong: p.tag === "P7" } })

    // the layers one panel draws: the metric's hue at the ch weights and alphas, never a second hue
    function layersFor(r, p, withPicked, withAlt) {
        if (!roles || !p) return []
        const c = roles.ch, col = roles.hue(r.key), out = []
        if (band === "fit") {
            const f = p.fit || {}
            if (!f.refused && f.v) {
                out.push({ k: "band", t: p.t, lo: f.lo, hi: f.hi, color: roles.hueA(r.key, roles.envAlpha) })
                out.push({ k: "line", t: p.t, v: f.v, color: col, w: c.fitW, dash: roles.dash(c.fitDash), z: 2 })
            }
        } else {
            if (band === "middle" && p.q1) out.push({ k: "band", t: p.t, lo: p.q1, hi: p.q3, color: roles.hueA(r.key, roles.envAlpha) })
            if (band === "every") for (const o of (p.others || [])) {
                out.push({ k: "line", t: p.t, v: o.v, color: col, w: 0.8, alpha: c.otherShotAlpha, shot: o.ordinal, z: 0 })
                if ((o.bridges || []).length) out.push({ k: "bridges", list: o.bridges, color: col, w: 0.8, alpha: c.otherShotAlpha, shot: o.ordinal, z: 0 })
            }
            if (p.med) out.push({ k: "line", t: p.t, v: p.med, color: col, w: c.medianW, alpha: c.medianAlpha, z: 1 })
        }
        if (withAlt && p.alt && p.alt.v)
            out.push({ k: "line", t: p.t, v: p.alt.v, color: col, w: c.otherFacetW, dash: roles.dash(c.otherFacetDash), z: 1 })
        if (withPicked && r.picked && r.picked.t) {
            out.push({ k: "line", t: r.picked.t, v: r.picked.v, runs: r.picked.runs, color: col, w: c.thisShotW, z: 3, hover: false })
            if ((r.picked.bridges || []).length) out.push({ k: "bridges", list: r.picked.bridges, color: col, w: c.thisShotW, z: 3 })
        }
        if (band !== "fit" && r.part && r.part.found)
            out.push({ k: "vline", t: r.part.canonMs, color: roles.ink3, dash: [roles.u(2), roles.u(2.5)] })
        return out
    }

    implicitHeight: topH + rowsCol.height + (roles ? roles.u(34) : 0) + cap.height + whenFlow.height + (roles ? roles.u(6) : 0)
    height: implicitHeight

    // ── facet heads and the P tags, once, over each panel ──
    Repeater {
        model: sf.roles ? sf.cols : 0
        delegate: Item {
            id: hd
            required property int index
            readonly property var g: sf.side ? sf.groups[index] : sf.groups[sf.ownIndex()]
            x: index * (sf.panelW + sf.gap); width: sf.panelW; height: sf.topH
            Row {
                visible: sf.hasHeads
                x: sf.yGut; spacing: sf.roles.u(8)
                Text { id: gh; text: hd.g ? hd.g.label : ""; font: sf.roles.fSmallM; color: sf.roles.ink2 }
                // overlaid: the other group is the dashed line, named once
                Text { visible: !sf.side && sf.nPanels > 1; text: "dashed:"; font: sf.roles.fLabel; color: sf.roles.ink3; anchors.baseline: gh.baseline }
                Repeater {
                    model: !sf.side ? sf.groups.filter(function (q) { return !q.own }) : []
                    delegate: Text { required property var modelData; text: modelData.label; font: sf.roles.fSmall; color: sf.roles.ink3 }
                }
            }
            Repeater {
                model: sf.pTicks
                delegate: Text {
                    required property var modelData
                    required property int index
                    readonly property real cx: sf.yGut + (modelData.canonMs - sf.x0) / Math.max(1e-9, sf.x1 - sf.x0) * sf.innerW
                    readonly property real prevX: index > 0 ? sf.yGut + (sf.pTicks[index - 1].canonMs - sf.x0) / Math.max(1e-9, sf.x1 - sf.x0) * sf.innerW : -1e9
                    visible: (cx - prevX >= sf.roles.u(19) || modelData.tag === "P7") && cx >= sf.yGut - 2 && cx <= sf.yGut + sf.innerW + 2
                    x: Math.max(0, Math.min(sf.panelW - width, cx - width / 2)); y: sf.topH - height - sf.roles.u(1)
                    text: modelData.tag; font: sf.roles.fFigLabel; color: modelData.tag === "P7" ? sf.roles.ink2 : sf.roles.ink3
                }
            }
        }
    }

    Column {
        id: rowsCol
        y: sf.topH
        width: sf.width
        Repeater {
            model: sf.roles ? sf.rows : []
            delegate: Item {
                id: rw
                required property var modelData
                readonly property var r: modelData
                objectName: "chEnvelope:" + r.key
                readonly property real lo: r.yLo
                readonly property real hi: r.yHi + (r.yHi - r.yLo) * (sf.nameBand / Math.max(1, sf.plotH - sf.nameBand))
                // the presenter's ticks (texts included); a short plot keeps every other one
                readonly property var yt0: r.yTicks || []
                readonly property int ytStep: sf.plotH < sf.roles.u(60) ? Math.max(1, yt0.length - 1) : sf.plotH < sf.roles.u(90) && yt0.length > 4 ? 2 : 1
                readonly property var yt: yt0.filter(function (t, i) { return (yt0.length - 1 - i) % ytStep === 0 })
                readonly property real cov: sf.covH(r)
                readonly property var nw: sf.byKey(sf.nowRows, r.key)
                readonly property var figs: sf.figMode === "now" && nw
                                            ? [{ label: "now", text: nw.text }, { label: "typical", text: nw.typicalText }]
                                            : (r.figures || [])
                width: sf.width
                height: sf.plotH + cov + (sf.figSide ? 0 : foldFlow.height + sf.roles.u(2)) + sf.roles.u(8)

                Repeater {
                    model: sf.cols
                    delegate: Item {
                        id: pn
                        required property int index
                        readonly property var g: sf.side ? sf.groups[index] : sf.groups[sf.ownIndex()]
                        readonly property var p: g ? sf.panelOf(rw.r, g.key) : null
                        readonly property bool own: !!g && g.own === true
                        objectName: sf.band === "fit" ? "chFit:" + rw.r.key : ""
                        x: index * (sf.panelW + sf.gap); width: sf.panelW; height: sf.plotH
                        PpChPlot {
                            id: plot
                            x: sf.yGut; width: sf.innerW; height: sf.plotH
                            roles: sf.roles; brush: sf.brush
                            x0: sf.x0; x1: sf.x1; lo: rw.lo; hi: rw.hi
                            pTicks: sf.pLines
                            yTicks: rw.yt.map(function (t) { return t.v })
                            layers: sf.layersFor(rw.r, pn.p, pn.own, !sf.side)
                        }
                        Repeater {
                            model: pn.index === 0 || sf.side ? rw.yt : []
                            delegate: Text {
                                required property var modelData
                                readonly property real yy: plot.yOf(modelData.v)
                                visible: yy > sf.nameBand * 0.7 && yy < sf.plotH - 1 && (pn.index === 0)
                                x: sf.yGut - width - sf.roles.u(5); y: yy - height / 2
                                text: modelData.text; font: sf.roles.fFigLabel; color: sf.roles.ink3
                            }
                        }
                        Row {
                            visible: pn.index === 0
                            x: sf.yGut + sf.roles.u(4); y: sf.roles.u(1); spacing: sf.roles.u(5)
                            Rectangle { anchors.verticalCenter: snm.verticalCenter; width: sf.roles.u(sf.roles.ch.swatch); height: sf.roles.u(2); color: sf.roles.hue(rw.r.key) }
                            Text { id: snm; text: rw.r.label; font: sf.roles.fSmallM; color: sf.roles.ink
                                   Rectangle { z: -1; anchors.fill: parent; anchors.margins: -2; color: sf.roles.knock } }
                            Text { anchors.baseline: snm.baseline; text: rw.r.unit; font: sf.roles.fSmall; color: sf.roles.ink3 }
                        }
                        // a refused fit says why, in the panel (never a curve read for a group under 3)
                        Text {
                            visible: sf.band === "fit" && !!pn.p && !!pn.p.fit && pn.p.fit.refused === true
                            x: sf.yGut + sf.roles.u(6); y: sf.plotH / 2 - height / 2; width: sf.innerW - sf.roles.u(12); wrapMode: Text.WordWrap
                            text: pn.p && pn.p.fit ? (pn.p.fit.why || "") : ""; font: sf.roles.fSmall; color: sf.roles.ink3
                        }
                        Text {
                            objectName: "chPartAt"
                            visible: sf.band !== "fit" && !!rw.r.part && rw.r.part.found && pn.index === 0
                            readonly property real px: rw.r.part ? sf.yGut + plot.xOf(rw.r.part.canonMs) : 0
                            x: px + width + sf.roles.u(6) > sf.panelW ? px - width - sf.roles.u(3) : px + sf.roles.u(3)
                            y: sf.plotH - height - sf.roles.u(1)
                            text: rw.r.part ? (rw.r.part.when || "") : ""; font: sf.roles.fFigLabel; color: sf.roles.ink3
                            Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: sf.roles.knock }
                        }
                        Rectangle {
                            visible: pn.own && isFinite(sf.playCanon) && sf.playCanon >= sf.x0 && sf.playCanon <= sf.x1
                            x: sf.yGut + Math.round(plot.xOf(sf.playCanon)); width: 1; height: sf.plotH; color: sf.roles.ink2
                        }
                        MouseArea {
                            enabled: sf.interactive
                            x: sf.yGut; width: sf.innerW; height: sf.plotH
                            hoverEnabled: sf.band === "every"
                            cursorShape: sf.brush && sf.brush.hoveredShot > 0 ? Qt.PointingHandCursor : Qt.SizeHorCursor
                            property var hit: null
                            function seekTo(px) {
                                if (!sf.presenter || typeof shotReplay === "undefined" || sf.pickedOrdinal <= 0) return
                                const us = sf.presenter.seekUsFor(sf.pickedOrdinal, plot.tOf(Math.max(0, Math.min(width, px))))
                                if (us >= 0) shotReplay.seekToUs(us)
                            }
                            onPositionChanged: (e) => {
                                if (pressed && !hit) { seekTo(e.x); return }
                                if (sf.band !== "every" || !sf.presenter || !sf.brush) return
                                const v = rw.lo + (1 - e.y / height) * (rw.hi - rw.lo)
                                const s = sf.presenter.shotAt(rw.r.key, plot.tOf(e.x), v)
                                hit = s && s.found && s.ordinal !== sf.pickedOrdinal ? s : null
                                sf.brush.hoveredShot = hit ? hit.ordinal : -1
                            }
                            onExited: { hit = null; if (sf.brush && sf.band === "every") sf.brush.hoveredShot = -1 }
                            onPressed: (e) => { if (hit) return; if (typeof shotReplay !== "undefined") shotReplay.beginScrub(); seekTo(e.x) }
                            onReleased: { if (hit) sf.shotActivated(hit.ordinal); else if (typeof shotReplay !== "undefined") shotReplay.endScrub() }
                            onCanceled: if (!hit && typeof shotReplay !== "undefined") shotReplay.endScrub()
                        }
                    }
                }
                // the coverage strip (the group fit's payoff): one hairline per shot where it was measured
                Canvas {
                    objectName: "chCoverage:" + rw.r.key
                    visible: rw.cov > 0
                    x: sf.yGut; y: sf.plotH + sf.roles.u(3); width: sf.innerW; height: Math.max(1, rw.cov - sf.roles.u(3))
                    readonly property var deps: [rw.r.coverage, width, sf.x0, sf.x1, sf.roles.ink, sf.brush ? sf.brush.hoveredShot : -1]
                    onDepsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        const cov = rw.r.coverage || [], R = sf.roles
                        for (let i = 0; i < cov.length; ++i) {
                            const row = cov[i], y = Math.round(i * sf.coverPitch) + 0.5
                            const hov = sf.brush && sf.brush.hoveredShot === row.ordinal
                            ctx.strokeStyle = row.picked || hov ? R.ink : Qt.alpha(R.ink3, 0.75)
                            ctx.lineWidth = row.picked ? 1.2 : 0.8
                            ctx.beginPath()
                            for (const run of (row.runs || [])) {
                                const a = Math.max(sf.x0, run.t0), b = Math.min(sf.x1, run.t1)
                                if (b <= a) continue
                                const xa = (a - sf.x0) / (sf.x1 - sf.x0) * width, xb = (b - sf.x0) / (sf.x1 - sf.x0) * width
                                ctx.moveTo(xa, y); ctx.lineTo(Math.max(xa + 1, xb), y)
                            }
                            ctx.stroke()
                        }
                    }
                }
                Text { visible: rw.cov > 0; x: sf.yGut - width - sf.roles.u(5); y: sf.plotH; text: "shots"; font: sf.roles.fLabel; color: sf.roles.ink3 }
                // the figure column, or its folded line
                Column {
                    visible: sf.figSide
                    x: sf.width - sf.figW; y: sf.roles.u(1); width: sf.figW
                    Repeater {
                        model: sf.figSide ? rw.figs : []
                        delegate: Item {
                            required property var modelData
                            width: sf.figW; height: sf.roles.u(15)
                            Text { text: modelData.label; font: sf.roles.fLabel; color: sf.roles.ink3; anchors.baseline: sv.baseline }
                            Text { id: sv; anchors.right: parent.right; text: modelData.text; font: sf.roles.fFigS; color: sf.roles.ink }
                        }
                    }
                }
                Flow {
                    id: foldFlow
                    visible: !sf.figSide
                    x: sf.yGut; y: sf.plotH + rw.cov + sf.roles.u(2); width: sf.width - sf.yGut
                    spacing: sf.roles.u(12)
                    Repeater {
                        model: sf.figSide ? [] : rw.figs
                        delegate: Row {
                            required property var modelData
                            spacing: sf.roles.u(4)
                            Text { text: modelData.label; font: sf.roles.fLabel; color: sf.roles.ink3; anchors.baseline: fv.baseline }
                            Text { id: fv; text: modelData.text; font: sf.roles.fFigS; color: sf.roles.ink }
                        }
                    }
                }
            }
        }
    }
    // ── the foot: the canonical ms axis (session.msTicks), the axis caption, then when each P-position
    // happened across the shots (the timing the warp removed) ──
    readonly property var msTicks: session && session.msTicks ? session.msTicks : []
    readonly property int xStep: Math.max(1, Math.ceil(msTicks.length / Math.max(2, Math.floor(innerW / (5 * em)))))
    Repeater {
        model: sf.roles ? sf.cols : 0
        delegate: Item {
            required property int index
            x: index * (sf.panelW + sf.gap); y: sf.topH + rowsCol.height - sf.roles.u(6); width: sf.panelW; height: sf.roles.u(14)
            Repeater {
                model: sf.msTicks.filter(function (t, i) { return i % sf.xStep === 0 })
                delegate: Text {
                    required property var modelData
                    readonly property real cx: sf.yGut + (modelData.v - sf.x0) / Math.max(1e-9, sf.x1 - sf.x0) * sf.innerW
                    visible: cx >= sf.yGut - 1 && cx <= sf.yGut + sf.innerW + 1
                    x: Math.max(sf.yGut - width / 2, Math.min(sf.panelW - width, cx - width / 2))
                    text: modelData.text; font: sf.roles.fFigLabel; color: sf.roles.ink3
                }
            }
        }
    }
    Text {
        id: cap
        y: sf.topH + rowsCol.height + (sf.roles ? sf.roles.u(10) : 0)
        x: sf.yGut; width: sf.areaW - sf.yGut; wrapMode: Text.WordWrap
        text: (sf.session && sf.session.axisCaption) ? sf.session.axisCaption : ""
        font: sf.roles ? sf.roles.fLabel : Qt.font({}); color: sf.roles ? sf.roles.ink3 : "grey"
    }
    Flow {
        id: whenFlow
        y: cap.y + cap.height + (sf.roles ? sf.roles.u(4) : 0)
        x: sf.yGut; width: sf.areaW - sf.yGut
        spacing: sf.roles ? sf.roles.u(12) : 12
        Text { visible: sf.pTicks.length > 0; text: "When, middle half"; font: sf.roles ? sf.roles.fLabel : Qt.font({}); color: sf.roles ? sf.roles.ink3 : "grey" }
        Repeater {
            model: sf.roles ? sf.pTicks.filter(function (p) { return !!p.when }) : []
            delegate: Row {
                required property var modelData
                spacing: sf.roles.u(4)
                Text { text: modelData.tag; font: sf.roles.fFigLabel; color: sf.roles.ink2 }
                Text { text: modelData.when; font: sf.roles.fFigLabel; color: sf.roles.ink3 }
            }
        }
    }
}
