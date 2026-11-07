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
#include "variability_result.h"
#include "variability_scale_type.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

// THE VARIABILITY GRADER (docs/design/variability-mark/spec.md §8). Pure maths: a window of readings
// in, a VariabilityResult out. It does not choose the window, find the corridor, align traces or look
// up a descriptor — the caller does all of that and hands over numbers — so the same grader serves a
// live session, a frozen assessment, Home and the trend funnel.
//
// Assembled ON DEMAND (makeVariabilityGrader), mirroring makeNormProvider(): no self-registering
// statics, nothing that runs before main().
//
// CONSISTENCY IS PRECISION (§8.1). The scalar, trace and ordinal graders see only spread about the
// golfer's own mean; a constant offset cannot change their grade. Bias belongs to the measurement
// grader, where it is already inside the validation RMSE the caller supplies.

namespace pinpoint::variability {

// One input for every basis; each grader reads its own part and ignores the rest.
struct VariabilityInput {
    // ---- Scale (§8.2 step 4, §8.3 step 2, §8.5) -------------------------------------------------
    // The norm corridor half-width for the context. The CALLER computes it — (σLo + σHi)/2 from the
    // norm (§8.2 step 4.1) — and the grader only receives the number. A value that is not finite and
    // positive counts as absent, and the next rung is tried.
    std::optional<double> corridorHalfWidth;
    std::optional<double> variabilityScale;      // MetricDescriptor::variabilityScale
    ScaleType             scaleType = ScaleType::Interval;

    // ---- Coupling to measurement (§8.6; Scalar and Trace amplitude) -----------------------------
    // σnoise in the measure's units: a VALIDATED noise figure, subtracted from the observed SD. No
    // reading has one yet. ⚠ NEVER a propagated σ (Mark's ruling R3, 7 Nov: grades are taken on the
    // observed spread) — that goes in `propagatedSigma` below.
    std::optional<double> noiseSd;
    // The reading's own PROPAGATED measurement σ (ConsistencyReading::ownSigma, the producer's error
    // budget), in the measure's units. Never subtracted. When finite and positive and the OBSERVED SD
    // (Circular: the circular SD; Trace: the RMS over t of the observed SD(t)) is no larger than it, the
    // spread cannot be told from the measurement's own and the result is measurementLimited (R3). The
    // grade is unchanged.
    std::optional<double> propagatedSigma;
    std::optional<int>    sourceNeedleGrade;     // the source's Needle grade; 1–2 sets measurementLimited

    // ---- Scalar (§8.2) --------------------------------------------------------------------------
    // One value per swing in the window. Degrees for ScaleType::Circular. Non-finite values are
    // missing readings and are not counted.
    std::vector<double> values;

    // ---- Trace (§8.3) ---------------------------------------------------------------------------
    // `scaleType` applies here too: a Circular trace takes the circular SD at each grid point, and
    // its peaks are measured on wrapped angles (see tracePeak()).
    // One curve per swing, ALREADY time-normalised by the caller to kTraceGridPoints, and each
    // swing's time-to-peak in real milliseconds. A swing whose curve or time-to-peak holds a
    // non-finite value is a missing reading and is not counted; a curve of the wrong length is a
    // caller error (MalformedInput).
    std::vector<std::vector<double>> curves;
    std::vector<double>              peakTimesMs;
    // Optional per-swing peak values for the TRACE_AMP_FRACTION rung. Empty: each curve's largest
    // |x(t)|.
    std::vector<double>              peakValues;
    std::optional<double>            timingCorridorHalfWidthMs;
    // Grade the TIMING half too (Mark's ruling R31, 7 Nov; calibration report-s5 §2): only a rate trace with a
    // real interior peak (MetricDescriptor::traceTimingGraded). Off, the default: the grade is the amplitude
    // half alone (ρ = ρₐ, limitedBy Amplitude) and the timing fields stay NaN / None — an angle's "peak" at the
    // top or at impact is not an event, and its time-to-peak spread is the downswing's duration. A swing's
    // reading is still its curve AND its peak time either way, so `peakTimesMs` is required as before.
    bool                             gradeTiming = false;

    // ---- Ordinal (§8.4) -------------------------------------------------------------------------
    // One category or order token per swing ("pelvis>chest>arm>club", "on"/"off"); empty is missing.
    // k: the number of possible outcomes — 2 for a binary checkpoint, m! for an order of m segments.
    QStringList          categories;
    int                  k = 0;

