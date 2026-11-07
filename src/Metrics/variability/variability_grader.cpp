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

#include "variability_grader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace pinpoint::variability {

namespace {

constexpr double kInf    = std::numeric_limits<double>::infinity();
constexpr double kNaNv   = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi     = 3.14159265358979323846;
constexpr double kDegRad = kPi / 180.0;

// A supplied scale is usable only when finite and positive; anything else is "not supplied", and the
// next rung of the ladder is tried.
bool usable(const std::optional<double> &v) { return v && std::isfinite(*v) && *v > 0.0; }

bool limitedByNeedle(const VariabilityInput &in)
{
    return in.sourceNeedleGrade && *in.sourceNeedleGrade >= 1 && *in.sourceNeedleGrade <= 2;
}

// R3: the observed spread is no larger than the reading's own propagated σ, so it cannot be told from
// the measurement's. False without a usable σ or a finite spread.
bool withinPropagatedSigma(double sdObserved, const std::optional<double> &propagatedSigma)
{
    return usable(propagatedSigma) && std::isfinite(sdObserved) && sdObserved <= *propagatedSigma;
}

// §8.6: the golfer's SD with the known measurement noise taken out. Never negative; an identity when
// σnoise is absent, not finite, or zero.
double withoutNoise(double sdObs, const std::optional<double> &noiseSd)
{
    if (!noiseSd || !std::isfinite(*noiseSd) || *noiseSd <= 0.0 || !std::isfinite(sdObs)) return sdObs;
    return std::sqrt(std::max(0.0, sdObs * sdObs - *noiseSd * *noiseSd));
}

VariabilityResult ungraded(Basis basis, UngradedReason why, int n = 0)
{
    VariabilityResult r;
    r.basis          = basis;
    r.graded         = false;
    r.grade          = 0;
    r.ungradedReason = why;
    r.n              = n;
    return r;
}

// The grade-0 mark: graded (a mark is shown), but no grade 1..5.
void markTooFew(VariabilityResult &r)
{
    r.graded         = true;
    r.grade          = 0;
    r.ungradedReason = UngradedReason::TooFewSwings;
}

// ---- Scalar (§8.2) ---------------------------------------------------------------------------------

class ScalarGrader final : public IVariabilityGrader {
public:
    Basis basis() const override { return Basis::Scalar; }

    VariabilityResult grade(const VariabilityInput &in) const override
    {
        if (in.scaleType == ScaleType::Ordinal || in.scaleType == ScaleType::Categorical)
            return ungraded(Basis::Scalar, UngradedReason::ScaleTypeMismatch);

        std::vector<double> v;
        v.reserve(in.values.size());
        for (double x : in.values) if (std::isfinite(x)) v.push_back(x);
        const int n = int(v.size());

        VariabilityResult r;
        r.basis              = Basis::Scalar;
        r.n                  = n;
        r.measurementLimited = limitedByNeedle(in);

        // Step 4: the first available scale. The scale is settled BEFORE the minimum n, because a
        // measure with no scale is never graded at all — showing it "Not enough swings" would
        // promise a mark that eight more swings could not deliver.
        if (usable(in.corridorHalfWidth)) {
            r.scale = *in.corridorHalfWidth;  r.scaleSource = ScaleSource::Corridor;
        } else if (usable(in.variabilityScale)) {
            r.scale = *in.variabilityScale;   r.scaleSource = ScaleSource::VariabilityScale;
        } else if (in.scaleType == ScaleType::Ratio) {
            // CV only with STRICTLY positive values: one zero or negative reading and the mean is no
            // longer a scale.
            if (std::any_of(v.begin(), v.end(), [](double x) { return x <= 0.0; }))
                return ungraded(Basis::Scalar, UngradedReason::NonPositiveForRatio, n);
            r.scaleSource = ScaleSource::CV;
            if (n > 0) {
                double sum = 0.0;
                for (double x : v) sum += x;
                r.scale = sum / n;
            }
        } else {
            return ungraded(Basis::Scalar, UngradedReason::NoScale, n);
        }

        if (n < kNMin) { markTooFew(r); return r; }

        r.sdObserved = in.scaleType == ScaleType::Circular ? circularSdDeg(v) : sampleSd(v);
        r.sd         = withoutNoise(r.sdObserved, in.noiseSd);
        r.rho        = r.sd / r.scale;
        r.graded     = true;
        r.grade      = bandGrade(r.rho, kBands.ratio);
        if (withinPropagatedSigma(r.sdObserved, in.propagatedSigma)) r.measurementLimited = true;
        return r;
    }
};

// ---- Trace (§8.3) ----------------------------------------------------------------------------------

class TraceGrader final : public IVariabilityGrader {
public:
    Basis basis() const override { return Basis::Trace; }

