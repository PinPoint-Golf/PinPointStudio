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

// THE PHASE GRID'S VALUE TYPES (OBJ-PHASE-GRID; platform refactor P-3, R-1): a swing reduced to every metric's value at
// each segmented phase, the spans between them and the millisecond spans a pack names, with each reading's σ. Moved
// unchanged from the sampler (src/Diagnostics/measures/measure_sample.h), which builds, reduces and serialises them; here
// so the Recording's reader contract (RecordingReader::phaseGrid, src/DomainModel/recording_reader.h) and its callers
// name the grid without including the sampler. Value types only: the three lookups are inline, no function needs the
// sampler's object file.
//
// Vocabulary ring.

#include "swing_analysis.h"                 // Phase
#include "gate_record.h"                    // GateRecord — why a reading is absent (schema 8)
#include "sigma_source.h"                   // SigmaSource
#include "../Common/pp_tuned_constants.h"  // tuned::sampler, tuned::reduce

#include <QString>
#include <QtGlobal>

#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

// A SPAN IN MILLISECONDS AFTER A PHASE that the grid is asked to answer (metric_reducer.h `windowMs`,
// plane stack D5). A phase-bounded window is exact by aggregation over the adjacent-phase spans; this
// one is not, so its extremes and its two endpoint medians are computed at build time for each span a
// caller names. THE PACK NAMES THEM (msWindowsOf), and that is the one way this cache becomes
// pack-aware — narrowly: the grid stores the windows it was built for, a reader whose windows are a
// SUBSET of those accepts it, and a reader asking for more rebuilds with the UNION (readPhaseGrid), so
// two readers with different packs cannot keep overwriting each other's sidecar. A reader that names
// none (every chart, the gate counts) accepts any grid.
struct PhaseGridMsWindow {
    Phase anchor  = Phase::Impact;
    int   startMs = 0;
    int   endMs   = 0;
};
inline bool operator==(const PhaseGridMsWindow &a, const PhaseGridMsWindow &b)
{
    return a.anchor == b.anchor && a.startMs == b.startMs && a.endMs == b.endMs;
}

struct PhaseGridConfig {
    // ±15 ms about the phase instant, the WristAngleSampler convention. Producers that stamp a
    // phase sample equal to what this window grades (shaftdir::assemble in src/Club/shaft_direction.h,
    // the 3-D body and trunk-sensor assemblies) read the same constant; if this ever becomes
    // configurable per grid, they must be handed the configured value or graded and stamped drift.
    int64_t windowHalfUs    = tuned::sampler::kWindowHalfUs;
    int     minValidSamples = 1;       // fewer in the window => this phase has no value

    // The centred window a span's extreme is the MEAN over. A one-sample outlier cannot be a peak
    // because a peak has to be there for the whole window; 40 ms is ≈5 samples where the pose grid
    // is dense and ≈2 where it is sparse (design §5.2, tuned::reduce).
    int64_t extremumWindowUs = tuned::reduce::kExtremumWindowUs;

    // The millisecond spans to answer (PhaseGridMsWindow). NOT part of sameGridReduction: it adds spans
    // and moves no number the grid already holds, so it is a subset test, not an equality (loadPhaseGrid).
    std::vector<PhaseGridMsWindow> msWindows;

    // A millisecond span reads only when its valid samples COVER it: no stretch longer than this unread,
    // the span's ends included (tuned::reduce::kMsSpanMaxGapUs, D5 round 3). Applied when a span is READ,
    // from the stored gap, so it is not part of sameGridReduction: a cached grid serves any limit.
    int64_t msSpanMaxGapUs = tuned::reduce::kMsSpanMaxGapUs;
};

// Whether two configurations would grid the same swing to the same numbers.
//
// Every field that MOVES A NUMBER is compared — the two windows and the sample floor — because they
// are what a sidecar's stored numbers were computed AT: a grid built at ±15 ms and served to a run
// configured for ±8 ms is a wrong answer carrying a matching size+mtime guard, which is the one
// failure mode a cache must not have. `minValidSamples` is in here rather than left out as "only a
// gate": raising it makes phases DISAPPEAR from the grid, which changes the spans as well as the
// values, and a cache holding the permissive answer would quietly re-admit every phase the stricter
// run had refused.
inline bool sameGridReduction(const PhaseGridConfig &a, const PhaseGridConfig &b)
{
    return a.windowHalfUs     == b.windowHalfUs
        && a.extremumWindowUs == b.extremumWindowUs
        && a.minValidSamples  == b.minValidSamples;
}

// One metric's value at one segmented phase.
struct PhaseGridValue {
    Phase   phase = Phase::Address;
    int64_t tUs   = 0;
    double  value = 0.0;
    // Measurement uncertainty of this value (§A8.3): NaN = none stated. grossRisk −1 = not assessed.
    double      sigma     = std::numeric_limits<double>::quiet_NaN();
    float       grossRisk = -1.f;
    SigmaSource sigmaSrc  = SigmaSource::None;
};

