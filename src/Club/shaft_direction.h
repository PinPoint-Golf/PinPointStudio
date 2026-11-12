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

// shaftDirection — WHERE THE SHAFT POINTS against the target line, read at P2 (shaft parallel,
// going back) and at the top (docs/design/shaft_fusion_design.md §7; the planned-metrics promotion,
// 12 Nov 2026). Pure std on top of shaft_fusion.h and skeleton3d_fit.h (also std-only): no Qt, no tracker types, standalone-testable.
// ShaftDirectionStage (analysis_pipeline.cpp) feeds it the fused track and the 3-D skeleton's
// shaft and turns the result into the series and the trust records.
//
// THE FRAME is the fusion's (shaft_fusion.h): X = face-on image-right, Y = the face-on view ray
// (camera → golfer), Z = up. The golfer faces the face-on camera with the ball between them, so the
// golfer stands on the +Y side of the target line and the FAR side — beyond the ball — is −Y, for a
// right-hander and a left-hander alike. +X is the target for a right-hander, −X for a left-hander.
//
// THE SIGN (pinpoint_sign_conventions.md, ball-flight table): POSITIVE = the clubhead end of the
// shaft points to the FAR side of the target line, away from the golfer — across the line at the
// top, outside in the takeaway; right of the target for a right-hander. Negative = toward the
// golfer: laid off at the top, inside in the takeaway. Zero = parallel to the target line.
//
//     bearing = atan2(−u_y, |u_x|)                 (u = butt → head, unit; degrees)
//
// It is the angle of u's ground projection to the target LINE — a line, not a ray — signed by which
// side of it the head end lies on. That one rule serves both positions: at P2 the shaft points AWAY
// from the target (u_x < 0 for a right-hander), at the top AT it (u_x > 0), and |u_x| reads each
// against the half of the line it points along. It is continuous everywhere the projection is not
// zero: through the end-on case (u ≈ ±X, the down-the-line camera looking straight down the shaft,
// bearing ≈ 0) and through u_x = 0, where the ray-referenced readings of the two positions
// (atan2(−u_y, −u_x) at P2, atan2(−u_y, u_x) at the top) meet at the same ±90°. So it does not flip
// as the shaft turns from pointing back to pointing at the target, and no phase argument is needed:
// a ray-referenced rule would read a short backswing whose shaft still points back at the top as
// ±180°, which is not a direction any fault means.
//
// LEFT-HANDERS ARE MIRRORED: the golfer frame mirrors X (the target becomes +X), and Y — the side
// the golfer stands on — is unchanged by the mirror, so the bearing's meaning (across / outside is
// +) holds for both hands. Because of |u_x| the mirror leaves the number unchanged; shaft_fusion_test
// pins that, so a later change of frame cannot quietly invert a left-hander's faults.
//
// σ OF A BEARING. A direction's 1σ δ (deg) moves the ground heading by δ / |h|, |h| the projection's
// length, so the bearing's σ is  √((δ/|h|)² + σ_yaw²),  σ_yaw the down-the-line camera's yaw σ: the
// heading moves one-for-one with it (shaft_fusion.h), which is why the fusion never claimed one
// until it could carry that σ and a trust record (club3d.cameraAssumed).
//
// THE SOURCE RULE, per position, deterministic:
//   1. the nearest FUSED frame within ±kFusionWindowUs that is face-on Measured and carries none of
//      kFusionRejectFlags (SignDisagree, IllConditioned, Reflected) — σ = its sigmaDeg;
//   2. else the nearest 3-D SKELETON frame within the same window whose shaft one or both cameras saw
//      (shaftTier ≥ kSkeletonMinShaftTier) — σ = the provisional tier σ (the fit publishes none);
//   3. else no reading at that position (a "-", never a 0).
// Either source also needs a ground projection of at least kMinHorizontalFrac. The rule picks the
// SOURCE; assemble() then makes the curve and the value agree with how the diagnostics grade it (the
// curve's median within ±15 ms — see there).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>   // std::llabs
#include <limits>
#include <vector>

