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

#include <QString>

#include <limits>

// WHAT A VARIABILITY GRADER FOUND (docs/design/variability-mark/spec.md §3, §7.5, §8). One value
// type for all four bases, so a surface holds one thing whichever grader produced it.
//
// Three outcomes, and a surface must tell them apart:
//   graded, grade 1..5          — a mark with its word.
//   graded, grade 0             — the grade-0 mark: "Not enough swings" / "Checking" (§3, §4.7).
//                                 `ungradedReason` says why (TooFewSwings).
//   NOT graded                  — no mark at all, and never a raw SD in its place (§8.2 step 4.4).
//
// `measurementLimited` (§8.6) does not change `grade`: the technical layer shows the grade with
// "· measurement-limited", and it is the golfer layer's job to show the grade-0 "Checking" mark
// instead. Keeping the computed grade here lets both layers read one result.

namespace pinpoint::variability {

// Which grader produced the result (§8.2 – §8.5).
enum class Basis { Scalar, Trace, Ordinal, Measurement };

// Where the scale the ratio was taken against came from. The first four are §8.2 step 4; the rest
// are the trace and measurement rungs (§8.3 steps 2–3, §8.5), named for what they are so a basis
// string never calls a fallback scale a corridor.
enum class ScaleSource {
    None,                 // no scale was needed (Ordinal) or none was available (ungraded)
    Corridor,             // the norm corridor half-width the caller supplied
    VariabilityScale,     // MetricDescriptor::variabilityScale
    CV,                   // the mean, for ScaleType::Ratio with every value > 0 (ρ is then the CV)
    PeakFraction,         // trace amplitude: TRACE_AMP_FRACTION × the mean per-swing peak
    TimingDefault,        // trace timing: TIMING_SCALE_MS
    MeasurementTolerance, // MetricDescriptor::measurementTolerance
    CorridorFraction,     // measurement: MEAS_TOL_FRACTION × the corridor half-width
};

// Why a result carries no grade 1..5. TooFewSwings is the one reason that still has a mark (grade 0);
// every other reason means `graded` is false.
enum class UngradedReason {
    None,
    TooFewSwings,         // n < N_MIN: grade 0, the "Not enough swings" / "Checking" mark
    NoScale,              // no corridor, no variabilityScale, no CV rung (§8.2 step 4.4), or no tolerance (§8.5)
    NonPositiveForRatio,  // ScaleType::Ratio with no corridor or variabilityScale, and a value ≤ 0: no CV
    NoUncertainty,        // measurement: none of the three §8.5 uncertainty sources was supplied
    ScaleTypeMismatch,    // a scalar basis asked to grade an Ordinal or Categorical quantity
    MalformedInput,       // a trace not on the 101-point grid, mismatched lengths, k < 2, a negative uncertainty
};

// The §8.5 uncertainty that was graded, in the spec's order of preference.
enum class UncertaintyKind { None, ValidationRmse, LiveResidualRmse, StationarySd };

// Which half of a trace grade set it (§8.3 step 4: the worse of ρₐ and ρₜ).
enum class TraceLimiter { None, Amplitude, Timing };

struct VariabilityResult {
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    Basis          basis          = Basis::Scalar;
    bool           graded         = false;  // false: NO mark (see the header comment)
    int            grade          = 0;      // 0..5; 0 with graded = true is the grade-0 mark
    UngradedReason ungradedReason = UngradedReason::None;
    int            n              = 0;      // swings (Measurement: the count behind the uncertainty, if given)

    double         scale          = kNaN;   // what ρ (or ρₐ, or ρm) was divided by
    ScaleSource    scaleSource    = ScaleSource::None;

    // §8.6 and R3. Set when the source's Needle grade is 1–2, or when the observed spread is no larger
    // than the reading's own propagated σ (VariabilityInput::propagatedSigma); never for Basis::Measurement.
    bool           measurementLimited = false;

    // Scalar (§8.2). `sd` is the spread GRADED: the golfer SD after any σnoise subtraction (§8.6);
    // `sdObserved` is the spread before it. Equal when no σnoise was given. Degrees for Circular.
    double sd         = kNaN;
    double sdObserved = kNaN;
    double rho        = kNaN;   // the value graded on kBands.ratio. Trace: max(ρₐ, ρₜ)

    // Trace (§8.3). `sd` holds RMS over t of SD(t) (after any σnoise subtraction).
    double       rhoAmp          = kNaN;
    double       rhoTiming       = kNaN;
    double       sdPeakMs        = kNaN;
    double       timingScaleMs   = kNaN;
    ScaleSource  timingScaleSource = ScaleSource::None;
    TraceLimiter limitedBy       = TraceLimiter::None;

    // Ordinal (§8.4).
    double s        = kNaN;     // modal share
    double sPrime   = kNaN;     // chance-corrected, graded on kBands.ordinal
    int    matching = 0;        // swings in the modal category
    int    k        = 0;

    // Measurement (§8.5). `scale` holds the tolerance.
    double          uncertainty     = kNaN;
    UncertaintyKind uncertaintyKind = UncertaintyKind::None;
    double          rhoM            = kNaN;

    // The value that sits on the bands (ρ, s′ or ρm) and the bands it sits on: what live hysteresis
    // (§8.9) is applied to. NaN when not graded or grade 0.
    double             bandValue() const;
    const GradeBands  &bands() const;

    // The technical-layer basis string (§7.5), built ONLY from computed values; empty when there is
    // no §7.5 row for this result (ungraded, grade 0, or Basis::Measurement — §7.5 has no Needle row).
    //   `unit`      the measure's unit, as the descriptor states it ("°", "cm", "km/h"); spaced as
    //               formatWithUnit() spaces it.
    //   `decimals`  the measure's display decimals for σ. ρ is always two places, ms always none.
    QString basisString(const QString &unit = {}, int decimals = 1) const;
};

// A value with its unit, as every basis string sets one: "2.1°", "12.3% stance width", "3.1:1",
// "4.8 cm", "12". A unit that starts with "°", "%" or ":" sits against the number; any other unit
// follows a space. Shared by every basis string so the rule cannot differ between them.
QString formatWithUnit(double value, const QString &unit, int decimals);

// The §7.5 / §9.6 launch-monitor ball-flight basis: "offline ±6 yd · carry ±5 yd · 12 shots", from
// the group's computed offline and carry SDs and its shot count. Unit and decimals as
// VariabilityResult::basisString (one unit for both: the launch monitor reports them alike).
QString ballFlightBasisString(double offlineSd, double carrySd, int shots,
                              const QString &unit = {}, int decimals = 0);

} // namespace pinpoint::variability
