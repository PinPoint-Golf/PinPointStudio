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

#include "variability_grader.h"   // sampleSd, circular helpers, tracePeak
#include "variability_result.h"   // formatWithUnit

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::variability {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double mean(const std::vector<double> &v)
{
    double s = 0.0;
    for (double x : v) s += x;
    return s / double(v.size());
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
    } else {
        t.usual   = mean(v);
        t.sd      = sampleSd(v);
        deviation = value - t.usual;
    }
    t.z    = zOf(deviation, t.sd);
    t.band = typicalityBand(t.z);
    return t;
}

QString ScalarTypicality::basisString(const QString &unit, int decimals) const
{
    //: Technical-layer typicality basis (variability spec §7.5). %1 this swing's value, %2 the
    //: golfer's usual (window mean), both with their unit.
    return QCoreApplication::translate("VariabilityBasis", "this swing %1 · usual %2")
        .arg(formatWithUnit(value, unit, decimals), formatWithUnit(usual, unit, decimals));
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

    // Shape: RMS over t of z(t). A grid point with no spread in the window carries no z; nor does a
    // circular one whose readings point every way at once (no circular mean).
    const bool circular = in.scaleType == ScaleType::Circular;
    double sumZ2 = 0.0;
    int    used  = 0;
    std::vector<double> column(static_cast<size_t>(n));
    for (int g = 0; g < kTraceGridPoints; ++g) {
        for (int j = 0; j < n; ++j) column[size_t(j)] = in.windowCurves[keep[size_t(j)]][size_t(g)];
        const double sd = circular ? circularSdDeg(column) : sampleSd(column);
        if (!(sd > 0.0) || !std::isfinite(sd)) continue;
        double deviation = 0.0;
        if (circular) {
            const double centre = circularMeanDeg(column);
            if (!std::isfinite(centre)) continue;
            deviation = wrapDeg(in.curve[size_t(g)] - centre);
        } else {
            deviation = in.curve[size_t(g)] - mean(column);
        }
        const double z = deviation / sd;
        sumZ2 += z * z;
        ++used;
    }
    if (used == 0) return std::nullopt;
    t.rmsZ = std::sqrt(sumZ2 / used);
    t.band = typicalityBand(t.rmsZ);

    // Timing: this swing's peak against the window's, in real milliseconds.
    std::vector<double> peakTimes, peakValues;
    for (size_t i : keep) {
        peakTimes.push_back(in.windowPeakTimesMs[i]);
        peakValues.push_back(in.windowPeakValues.empty() ? tracePeak(in.windowCurves[i], in.scaleType)
                                                         : peakMagnitude(in.windowPeakValues[i], in.scaleType));
    }
    t.timingDeviationMs = in.peakTimeMs - mean(peakTimes);
    t.timingZ           = zOf(t.timingDeviationMs, sampleSd(peakTimes));
    t.timing = std::abs(t.timingZ) <= kTypicalityUsualZ || std::round(std::abs(t.timingDeviationMs)) == 0.0
                 ? PeakTiming::OnTime
             : t.timingDeviationMs < 0.0               ? PeakTiming::Early
                                                       : PeakTiming::Late;

    // Amplitude: this swing's peak against the window's peaks, as a sign.
    const double peak = in.peakValue ? peakMagnitude(*in.peakValue, in.scaleType) : tracePeak(in.curve, in.scaleType);
    t.amplitudeZ    = zOf(peak - mean(peakValues), sampleSd(peakValues));
    t.amplitudeSign = std::abs(t.amplitudeZ) <= kTypicalityUsualZ ? 0 : (t.amplitudeZ > 0.0 ? 1 : -1);
    return t;
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
