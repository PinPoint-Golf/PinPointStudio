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

#pragma once

// THE SESSION MATHS' SHARED GROUND — one shot's curve as the session maths reads it, and the few
// pieces of arithmetic every other file of it shares (docs/design/charts_panel_traces_relations.md
// §6.2). Pure C++ over the value types in session_series_types.h: no QObject, no QVariant in the
// API, no files, no threads.
//
// The session maths is four files, layered so nothing is computed twice:
//   session_curves.h    (this)  a shot's windowed-mean curve, value-at, grids, type-7 quantiles
//   session_warp.h              P-alignment: canonical P-times, gated anchors, the warp (§4.1)
//   session_traces.h            direction A: envelope, where the curves part, THE group fit (§4),
//                               coverage, shapes, typical shots, gaps
//   session_relations.h         direction B: readings, box stats, r, drift, kinds, facets
//   session_sequence.h          the kinematic sequence across the session
//
// ── TIME, AND WHICH CLOCK A NUMBER IS ON ─────────────────────────────────────────────────────
// Every time in this maths is MILLISECONDS FROM IMPACT, on one of two axes, and every type says
// which:
//   · TimeAxis::ShotMs      — the shot's own clock: (t_us − impactUs) / 1000. A number shown to the
//                             reader is on this axis (design rule 4: "every number shown is unwarped
//                             to the shot's clock"), and shotReplay.seekToUs takes impactUs + 1000·ms.
//   · TimeAxis::CanonicalMs — the session's P-aligned axis (session_warp.h): the shot's clock warped
//                             so its P-positions land on the session's median P-times. Every
//                             CROSS-SHOT computation runs here (design §4.1) and only here.
// Field names carry the axis too (`shotMs`, `canonMs`, `tCanonMs`). Values are NEVER warped: only
// the time axis moves, and nothing is differentiated in warped time.
//
// ── THE CURVE ────────────────────────────────────────────────────────────────────────────────
// `v` is the ENGINE'S windowed mean (src/Metrics/series_reduce.h windowedMeans, the default
// ReduceConfig — the very call ChartMetrics::windowedMean makes, so a session curve is the line the
// single-shot chart draws, bit for bit). It is called directly rather than through ChartMetrics
// because the reduction is already a free, header-only function; ChartMetrics only marshals
// QVariantLists into it. An invalid (bridged) sample's `v` is its RAW value, as the chart draws it,
// and `ok` is 0 there: nothing in the session maths reads a value across an invalid sample.
//
// The short-mask rule is the chart's (chart_metrics.cpp haveMask): a `valid` list is honoured only
// when it covers the whole curve; a shorter one is a malformed document and is ignored wholesale.

#include "session_series_types.h"

#include <QString>

#include <cstdint>
#include <utility>
#include <vector>

namespace pinpoint::charts {

enum class TimeAxis { ShotMs, CanonicalMs };

// One shot's curve of one metric. Empty (t.size() < 2) when the shot has no curve for the key or no
// impact to measure time from.
struct TraceCurve {
    int      ordinal = 0;
    QString  key;
    TimeAxis axis    = TimeAxis::ShotMs;
    std::vector<double>  t;     // ms from impact, on `axis`, ascending
    std::vector<double>  v;     // the windowed mean (the drawn line); the RAW value where !ok
    std::vector<double>  raw;   // the persisted samples
    std::vector<uint8_t> ok;    // 1 = measured (honoured mask and a finite value), 0 = bridged
    double sigma = kNaN;        // the series σ, NaN when the producer characterised none

    bool empty() const { return t.size() < 2; }
};

// The shot's curve of `key` on its own clock (TimeAxis::ShotMs), windowed by the engine.
TraceCurve shotCurve(const ShotSeries &shot, const QString &key);

// The curve's value at time `x` on its own axis: linear between the two samples that bracket x; NaN
// outside the curve, or when either bracketing sample is not measured (the mock's `at`).
double valueAt(const TraceCurve &c, double x);

// x = t0, t0 + step, … while x ≤ t1 + 1e-6, accumulated exactly as the mocks accumulate it (so a
// grid point is the same double on both sides of a golden test).
std::vector<double> timeGrid(double t0, double t1, double step);

// The session panel's plot grid over [t0, t1]: step = max(2 ms, span / 220), the mocks' choice — fine
// enough that a 40 ms windowed mean is drawn smooth, coarse enough that 15 curves × 6 rows is cheap.
std::vector<double> sessionGrid(double t0, double t1);

// Type-7 quantile (R's default, the model's spreadQuantile convention) over the FINITE values; NaN
// when there are none. Takes its argument by value: it sorts a copy.
double quantile7(std::vector<double> values, double q);
inline double medianOf(std::vector<double> values) { return quantile7(std::move(values), 0.5); }

} // namespace pinpoint::charts
