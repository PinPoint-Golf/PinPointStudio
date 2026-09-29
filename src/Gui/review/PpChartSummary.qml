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

// PpChartSummary — the per-window summary cards row: one card per series, each fed by
// ChartMetrics.summaryMasked(series.t_us, series.value, series.valid, winStart, winEnd), where
// the window is the active one CLAMPED to the metric's phase domain. A card shows @impact /
// peak / Δ-segment / peak-rate, with the @impact value tinted by the band of the swing state at
// impact. @impact is the value at the impact landmark (read from the whole series, a fixed
// reference more useful to compare against PEAK than the window edge); peak/Δ/rate stay
// window-scoped, so selecting TOP→IMP shows the downswing's numbers. Recomputes live as the
// segment chips / brush move the window. Pure binding; all stats come from ChartMetrics.
//
// Nothing here shows a number it cannot stand behind (design metric_presentation_honesty.md §5.1):
// a bridged sample is excluded from every reduction, a window the domain clamp emptied prints "—"
// in the three window-scoped tiles, @impact prints "—" where it was not measured at impact, and a
// window with any unmeasured part in it wears a PARTIAL chip.
//
// Since Phase 2 (§5.2) PEAK is a 40 ms windowed-mean extremum and PK RATE a ≥50 ms least-squares
// slope, both from src/Analysis/series_reduce.h — the same reducers the diagnostics engine grades
// with, so a card and a corridor cannot disagree about the same window. PK RATE therefore also has
// an ABSENT state (`rateOk` false: no window long enough, or too few valid samples in it), and it
// prints "—" with its unit hidden rather than a fitted-from-nothing 0. The same for PEAK and Δ on a
// series with no valid sample anywhere (`edgeOk` false), where every window number is a 0 read out
// of nothing rather than a flat curve. PEAK has a third absence of its own (`extremumOk` false): a
// window narrower than the sample spacing contains no measurement to have an extremum, so the tile
// prints "—" rather than the interpolated edge the reducer falls back to.
//
// Since Phase 6 the CHART DRAWS those same windowed means (ChartMetrics.windowedMean, decorated as
// `mean` by PpMetricChart._plottable), so the PEAK tile here is now a point on the line beside it —
// bit-exactly, by construction, and pinned that way in chart_metrics_test. Nothing in this file
// changed for that: it already reduced on those means.
//
// ⚠ AND @IMPACT READS THE MEAN TOO. It was the last reading on this panel taken from a raw sample,
// which after Phase 6 meant the number a coach quotes could differ from the curve under the
// crosshair by the height of a single frame's wobble — one panel, two answers, at the one instant a
// reader trusts most. It is now the drawn value at the impact sample, with the RAW value one hover
// away in its tooltip (same "raw N" the chart's hover row prints). No reading on this panel is
// anything other than the line beside it.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic   // ToolTip on the σ chip
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: root

    // series: [{ key, label, unit, t_us, value, phaseSamples, color,
    //            sigma?, valid?, validFromUs?, validToUs? }]
    // `sigma` is OPTIONAL and its absence is meaningful — see the chip below. So are the last
    // three (design metric_presentation_honesty.md §5.1): `valid` is the per-sample validity mask
    // (0 = bridged across a gated or absent run), and validFromUs/validToUs are the metric's phase
    // domain resolved to instants by the host. Absent ⇒ nothing is masked and nothing is clamped,
    // which is every series that predates the field.
    property var    series:      []
    property real   startUs:     0
    property real   endUs:       0
    property real   impactUs:    -1       // impact instant; the @impact card reads the series here
    // The swing's phase ladder [{phase, t_us}] — every reading, fixed Δ and fixed PEAK span a card's
    // spec names is resolved against it, because only the swing knows when its P4 happened.
    property var    phases:      []
    property string segmentName: ""
    property bool   showHeader:  true     // false when a host SectionHeader labels this

    spacing: Theme.sp(9)

    ChartMetrics   { id: cm }
    TimelineLabels { id: labels }         // value-at-time lookup (impact landmark)

    // ── THE CARD NAMES ITS UNIT ONCE, IN THE HEADER ──────────────────────────────
    //
    // Every value here used to carry the full unit, so a sway card said "% stance width" five
    // times — beside the name, and again on @impact, peak, Δ and the rate. In a 150px column with
    // a data face that is wider than the number it qualifies, and the grid overprinted itself:
    // "12 % stance wi34 %tance w". Two things were wrong and only one of them was the length.
    //
    // The other is placement. Four values in one card share one unit, so the unit is a property of
    // the CARD, not of each number — it belongs beside the name, where it already was, and nowhere
    // else. Values are bare. The same rule strips the unit from the split-mode @end readout, whose
    // gutter names it directly above (PpMetricChart's _fmt / _num note states it in full).
    //
    // ChartMetrics.formatBare keeps the same sign convention formatValue uses, so a reading does
    // not change shape between the card and the legend: degrees carry a leading "+" when positive,
    // nothing else does. _unit is the card's own token — short, and shown in exactly one place.
    function _unit(unit) {
        return cm.shortUnit((unit === undefined || unit === null || unit === "") ? "°" : unit)
    }
    // The series' σ for FORMATTING is ChartMetrics.seriesSigma — one implementation of the
    // absent→0 substitution, in C++ where it can be tested, replacing what used to be a
    // four-clause guard copied into this file, PpMetricChart and PpChartPlot. Resolved once per
    // card (`card.sig`) because a QVariantMap argument marshals the whole series.

    // "" ⇒ NO VERDICT, tinted like any other unlabelled value. This used to fall through to
    // colorGood, and combined with bandAtNearest's old "good" default that meant a series with no
    // phaseSample anywhere near impact showed its @impact reading in PASS GREEN — a grade invented
    // from an empty list. A missing verdict is not a good one.
    function _bandColor(b) {
        return b === "warn"      ? Theme.colorWarn
             : b === "attention" ? Theme.colorAttention
             : b === "good"      ? Theme.colorGood
             :                     Theme.colorText
    }
    // WHICH ARRAY IS THE CURVE (Phase 6): `mean` — the 40 ms centred windowed mean the chart strokes
    // and summaryMasked reduces — where the host decorated one (PpMetricChart._plottable), else the
    // persisted `value`. The same one-line rule as PpChartPlot._meanOf, PpMetricChart._meanOf and
    // PpSegmentBrush._meanOf: this component takes a `series` list from anywhere and may not assume
    // another prepared it. The predicate is asked by name rather than by comparing the returned array
    // against s.value, so no drawing or display decision rests on object identity across the bridge.
    //
    // ⚠ THE REDUCERS ARE STILL FED RAW. summaryMasked below gets `value`, never this — reducing a
    // reduction would window the curve twice and no tile would mean what its definition says.
    function _hasMean(s) {
        return !!(s && s.mean && s.t_us && s.mean.length === s.t_us.length)
    }
    function _meanOf(s) {
        return root._hasMean(s) ? s.mean : s.value
    }

    // The measured-at-an-instant test, in JS for the reason chart_metrics.h gives: cm.measuredAt
    // marshals the whole series per call, and these bindings re-evaluate as the window moves.
    // Same rule, same short-mask discipline (a mask that does not cover the curve is discarded).
    //
    // ⚠ isFinite IS FOLDED IN (F5), which is why the nearest sample is now found before the
    // short-mask early return rather than after it: a series with NO mask must still not print a NaN
    // as a reading — `formatBare` on one renders "nan", in the band colour, as the card's headline
    // number. Same fold as PpChartPlot._measured and SeriesView::isValid, for the same reason:
    // nothing in the pipeline should produce a NaN, which is exactly why it cannot be assumed.
    function _measuredAt(s, t) {
        if (s.validFromUs !== undefined && s.validToUs !== undefined
            && s.validToUs > s.validFromUs && (t < s.validFromUs || t > s.validToUs))
            return false
        var tt = s.t_us
        if (!tt || tt.length === 0) return true
        var best = -1, bd = Infinity
        for (var i = 0; i < tt.length; ++i) {
            var d = Math.abs(tt[i] - t)
            if (d < bd) { bd = d; best = i }
        }
        if (best < 0) return true
        if (s.value && !isFinite(s.value[best])) return false
        if (!s.valid || s.valid.length < tt.length) return true
        return s.valid[best] !== 0
    }

    // The card's window: the active window CLAMPED to this metric's phase domain, because the
    // reducers must search only where the geometry means something (design §5.1). A pelvis-sway
    // peak found after impact is a reading of the pelvis TURNING, not of it sliding, and it was
    // beating the real peak on the corpus.
    //
    // ORDERED: the end is floored at the start, so a window entirely past the domain collapses to
    // a point at the domain's end rather than inverting — summaryMasked swaps an inverted pair,
    // which would turn the clamp into a window over exactly the region it was removing.
    // ⚠ The same two lines live in PpMetricChart.qml (the split-mode @end readout); keep them equal.
    // A phase's instant on this swing, or -1 when the ladder lacks it (the tile then prints "—").
    function _phaseUs(phase) {
        var ps = root.phases || []
        for (var i = 0; i < ps.length; ++i) if (ps[i].phase === phase) return ps[i].t_us
        return -1
    }

    function _winStart(s) {
        return Math.max(root.startUs, s.validFromUs !== undefined ? s.validFromUs : root.startUs)
    }
    function _winEnd(s) {
        return Math.max(root._winStart(s),
                        Math.min(root.endUs, s.validToUs !== undefined ? s.validToUs : root.endUs))
    }

    // Header — "SUMMARY · <segment>" + a hairline rule.
    RowLayout {
        visible: root.showHeader
        Layout.fillWidth: true
        spacing: Theme.sp(9)
        Text {
            text: qsTr("SUMMARY") + (root.segmentName ? " · " + root.segmentName : "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colorBorder }
    }

    // Cards — equal-width columns, wrapping to the available width.
    //
    // ── EVERY CARD IS THE SAME SHAPE ─────────────────────────────────────────────────
    //
    // Cards used to size to their own content, so one card wearing a PARTIAL chip, or one whose
    // PEAK had no ± line, stood taller or shorter than its neighbours and its values sat on a
    // different baseline — a row of four that read as four unrelated widgets. Three rules keep
    // the row one shape: every card fills its grid row's height; every optional line (PARTIAL,
    // the two ±) RESERVES its space and is merely hidden when it has nothing to say; and every
    // cell of the 2×2 grid is top-aligned, so "@ IMPACT" and "PEAK" share a label line even
    // though PEAK carries a ± beneath it. The PARTIAL line is reserved only when some card in
    // the row wears one (`anyPartial`), so a row with no caveat carries no blank line.
    //
    // ⚠ `Layout.fillHeight: false` IS LOAD-BEARING. A nested layout fills by default, and this
    // component sits in PpMetricChart's column beside the plot, so any spare height in the panel
    // reached the grid — and, once the cards fill their row, stretched every card to hundreds of
    // pixels of empty border. The grid is exactly as tall as its tallest card, and no taller.
    //
    // THE MINIMUM CARD WIDTH (190) is what the 2×2 grid needs to print its labels whole: at 150 px a
    // six-card row (Pelvis & lateral) cut "Δ SEGMENT" to "Δ SEGM…" on every card. Fewer, wider
    // columns — wrapping to a second row — beats a label that has to be guessed.
    GridLayout {
        id: grid
        Layout.fillWidth: true
        Layout.fillHeight: false
        columnSpacing: Theme.sp(10); rowSpacing: Theme.sp(10)
        columns: Math.max(1, Math.min(root.series.length,
                                      Math.floor((grid.width + grid.columnSpacing)
                                                 / (Theme.sp(190) + grid.columnSpacing))))

        readonly property bool anyPartial: {
            for (var i = 0; i < cards.count; ++i) {
                var c = cards.itemAt(i)
                if (c && c.partial) return true
            }
            return false
        }

        Repeater {
            id: cards
            model: root.series
            delegate: Rectangle {
                id: card
                required property var modelData

                // ── WHAT THIS CARD SHOWS (MetricDescriptor::card) ───────────────────────────
                //
                // Not the same four tiles for every metric any more: metric_descriptor.h's
                // MetricCardSpec says why — shaft lean has no PEAK worth printing, past parallel is
                // read at the top, balance at the finish, forward bend as its address→impact loss.
                // `spec` is resolved once per card; the default is the old card exactly.
                readonly property var    spec: cm.cardSpecFor(card.modelData.key)

                // The WINDOW-SCOPED statistics: the active window CLAMPED to the metric's phase
                // domain. Used by every tile the spec leaves window-scoped (PEAK without a span, Δ
                // without a span, PK RATE); a fixed-span tile reduces its own span below.
                readonly property real   winStartUs: root._winStart(card.modelData)
                readonly property real   winEndUs:   root._winEnd(card.modelData)
                // ⚠ `reduceValid`, NOT `valid` (F4): the host composes `valid` AND in-domain into one
                // mask (PpMetricChart._reduceMask) and every reduction on the panel is given that
                // same one, so a tile never differs from the line the chart drew. Falls back to
                // `valid` for a caller that has not composed one.
                readonly property var    mask: card.modelData.reduceValid || card.modelData.valid || []
                readonly property var    st:  cm.summaryMasked(card.modelData.t_us, card.modelData.value,
                                                               card.mask, card.winStartUs, card.winEndUs)
                // THE DOMAIN CLAMP EMPTIED THE WINDOW: the selected span lies wholly outside where
                // this metric means anything, so every window tile prints "—" rather than a Δ of
                // 0.0 and a rate of 0 read off a single instant. Guarded on a non-empty selection
                // so a chart that has not sized its window yet is not called empty.
                readonly property bool   collapsed: card.winEndUs <= card.winStartUs
                                                    && root.endUs > root.startUs
                // `edgeOk` false: the series has no valid sample anywhere, so every window number
                // is 0.0 out of nothing. `!== false` (not `=== true`): a map from a C++ that never
                // heard of the key must not blank every card.
                readonly property bool   valueOk: card.st.edgeOk !== false && !card.collapsed
                // PK RATE needs a ≥50 ms window with ≥3 valid samples (Phase 2); `=== true` so an
                // older map without the key prints no rate that was never fitted.
                readonly property bool   rateOk: card.st.rateOk === true && !card.collapsed
                // PEAK's own gate (F2): no valid sample INSIDE the window means the extremum came
                // from the interpolated edges — between measurements, not from any.
                readonly property bool   winPeakOk: card.valueOk && card.st.extremumOk !== false

                // A FIXED PEAK SPAN (e.g. Connection over Address→Top, Lead knee over Impact→P8):
                // reduced by the same summaryMasked over the span's own two instants, whatever
                // window is selected. {} when the swing lacks either phase.
                readonly property real   pkFromUs: card.spec.peakSpan ? root._phaseUs(card.spec.peakFrom) : -1
                readonly property real   pkToUs:   card.spec.peakSpan ? root._phaseUs(card.spec.peakTo)   : -1
                readonly property var    stSpan: (card.pkFromUs >= 0 && card.pkToUs > card.pkFromUs)
                                                 ? cm.summaryMasked(card.modelData.t_us, card.modelData.value,
                                                                    card.mask, card.pkFromUs, card.pkToUs)
                                                 : ({})
                readonly property bool   spanPeakOk: card.stSpan.edgeOk !== undefined
                                                     && card.stSpan.edgeOk !== false
                                                     && card.stSpan.extremumOk !== false

                // "" = NO VERDICT: bandAtNearest refuses to answer a frame or more from any
                // phaseSample, rather than defaulting to "good" off nothing at all.
                readonly property string nm:  cm.shortLabel(card.modelData.key)
                                              || card.modelData.label || card.modelData.key
                // This series' measurement noise, resolved ONCE: it governs the digits of every
                // READING (ChartMetrics.displayStep) and is what the header chip quotes. It governs
                // the readings and nothing else — every ± on the card is QUOTED, not quantised.
                readonly property real   sig: cm.seriesSigma(card.modelData)

                // ── A READING AT AN INSTANT ─────────────────────────────────────────────────
                //
                // The drawn curve's value (the 40 ms windowed mean, Phase 6 — so the tile is a point
                // on the line beside it) at the phase's instant, GATED on having been measured
                // there: valueAtNearest snaps unconditionally, and a bridged value printed as a
                // headline number in the band colour is the failure §5.1 exists to prevent. The raw
                // sample rides along for the tooltip. The @ IMPACT reading keeps its old fallback,
                // the window's @end, for a series with no impact landmark.
                function reading(phase) {
                    var us = root._phaseUs(phase)
                    if (us < 0 && phase === 5 && root.impactUs > 0) us = root.impactUs
                    if (us < 0) {
                        return phase === 5 && card.valueOk
                               ? { ok: true, val: card.st.end, us: root.endUs, raw: "" }
                               : { ok: false, val: 0, us: -1, raw: "" }
                    }
                    var ok = root._measuredAt(card.modelData, us)
                    var v  = labels.valueAtNearest(card.modelData.t_us, root._meanOf(card.modelData), us)
                    var raw = root._hasMean(card.modelData)
                              ? cm.formatBare(labels.valueAtNearest(card.modelData.t_us,
                                                                    card.modelData.value, us),
                                              card.modelData.unit, card.sig)
                              : ""
                    return { ok: ok, val: v, us: us, raw: raw }
                }
                function tag(phase) {
                    var t = labels.phaseShortTag(phase)
                    return t !== "" ? t : labels.phaseFullName(phase)
                }
                function readLabel(r) {
                    if (r.label) return r.label
                    // The four landmark names read better than their tags ("@ TOP", not "@ P4");
                    // every other position is its P-tag, because "@ SHAFT-PARALLEL THROUGH" does
                    // not fit a cell.
                    var name = labels.phaseFullName(r.phase)
                    return "@ " + (name.length <= 7 ? name.toUpperCase() : card.tag(r.phase))
                }
                function fmt(v) { return cm.formatBare(v, card.modelData.unit, card.sig) }

                // ── THE TILES, IN SPEC ORDER: readings, PEAK, Δ, PK RATE ────────────────────
                // Each: { label, text, ok, color, sub, unit, tip, window }. `sub` is the ± line and
                // is "" where the tile has none; `window` marks the tiles the PARTIAL chip speaks for.
                readonly property var tiles: {
                    var out = [], sp = card.spec, i
                    var rs = sp.readAt || []
                    for (i = 0; i < rs.length; ++i) {
                        var r = card.reading(rs[i].phase)
                        out.push({ label: card.readLabel(rs[i]),
                                   text: r.ok ? card.fmt(r.val) : "—",
                                   ok: r.ok,
                                   color: r.ok ? root._bandColor(cm.bandAtNearest(
                                                     card.modelData.phaseSamples, r.us))
                                               : Theme.colorText3,
                                   sub: "", unit: "", window: false,
                                   tip: (r.ok && r.raw !== "")
                                        ? qsTr("Drawn value (40 ms windowed mean). "
                                               + "Recorded sample there: raw %1.").arg(r.raw)
                                        : "" })
                    }
                    if (sp.peak) {
                        // ± is summaryMasked's peakSigma — the SE of the winning 40 ms mean about a
                        // local line — QUOTED at one decimal, never quantised to the value's step.
                        var span = sp.peakSpan
                        var pOk  = span ? card.spanPeakOk : card.winPeakOk
                        var ps   = span ? card.stSpan : card.st
                        out.push({ label: span ? qsTr("PEAK %1→%2").arg(card.tag(sp.peakFrom))
                                                                    .arg(card.tag(sp.peakTo))
                                               : qsTr("PEAK"),
                                   text: pOk ? card.fmt(ps.peak) : "—", ok: pOk,
                                   color: pOk ? Theme.colorText : Theme.colorText3,
                                   sub: pOk ? cm.formatUncertainty(ps.peakSigma) : "",
                                   unit: "", tip: "", window: !span })
                    }
                    if (sp.delta) {
                        // NO ± on a Δ (design §5.3 as pinned in C12): the reducers produce no error
                        // for a difference of two edges, and this file does not invent one.
                        if (sp.deltaSpan) {
                            var a = card.reading(sp.deltaFrom), b = card.reading(sp.deltaTo)
                            var dOk = a.ok && b.ok
                            out.push({ label: sp.deltaLabel
                                              || ("Δ " + card.tag(sp.deltaFrom) + "→" + card.tag(sp.deltaTo)),
                                       text: dOk ? card.fmt(b.val - a.val) : "—", ok: dOk,
                                       color: dOk ? Theme.colorText : Theme.colorText3,
                                       sub: "", unit: "", tip: "", window: false })
                        } else {
                            out.push({ label: sp.deltaLabel || qsTr("Δ SEGMENT"),
                                       text: card.valueOk ? card.fmt(card.st.delta) : "—",
                                       ok: card.valueOk,
                                       color: card.valueOk ? Theme.colorText : Theme.colorText3,
                                       sub: "", unit: "", tip: "", window: true })
                        }
                    }
                    if (sp.rate) {
                        // THE MAGNITUDE of a signed slope — this tile has always answered "how
                        // fast, at its fastest" — and NOT put through the σ step rule: σ is in the
                        // metric's unit and this is per 100 ms. Its ± is rateSigma, quoted. Unit
                        // hidden with the value: "— °/100ms" claims a rate that was never fitted.
                        out.push({ label: qsTr("PK RATE"),
                                   text: card.rateOk ? String(Math.round(Math.abs(card.st.rate))) : "—",
                                   ok: card.rateOk,
                                   color: card.rateOk ? Theme.colorText : Theme.colorText3,
                                   sub: card.rateOk ? cm.formatUncertainty(card.st.rateSigma) : "",
                                   unit: card.rateOk ? root._unit(card.modelData.unit) + qsTr("/100ms") : "",
                                   tip: "", window: true })
                    }
                    return out
                }

                // PARTIAL speaks for the WINDOW: part of it was never measured (summaryMasked says
                // so) or the domain clamp emptied it. A card with no window-scoped tile has nothing
                // for it to qualify — its fixed readings print "—" for themselves when unmeasured —
                // and so never wears it.
                readonly property bool   partial: {
                    for (var i = 0; i < card.tiles.length; ++i)
                        if (card.tiles[i].window) return card.st.partial === true || card.collapsed
                    return false
                }

                Layout.fillWidth: true
                Layout.fillHeight: true             // every card as tall as its row
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 1            // equal columns
                implicitHeight: cardCol.implicitHeight + Theme.sp(22)
                radius: Theme.sp(10)
                color: Theme.colorBg
                border.width: 1; border.color: Theme.colorBorder
                clip: true

                Rectangle {                          // series colour edge
                    width: Theme.sp(3); height: parent.height
                    color: card.modelData.color
                }

                ColumnLayout {
                    id: cardCol
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              leftMargin: Theme.sp(13); rightMargin: Theme.sp(11)
                              topMargin: Theme.sp(11) }
                    spacing: Theme.sp(10)

                    RowLayout {                       // name + unit + σ
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: card.nm; elide: Text.ElideRight
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                            color: Theme.colorText
                        }
                        Text {
                            text: root._unit(card.modelData.unit)
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingData
                            color: Theme.colorText3
                        }
                        // Measurement NOISE on this series, when its producer characterised one —
                        // one number for the whole curve, so it sits by the unit and not on each
                        // tile. Absent ⇒ nothing drawn: uncharacterised is not exact. QUOTED, not
                        // quantised (σ is the number that SET the step; quantising it is circular).
                        Text {
                            id: sigmaChip
                            readonly property real sigma: card.sig
                            visible: sigmaChip.sigma > 0
                            text: cm.formatUncertainty(sigmaChip.sigma,
                                                       root._unit(card.modelData.unit))
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingData
                            color: Theme.colorText3
                            HoverHandler { id: sigmaHover }
                            ToolTip.visible: sigmaHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: qsTr("Frame-to-frame measurement noise on this curve. "
                                               + "Not the overall accuracy of the reading.")
                        }
                    }

                    // PARTIAL — part of the window was never measured. Styled like the σ chip (a
                    // caveat, not an error). ITS OWN LINE, and RESERVED whenever any card in the row
                    // is partial, so the tile grids below stay level across the row.
                    Text {
                        visible: grid.anyPartial
                        opacity: card.partial ? 1 : 0
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("PARTIAL")
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color: Theme.colorText3
                        HoverHandler { id: partialHover; enabled: card.partial }
                        ToolTip.visible: partialHover.hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Part of this window had no valid measurement.")
                    }

                    // ── THE 2×2 TILE GRID — ALWAYS FOUR CELLS ────────────────────────────────
                    //
                    // A card with fewer tiles (past parallel has one) still lays out four cells,
                    // the unused ones transparent, so every card in a row is one shape. Every cell
                    // is the same three lines — label, value, ± — with the ± line reserved when
                    // empty, and top-aligned. Every cell is a LAYOUT with fillWidth + elide, so a
                    // long value truncates at its own boundary instead of drawing over the next
                    // cell (the plain-Column overprint this grid once had).
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.sp(12); rowSpacing: Theme.sp(9)

                        Repeater {
                            model: 4
                            delegate: ColumnLayout {
                                id: cell
                                required property int index
                                readonly property var tile: cell.index < card.tiles.length
                                                            ? card.tiles[cell.index] : null
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignTop
                                opacity: cell.tile ? 1 : 0
                                spacing: 0
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: cell.tile ? cell.tile.label : " "
                                       font.family: Theme.fontData
                                       font.pixelSize: Theme.fontSzMicro
                                       font.letterSpacing: Theme.trackingData
                                       color: Theme.colorText3 }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.sp(3)
                                    // The value elides too: once the unit has collapsed to its
                                    // ellipsis nothing else can give way. The UNIT is the half that
                                    // gives way first — the number is the reading.
                                    // NOT fillWidth: two fillWidth items split the row evenly,
                                    // which cut "1164" to "11…" beside a unit with room to spare.
                                    // The number takes what it needs (capped at the cell), and the
                                    // unit — fillWidth — gets the rest.
                                    Text { id: valText
                                           Layout.alignment: Qt.AlignBaseline
                                           Layout.maximumWidth: cell.width
                                           elide: Text.ElideRight
                                           text: cell.tile ? cell.tile.text : " "
                                           font.family: Theme.fontData
                                           font.pixelSize: Theme.fontSzData
                                           color: cell.tile ? cell.tile.color : Theme.colorText3
                                           HoverHandler { id: valHover }
                                           ToolTip.visible: valHover.hovered && !!cell.tile
                                                            && cell.tile.tip.length > 0
                                           ToolTip.delay: 400
                                           ToolTip.text: cell.tile ? cell.tile.tip : "" }
                                    Text { Layout.fillWidth: true; elide: Text.ElideRight
                                           Layout.alignment: Qt.AlignBaseline
                                           visible: !!cell.tile && cell.tile.unit.length > 0
                                           text: cell.tile ? cell.tile.unit : ""
                                           font.family: Theme.fontData
                                           font.pixelSize: Theme.fontSzMicro
                                           color: Theme.colorText3 }
                                }
                                // The ± line — RESERVED when empty (opacity, not visible) so a
                                // reading without one sits level with a PEAK that has one.
                                Text { Layout.fillWidth: true; elide: Text.ElideRight
                                       text: (cell.tile && cell.tile.sub) ? cell.tile.sub : "±"
                                       opacity: (cell.tile && cell.tile.sub) ? 1 : 0
                                       font.family: Theme.fontData
                                       font.pixelSize: Theme.fontSzMicro
                                       font.letterSpacing: Theme.trackingData
                                       color: Theme.colorText3 }
                            }
                        }
                    }
                }
            }
        }
    }
}
