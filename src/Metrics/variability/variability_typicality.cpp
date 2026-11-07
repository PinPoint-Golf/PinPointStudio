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

#include "variability_typicality.h"

#include "variability_grader.h"   // circular helpers, tracePeak, peakMagnitude
#include "variability_result.h"   // formatWithUnit

#include "../../Gui/typeset/box_stats.h"   // quantile7: the one type-7 quantile (header-only, no Qt)

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::variability {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// R22: the robust spread is the middle half ÷ 1.349 (a normal's IQR is 1.349 σ), as the stand-out pick
// (session_consistency.h kConsistencySpreadDivisor) and the matrix's consistency spread take it.
constexpr double kRobustSpreadDivisor = 1.349;

// The robust centre and spread of finite values: the type-7 median and IQR/1.349.
struct RobustCentre {
    double median = 0.0;
    double spread = 0.0;
};

RobustCentre robustCentre(const std::vector<double> &v)
{
    using pinpoint::box::quantile7;
    return RobustCentre{ quantile7(v, 0.5), (quantile7(v, 0.75) - quantile7(v, 0.25)) / kRobustSpreadDivisor };
}

// A z that stays honest when the window has no spread: 0 for the same value, ±∞ for any other.
double zOf(double deviation, double sd)
{
    if (sd > 0.0) return deviation / sd;
    return deviation == 0.0 ? 0.0 : std::copysign(kInf, deviation);
}

bool whole(const std::vector<double> &c)
{
    return int(c.size()) == kTraceGridPoints
        && std::all_of(c.begin(), c.end(), [](double x) { return std::isfinite(x); });
}

} // namespace

TypicalityBand typicalityBand(double z)
{
    const double a = std::abs(z);
    if (a <= kTypicalityUsualZ) return TypicalityBand::Usual;
    if (a <= kTypicalityOffZ)   return TypicalityBand::ALittleOff;
    return TypicalityBand::Unusual;
}

std::optional<ScalarTypicality> scalarTypicality(double value, const std::vector<double> &window,
                                                 ScaleType scaleType)
{
    if (scaleType == ScaleType::Ordinal || scaleType == ScaleType::Categorical) return std::nullopt;
    if (!std::isfinite(value)) return std::nullopt;

    std::vector<double> v;
    v.reserve(window.size());
    for (double x : window) if (std::isfinite(x)) v.push_back(x);
    if (int(v.size()) < kNMin) return std::nullopt;

    ScalarTypicality t;
    t.value = value;
    t.n     = int(v.size());
    double deviation = 0.0;
    if (scaleType == ScaleType::Circular) {
        t.usual = circularMeanDeg(v);
        t.sd    = circularSdDeg(v);
        if (!std::isfinite(t.usual)) return std::nullopt;   // the window points every way at once
        deviation = wrapDeg(value - t.usual);
        t.median = false;
    } else {
        const RobustCentre c = robustCentre(v);
        t.usual   = c.median;
        t.sd      = c.spread;
        deviation = value - t.usual;
    }
    t.z    = zOf(deviation, t.sd);
    t.band = typicalityBand(t.z);
    return t;
}

QString ScalarTypicality::basisString(const QString &unit, int decimals) const
{
    return basisString([&unit, decimals](double v) { return formatWithUnit(v, unit, decimals); });
}

QString ScalarTypicality::basisString(const std::function<QString(double)> &formatValue) const
{
    if (!median)
        //: Technical-layer typicality basis (variability spec §7.5) for an angle that wraps. %1 this swing's
        //: value, %2 the circular mean of the golfer's other swings, both with their unit.
        return QCoreApplication::translate("VariabilityBasis", "this swing %1 · average %2")
            .arg(formatValue(value), formatValue(usual));
    //: Technical-layer typicality basis (variability spec §7.5; ruling R22). %1 this swing's value, %2 the
    //: median of the golfer's other swings, both with their unit.
    return QCoreApplication::translate("VariabilityBasis", "this swing %1 · median %2")
        .arg(formatValue(value), formatValue(usual));
}

