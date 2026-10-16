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

// PPCHTRACEFIGURE — This shot's figure (design §2.1, CONTRACT Addendum): the preset's 40 ms windowed-mean
// traces as shared-axis small multiples, or OVERLAID on one shared value axis (F6), on ONE time axis of ms
// from impact over the reader's window. Everything it draws is a field of ChartsTracesPresenter.thisShot /
// .reference / .now: QML maps values to pixels and nothing else.
//   • a row: the metric's curve in its own hue (Theme.metricColor), its raw samples faint behind it (F17); the
//     honesty runs (F18): bridged dashed, outside the metric's phase window dimmed; NaN draws nothing;
//   • peaks with their σ (a timing whisker; the ± is in the figures), the late one ringed in warn plus its
//     word (D4: only the segment that peaked late), an unplaced peak a dim ring, a trunk not peaked by impact
//     the open chevron "rising" at the impact edge (S8);
//   • P dots by verdict (F23) — filled = pass, ring = watch, ring + dot in warn = outside, small ink3 =
//     "position, not judged" — with their key in words above the figure (thisShot.dotKey); toggled (F7);
//   • the playhead, one ink hairline (F24); click or drag seeks through the presenter (F25, scrub bracketed);
//   • the hover crosshair (F26), toggled (F7): one hairline through every row, the time over the P tags, and
//     each row's figure column turned IN PLACE into the reading there ("raw N" beside the mean, "-" where not
//     measured, the ⚠ wrong-structure word) — no floating tooltip (§15.7);
//   • Against (design §2.1): the reference curve in the same hue (median thin, a shot dashed, "like this one"
//     its own dash), and under each trace the difference strip with the largest gap marked.
// The figure column (tier 2; below compact it folds to one line under each plot): Analyse shows the row's
// figures (peak ± σ, order, at P7) and the reading at the playhead with its Δ from Address (F5); Replay shows
// the readings at the playhead ("now").
// objectNames: chTraces, chTrace:<key>, chTraceLabel:<key>, chTraceRaw:<key>, chPeak:<segment>,
// chRising:<segment>, chPlayhead, chSeek, chDiffStrip:<key>, chCrosshair.
Item {
    id: tf
    objectName: "chTraces"
    property QtObject roles: null
    property var brush: null
    property QtObject presenter: null           // ChartsTracesPresenter: hoverAt, seekUsForShotMs
    property bool interactive: true
    property bool seekable: interactive
    property var rows: []                       // thisShot.rows
    property var ticks: []                      // thisShot.ticks (P-positions in the window)
    property var msTicks: []                    // thisShot.msTicks: the window's ms axis, [{ v, text }]
    property real x0: -300                      // thisShot.window.t0Ms
    property real x1: 100                       // thisShot.window.t1Ms
    property bool overlay: false                // F6: one plot, one shared value axis
    property var overlayAxis: null              // thisShot.overlay { yLo, yHi, yTicks, sameUnit }
    property bool showDots: true                // F7
    property bool showCursor: true              // F7: the hover crosshair
    property var reference: null                // the presenter's `reference`
    property var nowRows: []                    // now.rows
    property real playMs: NaN                   // now.shotMs (the playhead on this shot's clock)
    property string playText: ""                // now.when, set over the P tags in Replay
    property string figMode: "figures"          // "figures" (Analyse) | "now" (Replay)
    property real plotH: roles ? roles.u(70) : 70
    property var demoHoverMs: undefined         // renders only: a hover held at this ms
    readonly property var keyKinds: []
    property bool atom: true

    // ── the hover readout: asked of the presenter on a pointer event only (never in a binding) ──
    property var hover: null
    function hoverAtMs(ms) { hover = presenter ? presenter.hoverAt(ms) : null }
    onDemoHoverMsChanged: if (demoHoverMs !== undefined && isFinite(demoHoverMs)) hoverAtMs(demoHoverMs); else hover = null
    readonly property real hoverMs: hover && isFinite(hover.shotMs) ? hover.shotMs : NaN

    readonly property real em: roles ? roles.em : 14
    readonly property real gap: roles ? roles.gutter(width) : 14
    readonly property bool figSide: roles ? roles.shows("figColumn", width) : true
    readonly property real figW: figSide ? Math.round((roles.atLeast(width, "medium") ? roles.flows.figColWide : roles.flows.figCol) * em) : 0
    readonly property real areaW: Math.max(40, width - (figSide ? figW + gap : 0))
    readonly property real yGut: Math.round(3.4 * em)
    readonly property real innerW: Math.max(20, areaW - yGut)
    readonly property bool refOn: !!reference && reference.active === true
    readonly property bool diffOn: refOn && reference.difference === true
    readonly property real diffH: roles ? roles.u(32) : 32
    readonly property real nameBand: roles ? roles.u(17) : 17
    // the time line over the P tags is always reserved: a hover that brought it in would resize the figure
    readonly property real topH: roles ? roles.u(16) + roles.u(14) : 0
    readonly property real footH: roles ? roles.u(30) : 30
    function xOf(t) { return yGut + (t - x0) / Math.max(1e-9, x1 - x0) * innerW }
    function tOf(px) { return x0 + (px - yGut) / Math.max(1, innerW) * (x1 - x0) }
    function refRowOf(key) {
        const rs = refOn ? (reference.rows || []) : []
        for (let i = 0; i < rs.length; ++i) if (rs[i].key === key) return rs[i]
        return null
    }
    function byKey(list, key) { for (let i = 0; i < (list || []).length; ++i) if (list[i].key === key) return list[i]; return null }
    // the reference's stroke: the median thin, a shot dashed, "shots like this one" its own dash (ch roles)
    function refStyle() {
        const c = roles ? roles.ch : ({}), m = reference ? reference.mode : ""
        if (m === "median") return { w: c.medianW, a: c.medianAlpha, dash: [] }
        if (m === "like") return { w: c.likeW, a: 1, dash: roles.dash(c.likeDash) }
        return { w: c.ghostW, a: 1, dash: roles.dash(c.ghostDash) }
    }
    // the layers one row draws (its own and, overlaid, every row's)
    function layersFor(r) {
        if (!roles || !r) return []
        const c = roles.ch, col = roles.hue(r.key), out = []
        // the stretch the route could not see, shaded behind everything — in ink on its own plot, in the
        // curve's hue when the curves are overlaid (each segment's band is its own; they overlap)
        for (const b of (r.blind || []))
            out.push({ k: "span", t0: b.t0, t1: b.t1, color: overlay ? Qt.alpha(col, 0.09) : Qt.alpha(roles.ink, 0.07) })
        if (showRawDots) out.push({ k: "dots", t: r.t, v: r.raw, color: col, alpha: c.rawDotAlpha, d: roles.u(c.rawDot) })
        const rr = refRowOf(r.key)
        if (rr) {
            const st = refStyle()
            out.push({ k: "line", t: rr.t, v: rr.v, color: col, w: st.w, alpha: st.a, dash: st.dash, z: 1 })
            if ((rr.bridges || []).length) out.push({ k: "bridges", list: rr.bridges, color: col, w: st.w, alpha: st.a, z: 1 })
        }
        out.push({ k: "line", t: r.t, v: r.mean, runs: r.runs, color: col, w: c.thisShotW, z: 3 })
        if ((r.bridges || []).length) out.push({ k: "bridges", list: r.bridges, color: col, w: c.thisShotW, z: 3 })
        return out
    }
    property bool showRawDots: true
    // a click on a gate's mark on the curve (a shaded stretch, an unplaced ring): open its reason
    signal trustClicked(string subject)
    // a row's figure column: the hover reading while the pointer is on the figure, else the row's figures
    // (Analyse) and the reading at the playhead with its Δ from Address (F5), and the reference's reading
    // `still` asks for the readings with no hover — what a row is sized from, so a hover never resizes it.
    function itemsFor(r, still) {
        if (!r) return []
        const hv = !still && isFinite(hoverMs) ? byKey(hover.rows, r.key) : null
        if (hv) return [{ label: hover.when || "", text: hv.text, sub: hv.raw || "", warn: false, word: hv.grossWarn ? "⚠" : "" }]
        const out = []
        if (figMode === "figures") for (const f of (r.figures || [])) out.push({ label: f.label, text: f.text, warn: f.warn === true })
        const nw = byKey(nowRows, r.key)
        if (nw) {
            out.push({ label: figMode === "now" ? "now" : "at playhead", text: nw.text, sub: nw.addrText || "" })
            if (refOn && (nw.refText || nw.gapText)) out.push({ label: "against", text: nw.refText || "-", sub: nw.gapText || "" })
        }
        return out
    }
    readonly property var pLines: (ticks || []).map(function (p) { return { t: p.shotMs, strong: p.landmark !== "" } })
    // the ms axis: the presenter's ticks, thinned by width (every other one while they would touch)
    readonly property int xStep: Math.max(1, Math.ceil((msTicks || []).length / Math.max(2, Math.floor(innerW / (5 * em)))))
    readonly property var xTicks: (msTicks || []).filter(function (t, i) { return i % xStep === 0 })
    readonly property int nRows: overlay ? 1 : Math.max(1, (rows || []).length)

    implicitHeight: topH + rowsCol.height + footH
    height: implicitHeight

    // ── the top band: P tags (thinned by width; impact kept), the playhead's or the hover's time ──
    Item {
        width: tf.areaW; height: tf.topH
        Repeater {
            model: tf.roles ? tf.ticks : []
            delegate: Text {
                required property var modelData
                required property int index
                readonly property real cx: tf.xOf(modelData.shotMs)
                // a tag that would touch the one before it gives way (impact never does)
                readonly property bool crowded: index > 0 && Math.abs(tf.xOf(tf.ticks[index - 1].shotMs) - cx) < tf.roles.u(19) && modelData.landmark !== "impact"
                visible: !crowded && cx >= tf.yGut - 2 && cx <= tf.yGut + tf.innerW + 2
                x: Math.max(0, Math.min(tf.areaW - width, cx - width / 2)); y: tf.topH - height - tf.roles.u(1)
                text: modelData.tag; font: tf.roles.fFigLabel
                color: modelData.landmark !== "" ? tf.roles.ink2 : tf.roles.ink3
            }
        }
        Text {
            readonly property bool hov: isFinite(tf.hoverMs) && tf.showCursor
            visible: (hov || tf.playText !== "") && tf.roles !== null
            readonly property real at: hov ? tf.hoverMs : tf.playMs
            x: isFinite(at) ? Math.max(0, Math.min(tf.areaW - width, tf.xOf(at) - width / 2)) : 0; y: 0
            text: hov ? (tf.hover.when || "") : tf.playText
            font: tf.roles ? tf.roles.fFigLabel : Qt.font({}); color: tf.roles ? tf.roles.ink : "black"
            Rectangle { z: -1; anchors.fill: parent; anchors.margins: -2; color: tf.roles ? tf.roles.knock : "transparent" }
        }
    }

    // ── the rows ──
    Column {
        id: rowsCol
        y: tf.topH
        width: tf.width
        Repeater {
            model: tf.roles ? (tf.overlay ? (tf.rows.length ? [{ overlayRow: true }] : []) : tf.rows) : []
            delegate: Item {
                id: rw
                required property var modelData
                required property int index
                readonly property bool ov: modelData.overlayRow === true
                readonly property var r: ov ? null : modelData
                readonly property string key: ov ? "" : modelData.key
                objectName: ov ? "chTrace:overlay" : "chTrace:" + key
                readonly property real ph: ov ? tf.plotH * Math.min(2, Math.max(1, tf.rows.length * 0.6)) : tf.plotH
                readonly property real lo: ov ? (tf.overlayAxis ? tf.overlayAxis.yLo : 0) : r.yLo
                readonly property real hi: ov ? (tf.overlayAxis ? tf.overlayAxis.yHi : 1) : r.yHi
                // the value range is lifted so the curve clears the name band at the top
                readonly property real hiL: hi + (hi - lo) * (tf.nameBand / Math.max(1, ph - tf.nameBand))
                readonly property string unit: ov ? (tf.overlayAxis && tf.overlayAxis.sameUnit && tf.rows.length ? tf.rows[0].unit : "") : r.unit
                // the presenter's ticks (texts included); a short plot keeps every other one
                readonly property var yt0: ov ? (tf.overlayAxis ? (tf.overlayAxis.yTicks || []) : []) : (r.yTicks || [])
                readonly property int ytStep: ph < tf.roles.u(60) ? Math.max(1, yt0.length - 1) : ph < tf.roles.u(90) && yt0.length > 4 ? 2 : 1
                readonly property var yt: yt0.filter(function (t, i) { return (yt0.length - 1 - i) % ytStep === 0 })
                readonly property var rref: ov ? null : tf.refRowOf(key)
                readonly property bool hasDiff: tf.diffOn && !ov && !!rref && rref.diffLo !== null && rref.diffLo !== undefined && isFinite(rref.diffLo)
                readonly property var diffGap: hasDiff && rref.gap && rref.gap.found ? rref.gap : null
                width: tf.width
                height: Math.max(ph, ov && tf.figSide ? figCol.stillH : 0) + (tf.figSide ? 0 : foldMeasure.height + tf.roles.u(4))
                        + (hasDiff ? tf.diffH + tf.roles.u(4) : 0) + tf.roles.u(8)

                PpChPlot {
                    id: plot
                    x: tf.yGut; width: tf.innerW; height: rw.ph
                    roles: tf.roles; brush: tf.brush
                    x0: tf.x0; x1: tf.x1; lo: rw.lo; hi: rw.hiL
                    pTicks: tf.pLines
                    yTicks: rw.yt.map(function (t) { return t.v })
                    layers: rw.ov ? [].concat.apply([], tf.rows.map(function (q) { return tf.layersFor(q) })) : tf.layersFor(rw.r)
                }
                // the value axis' figures, clear of the name band
                Repeater {
                    model: rw.yt
                    delegate: Text {
                        required property var modelData
                        readonly property real yy: plot.yOf(modelData.v)
                        visible: yy > tf.nameBand * 0.7 && yy < rw.ph - 1
                        x: tf.yGut - width - tf.roles.u(5); y: yy - height / 2
                        text: modelData.text; font: tf.roles.fFigLabel; color: tf.roles.ink3
                    }
                }
                // the name inside the plot, on a knock-out: swatch, name, unit (as written, never capitalised)
                Row {
                    id: nameRow
                    objectName: "chTraceLabel:" + rw.key
                    visible: !rw.ov
                    x: tf.yGut + tf.roles.u(4); y: tf.roles.u(1)
                    spacing: tf.roles.u(5)
                    Rectangle { anchors.verticalCenter: nmT.verticalCenter; width: tf.roles.u(tf.roles.ch.swatch); height: tf.roles.u(2); color: rw.ov ? "transparent" : tf.roles.hue(rw.key) }
                    Text { id: nmT; text: rw.r ? rw.r.label : ""; font: tf.roles.fSmallM; color: tf.roles.ink
                           Rectangle { z: -1; anchors.fill: parent; anchors.margins: -2; color: tf.roles.knock } }
                    Text { anchors.baseline: nmT.baseline; text: rw.unit; font: tf.roles.fSmall; color: tf.roles.ink3 }
                }
                // overlaid: each curve named at its line end instead (direct labels), in a column at the right
                Column {
                    visible: rw.ov
                    x: tf.yGut + tf.roles.u(4); y: tf.roles.u(1)
                    Repeater {
                        model: rw.ov ? tf.rows : []
                        delegate: Row {
                            required property var modelData
                            objectName: "chTraceLabel:" + modelData.key
                            spacing: tf.roles.u(5)
                            Rectangle { anchors.verticalCenter: ovT.verticalCenter; width: tf.roles.u(tf.roles.ch.swatch); height: tf.roles.u(2); color: tf.roles.hue(modelData.key) }
                            Text { id: ovT; text: modelData.label; font: tf.roles.fSmall; color: tf.roles.ink
                                   Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tf.roles.knock } }
                            Text { anchors.baseline: ovT.baseline; text: modelData.unit; font: tf.roles.fSmall; color: tf.roles.ink3 }
                        }
                    }
                }
                // ── marks on the curve(s): P dots by verdict, peaks (σ whisker, late ring + word, rising chevron) ──
                Item {
                    x: tf.yGut; width: tf.innerW; height: rw.ph
                    // the shaded stretch's words at its foot ("out of sight"); overlaid, one line per segment,
                    // stacked from the bottom, each over a rule in its hue that spans its band
                    Repeater {
                        model: rw.ov ? [].concat.apply([], tf.rows.map(function (q) {
                                           return (q.blind || []).map(function (b) { return { b: b, key: q.key, who: q.sequence && q.sequence.word ? q.sequence.word : q.label, subject: q.trustSubject || "" } }) }))
                                     : (rw.r.blind || []).map(function (b) { return { b: b, key: rw.key, who: "", subject: rw.r.trustSubject || "" } })
                        delegate: Item {
                            id: bl
                            required property var modelData
                            required property int index
                            objectName: "chBlind:" + modelData.key
                            z: 2                 // its words take a click over the seek area (chSeek); the rest passes through
                            readonly property real xa: Math.max(0, plot.xOf(modelData.b.t0))
                            readonly property real xb: Math.min(tf.innerW, plot.xOf(modelData.b.t1))
                            readonly property string text: (modelData.who ? modelData.who + " " : "") + (modelData.b.label || "")
                            readonly property real lineH: blT.implicitHeight + tf.roles.u(3)
                            visible: xb > xa && !!modelData.b.label
                            x: 0; y: rw.ph - (index + 1) * lineH; width: tf.innerW; height: lineH
                            Rectangle { visible: rw.ov; x: bl.xa; width: bl.xb - bl.xa; y: bl.height - tf.roles.u(2); height: tf.roles.u(2)
                                        color: Qt.alpha(tf.roles.hue(bl.modelData.key), 0.8) }
                            Text {
                                id: blT
                                x: Math.min(bl.xa + tf.roles.u(3), tf.innerW - implicitWidth); y: 0
                                text: bl.text; font: tf.roles.fSmall; color: tf.roles.ink3
                                Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: Qt.alpha(tf.roles.knock, 0.85) }
                                MouseArea { enabled: !!bl.modelData.subject; anchors.fill: parent; anchors.margins: -3
                                            cursorShape: Qt.PointingHandCursor; onClicked: tf.trustClicked(bl.modelData.subject) }
                            }
                        }
                    }
                    Repeater {
                        model: tf.showDots ? (rw.ov ? [].concat.apply([], tf.rows.map(function (q) { return q.dots || [] })) : (rw.r.dots || [])) : []
                        delegate: Item {
                            required property var modelData
                            readonly property real cx: Math.round(plot.xOf(modelData.shotMs))
                            readonly property real cy: Math.round(plot.yOf(modelData.value))
                            readonly property string vd: modelData.verdict || ""
                            readonly property real d: 2 * Math.round(tf.roles.u(vd === "" ? 4 : 6) / 2)
                            visible: modelData.shotMs >= tf.x0 && modelData.shotMs <= tf.x1 && isFinite(modelData.value)
                            // pass: filled ink · watch: an ink ring · outside: a warn ring with a warn dot · not judged: small ink3
                            Rectangle {
                                x: parent.cx - parent.d / 2; y: parent.cy - parent.d / 2; width: parent.d; height: parent.d
                                radius: tf.roles.square ? 0 : parent.d / 2
                                color: parent.vd === "good" ? tf.roles.ink : parent.vd === "" ? tf.roles.ink3 : "transparent"
                                border.width: parent.vd === "attention" || parent.vd === "warn" ? 1.25 : 0
                                border.color: parent.vd === "warn" ? tf.roles.warn : tf.roles.ink
                            }
                            Rectangle {
                                visible: parent.vd === "warn"
                                x: parent.cx - 1.5; y: parent.cy - 1.5; width: 3; height: 3; radius: tf.roles.square ? 0 : 1.5; color: tf.roles.warn
                            }
                        }
                    }
                    Repeater {
                        model: rw.ov ? [].concat.apply([], tf.rows.map(function (q) { return (q.peaks || []).map(function (p) { return { p: p, seg: q.segment } }) }))
                                     : (rw.r.peaks || []).map(function (p) { return { p: p, seg: rw.r.segment } })
                        delegate: Item {
                            id: pk
                            required property var modelData
                            readonly property var p: modelData.p
                            objectName: p.rising ? "chRising:" + modelData.seg : "chPeak:" + modelData.seg
                            z: 2                 // an unplaced ring takes a click over the seek area (chSeek)
                            readonly property real cx: plot.xOf(p.shotMs)
                            readonly property real cy: isFinite(p.value) ? plot.yOf(p.value) : rw.ph / 2
                            readonly property bool onRight: cx + tf.roles.u(8) + lab.implicitWidth + word.implicitWidth <= tf.innerW
                            visible: p.shotMs >= tf.x0 - 1 && p.shotMs <= tf.x1 + 1
                            // the timing σ: a whisker either side of the peak
                            Rectangle {
                                visible: !pk.p.rising && isFinite(pk.p.sigmaMs) && pk.p.sigmaMs > 0 && pk.p.placed
                                x: plot.xOf(pk.p.shotMs - pk.p.sigmaMs); width: Math.max(1, plot.xOf(pk.p.shotMs + pk.p.sigmaMs) - x)
                                y: Math.round(pk.cy) - 0.5; height: 1; color: Qt.alpha(tf.roles.ink2, 0.7)
                            }
                            // the peak: a dot; the late one a warn ring (the one warn mark, D4); unplaced a DASHED dim
                            // ring (trust_and_gates §7.2: a solid hollow ring read as "placed but faint")
                            Canvas {
                                objectName: "chUnplaced:" + pk.modelData.seg
                                visible: !pk.p.rising && !pk.p.placed && !pk.p.late
                                readonly property real d: 2 * Math.round(tf.roles.u(10) / 2)
                                x: Math.round(pk.cx) - d / 2 - 1; y: Math.round(pk.cy) - d / 2 - 1; width: d + 2; height: d + 2
                                readonly property var deps: [d, tf.roles.ink3, tf.roles.square]
                                onDepsChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d"); ctx.reset()
                                    ctx.strokeStyle = tf.roles.ink2; ctx.lineWidth = 1.25
                                    ctx.setLineDash([2, 2])
                                    ctx.beginPath()
                                    if (tf.roles.square) ctx.rect(1, 1, d, d)
                                    else ctx.arc(width / 2, height / 2, d / 2, 0, 2 * Math.PI)
                                    ctx.stroke()
                                }
                                MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                                            readonly property string subject: "ks." + pk.modelData.seg
                                            onClicked: tf.trustClicked(subject) }
                            }
                            Rectangle {
                                visible: !pk.p.rising && (pk.p.placed || pk.p.late)
                                readonly property real d: 2 * Math.round(tf.roles.u(pk.p.late ? 12 : pk.p.placed ? 6 : 9) / 2)
                                x: Math.round(pk.cx) - d / 2; y: Math.round(pk.cy) - d / 2; width: d; height: d
                                radius: tf.roles.square ? 0 : d / 2
                                color: pk.p.late || !pk.p.placed ? "transparent" : tf.roles.ink
                                border.width: pk.p.late ? 1.5 : pk.p.placed ? 1.5 : 1
                                border.color: pk.p.late ? tf.roles.warn : pk.p.placed ? tf.roles.knock : tf.roles.ink3
                                opacity: pk.p.placed ? 1 : 0.7
                            }
                            // a trunk not peaked by impact: the open chevron at the impact edge, no ring (S8)
                            Text {
                                visible: pk.p.rising
                                x: Math.round(pk.cx) - width / 2; y: Math.round(pk.cy) - height / 2
                                text: "›"; font: tf.roles.fBodyM; color: tf.roles.ink2
                            }
                            Text {
                                id: lab
                                readonly property real want: pk.cy - height - tf.roles.u(3)
                                x: pk.onRight ? pk.cx + tf.roles.u(7) : pk.cx - tf.roles.u(7) - implicitWidth - word.implicitWidth - tf.roles.u(3)
                                y: Math.max(tf.nameBand, Math.min(rw.ph - height, want < tf.nameBand ? pk.cy + tf.roles.u(4) : want))
                                text: pk.p.rising ? "" : (pk.p.text || "")
                                font: tf.roles.fFigS; color: pk.p.late ? tf.roles.warn : tf.roles.ink2
                                Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tf.roles.knock; visible: parent.text !== "" }
                            }
                            Text {
                                id: word
                                x: lab.x + lab.implicitWidth + (lab.text !== "" ? tf.roles.u(3) : 0); y: lab.y
                                text: pk.p.rising ? (pk.p.risingWord || "") : pk.p.late ? (pk.p.lateWord || "") : ""
                                font: tf.roles.fSmall; color: pk.p.late ? tf.roles.warn : tf.roles.ink2
                                Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tf.roles.knock; visible: parent.text !== "" }
                            }
                        }
                    }
                    // the playhead: one ink hairline (never orange)
                    Rectangle {
                        objectName: "chPlayhead"
                        visible: isFinite(tf.playMs) && tf.playMs >= tf.x0 && tf.playMs <= tf.x1
                        x: Math.round(plot.xOf(tf.playMs)); width: tf.roles.isPrint ? 0.5 : 1; height: rw.ph
                        color: tf.roles.ink2
                    }
                    // the hover crosshair
                    Rectangle {
                        objectName: "chCrosshair"
                        visible: tf.showCursor && isFinite(tf.hoverMs) && tf.hoverMs >= tf.x0 && tf.hoverMs <= tf.x1
                        x: Math.round(plot.xOf(tf.hoverMs)); width: 1; height: rw.ph
                        color: Qt.alpha(tf.roles.ink, 0.45)
                    }
                    MouseArea {
                        objectName: "chSeek"
                        enabled: tf.interactive
                        anchors.fill: parent
                        hoverEnabled: tf.showCursor
                        cursorShape: tf.seekable ? Qt.SizeHorCursor : Qt.ArrowCursor
                        function seekTo(px) {
                            if (!tf.presenter || typeof shotReplay === "undefined") return
                            const us = tf.presenter.seekUsForShotMs(plot.tOf(Math.max(0, Math.min(width, px))))
                            if (us >= 0) shotReplay.seekToUs(us)
                        }
                        onPressed: (e) => { if (!tf.seekable) return; if (typeof shotReplay !== "undefined") shotReplay.beginScrub(); seekTo(e.x) }
                        onPositionChanged: (e) => {
                            if (pressed && tf.seekable) { seekTo(e.x); return }
                            if (tf.showCursor) tf.hoverAtMs(plot.tOf(e.x))
                        }
                        onReleased: if (tf.seekable && typeof shotReplay !== "undefined") shotReplay.endScrub()
                        onCanceled: if (tf.seekable && typeof shotReplay !== "undefined") shotReplay.endScrub()
                        onExited: if (tf.demoHoverMs === undefined) tf.hover = null
                    }
                }
                // the largest gap from the reference, marked on the plot
                Item {
                    visible: !!rw.diffGap
                    x: tf.yGut; width: tf.innerW; height: rw.ph
                    Rectangle { x: rw.diffGap ? Math.round(plot.xOf(rw.diffGap.shotMs)) : 0; width: 1; height: rw.ph; color: tf.roles.ink3; opacity: 0.8 }
                }
                // ── the figure column (or its folded line): figures / now / the hover reading ──
                Item {
                    id: figCol
                    // { label, text, sub, warn, word, head } — chosen, never computed: every text is the presenter's
                    readonly property var items: {
                        if (rw.ov) {
                            // overlaid: each curve's figures under its own name, in one column
                            let out = []
                            for (const q of tf.rows) out = out.concat([{ label: q.label, text: "", head: true }]).concat(tf.itemsFor(q))
                            return out
                        }
                        return tf.itemsFor(rw.r)
                    }
                    // the column's height with no hover, or with one hover reading per curve if that is taller
                    function hOf(list) { let h = 0; for (const it of list) h += tf.roles.u(it.head ? 18 : 15) + (it.sub ? tf.roles.u(13) : 0); return h }
                    readonly property real stillH: {
                        if (!tf.roles || !tf.figSide || !rw.ov) return 0
                        let h = 0
                        for (const q of tf.rows) h += tf.roles.u(18) + Math.max(hOf(tf.itemsFor(q, true)), tf.roles.u(28))
                        return h
                    }
                    visible: tf.figSide
                    x: tf.width - tf.figW; y: tf.roles.u(1); width: tf.figW; height: figColumn.height
                    Column {
                        id: figColumn
                        width: parent.width
                        Repeater {
                            model: tf.figSide ? figCol.items : []
                            delegate: Item {
                                required property var modelData
                                width: figCol.width; height: tf.roles.u(modelData.head ? 18 : 15) + (modelData.sub ? tf.roles.u(13) : 0)
                                Row {
                                    width: parent.width
                                    Text { id: fl; text: modelData.label; font: modelData.head ? tf.roles.fSmallM : tf.roles.fLabel; color: modelData.head ? tf.roles.ink : tf.roles.ink3; anchors.baseline: fv.baseline }
                                    Item { width: Math.max(tf.roles.u(4), parent.width - fl.implicitWidth - fv.implicitWidth - (fw.visible ? fw.implicitWidth + tf.roles.u(4) : 0)); height: 1 }
                                    Text { id: fv; text: modelData.text; font: tf.roles.fFigS; color: modelData.warn ? tf.roles.warn : tf.roles.ink }
                                    Item { visible: fw.visible; width: tf.roles.u(4); height: 1 }
                                    Text { id: fw; visible: !!modelData.word; anchors.baseline: fv.baseline; text: modelData.word || ""; font: tf.roles.fSmall; color: tf.roles.warn }
                                }
                                Text { visible: !!modelData.sub; anchors.right: parent.right; y: tf.roles.u(14); text: modelData.sub || ""; font: tf.roles.fFigLabel; color: tf.roles.ink3 }
                            }
                        }
                    }
                }
                // the folded line laid out with no hover, unseen: what the row is sized from
                Flow {
                    id: foldMeasure
                    visible: false
                    x: foldFlow.x; width: foldFlow.width
                    spacing: foldFlow.spacing
                    Repeater {
                        // never empty: a hover reading needs its one line even where the row has no readings
                        model: {
                            if (tf.figSide) return []
                            let out = []
                            if (rw.ov) for (const q of tf.rows) out = out.concat([{ label: q.label, text: "" }]).concat(tf.itemsFor(q, true))
                            else out = tf.itemsFor(rw.r, true)
                            return out.length ? out : [{ label: "", text: " ", sub: " " }]
                        }
                        delegate: Row {
                            required property var modelData
                            spacing: tf.roles.u(4)
                            Text { text: modelData.label; font: tf.roles.fLabel }
                            Text { text: modelData.text; font: tf.roles.fFigS }
                            Text { visible: !!modelData.sub; text: modelData.sub || ""; font: tf.roles.fFigLabel }
                            Text { visible: !!modelData.word; text: modelData.word || ""; font: tf.roles.fSmall }
                        }
                    }
                }
                Flow {
                    id: foldFlow
                    visible: !tf.figSide
                    x: tf.yGut; y: rw.ph + tf.roles.u(2); width: tf.width - tf.yGut
                    spacing: tf.roles.u(12)
                    Repeater {
                        model: tf.figSide ? [] : figCol.items
                        delegate: Row {
                            required property var modelData
                            spacing: tf.roles.u(4)
                            Text { text: modelData.label; font: tf.roles.fLabel; color: tf.roles.ink3; anchors.baseline: fv2.baseline }
                            Text { id: fv2; text: modelData.text; font: tf.roles.fFigS; color: modelData.warn ? tf.roles.warn : tf.roles.ink }
                            Text { visible: !!modelData.sub; text: modelData.sub || ""; font: tf.roles.fFigLabel; color: tf.roles.ink3; anchors.baseline: fv2.baseline }
                            Text { visible: !!modelData.word; text: modelData.word || ""; font: tf.roles.fSmall; color: tf.roles.warn; anchors.baseline: fv2.baseline }
                        }
                    }
                }
                // ── the difference strip: this − the reference, symmetric about zero, the largest gap marked ──
                Item {
                    objectName: "chDiffStrip:" + rw.key
                    visible: rw.hasDiff
                    y: rw.ph + (tf.figSide ? 0 : foldMeasure.height + tf.roles.u(4)) + tf.roles.u(4)
                    width: tf.areaW; height: tf.diffH
                    PpChPlot {
                        id: dplot
                        x: tf.yGut; width: tf.innerW; height: parent.height
                        roles: tf.roles; brush: tf.brush
                        x0: tf.x0; x1: tf.x1; lo: rw.hasDiff ? rw.rref.diffLo : -1; hi: rw.hasDiff ? rw.rref.diffHi : 1
                        pTicks: tf.pLines
                        layers: rw.hasDiff ? [{ k: "hline", v: 0, color: Qt.alpha(tf.roles.ink3, 0.8) },
                                              { k: "line", t: rw.rref.t, v: rw.rref.diff, color: tf.roles.hue(rw.key), w: tf.roles.ch.thisShotW * 0.75, z: 2 }] : []
                    }
                    Text { x: tf.yGut + tf.roles.u(4); y: 0; text: tf.reference ? (tf.reference.diffLabel || "") : ""; font: tf.roles.fLabel; color: tf.roles.ink3
                           Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tf.roles.knock } }
                    Text {
                        visible: !!rw.diffGap
                        readonly property real gx: rw.diffGap ? dplot.x + dplot.xOf(rw.diffGap.shotMs) : 0
                        x: Math.max(tf.yGut, Math.min(tf.yGut + tf.innerW - width, gx + tf.roles.u(4))); y: parent.height - height
                        text: rw.diffGap ? rw.diffGap.text : ""; font: tf.roles.fFigLabel; color: tf.roles.ink
                        Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tf.roles.knock }
                    }
                    Rectangle {
                        visible: isFinite(tf.playMs) && tf.playMs >= tf.x0 && tf.playMs <= tf.x1
                        x: dplot.x + Math.round(dplot.xOf(tf.playMs)); width: 1; height: parent.height; color: tf.roles.ink2
                    }
                }
            }
        }
    }

    // ── the foot: ms from impact, once ──
    Item {
        y: tf.topH + rowsCol.height - (tf.roles ? tf.roles.u(6) : 0)
        width: tf.areaW; height: tf.footH
        Repeater {
            model: tf.xTicks
            delegate: Text {
                required property var modelData
                readonly property real cx: tf.xOf(modelData.v)
                visible: cx >= tf.yGut - 1 && cx <= tf.yGut + tf.innerW + 1
                x: Math.max(tf.yGut - width / 2, Math.min(tf.areaW - width, cx - width / 2)); y: 0
                text: modelData.text; font: tf.roles.fFigLabel; color: tf.roles.ink3
            }
        }
        Text { x: tf.yGut; y: tf.roles ? tf.roles.u(14) : 14; text: "ms from impact"; font: tf.roles ? tf.roles.fLabel : Qt.font({}); color: tf.roles ? tf.roles.ink3 : "grey" }
    }
}
