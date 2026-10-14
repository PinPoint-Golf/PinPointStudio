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

// PPCHKINDCURVE — each kind's curve (design §2.4, §4; Mark's group fit): ONE curve per kind, fitted to the
// pooled valid samples of all its shots with their P-positions lined up (D9), in the metric's hue — the first
// kind solid (ch.kindW), the second dashed (ch.kindDash, α kindDashAlpha) — with ± 1 sd hairlines (no wash;
// ch.spreadEdgeW / spreadEdgeAlpha). It breaks where fewer than 3 shots contribute, a kind under 3 is refused
// with the presenter's sentence, and each curve is labelled with its kind and n. Under it, quietly, each
// shot's own measured stretches (ch.coverRowPitch, coverBar), this shot in ink: what the kind filled. No shot's
// number is read from it. Data: ChartsRelationsPresenter kinds.curves[i].
// objectName: chKindFit:<key>.
Item {
    id: kf
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null
    property var curve: null
    property real plotH: roles ? roles.u(roles.isPrint ? 96 : 110) : 110
    property bool showCover: true
    property bool atom: true
    readonly property var keyKinds: []
    objectName: "chKindFit:" + (curve ? curve.key : "")
    readonly property var fits: curve && curve.fits ? curve.fits : []
    // the presenter's window is null when no fit has a finite value: then nothing is drawn but the kinds'
    // own refusals (the legend column)
    readonly property bool hasWindow: !!curve && !!curve.window
    readonly property real t0: curve && curve.window ? curve.window.t0 : 0
    readonly property real t1: curve && curve.window ? curve.window.t1 : 1
    // the value range: the presenter's (every fit's v ± sd, padded)
    readonly property var yr: curve && curve.yRange ? curve.yRange : ({ lo: 0, hi: 1 })
    readonly property var yt: ticker && curve ? ticker.ticks(yr.lo, yr.hi, 4, curve.unit) : []
    readonly property real gutL: roles ? roles.u(40) : 40
    readonly property real gutR: roles ? roles.u(86) : 86
    readonly property real pTop: roles ? roles.u(18) : 18
    readonly property real pw: Math.max(10, width - gutL - gutR)
    function tx(t) { return gutL + (t - t0) / Math.max(1e-9, t1 - t0) * pw }
    function vy(v) { return pTop + (1 - (v - yr.lo) / Math.max(1e-9, yr.hi - yr.lo)) * plotH }
    readonly property real pitch: roles ? Math.max(3, roles.u(roles.ch.coverRowPitch)) : 5
    readonly property real coverTop: pTop + plotH + (roles ? roles.u(24) : 24)
    readonly property real coverH: showCover && hasWindow && fits.length ? ((fits[0].cover || []).length + (fits.length > 1 ? (fits[1].cover || []).length : 0)) * pitch + (roles ? roles.u(8) : 8) : 0
    implicitHeight: coverTop + coverH + (roles ? roles.u(4) : 4)
    height: implicitHeight

    Repeater {
        model: kf.roles ? kf.yt : []
        delegate: Item {
            required property var modelData
            Rectangle { x: kf.gutL; width: kf.pw; y: Math.round(kf.vy(modelData.v)); height: kf.roles.hw; color: kf.roles.grid; opacity: kf.roles.gridOpacity }
            Text { x: kf.gutL - width - kf.roles.u(5); y: kf.vy(modelData.v) - height / 2; text: modelData.text; font: kf.roles.fFigLabel; color: kf.roles.ink3 }
        }
    }
    Text { text: kf.curve ? (kf.curve.unitText || "") : ""; font: kf.roles ? kf.roles.fLabelUnit : Qt.font({}); color: kf.roles ? kf.roles.ink3 : "grey" }
    Repeater {
        model: kf.roles && kf.curve && kf.hasWindow ? (kf.curve.phases || []).filter(function (p) { return p.tMs >= kf.t0 && p.tMs <= kf.t1 }) : []
        delegate: Item {
            required property var modelData
            readonly property real px: Math.round(kf.tx(modelData.tMs))
            Rectangle { x: parent.px; y: kf.pTop; width: kf.roles.hw; height: kf.plotH; color: kf.roles.grid; opacity: modelData.tag === "P7" ? 1 : 0.55 }
            Text { x: parent.px - width / 2; y: kf.pTop - height - kf.roles.u(2); text: modelData.tag; font: kf.roles.fFigLabel; color: modelData.tag === "P7" ? kf.roles.ink2 : kf.roles.ink3 }
        }
    }
    Text { x: kf.gutL + kf.pw - width; y: kf.pTop + kf.plotH + (kf.roles ? kf.roles.u(5) : 5); text: "time lined up on the P-positions"
           font: kf.roles ? kf.roles.fLabel : Qt.font({}); color: kf.roles ? kf.roles.ink3 : "grey" }
    PpChPlot {
        x: kf.gutL; y: kf.pTop; width: kf.pw; height: kf.plotH
        roles: kf.roles; zeroLine: false
        x0: kf.t0; x1: kf.t1; lo: kf.yr.lo; hi: kf.yr.hi
        layers: {
            if (!kf.roles || !kf.curve || !kf.hasWindow) return []
            const c = kf.roles.ch, col = kf.roles.hue(kf.curve.key), out = []
            for (let i = 0; i < kf.fits.length; ++i) {
                const f = kf.fits[i]
                if (f.refused || !f.v) continue
                const second = i > 0
                // the ± 1 sd hairlines are the presenter's lo / hi (no wash)
                for (const edge of [f.lo, f.hi]) if (edge)
                    out.push({ k: "line", t: f.t, v: edge, color: col, w: c.spreadEdgeW, alpha: c.spreadEdgeAlpha, dash: second ? kf.roles.dash([2, 3]) : [], z: 0 })
                out.push({ k: "line", t: f.t, v: f.v, color: col, w: c.kindW, alpha: second ? c.kindDashAlpha : 1, dash: second ? kf.roles.dash(c.kindDash) : [], z: second ? 1 : 2 })
            }
            return out
        }
    }
    // each kind named at its line end, with its n (or why it has no curve)
    Column {
        x: kf.gutL + kf.pw + (kf.roles ? kf.roles.u(6) : 6); y: kf.pTop
        width: kf.gutR - (kf.roles ? kf.roles.u(6) : 6)
        spacing: kf.roles ? kf.roles.u(6) : 6
        Repeater {
            model: kf.roles ? kf.fits : []
            delegate: Row {
                required property var modelData
                required property int index
                spacing: kf.roles.u(4)
                // the legend line in the curve's own stroke: the first kind solid, the second dashed (ch.kindDash)
                Canvas {
                    anchors.verticalCenter: lt.verticalCenter; width: kf.roles.u(16); height: kf.roles.u(4)
                    readonly property var deps: [kf.curve ? kf.curve.key : "", index, width]
                    onDepsChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        if (!kf.curve) return
                        const c = kf.roles.ch
                        ctx.strokeStyle = Qt.alpha(kf.roles.hue(kf.curve.key), index > 0 ? c.kindDashAlpha : 1)
                        ctx.lineWidth = c.kindW; ctx.setLineDash(index > 0 ? kf.roles.dash(c.kindDash) : [])
                        ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                    }
                }
                Text { id: lt; width: kf.gutR - kf.roles.u(26); wrapMode: Text.WordWrap
                       text: modelData.refused || !kf.hasWindow ? (modelData.refusal || modelData.label) : modelData.label
                       font: kf.roles.fLabel; color: index === 0 ? kf.roles.ink : kf.roles.ink2 }
            }
        }
    }
    // each shot's own measured stretches, quietly: the kind's curve fills the rest
    Repeater {
        model: kf.roles && kf.showCover && kf.hasWindow ? kf.fits : []
        delegate: Item {
            id: cv
            required property var modelData
            required property int index
            readonly property real y0: kf.coverTop + (index === 0 ? 0 : ((kf.fits[0].cover || []).length * kf.pitch + kf.roles.u(8)))
            Text { x: 0; y: cv.y0 - kf.roles.u(2); width: kf.gutL - kf.roles.u(6); horizontalAlignment: Text.AlignRight; text: cv.modelData.name || ""; font: kf.roles.fLabel; color: kf.roles.ink3 }
            Repeater {
                model: cv.modelData.cover || []
                delegate: Item {
                    id: covRow
                    required property var modelData
                    required property int index
                    readonly property bool pk: modelData.picked === true
                    readonly property bool hv: !!kf.brush && kf.brush.hoveredShot === modelData.ordinal
                    y: cv.y0 + index * kf.pitch
                    Repeater {
                        model: covRow.modelData.runs || []
                        delegate: Rectangle {
                            required property var modelData
                            readonly property real a: Math.max(kf.t0, modelData[0])
                            readonly property real b: Math.min(kf.t1, modelData[1])
                            visible: b > a
                            x: Math.round(kf.tx(a)); width: Math.max(1, Math.round(kf.tx(b) - x))
                            y: 0; height: kf.roles.ch.coverBar
                            color: covRow.pk || covRow.hv ? kf.roles.ink : kf.roles.ink3
                        }
                    }
                    MouseArea { x: kf.gutL; width: kf.pw; height: kf.pitch; hoverEnabled: true; enabled: !!kf.brush && !kf.roles.isPrint
                                onContainsMouseChanged: kf.brush.hoverShot(parent.modelData.ordinal, containsMouse) }
                }
            }
        }
    }
}
