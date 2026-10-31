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
.pragma library

// The chart's sample rules — "was this sample MEASURED", "which sample is nearest", "which array
// is the curve" — read by PpChartPlot, PpSegmentBrush, PpMetricChart and PpChartSummary. The ONE
// QML place they are written (C3); each of those four used to carry its own copy with a comment
// asking it to be kept equal to the others. Pure: each function reads only the `series` entry it
// is handed ({ t_us, value, valid?, validFromUs?, validToUs?, mean? }), so a component taking a
// `series` list from anywhere can ask without assuming another component prepared it.
//
// ChartMetrics::measuredAt (chart_metrics.h) is the C++ reference for the same rule at an
// arbitrary instant. It stays the reference, but every call into it copies the whole series across
// the QML boundary, so anything that re-evaluates with the cursor, the playhead or the view window
// answers it here instead — a few comparisons off an index, no marshalling.

// A phase domain is set: the metric means something only between validFromUs and validToUs.
function hasDomain(s) {
    return s.validFromUs !== undefined && s.validToUs !== undefined
           && s.validToUs > s.validFromUs
}

// THE INDEX FORM IS THE REAL ONE. Sample `i` counts as measured when it is inside the metric's
// phase domain, its value is finite, and it is not marked 0 in `valid`. No sample (i < 0) is not
// a measurement.
//
// Short-mask rule, shared with the C++ (chart_metrics.h) and with measure_sample.cpp: a `valid`
// list that does not cover the curve is a malformed document and is discarded WHOLESALE, not
// applied to the prefix it covers — two views of one curve must not disagree about it.
//
// ⚠ A NON-FINITE VALUE IS NOT A MEASUREMENT EITHER (F5), the same fold SeriesView::isValid makes
// on the C++ side and for the same reason: nothing in the pipeline should produce a NaN, which is
// exactly why it must not be trusted as an invariant. It is also a rendering matter — one NaN in a
// ShapePath's point list takes out the WHOLE run, so a single bad sample would silently erase a
// series' trace, its dots or its ribbon rather than showing a gap where it is — and a reading
// matter: formatBare on a NaN prints "nan" as a card's headline number.
function measured(s, i) {
    if (i < 0) return false
    var tt = s.t_us
    if (hasDomain(s) && (tt[i] < s.validFromUs || tt[i] > s.validToUs))
        return false
    if (s.value && !isFinite(s.value[i])) return false
    if (!s.valid || s.valid.length < (tt ? tt.length : 0)) return true
    return s.valid[i] !== 0
}

// Nearest sample index to `t`, or -1 on an empty curve — what the per-frame callers need so they
// can use the index form above. The same rule labels.valueAtNearest applies, without the
// marshalling.
function nearestIndex(s, t) {
    var tt = s.t_us
    if (!tt || tt.length === 0) return -1
    var best = -1, bd = Infinity
    for (var i = 0; i < tt.length; ++i) {
        var d = Math.abs(tt[i] - t)
        if (d < bd) { bd = d; best = i }
    }
    return best
}

// Measured at the instant `t`? A series with no curve answers true — the caller's own "is there
// one" test decides what to show. ⚠ The domain is judged on the NEAREST SAMPLE'S time, not on `t`
// (chart_metrics.cpp measuredAt says why: a domain end snapped to a frame 2.9 ms before the impact
// instant was refusing the reading AT impact of that very frame).
function measuredAt(s, t) {
    var i = nearestIndex(s, t)
    return i < 0 || measured(s, i)
}

// WHICH ARRAY IS THE CURVE (Phase 6): `mean` — the 40 ms centred windowed mean the chart's traces
// stroke and the summary tiles reduce — where the host decorated one (PpMetricChart._plottable),
// the persisted `value` otherwise. A "which array" choice rather than arithmetic, so it stays in
// JS: putting it in C++ would marshal a whole series to answer a question about an array's length.
//
// The predicate is separate from the getter, and asked by name rather than by comparing the
// returned array against s.value: an array identity test would be answering "is there a reduction
// here" with a question about object lifetimes across the QML/C++ boundary, which is not something
// a drawing or display decision should depend on.
//
// ⚠ THE FALLBACK IS NOT A SILENT DOWNGRADE. It fires only for a caller that never called
// windowedMean, or a curve whose arrays disagree in length — which _plottable already refuses to
// plot. In both cases the honest line is the persisted one.
function hasMean(s) {
    return !!(s && s.mean && s.t_us && s.mean.length === s.t_us.length)
}
function meanOf(s) {
    return hasMean(s) ? s.mean : s.value
}