    // ---- Measurement (§8.5) ---------------------------------------------------------------------
    // The three uncertainty sources; the grader takes the FIRST supplied, in the spec's order. A
    // validation RMSE already includes bias (RMSE² = bias² + SD²), which is the point.
    std::optional<double> validationRmse;
    std::optional<double> liveResidualRmse;
    std::optional<double> stationarySd;
    std::optional<double> measurementTolerance;  // MetricDescriptor::measurementTolerance
    // The count behind the uncertainty; below N_MIN is grade 0 ("Checking"). When it is NOT given,
    // N_MIN is not applied at all — deliberately: a validation RMSE comes from a separate study with
    // its own sample, and a stationary or live residual is over samples, not swings, so the swing
    // minimum has nothing to say about it.
    std::optional<int>    uncertaintyN;
};

class IVariabilityGrader {
public:
    virtual ~IVariabilityGrader() = default;

    virtual Basis basis() const = 0;

    // The plain (frozen-assessment) grade. Live surfaces pass the result's bandValue() through
    // hysteresisGrade() with the grade they showed last.
    virtual VariabilityResult grade(const VariabilityInput &in) const = 0;
};

std::unique_ptr<IVariabilityGrader> makeVariabilityGrader(Basis basis);

// ---- Bands and hysteresis (§8.2, §8.4, §8.9) ------------------------------------------------------

// The plain grade 1..5 of a value on a band table. An edge belongs to the tighter grade. +∞ on a
// LowerIsTighter table is grade 1. NaN is 0 (no grade).
int bandGrade(double value, const GradeBands &bands);

// The live grade (§8.9): the grade changes from `previousGrade` only once the value is past one of
// that grade's band edges by MORE than `margin`; it then takes the plain grade. A previous grade outside 1..5 (none shown yet, or grade 0)
// imposes nothing and the plain grade is returned. Frozen assessments call bandGrade() instead.
int hysteresisGrade(double value, int previousGrade, const GradeBands &bands,
                    double margin = kHysteresis);

// ---- Cells that pool readings (Mark's ruling R1, 7 Nov; docs/design/system/variability.md) --------

// The grade of a table cell that rolls up several readings (the Consistency matrix's area at a span).
// It is the grade at the cell's LOOSEST THIRD: the 67th-percentile ρ of its graded readings, by nearest
// rank, through the scalar bands (kBands.ratio). The cell shows the word alone; the marks go on the
// readings it opens to.
//   graded, grade 1..5   the cell's grade; `rho` and `measurementLimited` are the setting reading's
//   graded, grade 0      no reading had a ρ, but one was graded 0 ("Not enough swings")
//   not graded           no reading was graded: the cell shows "-"
struct CellGrade {
    bool   graded             = false;
    int    grade              = 0;
    bool   measurementLimited = false;   // the setting reading's; golfer layer "Checking" (R3)
    double rho                = VariabilityResult::kNaN;   // the setting reading's ρ
    int    n                  = 0;       // readings considered: graded, grade ≥ 1, finite ρ
    int    setting            = -1;      // index into the input of the reading that set the grade; −1 for none
};

// The rank is kCellLoosestPercentile's nearest rank: of n readings sorted by ρ ascending, index
// ⌈0.67·n⌉ − 1 (n = 6 → the 5th smallest, the 2nd loosest).
// `readings`: the cell's readings, each already graded on its own. Considered: `graded`, grade ≥ 1 and a
// finite ρ (ordinal and Needle results carry no ρ and do not count). Ties in ρ keep input order.
CellGrade cellGrade(const std::vector<VariabilityResult> &readings);

// ---- Spread helpers, shared with typicality --------------------------------------------------------

// The sample SD (n − 1) of the values, or NaN for fewer than two.
double sampleSd(const std::vector<double> &values);

// The circular SD √(−2 ln R̄), DEGREES in and out. +∞ when the resultant vanishes; NaN when empty.
double circularSdDeg(const std::vector<double> &degrees);

// The circular mean in degrees, in (−180, 180]; NaN when empty or the resultant vanishes.
double circularMeanDeg(const std::vector<double> &degrees);

// An angle in degrees wrapped into (−180, 180]: wrapDeg(x − mean) is a circular difference.
double wrapDeg(double degrees);

// A trace's peak for the TRACE_AMP_FRACTION rung and for typicality's "higher"/"lower": its largest
// |x(t)|. For ScaleType::Circular each sample is first wrapped into (−180, 180], so the peak is the
// largest angle magnitude in the principal range and a curve reported one turn higher (+360°) has
// the same peak. Time-to-peak is unaffected: the caller supplies it in real milliseconds.
double tracePeak(const std::vector<double> &curve, ScaleType scaleType = ScaleType::Interval);

// One peak value as tracePeak() measures it: |x|, or |wrapDeg(x)| for ScaleType::Circular.
double peakMagnitude(double value, ScaleType scaleType = ScaleType::Interval);

} // namespace pinpoint::variability
