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

#include "variability_constants.h"
#include "variability_scale_type.h"

#include <QString>

#include <optional>
#include <vector>

// TYPICALITY: THIS SWING COMPARED WITH YOUR USUAL (docs/design/variability-mark/spec.md §6.2, §7.4,
// §8.7). Never a grade, and never praise or fault: an unusual swing may be the best one. The result
// is a z and a word band; whether the swing was GOOD is the corridor's job.
//
// The caller chooses the window (the session, or the last N_WINDOW swings) and whether this swing is
// in it. Fewer than N_MIN readings in the window is no result at all.

namespace pinpoint::variability {

// §7.4: |z| ≤ 1 "Your usual", ≤ 2 "A little off", above "Unusual". A word band, never a grade.
enum class TypicalityBand { Usual, ALittleOff, Unusual };

TypicalityBand typicalityBand(double z);

// ---- Scalar ---------------------------------------------------------------------------------------

struct ScalarTypicality {
    double         value = 0.0;   // this swing
    double         usual = 0.0;   // the window mean (circular mean for ScaleType::Circular)
    double         sd    = 0.0;   // the window SD (circular SD for ScaleType::Circular)
    double         z     = 0.0;   // (x − x̄)/SD; ±∞ when the window has no spread and x differs
    int            n     = 0;
    TypicalityBand band  = TypicalityBand::Usual;

    // "this swing 5.2 cm · usual 4.8 cm" (§7.5). Unit and decimals as VariabilityResult::basisString.
    QString basisString(const QString &unit = {}, int decimals = 1) const;
};

// z of `value` against the window's finite readings. nullopt when the window holds fewer than N_MIN
// or the value is not finite. Ordinal and Categorical quantities have no z (nullopt).
std::optional<ScalarTypicality> scalarTypicality(double value, const std::vector<double> &window,
                                                 ScaleType scaleType = ScaleType::Interval);

// ---- Trace ----------------------------------------------------------------------------------------

struct TraceTypicalityInput {
    // This swing and the window, every curve ALREADY on the kTraceGridPoints grid (the caller aligns
    // them, as for the trace grader). Peak times in real milliseconds.
    std::vector<double>              curve;
    double                           peakTimeMs = 0.0;
    std::optional<double>            peakValue;          // absent: the curve's largest |x(t)|
    std::vector<std::vector<double>> windowCurves;
    std::vector<double>              windowPeakTimesMs;
    std::vector<double>              windowPeakValues;   // empty: each curve's largest |x(t)|

    // Circular: z(t) is the wrapped deviation from the window's circular mean at t over its circular
    // SD, and peaks are wrapped angle magnitudes (tracePeak()), so a curve one turn higher (+360°) is
    // the same swing. Peak timing is in real ms and is the same for every scale type.
    ScaleType                        scaleType = ScaleType::Interval;
};

// Where this swing's peak fell against the window's. "On time" is within the §7.4 "Your usual" band
// of the window's time-to-peak spread (|z| ≤ 1); the spec gives the words but no separate threshold.
// A deviation that rounds to 0 ms is also on time: "peak 0 ms early" is not a thing to print.
enum class PeakTiming { OnTime, Early, Late };

struct TraceTypicality {
    double         rmsZ  = 0.0;   // RMS over t of z(t) = (x(t) − x̄(t))/SD(t), over t where SD(t) > 0
    TypicalityBand band  = TypicalityBand::Usual;
    int            n     = 0;

    double         timingDeviationMs = 0.0;   // this swing's peak − the window's mean peak
    double         timingZ           = 0.0;
    PeakTiming     timing            = PeakTiming::OnTime;

    // +1 higher, −1 lower, 0 within the usual band (|z| ≤ 1) of the window's peak values.
    double         amplitudeZ    = 0.0;
    int            amplitudeSign = 0;

    // "peak on time", "peak 25 ms early", "peak 45 ms late, higher" (§7.5).
    QString basisString() const;
};

// nullopt when the window holds fewer than N_MIN whole curves, any curve is off the grid or the
// window's lengths disagree, this swing is not whole, or SD(t) is zero everywhere.
std::optional<TraceTypicality> traceTypicality(const TraceTypicalityInput &in);

} // namespace pinpoint::variability