std::optional<TraceTypicality> traceTypicality(const TraceTypicalityInput &in)
{
    const size_t swings = in.windowCurves.size();
    if (in.windowPeakTimesMs.size() != swings
        || (!in.windowPeakValues.empty() && in.windowPeakValues.size() != swings))
        return std::nullopt;
    if (!whole(in.curve) || !std::isfinite(in.peakTimeMs)) return std::nullopt;
    if (in.peakValue && !std::isfinite(*in.peakValue)) return std::nullopt;

    std::vector<size_t> keep;
    for (size_t i = 0; i < swings; ++i) {
        if (int(in.windowCurves[i].size()) != kTraceGridPoints) return std::nullopt;
        if (whole(in.windowCurves[i]) && std::isfinite(in.windowPeakTimesMs[i])
            && (in.windowPeakValues.empty() || std::isfinite(in.windowPeakValues[i])))
            keep.push_back(i);
    }
    const int n = int(keep.size());
    if (n < kNMin) return std::nullopt;

    TraceTypicality t;
    t.n = n;

    // Shape: RMS over t of z(t), the robust z about the per-point median (R22; Circular: circular mean and
    // SD). A grid point with no spread in the window carries no z; nor does a circular one whose readings
    // point every way at once (no circular mean).
    const bool circular = in.scaleType == ScaleType::Circular;
    double sumZ2 = 0.0;
    int    used  = 0;
    std::vector<double> column(static_cast<size_t>(n));
    for (int g = 0; g < kTraceGridPoints; ++g) {
        for (int j = 0; j < n; ++j) column[size_t(j)] = in.windowCurves[keep[size_t(j)]][size_t(g)];
        double sd = 0.0, deviation = 0.0;
        if (circular) {
            sd = circularSdDeg(column);
            if (!(sd > 0.0) || !std::isfinite(sd)) continue;
            const double centre = circularMeanDeg(column);
            if (!std::isfinite(centre)) continue;
            deviation = wrapDeg(in.curve[size_t(g)] - centre);
        } else {
            const RobustCentre c = robustCentre(column);
            sd = c.spread;
            if (!(sd > 0.0) || !std::isfinite(sd)) continue;
            deviation = in.curve[size_t(g)] - c.median;
        }
        const double z = deviation / sd;
        sumZ2 += z * z;
        ++used;
    }
    if (used == 0) return std::nullopt;
    t.rmsZ = std::sqrt(sumZ2 / used);
    t.band = typicalityBand(t.rmsZ);

    // Timing: this swing's peak against the window's, in real milliseconds, by the robust z (R22).
    std::vector<double> peakTimes, peakValues;
    for (size_t i : keep) {
        peakTimes.push_back(in.windowPeakTimesMs[i]);
        peakValues.push_back(in.windowPeakValues.empty() ? tracePeak(in.windowCurves[i], in.scaleType)
                                                         : peakMagnitude(in.windowPeakValues[i], in.scaleType));
    }
    const RobustCentre timing = robustCentre(peakTimes);
    t.timingDeviationMs = in.peakTimeMs - timing.median;
    t.timingZ           = zOf(t.timingDeviationMs, timing.spread);
    t.timing = std::abs(t.timingZ) <= kTypicalityUsualZ || std::round(std::abs(t.timingDeviationMs)) == 0.0
                 ? PeakTiming::OnTime
             : t.timingDeviationMs < 0.0               ? PeakTiming::Early
                                                       : PeakTiming::Late;

    // Amplitude: this swing's peak against the window's peaks, as a sign. Peaks are magnitudes (a wrapped
    // angle's included), so the robust z applies whatever the scale type.
    const double peak = in.peakValue ? peakMagnitude(*in.peakValue, in.scaleType) : tracePeak(in.curve, in.scaleType);
    const RobustCentre peaks = robustCentre(peakValues);
    t.amplitudeZ    = zOf(peak - peaks.median, peaks.spread);
    t.amplitudeSign = std::abs(t.amplitudeZ) <= kTypicalityUsualZ ? 0 : (t.amplitudeZ > 0.0 ? 1 : -1);
    return t;
}

std::optional<ScalarTypicalityWindow> scalarTypicalityWindow(const std::vector<TypicalitySwing> &swings,
                                                             const QString &swingId, ScaleType scaleType,
                                                             std::optional<double> corridorLo,
                                                             std::optional<double> corridorHi)
{
    const auto self = std::find_if(swings.begin(), swings.end(),
                                   [&](const TypicalitySwing &s) { return s.swingId == swingId; });
    if (self == swings.end() || !std::isfinite(self->value)) return std::nullopt;

    // The window: the session's other swings of this club with a value, strike order.
    std::vector<const TypicalitySwing *> others;
    std::vector<double> window;
    for (const TypicalitySwing &s : swings) {
        if (&s == &*self || s.club != self->club || !std::isfinite(s.value)) continue;
        others.push_back(&s);
        window.push_back(s.value);
    }
    const std::optional<ScalarTypicality> t = scalarTypicality(self->value, window, scaleType);
    if (!t) return std::nullopt;

    ScalarTypicalityWindow w;
    w.typicality = *t;
    const bool circular = scaleType == ScaleType::Circular;
    auto zAgainstWindow = [&](double x) {
        return zOf(circular ? wrapDeg(x - t->usual) : x - t->usual, t->sd);
    };
    const size_t first = others.size() > size_t(kTypicalityMaxGhosts) ? others.size() - size_t(kTypicalityMaxGhosts) : 0;
    for (size_t i = first; i < others.size(); ++i)
        w.ghosts.push_back(TypicalityGhost{ others[i]->swingId, zAgainstWindow(others[i]->value) });
    if (corridorLo && corridorHi && std::isfinite(*corridorLo) && std::isfinite(*corridorHi)
        && t->sd > 0.0 && std::isfinite(t->sd))
        w.corridor = TypicalityCorridorZ{ zAgainstWindow(*corridorLo), zAgainstWindow(*corridorHi) };
    return w;
}

