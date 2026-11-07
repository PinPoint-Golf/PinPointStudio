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

#include <functional>

#include <limits>
#include <optional>
#include <vector>

// TYPICALITY: THIS SWING COMPARED WITH YOUR USUAL (docs/design/variability-mark/spec.md §6.2, §7.4,
// §8.7). Never a grade, and never praise or fault: an unusual swing may be the best one. The result
// is a z and a word band; whether the swing was GOOD is the corridor's job.
//
// The caller chooses the window (the session, or the last N_WINDOW swings) and whether this swing is
// in it. Fewer than N_MIN readings in the window is no result at all.
//
// THE ROBUST z (Mark's ruling R22, 7 Nov; amends spec §8.7's mean/SD): z = (x − median) / (IQR / 1.349)
// over the window, the quartiles by type-7 quantiles (box_stats.h quantile7, the one the stand-out pick
// and the screens' medians use). The band, the ghosts and the corridor in z all take this scale, and a
// trace takes the per-point median and IQR/1.349 envelope, its peak timing and amplitude the same.
// ScaleType::Circular keeps the circular mean and circular SD: the code has no circular median or IQR.

namespace pinpoint::variability {

// §7.4: |z| ≤ 1 "Your usual", ≤ 2 "A little off", above "Unusual". A word band, never a grade.
enum class TypicalityBand { Usual, ALittleOff, Unusual };

TypicalityBand typicalityBand(double z);

// A whole curve's band from its RMS z(t) (Mark's ruling R30): "Your usual" up to kTraceTypicalityUsualZ (1.25),
// "A little off" up to kTypicalityOffZ, above "Unusual".
TypicalityBand traceTypicalityBand(double rmsZ);

// ---- Scalar ---------------------------------------------------------------------------------------

struct ScalarTypicality {
    double         value = 0.0;   // this swing
    double         usual = 0.0;   // the window median (circular mean for ScaleType::Circular)
    double         sd    = 0.0;   // the window's robust spread, IQR/1.349 (circular SD for ScaleType::Circular)
    double         z     = 0.0;   // (x − median)/spread; ±∞ when the window has no spread (IQR 0) and x differs
    int            n     = 0;
    TypicalityBand band  = TypicalityBand::Usual;

    bool           median = true; // `usual` is the window median; false for Circular, whose `usual` is the circular mean

    // "this swing 5.2 cm · median 4.8 cm" (§7.5; R22: the window median, so "median", never "usual"; it
    // supersedes R18's "average"). Circular, whose centre is the circular mean, keeps "average", so the basis
    // never calls a mean a median. Unit and decimals as VariabilityResult::basisString.
    QString basisString(const QString &unit = {}, int decimals = 1) const;
    // The same with each figure set by `formatValue` (value and unit together), for a surface whose rows print a
    // signed reading their own way (the launch monitor's "3.5° L"), so the basis reads as the row beside it does.
    QString basisString(const std::function<QString(double)> &formatValue) const;
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

    // Interval and Ratio: z(t) = (x(t) − median(t)) / (IQR(t)/1.349). Circular: z(t) is the wrapped
    // deviation from the window's circular mean at t over its circular SD, and peaks are wrapped angle magnitudes (tracePeak()), so a curve one turn higher (+360°) is
    // the same swing. Peak timing is in real ms and is the same for every scale type.
    ScaleType                        scaleType = ScaleType::Interval;
};

// Where this swing's peak fell against the window's. "On time" is within the §7.4 "Your usual" band
// of the window's time-to-peak spread (|z| ≤ 1); the spec gives the words but no separate threshold.
// A deviation that rounds to 0 ms is also on time: "peak 0 ms early" is not a thing to print.
enum class PeakTiming { OnTime, Early, Late };

struct TraceTypicality {
    double         rmsZ  = 0.0;   // RMS over t of z(t) = (x(t) − median(t))/spread(t), over t where spread(t) > 0
    TypicalityBand band  = TypicalityBand::Usual;   // traceTypicalityBand(rmsZ) (R30)
    int            n     = 0;

    double         timingDeviationMs = 0.0;   // this swing's peak − the window's median peak (robust z, R22)
    double         timingZ           = 0.0;
    PeakTiming     timing            = PeakTiming::OnTime;

    // +1 higher, −1 lower, 0 within the usual band (|z| ≤ 1) of the window's peak values.
    double         amplitudeZ    = 0.0;
    int            amplitudeSign = 0;