    VariabilityResult grade(const VariabilityInput &in) const override
    {
        const size_t swings = in.curves.size();
        if (in.peakTimesMs.size() != swings || (!in.peakValues.empty() && in.peakValues.size() != swings))
            return ungraded(Basis::Trace, UngradedReason::MalformedInput);
        for (const auto &c : in.curves)
            if (int(c.size()) != kTraceGridPoints) return ungraded(Basis::Trace, UngradedReason::MalformedInput);

        // The swings with a whole reading; a non-finite sample anywhere is a missing reading.
        std::vector<size_t> keep;
        for (size_t i = 0; i < swings; ++i) {
            const auto &c = in.curves[i];
            const bool whole = std::isfinite(in.peakTimesMs[i])
                            && (in.peakValues.empty() || std::isfinite(in.peakValues[i]))
                            && std::all_of(c.begin(), c.end(), [](double x) { return std::isfinite(x); });
            if (whole) keep.push_back(i);
        }
        const int n = int(keep.size());

        VariabilityResult r;
        r.basis              = Basis::Trace;
        r.n                  = n;
        r.measurementLimited = limitedByNeedle(in);

        // Timing scale: the timing corridor, else TIMING_SCALE_MS. Always available.
        if (usable(in.timingCorridorHalfWidthMs)) {
            r.timingScaleMs = *in.timingCorridorHalfWidthMs; r.timingScaleSource = ScaleSource::Corridor;
        } else {
            r.timingScaleMs = kTimingScaleMs;                r.timingScaleSource = ScaleSource::TimingDefault;
        }

        // Amplitude scale: corridor, else variabilityScale, else TRACE_AMP_FRACTION × the mean peak.
        // Settled BEFORE the minimum n, as for a scalar: a flat trace with no corridor has no scale at
        // any n, so it is ungraded (NoScale) and never promised a grade by "Not enough swings". The
        // whole trace is ungraded then, timing included — a grade on ρₜ alone would be a grade on
        // half the basis. Only with no swings at all is the peak rung unknowable; that is grade 0.
        const bool circular = in.scaleType == ScaleType::Circular;
        if (usable(in.corridorHalfWidth)) {
            r.scale = *in.corridorHalfWidth;  r.scaleSource = ScaleSource::Corridor;
        } else if (usable(in.variabilityScale)) {
            r.scale = *in.variabilityScale;   r.scaleSource = ScaleSource::VariabilityScale;
        } else {
            r.scaleSource = ScaleSource::PeakFraction;
            if (n > 0) {
                double sum = 0.0;
                for (size_t i : keep)
                    sum += in.peakValues.empty() ? tracePeak(in.curves[i], in.scaleType)
                                                 : peakMagnitude(in.peakValues[i], in.scaleType);
                const double scale = kTraceAmpFraction * (sum / n);
                // Flat-zero curves have no peak to take a fraction of.
                if (!(std::isfinite(scale) && scale > 0.0))
                    return ungraded(Basis::Trace, UngradedReason::NoScale, n);
                r.scale = scale;
            }
        }

        if (n < kNMin) { markTooFew(r); return r; }

        // Amplitude: RMS over t of SD(t), each SD(t) with any known σnoise taken out (§8.6). A
        // circular quantity takes the circular SD at each grid point, so a curve that crosses the
        // ±180° seam, or one reported a turn higher, spreads by its angle and not by 360°.
        double sumSq = 0.0, sumSqObs = 0.0;
        std::vector<double> column(static_cast<size_t>(n));
        for (int t = 0; t < kTraceGridPoints; ++t) {
            for (int j = 0; j < n; ++j) column[size_t(j)] = in.curves[keep[size_t(j)]][size_t(t)];
            const double sdObs = circular ? circularSdDeg(column) : sampleSd(column);
            const double sd    = withoutNoise(sdObs, in.noiseSd);
            sumSqObs += sdObs * sdObs;
            sumSq    += sd * sd;
        }
        r.sdObserved = std::sqrt(sumSqObs / kTraceGridPoints);
        r.sd         = std::sqrt(sumSq / kTraceGridPoints);
        r.rhoAmp     = r.sd / r.scale;
        // R3 on the amplitude: the RMS of the OBSERVED SD(t) against the propagated σ. The propagated σ
        // is in the curve's units, so it can only limit the amplitude half; it is applied below, once
        // the half that sets the grade is known.
        const bool ampWithinOwnSigma = withinPropagatedSigma(r.sdObserved, in.propagatedSigma);

        // Timing: SD of time-to-peak in real milliseconds.
        std::vector<double> peaks;
        peaks.reserve(keep.size());
        for (size_t i : keep) peaks.push_back(in.peakTimesMs[i]);
        r.sdPeakMs  = sampleSd(peaks);
        r.rhoTiming = r.sdPeakMs / r.timingScaleMs;

        // The WORSE of the two (step 4). A tie is called timing: it is the half averaging would hide.
        r.limitedBy = r.rhoTiming >= r.rhoAmp ? TraceLimiter::Timing : TraceLimiter::Amplitude;
        r.rho       = std::max(r.rhoAmp, r.rhoTiming);
        // Measurement-limited only when the amplitude sets the grade: a timing spread is not measured
        // in the curve's units, so an amplitude inside its own σ says nothing about it.
        if (ampWithinOwnSigma && r.limitedBy == TraceLimiter::Amplitude) r.measurementLimited = true;
        r.graded    = true;
        r.grade     = bandGrade(r.rho, kBands.ratio);
        return r;
    }
};

// ---- Ordinal and categorical (§8.4) ----------------------------------------------------------------

class OrdinalGrader final : public IVariabilityGrader {
public:
    Basis basis() const override { return Basis::Ordinal; }