// The extremes of the continuous curve BETWEEN two adjacent segmented phases, CLOSED AT BOTH ENDS
// ([from, to]).
//
// It was half-open at the start until schema 4, to keep an aggregate from counting a sample twice.
// That reasoning was wrong twice over. Min and max are idempotent, so counting a sample twice
// changes no answer and half-openness bought nothing; and the review chart's own candidate set over
// a phase window IS closed, so a sample sitting exactly on the opening instant was a peak candidate
// for the card and not for the engine. On one corpus swing that alone put the two surfaces 9.4°
// apart on shaftAngleVsHorizontal P1→P4. The cache exists so that a corridor can be authored by
// looking at the chart; a boundary convention it does not share is a defect, not a detail.
//
// A ZERO-WIDTH SPAN IS NOT STORED. Two phases can share an instant — rich_7iron has Address and
// Takeaway at the same t_us — and a span from an instant to itself is not a search window, it is one
// sample wearing a window's clothes. Storing its value puts a nearly-raw reading into an aggregate
// that is otherwise all windowed means. So `spans` holds at MOST values.size() − 1 entries, and a
// span whose whole interval is bridged is absent for the same reason.
//
// "Extreme" means the extreme of the CENTRED-WINDOW MEAN, not of the raw samples (schema 3): the
// candidates are the valid samples inside the span, but each one is scored by the mean of its own
// ±extremumWindowUs/2 neighbourhood. So a single wild sample can no longer be a peak, and a peak
// that is genuinely there for 40 ms still is.
struct PhaseGridSpan {
    Phase  from = Phase::Address;
    Phase  to   = Phase::Address;
    double min  = 0.0;
    double max  = 0.0;
    // The larger of the series σ and the two endpoint σ (§A8.3) — an extreme inside the span is no
    // better known than the readings that bound it. NaN = none stated.
    double      sigma    = std::numeric_limits<double>::quiet_NaN();
    SigmaSource sigmaSrc = SigmaSource::None;
};

// One metric over one millisecond span after a phase: [t(anchor) + startMs, t(anchor) + endMs], where
// t(anchor) is the instant of the metric's own value at that phase. Absent when the metric has no
// value at the anchor or no curve at all (a phase-sample-only setup metric has nothing to search).
struct PhaseGridMsSpan {
    PhaseGridMsWindow window;
    // The extremes of the CENTRED-WINDOW MEAN over the span, closed at both ends — the same reduction a
    // phase span stores (reduceExtremum). haveExtremes false = nothing valid in the span.
    bool   haveExtremes = false;
    double min = 0.0, max = 0.0;
    // The ±15 ms windowed medians at the two ends, for a Delta. NaN = no reading there. At startMs 0
    // the start IS the anchor's own grid value.
    double atStart = std::numeric_limits<double>::quiet_NaN();
    double atEnd   = std::numeric_limits<double>::quiet_NaN();
    // The larger of the series σ and the anchor value's σ — the extreme is no better known than the
    // reading it is timed from, nor than the series (as a phase span's σ). NaN = none stated.
    double      sigma    = std::numeric_limits<double>::quiet_NaN();
    SigmaSource sigmaSrc = SigmaSource::None;
    // The longest stretch of the span with no valid sample, its two ends included (µs). The span is not
    // read when this exceeds PhaseGridConfig::msSpanMaxGapUs. A sidecar span with no stored gap reads as
    // uncovered, never as covered.
    int64_t maxGapUs = std::numeric_limits<int64_t>::max();
};

struct MetricPhaseGrid {
    QString                     key;
    QString                     unit;
    std::vector<PhaseGridValue> values;   // ascending by time; one per segmented phase with samples
    std::vector<PhaseGridSpan>  spans;    // between consecutive `values` entries
    std::vector<PhaseGridMsSpan> msSpans; // one per configured millisecond span this metric answers
    // The series' own σ (MetricSeries::sigma) and its source (Series, or Noise when the producer
    // stated no sigmaKind). NaN = none.
    double      seriesSigma    = std::numeric_limits<double>::quiet_NaN();
    SigmaSource seriesSigmaSrc = SigmaSource::None;
    // The series' producer route (MetricSeries::routeId / routeDirect). Empty + false = the
    // producer stated none, which is every metric that is not a KS node's.
    QString     routeId;
    bool        routeDirect = false;
    // MetricSeries::withheld: non-empty = drawn but not judged; the engine refuses its signals.
    QString     withheld;

    const PhaseGridValue *at(Phase p) const
    {
        for (const PhaseGridValue &v : values)
            if (v.phase == p) return &v;
        return nullptr;
    }
    const PhaseGridMsSpan *msSpan(Phase anchor, int startMs, int endMs) const
    {
        const PhaseGridMsWindow w{ anchor, startMs, endMs };
        for (const PhaseGridMsSpan &s : msSpans)
            if (s.window == w) return &s;
        return nullptr;
    }
};

// One swing, reduced to everything a measure could need from it.
struct SwingPhaseGrid {
    QString swingDir;
    QString sessionId;        // the session directory name, for the draw-from filter
    QString club;
    qint64  wallclockMs = 0;
    int     ordinal     = 0;

    std::vector<MetricPhaseGrid> metrics;

    // analysis.gates as stored (schema 8); gatesVersion 0 = analysed before records existed.
    std::vector<GateRecord> gates;
    int                     gatesVersion = 0;

    // The reduction these numbers were computed under. Carried so a sidecar can be REFUSED when the
    // run asking for it is configured differently — see sameGridReduction().
    PhaseGridConfig config;

    const MetricPhaseGrid *metric(const QString &) const && = delete;   // a temporary would leave the result dangling (#247, #248)
    const MetricPhaseGrid *metric(const QString &key) const &
    {
        if (key.isEmpty())
            return nullptr;
        for (const MetricPhaseGrid &m : metrics)
            if (m.key == key) return &m;
        return nullptr;
    }
    bool                   isEmpty() const { return metrics.empty(); }
};

} // namespace pinpoint::analysis
