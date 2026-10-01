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

// The small pieces every shaft-metric error budget is built from
// (docs/design/shaft_uncertainty_propagation_design.md §5–§6): σθ and gross risk read off a
// track at an instant, the grip's positional σ from the pose smoother, the angular σ of a line
// between two uncertain points, and quadrature. Pure functions over the analysis types;
// unit-tested against finite differences in uncertainty_delta_test.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "swing_analysis.h"
#include "../Core/pp_tuned_constants.h"

namespace pinpoint::analysis::shaftsigma {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kRad2Deg = 57.29577951308232;

inline double quad(double a, double b) { return std::sqrt(a * a + b * b); }
inline double quad(double a, double b, double c) { return std::sqrt(a * a + b * b + c * c); }

// σθ (deg), gross risk and rotation rate (deg/s) of a track at time t: the two samples that
// straddle t, the LARGER σ and gross risk of the pair (an instant between a measured and a
// coasted frame is only as good as the coasted one), the rate interpolated. Outside the track
// the boundary sample. NaN σ when the samples were never assessed (σ < 0) or the track is empty.
struct SigmaAt {
    double sigDeg       = kNaN;
    double pGross       = kNaN;
    double thetaDotDegS = kNaN;
};
inline SigmaAt sigmaAt(const std::vector<ShaftSample2D> &S, int64_t t)
{
    SigmaAt r;
    if (S.empty()) return r;
    size_t b = 0;
    while (b < S.size() && S[b].t_us < t) ++b;
    const ShaftSample2D &a = S[b == 0 ? 0 : b - 1];
    const ShaftSample2D &c = S[b >= S.size() ? S.size() - 1 : b];
    if (a.sigmaThetaDeg < 0.f || c.sigmaThetaDeg < 0.f) return r;
    r.sigDeg = std::max(double(a.sigmaThetaDeg), double(c.sigmaThetaDeg));
    r.pGross = std::max(double(std::max(a.pGross, 0.f)), double(std::max(c.pGross, 0.f)));
    const double den = double(c.t_us - a.t_us);
    const double u = den > 0.0 ? std::clamp(double(t - a.t_us) / den, 0.0, 1.0) : 0.0;
    r.thetaDotDegS = (a.thetaDotRadS + u * (c.thetaDotRadS - a.thetaDotRadS)) * kRad2Deg;
    return r;
}

// One hand's positional σ (px) at pose frame k: the median of its 21 keypoints' smoother
// posterior σ. NOT divided by √21 — the keypoints of one hand share their error. ≤ 0 when the
// smoother left no posterior for that hand.
inline double handSigmaPx(const PoseKpAux &aux, int firstKp)
{
    std::vector<double> v;
    v.reserve(21);
    for (int k = firstKp; k < firstKp + 21 && k < kWholeBodyJoints; ++k)
        if (aux.sigma[size_t(k)] > 0.f) v.push_back(aux.sigma[size_t(k)]);
    if (v.empty()) return -1.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

// The grip's σ (px) at time t: ½·√(σ_lead² + σ_trail²) at the nearest smoothed pose frame (the
// grip is the mean of the two hand centroids), floored at kGripSigmaFloorPx when the smoother
// left nothing there.
inline double gripSigmaPx(const PoseTrack2D &pose, int64_t t)
{
    const double floorPx = tuned::uncertainty::kGripSigmaFloorPx;
    if (pose.smoothed.empty() || pose.smoothedAux.size() != pose.smoothed.size()) return floorPx;
    size_t best = 0;
    int64_t bd = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < pose.smoothed.size(); ++i) {
        const int64_t d = std::llabs(pose.smoothed[i].t_us - t);
        if (d < bd) { bd = d; best = i; }
    }
    const double l = handSigmaPx(pose.smoothedAux[best], kLeftHandFirstKp);
    const double r = handSigmaPx(pose.smoothedAux[best], kRightHandFirstKp);
    if (l <= 0.0 || r <= 0.0) return floorPx;
    return std::max(floorPx, 0.5 * quad(l, r));
}

// Angular σ (deg) of the direction of a segment of length lenPx whose two ends carry
// independent positional σ (px): σ_ang ≈ √(σa² + σb²) / L (small-angle, perpendicular share).
inline double lineAngleSigmaDeg(double sigAPx, double sigBPx, double lenPx)
{
    if (!(lenPx > 1.0)) return kNaN;
    return quad(sigAPx, sigBPx) / lenPx * kRad2Deg;
}

// The timing term of an instant reading: |d(value)/dt| × σ_t, with the rate in units/s and
// σ_t in µs.
inline double timingTerm(double rateUnitsPerS, double sigmaTUs)
{
    if (!std::isfinite(rateUnitsPerS) || !(sigmaTUs > 0.0)) return 0.0;
    return std::abs(rateUnitsPerS) * sigmaTUs * 1e-6;
}

// σ_t (µs) of the ladder's impact instant: the P7 position's own σ_t when the tracker assessed
// it, else the trigger's scatter.
inline double impactSigmaTUs(const ShaftTrack2D &shaft)
{
    for (const ShaftPosition &p : shaft.positions)
        if (p.p == 7 && p.sigmaTUs >= 0.f) return double(p.sigmaTUs);
    return tuned::uncertainty::kTriggerSigmaUs;
}

inline uint8_t kindOf(bool calibrated) { return uint8_t(calibrated ? SigmaKind::Calibrated : SigmaKind::Propagated); }

} // namespace pinpoint::analysis::shaftsigma