    VariabilityResult grade(const VariabilityInput &in) const override
    {
        if (in.k < 2) return ungraded(Basis::Ordinal, UngradedReason::MalformedInput);

        std::map<QString, int> counts;
        int n = 0;
        for (const QString &c : in.categories) {
            if (c.isEmpty()) continue;
            ++counts[c];
            ++n;
        }

        VariabilityResult r;
        r.basis              = Basis::Ordinal;
        r.n                  = n;
        r.k                  = in.k;
        r.measurementLimited = limitedByNeedle(in);

        if (n < kNMin) { markTooFew(r); return r; }

        // The modal category, whatever it is. Which ordering is RIGHT is diagnostics' question: a
        // repeated wrong order is still Grooved.
        for (const auto &[cat, count] : counts) r.matching = std::max(r.matching, count);
        const double chance = 1.0 / in.k;
        r.s      = double(r.matching) / n;
        r.sPrime = (r.s - chance) / (1.0 - chance);
        r.graded = true;
        r.grade  = bandGrade(r.sPrime, kBands.ordinal);
        return r;
    }
};

// ---- Measurement quality (§8.5) --------------------------------------------------------------------

class MeasurementGrader final : public IVariabilityGrader {
public:
    Basis basis() const override { return Basis::Measurement; }

    VariabilityResult grade(const VariabilityInput &in) const override
    {
        // Uncertainty: the first supplied, in the spec's order. A non-finite value is not supplied.
        VariabilityResult r;
        r.basis = Basis::Measurement;
        const std::pair<const std::optional<double> *, UncertaintyKind> ladder[] = {
            {&in.validationRmse,   UncertaintyKind::ValidationRmse},
            {&in.liveResidualRmse, UncertaintyKind::LiveResidualRmse},
            {&in.stationarySd,     UncertaintyKind::StationarySd},
        };
        for (const auto &[value, kind] : ladder) {
            if (*value && std::isfinite(**value)) { r.uncertainty = **value; r.uncertaintyKind = kind; break; }
        }
        if (r.uncertaintyKind == UncertaintyKind::None)
            return ungraded(Basis::Measurement, UngradedReason::NoUncertainty);
        if (r.uncertainty < 0.0)
            return ungraded(Basis::Measurement, UngradedReason::MalformedInput);

        // Tolerance: measurementTolerance, else MEAS_TOL_FRACTION × the corridor half-width.
        if (usable(in.measurementTolerance)) {
            r.scale = *in.measurementTolerance;                 r.scaleSource = ScaleSource::MeasurementTolerance;
        } else if (usable(in.corridorHalfWidth)) {
            r.scale = kMeasTolFraction * *in.corridorHalfWidth; r.scaleSource = ScaleSource::CorridorFraction;
        } else {
            VariabilityResult u = ungraded(Basis::Measurement, UngradedReason::NoScale);
            u.uncertainty = r.uncertainty;
            u.uncertaintyKind = r.uncertaintyKind;
            return u;
        }

        r.n = in.uncertaintyN.value_or(0);
        if (in.uncertaintyN && *in.uncertaintyN < kNMin) { markTooFew(r); return r; }

        r.rhoM   = r.uncertainty / r.scale;
        r.graded = true;
        r.grade  = bandGrade(r.rhoM, kBands.ratio);
        return r;
    }
};

// The open interval a grade occupies on a band table, as (lo, hi). Edge ownership does not matter
// here: hysteresis only asks how far outside the interval a value has gone.
std::pair<double, double> gradeInterval(int g, const GradeBands &b)
{
    // edges[4 − g] is the tighter edge of grade g (grade 5 has none), edges[5 − g] the looser one
    // (grade 1 has none).
    const double tighter = g == 5 ? kNaNv : b.edges[size_t(4 - g)];
    const double looser  = g == 1 ? kNaNv : b.edges[size_t(5 - g)];
    if (b.sense == GradeBands::Sense::LowerIsTighter)
        return {g == 5 ? -kInf : tighter, g == 1 ? kInf : looser};
    return {g == 1 ? -kInf : looser, g == 5 ? kInf : tighter};
}

} // namespace