std::optional<TraceTypicalityWindow> traceTypicalityWindow(const std::vector<TypicalityTraceSwing> &swings,
                                                           const QString &swingId, ScaleType scaleType,
                                                           std::optional<double> corridorLoT,
                                                           std::optional<double> corridorHiT)
{
    const auto self = std::find_if(swings.begin(), swings.end(),
                                   [&](const TypicalityTraceSwing &s) { return s.swingId == swingId; });
    if (self == swings.end() || !whole(self->curve) || !std::isfinite(self->peakTimeMs)) return std::nullopt;

    // The window: the other swings of this club with a whole reading, so the envelope below is over exactly
    // the curves traceTypicality() reads.
    TraceTypicalityInput in;
    in.curve      = self->curve;
    in.peakTimeMs = self->peakTimeMs;
    in.scaleType  = scaleType;
    for (const TypicalityTraceSwing &s : swings) {
        if (&s == &*self || s.club != self->club || !whole(s.curve) || !std::isfinite(s.peakTimeMs)) continue;
        in.windowCurves.push_back(s.curve);
        in.windowPeakTimesMs.push_back(s.peakTimeMs);
    }
    const std::optional<TraceTypicality> t = traceTypicality(in);
    if (!t) return std::nullopt;

    TraceTypicalityWindow w;
    w.typicality = *t;
    const bool circular = scaleType == ScaleType::Circular;
    const size_t n = in.windowCurves.size();
    w.mean.resize(size_t(kTraceGridPoints));
    w.sd.resize(size_t(kTraceGridPoints));
    w.swing = self->curve;
    std::vector<double> column(n);
    for (int g = 0; g < kTraceGridPoints; ++g) {
        for (size_t j = 0; j < n; ++j) column[j] = in.windowCurves[j][size_t(g)];
        const size_t at = size_t(g);
        if (circular) {
            w.mean[at] = circularMeanDeg(column);
            w.sd[at]   = circularSdDeg(column);
            // Drawn beside the envelope, so the swing sits on the mean's side of the seam.
            if (std::isfinite(w.mean[at])) w.swing[at] = w.mean[at] + wrapDeg(self->curve[at] - w.mean[at]);
        } else {
            // R22: `mean` and `sd` carry the per-point median and IQR/1.349 (the payload's key names stay).
            const RobustCentre c = robustCentre(column);
            w.mean[at] = c.median;
            w.sd[at]   = c.spread;
        }
    }
    if (corridorLoT && corridorHiT && std::isfinite(*corridorLoT) && std::isfinite(*corridorHiT)
        && *corridorLoT <= *corridorHiT)
        w.corridor = TypicalityTimeCorridor{ *corridorLoT, *corridorHiT };
    return w;
}

QString TraceTypicality::basisString() const
{
    const QString ms = QString::number(std::abs(std::round(timingDeviationMs)), 'f', 0);
    QString out;
    switch (timing) {
    case PeakTiming::OnTime:
        //: Technical-layer trace typicality basis (variability spec §7.5): this swing's peak fell
        //: where the golfer's usually does.
        out = QCoreApplication::translate("VariabilityBasis", "peak on time");
        break;
    case PeakTiming::Early:
        //: Trace typicality basis. %1 milliseconds earlier than the golfer's usual peak.
        out = QCoreApplication::translate("VariabilityBasis", "peak %1 ms early").arg(ms);
        break;
    case PeakTiming::Late:
        //: Trace typicality basis. %1 milliseconds later than the golfer's usual peak.
        out = QCoreApplication::translate("VariabilityBasis", "peak %1 ms late").arg(ms);
        break;
    }
    if (amplitudeSign > 0)
        //: Trace typicality basis: the peak was higher than usual. %1 is the timing phrase.
        out = QCoreApplication::translate("VariabilityBasis", "%1, higher").arg(out);
    else if (amplitudeSign < 0)
        //: Trace typicality basis: the peak was lower than usual. %1 is the timing phrase.
        out = QCoreApplication::translate("VariabilityBasis", "%1, lower").arg(out);
    return out;
}

} // namespace pinpoint::variability