#include "shaft_fusion.h"
#include "../Core/pp_tuned_constants.h"
#include "../Skeleton3d/skeleton3d_fit.h"   // Tier: the skeleton's shaftTier values (pinned below)

namespace pinpoint::analysis::shaftdir {

using fusion::Vec3;

static_assert(pinpoint::tuned::shaftDirection::kFusionRejectFlags
                  == uint8_t(fusion::SignDisagree | fusion::IllConditioned | fusion::Reflected),
              "kFusionRejectFlags must be SignDisagree | IllConditioned | Reflected");
static_assert(pinpoint::tuned::shaftDirection::kSkeletonMinShaftTier == int(::pinpoint::skeleton3d::TierConstrained),
              "kSkeletonMinShaftTier is the skeleton's 'constrained' tier: one camera saw the shaft");

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// Length of u's ground projection as a fraction of u's length (0 = vertical).
inline double horizontalFrac(const Vec3 &u)
{
    const double n = u.norm();
    return n > 0 ? std::hypot(u.x, u.y) / n : 0.0;
}

// The signed bearing (deg) of a butt → head direction against the target line; + = the far side of
// the line from the golfer (across / outside). NaN for a vertical (or zero) direction.
inline double shaftBearingDeg(const Vec3 &u, bool leftHanded)
{
    const double x = leftHanded ? -u.x : u.x;   // the golfer frame: the target at +X for both hands
    if (!(std::hypot(x, u.y) > 0)) return kNan;
    return std::atan2(-u.y, std::fabs(x)) * 180.0 / fusion::kPi;
}

// The bearing's 1σ (deg) from the direction's 1σ and the camera's yaw σ. NaN when the direction's
// σ was not assessed or the projection is empty.
inline double shaftBearingSigmaDeg(const Vec3 &u, double dirSigmaDeg, double yawSigmaDeg)
{
    const double h = horizontalFrac(u);
    if (!std::isfinite(dirSigmaDeg) || !(h > 0)) return kNan;
    return std::hypot(dirSigmaDeg / h, std::isfinite(yawSigmaDeg) ? yawSigmaDeg : 0.0);
}

enum class Source : uint8_t { None = 0, Fusion = 1, Fit = 2 };

// One frame of the 3-D skeleton's shaft (skeleton3d FitResult::shaftDir / shaftTier), copied out so
// this header does not need the solver's types.
struct FitFrame {
    int64_t t_us = 0;
    Vec3    u;            // butt → head, unit, the fusion's frame (skeleton3d_rig.h: the same frame)
    uint8_t tier = 0;     // 0 none, 1 model only, 2 one view, 3 both views
};

struct Config {
    int64_t windowUs          = pinpoint::tuned::shaftDirection::kFusionWindowUs;
    uint8_t rejectFlags       = pinpoint::tuned::shaftDirection::kFusionRejectFlags;
    int     minFitTier        = pinpoint::tuned::shaftDirection::kSkeletonMinShaftTier;
    double  fitSigmaBothDeg   = pinpoint::tuned::shaftDirection::kShaftDirSkeletonSigmaDeg;
    double  fitSigmaOneDeg    = pinpoint::tuned::shaftDirection::kShaftDirSkeletonOneViewSigmaDeg;
    double  minHorizontalFrac = pinpoint::tuned::shaftDirection::kMinHorizontalFrac;
    // The window the diagnostics grade a position over (measure_sample.cpp: the curve's median within
    // ±sampler::kWindowHalfUs). assemble() makes the curve agree with each position's source inside it.
    int64_t gradeHalfWindowUs = pinpoint::tuned::sampler::kWindowHalfUs;
    // The caller sets these: the camera's yaw σ (kDtlYawSigmaUncalDeg / kDtlYawSigmaCalDeg by the
    // job's calibration), the handedness, and whether σ is written at all (uncertainty.enabled).
    double  yawSigmaDeg = pinpoint::tuned::dtlCamera::kDtlYawSigmaUncalDeg;
    bool    leftHanded  = false;
    bool    withSigma   = true;
};

// A reading: a value, its σ (NaN = not assessed / not written), the instant it was read at and where
// it came from. src None = no reading.
struct Reading {
    Source  src = Source::None;
    int64_t sampleUs = 0;
    double  deg = kNan;
    double  sigmaDeg = kNan;
    uint8_t fitTier = 0;   // the skeleton frame's shaft tier, when src == Fit
};

inline bool fusionUsable(const fusion::Sample3D &s, const Config &c)
{
    return s.foSrc == fusion::FoSource::Measured && !(s.flags & c.rejectFlags)
        && horizontalFrac(s.u) >= c.minHorizontalFrac;
}
inline bool fitUsable(const FitFrame &f, const Config &c)
{
    return int(f.tier) >= c.minFitTier && horizontalFrac(f.u) >= c.minHorizontalFrac;
}

inline Reading fromFusion(const fusion::Sample3D &s, const Config &c)
{
    Reading r;
    r.src = Source::Fusion;
    r.sampleUs = s.t_us;
    r.deg = shaftBearingDeg(s.u, c.leftHanded);
    if (c.withSigma) r.sigmaDeg = shaftBearingSigmaDeg(s.u, s.sigmaDeg, c.yawSigmaDeg);
    return r;
}
inline Reading fromFit(const FitFrame &f, const Config &c)
{
    Reading r;
    r.src = Source::Fit;
    r.sampleUs = f.t_us;
    r.fitTier = f.tier;
    r.deg = shaftBearingDeg(f.u, c.leftHanded);
    if (c.withSigma)
        r.sigmaDeg = shaftBearingSigmaDeg(f.u, f.tier >= 3 ? c.fitSigmaBothDeg : c.fitSigmaOneDeg, c.yawSigmaDeg);
    return r;
}

// The source rule at one instant (see the header): fusion first, the skeleton second, else none.
inline Reading readAt(const std::vector<fusion::Sample3D> &fused, const std::vector<FitFrame> &fit, int64_t tUs,
                      const Config &c)
{
    const fusion::Sample3D *bestF = nullptr;
    int64_t dF = std::numeric_limits<int64_t>::max();
    for (const fusion::Sample3D &s : fused) {
        if (!fusionUsable(s, c)) continue;
        const int64_t d = std::llabs(s.t_us - tUs);
        if (d <= c.windowUs && d < dF) { dF = d; bestF = &s; }
    }
    if (bestF) return fromFusion(*bestF, c);
    const FitFrame *bestK = nullptr;
    int64_t dK = std::numeric_limits<int64_t>::max();
    for (const FitFrame &f : fit) {
        if (!fitUsable(f, c)) continue;
        const int64_t d = std::llabs(f.t_us - tUs);
        if (d <= c.windowUs && d < dK) { dK = d; bestK = &f; }
    }
    if (bestK) return fromFit(*bestK, c);
    return {};
}

// One point of the curve. valid false = bridged: a frame the curve does not read (a fused frame the
// rule refused, or a reading of the source a position did NOT choose, inside that position's
// grading window), its value the straight line between the valid neighbours — drawn dashed, skipped
// by every reducer, never the site of a phase reading.
struct Point {
    int64_t t_us = 0;
    double  deg = kNan;
    double  sigmaDeg = kNan;
    Source  src = Source::None;
    bool    valid = false;
    uint8_t fitTier = 0;
};

struct Curve {
    std::vector<Point> points;     // ascending in time; first and last are valid
    int nFusion = 0, nFit = 0;     // valid points from each source
    double medianSigmaDeg = kNan;  // over the valid points with a σ
};

// A position to read: the caller's tag (a Phase, cast) and its instant.
struct PhaseIn  { int id = 0; int64_t tUs = 0; };
struct PhaseOut { int id = 0; int64_t tUs = 0; Reading r; };

struct Assembly {
    Curve curve;
    std::vector<PhaseOut> phases;  // one per PhaseIn, in order; r.src None = no reading there
};

// The median the grader takes (series_reduce.h reduceAt / detail::medianOfWindow): the middle value,
// the mean of the two middles when the count is even.
inline double medianOf(std::vector<double> v)
{
    if (v.empty()) return kNan;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// THE CURVE AND THE READINGS, MADE TO AGREE (W1 review, 12 Nov 2026). The diagnostics grade a
// position off the CURVE — the median of its valid samples within ±gradeHalfWindowUs of the
// position's instant (measure_sample.cpp → series_reduce.h reduceAt) — and fall back to the phase
// reading only when that window holds none, while always stamping the phase reading's σ. So:
//
//   1. each position's SOURCE is decided by the rule (readAt: fusion within ±windowUs, else the
//      skeleton, else none);
//   2. the curve is every usable fused frame, plus the usable skeleton frames no usable fused frame
//      lies within ±windowUs of, plus the refused fused frames between them, bridged;
//   3. INSIDE EACH POSITION'S GRADING WINDOW only the chosen source's frames are valid: the other
//      source's are marked bridged, and where the skeleton was chosen every usable skeleton frame in
//      the window joins the curve (step 2 had kept some out for lying near a fused frame further off);
//   4. the position's VALUE is then the grader's own median over the curve's valid frames in that
//      window (and its σ the median of their σs); with none there it is the nearest chosen-source
//      frame within ±windowUs, exactly the phase reading the grader falls back to.
//
// So the number graded, the number stamped and the σ stamped come from the same frames of the same
// source, whichever path the grader takes. Two positions whose windows overlap keep each other's
// choices: a skeleton frame is never added inside a window whose position chose fusion.
inline Assembly assemble(const std::vector<fusion::Sample3D> &fused, const std::vector<FitFrame> &fit,
                         const std::vector<PhaseIn> &phases, const Config &c)
{
    Assembly out;
    for (const PhaseIn &ph : phases) out.phases.push_back({ ph.id, ph.tUs, readAt(fused, fit, ph.tUs, c) });

    const int64_t W = c.gradeHalfWindowUs;
    const auto inWin = [W](int64_t t, int64_t at) { return std::llabs(t - at) <= W; };
    const auto chosenAt = [&](int64_t t, Source s) {   // t lies in the window of a position that chose s
        for (const PhaseOut &po : out.phases)
            if (po.r.src == s && inWin(t, po.tUs)) return true;
        return false;
    };

    std::vector<int64_t> goodT;
    for (const fusion::Sample3D &s : fused)
        if (fusionUsable(s, c)) goodT.push_back(s.t_us);
    std::sort(goodT.begin(), goodT.end());
    const auto nearAny = [&c](const std::vector<int64_t> &ts, int64_t t) {
        auto it = std::lower_bound(ts.begin(), ts.end(), t - c.windowUs);
        return it != ts.end() && *it <= t + c.windowUs;
    };
    std::vector<Point> pts;
    std::vector<int64_t> fitT;
    for (const FitFrame &f : fit) {
        if (!fitUsable(f, c)) continue;
        // Step 2's rule, or step 3's: a skeleton frame in the window of a position that chose it.
        if (nearAny(goodT, f.t_us) && !chosenAt(f.t_us, Source::Fit)) continue;
        const Reading r = fromFit(f, c);
        pts.push_back({ f.t_us, r.deg, r.sigmaDeg, Source::Fit, true, f.tier });
        fitT.push_back(f.t_us);
    }
    std::sort(fitT.begin(), fitT.end());
    for (const fusion::Sample3D &s : fused) {
        if (fusionUsable(s, c)) {
            const Reading r = fromFusion(s, c);
            // Step 3: a fused frame in the window of a position that chose the skeleton is bridged.
            // (Unreachable by the rule — the skeleton is chosen only when no usable fused frame lies
            // within ±windowUs ⊇ the grading window — and kept so the invariant does not rest on it.)
            pts.push_back({ s.t_us, r.deg, r.sigmaDeg, Source::Fusion, !chosenAt(s.t_us, Source::Fit), 0 });
        } else if (!nearAny(fitT, s.t_us)) {
            pts.push_back({ s.t_us, kNan, kNan, Source::None, false, 0 });
        }
    }
    std::stable_sort(pts.begin(), pts.end(), [](const Point &a, const Point &b) { return a.t_us < b.t_us; });
    // Step 3 for the skeleton's frames: in the window of a position that chose fusion, bridged — the
    // case the review found: the chosen fused frame 15–25 ms off, so skeleton frames inside the window
    // were more than ±windowUs from it and step 2 had kept them valid.
    for (Point &q : pts)
        if (q.src == Source::Fit && q.valid && chosenAt(q.t_us, Source::Fusion)) q.valid = false;

    size_t lo = 0, hi = pts.size();
    while (lo < hi && !pts[lo].valid) ++lo;
    while (hi > lo && !pts[hi - 1].valid) --hi;
    if (lo < hi) {
        pts = std::vector<Point>(pts.begin() + long(lo), pts.begin() + long(hi));
        // Bridge: every invalid point takes the straight line between its valid neighbours.
        size_t prev = 0;
        for (size_t i = 1; i < pts.size(); ++i) {
            if (!pts[i].valid) continue;
            for (size_t k = prev + 1; k < i; ++k) {
                const Point &a = pts[prev], &b = pts[i];
                const double w = b.t_us > a.t_us ? double(pts[k].t_us - a.t_us) / double(b.t_us - a.t_us) : 0.0;
                pts[k].deg = a.deg + w * (b.deg - a.deg);
            }
            prev = i;
        }
        std::vector<double> sig;
        for (const Point &q : pts) {
            if (!q.valid) continue;
            if (q.src == Source::Fusion) ++out.curve.nFusion;
            if (q.src == Source::Fit) ++out.curve.nFit;
            if (std::isfinite(q.sigmaDeg)) sig.push_back(q.sigmaDeg);
        }
        if (!sig.empty()) {
            std::nth_element(sig.begin(), sig.begin() + long(sig.size() / 2), sig.end());
            out.curve.medianSigmaDeg = sig[sig.size() / 2];
        }
        out.curve.points = std::move(pts);
    }

    // Step 4: each position's value is the grader's median over the curve's window, when it has one.
    for (PhaseOut &po : out.phases) {
        if (po.r.src == Source::None) continue;
        std::vector<double> v, s;
        const Point *nearest = nullptr;
        for (const Point &q : out.curve.points) {
            if (!q.valid || !std::isfinite(q.deg) || !inWin(q.t_us, po.tUs)) continue;
            v.push_back(q.deg);
            if (std::isfinite(q.sigmaDeg)) s.push_back(q.sigmaDeg);
            if (!nearest || std::llabs(q.t_us - po.tUs) < std::llabs(nearest->t_us - po.tUs)) nearest = &q;
        }
        if (v.empty()) continue;   // the grader falls back to the phase reading: the nearest frame, as read
        po.r.deg      = medianOf(v);
        po.r.sigmaDeg = s.size() == v.size() ? medianOf(s) : kNan;
        po.r.sampleUs = nearest->t_us;
        po.r.src      = nearest->src;   // == the chosen source, by step 3
        po.r.fitTier  = nearest->fitTier;
    }
    return out;
}

// The curve alone (no positions to honour).
inline Curve buildCurve(const std::vector<fusion::Sample3D> &fused, const std::vector<FitFrame> &fit, const Config &c)
{
    return assemble(fused, fit, {}, c).curve;
}

} // namespace pinpoint::analysis::shaftdir