std::unique_ptr<IVariabilityGrader> makeVariabilityGrader(Basis basis)
{
    switch (basis) {
    case Basis::Scalar:      return std::make_unique<ScalarGrader>();
    case Basis::Trace:       return std::make_unique<TraceGrader>();
    case Basis::Ordinal:     return std::make_unique<OrdinalGrader>();
    case Basis::Measurement: return std::make_unique<MeasurementGrader>();
    }
    return nullptr;
}

CellGrade cellGrade(const std::vector<VariabilityResult> &readings)
{
    CellGrade cell;
    std::vector<size_t> considered;
    bool anyGradeZero = false;
    for (size_t i = 0; i < readings.size(); ++i) {
        const VariabilityResult &r = readings[i];
        if (!r.graded) continue;
        if (r.grade == 0) { anyGradeZero = true; continue; }
        if (r.grade >= 1 && std::isfinite(r.rho)) considered.push_back(i);
    }
    if (considered.empty()) {
        // Every graded reading was under N_MIN: the grade-0 cell. Nothing graded at all: "-".
        cell.graded = anyGradeZero;
        return cell;
    }
    std::stable_sort(considered.begin(), considered.end(),
                     [&](size_t a, size_t b) { return readings[a].rho < readings[b].rho; });
    const int n = int(considered.size());
    const int rank = std::clamp(int(std::ceil(kCellLoosestPercentile * n)), 1, n);
    const size_t at = considered[size_t(rank - 1)];
    cell.graded             = true;
    cell.n                  = n;
    cell.setting            = int(at);
    cell.rho                = readings[at].rho;
    cell.grade              = bandGrade(cell.rho, kBands.ratio);
    cell.measurementLimited = readings[at].measurementLimited;
    return cell;
}

int bandGrade(double value, const GradeBands &bands)
{
    if (std::isnan(value)) return 0;
    for (size_t i = 0; i < bands.edges.size(); ++i) {
        const bool inside = bands.sense == GradeBands::Sense::LowerIsTighter ? value <= bands.edges[i]
                                                                             : value >= bands.edges[i];
        if (inside) return 5 - int(i);
    }
    return 1;
}

int hysteresisGrade(double value, int previousGrade, const GradeBands &bands, double margin)
{
    const int plain = bandGrade(value, bands);
    if (plain == 0 || previousGrade < 1 || previousGrade > 5 || plain == previousGrade) return plain;

    // Hold the previous grade until the value is MORE than `margin` outside that grade's interval.
    const auto [lo, hi] = gradeInterval(previousGrade, bands);
    if (value >= lo - margin && value <= hi + margin) return previousGrade;
    return plain;
}

double sampleSd(const std::vector<double> &values)
{
    const size_t n = values.size();
    if (n < 2) return kNaNv;
    double mean = 0.0;
    for (double x : values) mean += x;
    mean /= double(n);
    double ss = 0.0;
    for (double x : values) ss += (x - mean) * (x - mean);
    return std::sqrt(ss / double(n - 1));
}

namespace {
// The mean resultant (C, S) of unit vectors at the given angles.
std::pair<double, double> resultant(const std::vector<double> &degrees)
{
    double c = 0.0, s = 0.0;
    for (double d : degrees) { c += std::cos(d * kDegRad); s += std::sin(d * kDegRad); }
    return {c / double(degrees.size()), s / double(degrees.size())};
}
} // namespace

double circularSdDeg(const std::vector<double> &degrees)
{
    if (degrees.empty()) return kNaNv;
    const auto [c, s] = resultant(degrees);
    const double rbar = std::min(1.0, std::hypot(c, s));  // rounding can nudge R̄ a hair above 1
    if (rbar <= 0.0) return kInf;
    return std::sqrt(-2.0 * std::log(rbar)) / kDegRad;
}

double circularMeanDeg(const std::vector<double> &degrees)
{
    if (degrees.empty()) return kNaNv;
    const auto [c, s] = resultant(degrees);
    if (std::hypot(c, s) <= 0.0) return kNaNv;
    return wrapDeg(std::atan2(s, c) / kDegRad);
}

double wrapDeg(double degrees)
{
    double w = std::fmod(degrees, 360.0);
    if (w <= -180.0) w += 360.0;
    else if (w > 180.0) w -= 360.0;
    return w;
}

double peakMagnitude(double value, ScaleType scaleType)
{
    return std::abs(scaleType == ScaleType::Circular ? wrapDeg(value) : value);
}

double tracePeak(const std::vector<double> &curve, ScaleType scaleType)
{
    double peak = 0.0;
    for (double x : curve) peak = std::max(peak, peakMagnitude(x, scaleType));
    return peak;
}

} // namespace pinpoint::variability