    // "peak on time", "peak 25 ms early", "peak 45 ms late, higher" (§7.5).
    QString basisString() const;
};

// nullopt when the window holds fewer than N_MIN whole curves, any curve is off the grid or the
// window's lengths disagree, this swing is not whole, or the spread at t is zero everywhere.
std::optional<TraceTypicality> traceTypicality(const TraceTypicalityInput &in);

// ---- The session window (Mark's ruling R8, 7 Nov) -------------------------------------------------
// "Your usual" is this swing against the session's OTHER swings with the SAME club, at least N_MIN of
// them. These build that window from every swing of the session and hand back what the two typicality
// marks draw (docs/design/system/components/PpVariabilityMark.md, "Consumer supplies"). The QML shapes
// are built from these by the façade (src/Gui/variability/variability_facade.h).
//
// `swings` is the session in strike order, oldest first. The club is compared exactly; a blank club is
// a club of its own. A swing whose reading is missing (non-finite, or a curve off the grid) is not in
// the window.

struct TypicalitySwing {
    QString swingId;
    QString club;
    double  value = std::numeric_limits<double>::quiet_NaN();
};

// One ghost dot: a recent other swing's z against the SAME window as this swing's.
struct TypicalityGhost {
    QString swingId;
    double  z = 0.0;
};

// The measure's corridor in the window's z: (lo − median)/spread and (hi − median)/spread.
struct TypicalityCorridorZ {
    double loZ = 0.0;
    double hiZ = 0.0;
};

struct ScalarTypicalityWindow {
    ScalarTypicality                   typicality;   // z, band, the window's median and robust spread; basisString()
    std::vector<TypicalityGhost>       ghosts;       // the latest kTypicalityMaxGhosts of the window, oldest first
    std::optional<TypicalityCorridorZ> corridor;     // none without both ends, or when the window has no spread
};

// nullopt when `swingId` is not in `swings`, its value is missing, or fewer than N_MIN other swings of
// its club have a value. Ghosts are the most recent other same-club swings of the session, whether
// before or after this one. Circular: deviations are wrapped (wrapDeg) about the circular mean, over the
// circular SD.
std::optional<ScalarTypicalityWindow> scalarTypicalityWindow(const std::vector<TypicalitySwing> &swings,
                                                             const QString &swingId,
                                                             ScaleType scaleType = ScaleType::Interval,
                                                             std::optional<double> corridorLo = {},
                                                             std::optional<double> corridorHi = {});

struct TypicalityTraceSwing {
    QString             swingId;
    QString             club;
    std::vector<double> curve;          // on the kTraceGridPoints grid, top → impact
    double              peakTimeMs = std::numeric_limits<double>::quiet_NaN();   // real ms
};

// The trace corridor: a window in normalised time 0…1 (the build's reading of an undefined spec item,
// open for Mark: decisions.md, Session 3).
struct TypicalityTimeCorridor {
    double loT = 0.0;
    double hiT = 0.0;
};

struct TraceTypicalityWindow {
    TraceTypicality                       typicality;   // band (from RMS z), timing; basisString()
    // The envelope (R22). The names stay `mean` and `sd`, as the payload's keys do, but they carry the
    // window's per-point MEDIAN and ROBUST SPREAD IQR/1.349 (Circular: the circular mean and circular SD).
    std::vector<double>                   mean;         // the per-point median (circular mean), kTraceGridPoints
    std::vector<double>                   sd;           // the per-point IQR/1.349 (circular SD), kTraceGridPoints
    std::vector<double>                   swing;        // this swing's curve; Circular: mean + wrapDeg(x − mean)
    std::optional<TypicalityTimeCorridor> corridor;     // none without both ends, or loT > hiT
};

// nullopt when `swingId` is not in `swings`, its curve or peak time is missing, or traceTypicality()
// finds no result over the other same-club swings with a whole curve and peak time (fewer than N_MIN,
// or no spread anywhere).
std::optional<TraceTypicalityWindow> traceTypicalityWindow(const std::vector<TypicalityTraceSwing> &swings,
                                                           const QString &swingId,
                                                           ScaleType scaleType = ScaleType::Interval,
                                                           std::optional<double> corridorLoT = {},
                                                           std::optional<double> corridorHiT = {});

} // namespace pinpoint::variability
