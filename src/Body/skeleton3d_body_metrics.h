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

// READINGS FROM THE 3-D BODY — the per-frame geometry Skeleton3dBodyStage reads off the two-camera
// skeleton fit (skeleton3d_fit.h FitResult; planned-metrics promotion, 12 Nov 2026). Pure, no Qt:
// skeleton3d_body_series.h turns these into the published series, and skeleton3d_body_metrics_test
// pins every value and sign on synthetic joint sets.
//
// THE FRAME (skeleton3d_rig.h): +X face-on image-right, +Y the face-on view ray (away from that
// camera), +Z up. The golfer faces the face-on camera (their front −Y), so for a right-hander +X is
// the target and the golfer stands on the +Y side of the target line, the ball between them and the
// camera. A left-hander is the mirror image in X: target −X, still on the +Y side.
//
// THE JOINTS (ybot_rig.h): shoulders LeftArm 8 / RightArm 13 (the upper arm's root), hips LeftUpLeg 17
// / RightUpLeg 22, knees LeftLeg 18 / RightLeg 23, ankles LeftFoot 19 / RightFoot 24. Lead = left for
// a right-hander. A joint is read only at TierConstrained or better — measured, or held by both views'
// geometry — never merely inferred (the rule skeletonLines() applies, analysis_pipeline.cpp), and only
// with its posterior σ (`sigmaM`, per axis, metres: trace(Σ)/3, skeleton3d_fit.cpp).
//
// THE READINGS, each in degrees with a first-order σ propagated from the joints' σ (isotropic per axis,
// so only the component of a joint's error that moves the angle counts):
//
//   shoulderPlaneDeg  the shoulder line's inclination out of the horizontal, in its own vertical
//                     plane: atan2(z_trail − z_lead, |horizontal|). + = the TRAIL shoulder higher.
//                     σ = √(σ_lead² + σ_trail²) / |line|. No camera-yaw term: an inclination does
//                     not turn with the camera's heading.
//   shoulderAimDeg    the shoulder line's bearing in the ground plane against the target line,
//                     atan2(−d_y, d_x') with d = lead − trail and d_x' = d_x toward the target
//                     (mirrored for a left-hander). OPEN IS NEGATIVE: open aims the lead end toward
//                     the golfer's side of the target line (+Y) for either hand. σ = √(σ_l² + σ_t²)
//                     / |horizontal| — the LINE's σ only; the camera-yaw term is added by the series
//                     (skeleton3d_body_series.h), which also fires the camera-assumed record.
//   kneeFlexDeg       180° − the angle at the knee between hip and ankle: the thigh against the
//                     shin, 0 = a straight leg (dtl_posture.h kneeFlexDeg, in 3-D). σ: the angle's
//                     gradient — 1/|a| for the hip, 1/|b| for the ankle, √(1/a² + 1/b² − 2cos φ/(ab))
//                     for the knee (a, b the two segments, φ the interior angle).
//   sideBendDeg       the hip line's frontal tilt minus the shoulder line's, both taken in the
//                     frontal plane square to the PELVIS heading at that frame (spanned by the hip
//                     line's ground direction and the vertical), so the turn does not leak in; each
//                     tilt is + with the trail end higher, as the face-on lineTiltDeg is. + = side
//                     bend toward the TRAIL side (the trail shoulder dropped against the hips): the
//                     face-on route's sign (upper_body_metrics.cpp). σ = the two tilts' σ in
//                     quadrature, each line's joint σ over its length in that plane. Left out: the
//                     hip line's own error TURNING the plane. That is first order once the shoulders
//                     have turned against the hips (the shoulder tilt moves by ≈ ε·|s|·sin β·s_z / L²
//                     for a plane turn ε and an X-factor β) but small — 0.7° against 5.6° on the
//                     §7 Monte Carlo pose (β 20°), whose σ the formula meets within 3 %.
//   armLiftDeg        (D2, 13 Nov 2026) the lead UPPER ARM against the shoulder-turn plane at that
//                     frame: asin(â · n̂), â the unit vector lead shoulder (LeftArm) → lead ELBOW
//                     (LeftForeArm) — NEVER to the wrist (Mark, 13 Nov: a bent lead arm makes the arm
//                     look lifted, and is its own fault, bent_lead_arm) — and n̂ the spine (hip midpoint,
//                     LeftUpLeg/RightUpLeg → shoulder midpoint) with its component along the shoulder
//                     line removed: the normal of the plane that holds the shoulder line and is as
//                     square to the spine as such a plane can be (plane_stack RESULTS.md, a2). 0 = the
//                     upper arm lies in the plane the shoulders turn on (one-plane); + = lifted above
//                     it, toward the head; − = under it. Mirror-invariant: only relative vectors.
//                     σ: first order, the five joints' isotropic per-axis σ through the exact gradient,
//                     taken by central differences (the closed form is long and buys nothing).
//   leadElbowBendDeg  (D2) 180° − the angle at the lead elbow between the lead shoulder and the lead
//                     wrist (LeftArm, LeftForeArm, LeftHand): 0 = straight, + = more bend — the wrist
//                     sensors' leadArmFlexion sign. Read ONLY where the fit's lead elbow is MEASURED
//                     (TierMeasured: both cameras saw it, FB's honest tiers); the shoulder and wrist at
//                     TierConstrained or better. σ: kneeFlexDeg's formula on the arm.
//   thighHeadingRad   the heading in the ground plane of the normal to one leg's hip–knee–ankle
//                     plane — the plane the knee hinge bends in, which turns with the thigh.
//                     Unmeasured below kThighTurnMinKneeFlexDeg of knee bend (no plane) or when the
//                     normal is within 30° of vertical. σ: each joint's out-of-plane error tips the
//                     plane about the line through the other two (by σ / its distance from that
//                     line), and each tip is carried exactly onto the heading.
//   thighTurn         that heading's change from Address, LEAD-RELATIVE like pelvisRotationSigned
//                     (body_rotation.cpp): + = turned toward the target, − = toward the trail side,
//                     for either hand — so W3's hip internal rotation can subtract it from the
//                     pelvis's turn. Its reference and σ follow the two-camera turn exactly
//                     (triReference: the median of the headings within addrWindowUs of Address, the
//                     nearest no further than triAddrMaxGapUs; σ = √(σ_t² + σ_ref² + (frac·Δ)²)).

#include "../Core/pp_tuned_constants.h"
#include "../Skeleton3d/skeleton3d_fit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace pinpoint::analysis::body3d {

namespace sk = pinpoint::skeleton3d;

inline constexpr double kRad2Deg = 57.29577951308232;
inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// One frame's reading: NaN value = no reading at this frame. σ NaN = not assessed.
struct Reading {
    double deg   = kNaN;
    double sigma = kNaN;   // degrees
    bool ok() const { return std::isfinite(deg); }
};

inline double wrapRadPi(double a)
{
    const double twoPi = 2.0 * 3.14159265358979323846;
    a = std::fmod(a + 3.14159265358979323846, twoPi);
    if (a < 0) a += twoPi;
    return a - 3.14159265358979323846;
}

// The four joints of one side, by handedness.
struct Side { int shoulder, hip, knee, ankle; };
inline Side sideJoints(bool left)
{
    using namespace sk::ybot;
    return left ? Side{ LeftArm, LeftUpLeg, LeftLeg, LeftFoot } : Side{ RightArm, RightUpLeg, RightLeg, RightFoot };
}

// A joint at frame t, only at TierConstrained or better and with its σ.
inline bool jointAt(const sk::FitResult &fr, size_t t, int j, sk::V3 &p, double &sigma)
{
    if (t >= fr.joints.size() || t >= fr.tier.size() || t >= fr.sigmaM.size()) return false;
    if (fr.tier[t][size_t(j)] < sk::TierConstrained) return false;
    const double s = double(fr.sigmaM[t][size_t(j)]);
    if (!std::isfinite(s) || s < 0.0) return false;
    p = fr.joints[t][size_t(j)];
    sigma = s;
    return true;
}

// A body line shorter than this (in whatever plane it is read) is not a line the fit can point:
// the two-camera turn's floor, shared so both read the same lines the same way.
inline double minLineM() { return tuned::bodyRotation::kTriMinBaselineM; }

inline Reading shoulderPlaneDeg(const sk::FitResult &fr, size_t t, bool leadIsLeft)
{
    Reading r;
    const Side L = sideJoints(leadIsLeft), T = sideJoints(!leadIsLeft);
    sk::V3 pl, pt; double sl, st;
    if (!jointAt(fr, t, L.shoulder, pl, sl) || !jointAt(fr, t, T.shoulder, pt, st)) return r;
    const sk::V3 d = pl - pt;
    const double len = d.norm();
    if (len < minLineM()) return r;
    r.deg   = std::atan2(pt.z - pl.z, std::hypot(d.x, d.y)) * kRad2Deg;
    r.sigma = std::hypot(sl, st) / len * kRad2Deg;
    return r;
}

inline Reading shoulderAimDeg(const sk::FitResult &fr, size_t t, bool leadIsLeft)
{
    Reading r;
    const Side L = sideJoints(leadIsLeft), T = sideJoints(!leadIsLeft);
    sk::V3 pl, pt; double sl, st;
    if (!jointAt(fr, t, L.shoulder, pl, sl) || !jointAt(fr, t, T.shoulder, pt, st)) return r;
    const double dx = leadIsLeft ? pl.x - pt.x : -(pl.x - pt.x);   // toward the target
    const double dy = pl.y - pt.y;
    const double lh = std::hypot(dx, dy);
    if (lh < minLineM()) return r;
    r.deg   = std::atan2(-dy, dx) * kRad2Deg;
    r.sigma = std::hypot(sl, st) / lh * kRad2Deg;
    return r;
}

// 180° − the interior angle at hinge `k` between ends `h` and `a`, with its first-order σ (kneeFlexDeg's
// derivation, the hinge in both segments). The knee and the lead elbow share it.
inline Reading hingeBendDeg(const sk::V3 &h, double sh, const sk::V3 &k, double sk_, const sk::V3 &a, double sa)
{
    Reading r;
    const sk::V3 u = h - k, v = a - k;   // the interior angle φ is between these
    const double lu = u.norm(), lv = v.norm();
    if (lu < minLineM() || lv < minLineM()) return r;
    const double c = std::clamp(u.dot(v) / (lu * lv), -1.0, 1.0);
    const double phi = std::acos(c);
    r.deg = 180.0 - phi * kRad2Deg;
    const double gk2 = std::max(0.0, 1.0 / (lu * lu) + 1.0 / (lv * lv) - 2.0 * c / (lu * lv));
    r.sigma = std::sqrt(sh * sh / (lu * lu) + sa * sa / (lv * lv) + sk_ * sk_ * gk2) * kRad2Deg;
    return r;
}

inline Reading kneeFlexDeg(const sk::FitResult &fr, size_t t, bool leftLeg)
{
    const Side S = sideJoints(leftLeg);
    sk::V3 h, k, a; double sh, sk_, sa;
    if (!jointAt(fr, t, S.hip, h, sh) || !jointAt(fr, t, S.knee, k, sk_) || !jointAt(fr, t, S.ankle, a, sa)) return {};
    return hingeBendDeg(h, sh, k, sk_, a, sa);
}

// The lead arm's three joints by handedness: the shoulder (the upper arm's root), the elbow (the
// forearm's root) and the wrist (the hand's root).
struct ArmJoints { int shoulder, elbow, wrist; };
inline ArmJoints armJoints(bool left)
{
    using namespace sk::ybot;
    return left ? ArmJoints{ LeftArm, LeftForeArm, LeftHand } : ArmJoints{ RightArm, RightForeArm, RightHand };
}

// The lead elbow's bend, only where the fit MEASURED the elbow (both views saw it; FB's honest tier).
inline Reading leadElbowBendDeg(const sk::FitResult &fr, size_t t, bool leadIsLeft)
{
    const ArmJoints A = armJoints(leadIsLeft);
    if (t >= fr.tier.size() || fr.tier[t][size_t(A.elbow)] < sk::TierMeasured) return {};
    sk::V3 s, e, w; double ss, se, sw;
    if (!jointAt(fr, t, A.shoulder, s, ss) || !jointAt(fr, t, A.elbow, e, se) || !jointAt(fr, t, A.wrist, w, sw)) return {};
    return hingeBendDeg(s, ss, e, se, w, sw);
}

// asin(â · n̂) in degrees for the lead upper arm against the shoulder-turn plane (top of file); NaN where a
// line is too short to point. Pure, so armLiftDeg can differentiate it.
inline double armLiftOf(const sk::V3 &ls, const sk::V3 &le, const sk::V3 &ts, const sk::V3 &lh, const sk::V3 &th)
{
    const sk::V3 s = ts - ls, a = le - ls;
    const double lsn = s.norm(), lan = a.norm();
    if (lsn < minLineM() || lan < minLineM()) return kNaN;
    const sk::V3 sh = s * (1.0 / lsn);
    const sk::V3 spine = (ls + ts) * 0.5 - (lh + th) * 0.5;
    const sk::V3 n = spine - sh * spine.dot(sh);
    const double nn = n.norm();
    if (nn < minLineM()) return kNaN;
    const double c = std::clamp(a.dot(n) / (lan * nn), -1.0, 1.0);
    return std::asin(c) * kRad2Deg;
}

inline Reading armLiftDeg(const sk::FitResult &fr, size_t t, bool leadIsLeft)
{
    Reading r;
    const ArmJoints A = armJoints(leadIsLeft), T = armJoints(!leadIsLeft);
    const Side L = sideJoints(leadIsLeft), Tr = sideJoints(!leadIsLeft);
    std::array<sk::V3, 5> p;
    std::array<double, 5> sg;
    const int js[5] = { A.shoulder, A.elbow, T.shoulder, L.hip, Tr.hip };
    for (int k = 0; k < 5; ++k)
        if (!jointAt(fr, t, js[k], p[size_t(k)], sg[size_t(k)])) return r;
    const auto f = [&](const std::array<sk::V3, 5> &q) { return armLiftOf(q[0], q[1], q[2], q[3], q[4]); };
    r.deg = f(p);
    if (!std::isfinite(r.deg)) return Reading{};
    // σ² = Σ_joint σ_j² · |∇_j f|², each joint's σ isotropic per axis (sigmaM); the gradient by central
    // differences of 0.1 mm, a step far below every joint's σ and every line's length.
    const double h = 1e-4;
    double var = 0.0;
    for (size_t k = 0; k < 5; ++k)
        for (int ax = 0; ax < 3; ++ax) {
            std::array<sk::V3, 5> up = p, dn = p;
            double *u = ax == 0 ? &up[k].x : ax == 1 ? &up[k].y : &up[k].z;
            double *d = ax == 0 ? &dn[k].x : ax == 1 ? &dn[k].y : &dn[k].z;
            *u += h; *d -= h;
            const double fu = f(up), fd = f(dn);
            if (!std::isfinite(fu) || !std::isfinite(fd)) return r;   // σ not assessed on a degenerate frame
            const double g = (fu - fd) / (2.0 * h);                   // degrees per metre
            var += sg[k] * sg[k] * g * g;
        }
    r.sigma = std::sqrt(var);
    return r;
}

inline Reading sideBendDeg(const sk::FitResult &fr, size_t t, bool leadIsLeft)
{
    Reading r;
    const Side L = sideJoints(leadIsLeft), T = sideJoints(!leadIsLeft);
    sk::V3 lh, th, ls, ts; double slh, sth, sls, sts;
    if (!jointAt(fr, t, L.hip, lh, slh) || !jointAt(fr, t, T.hip, th, sth)
        || !jointAt(fr, t, L.shoulder, ls, sls) || !jointAt(fr, t, T.shoulder, ts, sts)) return r;
    const sk::V3 p = lh - th, s = ls - ts;
    const double ph = std::hypot(p.x, p.y);           // the hip line's ground length
    if (ph < minLineM()) return r;
    const double hx = p.x / ph, hy = p.y / ph;         // the frontal plane's horizontal axis
    const double along = s.x * hx + s.y * hy;          // the shoulder line in that plane
    if (std::fabs(along) < minLineM()) return r;
    const double hipTilt      = std::atan2(-p.z, ph);
    const double shoulderTilt = std::atan2(-s.z, std::fabs(along));
    r.deg = (hipTilt - shoulderTilt) * kRad2Deg;
    const double sH = std::hypot(slh, sth) / std::hypot(ph, p.z);
    const double sS = std::hypot(sls, sts) / std::hypot(along, s.z);
    r.sigma = std::hypot(sH, sS) * kRad2Deg;
    return r;
}

// The heading (rad) of the normal to one leg's hip–knee–ankle plane, and its σ (rad).
struct Heading {
    double rad   = kNaN;
    double sigma = kNaN;
    bool ok() const { return std::isfinite(rad); }
};
inline Heading thighHeading(const sk::FitResult &fr, size_t t, bool leftLeg,
                            double minKneeFlexDeg = tuned::body3d::kThighTurnMinKneeFlexDeg)
{
    Heading out;
    const Reading flex = kneeFlexDeg(fr, t, leftLeg);
    if (!flex.ok() || flex.deg < minKneeFlexDeg) return out;
    const Side S = sideJoints(leftLeg);
    sk::V3 h, k, a; double sh, sk_, sa;
    if (!jointAt(fr, t, S.hip, h, sh) || !jointAt(fr, t, S.knee, k, sk_) || !jointAt(fr, t, S.ankle, a, sa)) return out;
    const sk::V3 u = k - h, v = a - k;
    const sk::V3 n = u.cross(v);
    const double nn = n.norm();
    if (!(nn > 0.0)) return out;
    const sk::V3 nu = n * (1.0 / nn);
    const double nh = std::hypot(nu.x, nu.y);
    if (nh < tuned::body3d::kThighTurnMinNormalHorizFrac) return out;
    out.rad = std::atan2(nu.y, nu.x);
    // A joint's error out of the plane tips the plane about the line through the other two by
    // δ / (its distance from that line); a tip ε about the unit axis e moves the normal by ε·(e × n),
    // whose heading change is (n_x·Δn_y − n_y·Δn_x) / n_h².
    const auto headingPerRad = [&](const sk::V3 &e) {
        const sk::V3 dn = e.cross(nu);
        return std::fabs(nu.x * dn.y - nu.y * dn.x) / (nh * nh);
    };
    const sk::V3 ha = a - h;
    const double area2 = nn;                                        // |u × v| = twice the triangle
    const double dH = area2 / std::max(1e-9, v.norm());             // hip from the knee–ankle line
    const double dA = area2 / std::max(1e-9, u.norm());             // ankle from the hip–knee line
    const double dK = area2 / std::max(1e-9, ha.norm());            // knee from the hip–ankle line
    const double cH = headingPerRad(v.unit()) / dH;
    const double cA = headingPerRad(u.unit()) / dA;
    const double cK = headingPerRad(ha.unit()) / dK;
    out.sigma = std::sqrt(sh * sh * cH * cH + sa * sa * cA * cA + sk_ * sk_ * cK * cK);
    return out;
}

// One thigh's turn from address over the fit's frames. `turn[i]` NaN = unmeasured at frame i.
struct ThighTurn {
    std::vector<double> turnDeg, sigmaDeg;
    double refRad = kNaN, refSigmaRad = kNaN;
    bool   ok = false;                       // a reference was found at Address
};
struct ThighTurnConfig {
    int64_t addrWindowUs    = tuned::bodyRotation::kAddrWindowUs;
    int64_t addrMaxGapUs    = tuned::bodyRotation::kTriAddrMaxGapUs;
    double  scaleFrac       = tuned::bodyRotation::kTriScaleFrac;   // uncalibrated: the fit solves its own cameras
    double  minKneeFlexDeg  = tuned::body3d::kThighTurnMinKneeFlexDeg;
};
inline ThighTurn thighTurn(const sk::FitResult &fr, bool leftLeg, bool leadIsLeft, int64_t addressUs,
                           const ThighTurnConfig &cfg = {})
{
    ThighTurn out;
    const size_t n = fr.t_us.size();
    out.turnDeg.assign(n, kNaN);
    out.sigmaDeg.assign(n, kNaN);
    std::vector<Heading> hd(n);
    for (size_t i = 0; i < n; ++i) hd[i] = thighHeading(fr, i, leftLeg, cfg.minKneeFlexDeg);
    // The reference: the two-camera turn's rule (body_rotation.cpp triReference).
    std::vector<size_t> idx;
    int64_t nearest = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < n; ++i) {
        if (!hd[i].ok()) continue;
        const int64_t d = std::llabs(fr.t_us[i] - addressUs);
        nearest = std::min(nearest, d);
        if (d <= cfg.addrWindowUs) idx.push_back(i);
    }
    if (idx.empty() || nearest > cfg.addrMaxGapUs) return out;
    const auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const size_t m = v.size();
        return (m & 1u) ? v[m / 2] : 0.5 * (v[m / 2 - 1] + v[m / 2]);
    };
    std::vector<double> v, sg;
    for (size_t i : idx) {
        v.push_back(hd[idx[0]].rad + wrapRadPi(hd[i].rad - hd[idx[0]].rad));
        sg.push_back(hd[i].sigma);
    }
    out.refRad = median(v);
    out.refSigmaRad = 1.2533 * median(sg) / std::sqrt(double(v.size()));
    out.ok = true;
    const double leadSign = leadIsLeft ? 1.0 : -1.0;
    for (size_t i = 0; i < n; ++i) {
        if (!hd[i].ok()) continue;
        const double d = wrapRadPi(hd[i].rad - out.refRad);
        out.turnDeg[i]  = leadSign * d * kRad2Deg;
        out.sigmaDeg[i] = std::sqrt(hd[i].sigma * hd[i].sigma + out.refSigmaRad * out.refSigmaRad
                                    + (cfg.scaleFrac * d) * (cfg.scaleFrac * d)) * kRad2Deg;
    }
    return out;
}

// ── THE TWO CAMERAS DISAGREE ABOUT THE SHOULDERS AT THE TOP (FB, 13 Nov 2026) ──────────────────────
// The down-the-line camera's own shoulder line against the fit's, in that camera's image. On each fit
// frame within ±halfWindowUs of the Top whose down-the-line observation has both shoulder keypoints (5
// left, 6 right) at confidence ≥ minConf and at least minLinePx apart, the angle between the keypoint
// line and the fit's shoulder joints (LeftArm, RightArm) projected through the fit's down-the-line
// camera, as LINES (folded into 0…90°: a left/right label swap does not turn a line). The median of
// those frames above limitDeg is the disagreement. At the top the face-on trail shoulder is hidden and
// placed ~5 cm low; a fit that followed it turns the shoulder line away from what the down-the-line
// camera sees (sweep_p4.csv: 7° catches 38 of the library's 70 fits that leave the down-the-line trail
// shoulder more than 20 px off). The joints, not the acromion markers: the markers ride a few cm above
// the joints on the same girdle, which moves the line, not its angle, to within a degree.
//   `obsT` / `dtl`: the frame grid the fit was handed and its down-the-line observations (FitInput t_us,
//   dtl), matched to the fit's (trimmed) frames by instant.
struct ShoulderViewsConfig {
    double  limitDeg     = tuned::body3d::kShoulderViewsDisagreeDeg;
    int64_t halfWindowUs = tuned::body3d::kShoulderViewsHalfWindowUs;
    double  minConf      = tuned::body3d::kShoulderViewsMinConf;
    double  minLinePx    = tuned::body3d::kShoulderViewsMinLinePx;
};
struct ShoulderViews {
    int     frames = 0;                 // frames compared
    double  medianDeg = kNaN;           // their median angle, degrees (NaN: none compared)
    int64_t fromUs = 0, toUs = 0;       // the compared frames' span, from the Top
    bool    disagree = false;           // medianDeg > limitDeg
};
inline double shoulderLineAngleDiffDeg(double au, double av, double bu, double bv, double cu, double cv, double du, double dv)
{
    const double a = std::atan2(bv - av, bu - au), b = std::atan2(dv - cv, du - cu);
    const double pi = 3.14159265358979323846;
    double d = std::remainder(a - b, pi);           // lines: modulo 180°
    return std::fabs(d) * kRad2Deg;
}
inline ShoulderViews shoulderViewsAtTop(const sk::FitResult &fr, const std::vector<int64_t> &obsT,
                                        const std::vector<sk::ViewObs> &dtl, int dtlW, int dtlH, int64_t topUs,
                                        const ShoulderViewsConfig &cfg = {})
{
    ShoulderViews out;
    if (!fr.valid || !fr.dtlUsed || dtl.size() != obsT.size() || dtlW <= 0 || dtlH <= 0) return out;
    std::vector<double> d;
    int64_t lo = 0, hi = 0;
    for (size_t i = 0; i < fr.t_us.size() && i < fr.joints.size(); ++i) {
        const int64_t t = fr.t_us[i];
        if (std::llabs(t - topUs) > cfg.halfWindowUs) continue;
        const auto it = std::lower_bound(obsT.begin(), obsT.end(), t);
        if (it == obsT.end() || *it != t) continue;
        const sk::ViewObs &o = dtl[size_t(it - obsT.begin())];
        const sk::KpObs &l = o.kp[5], &r = o.kp[6];
        if (l.sigma <= 0 || r.sigma <= 0 || l.conf < cfg.minConf || r.conf < cfg.minConf) continue;
        if (std::hypot(r.u - l.u, r.v - l.v) < cfg.minLinePx) continue;
        double pu, pv, qu, qv;
        if (!sk::projectPoint(fr.cam, 1, dtlW, dtlH, fr.joints[i][size_t(sk::ybot::LeftArm)], pu, pv)
            || !sk::projectPoint(fr.cam, 1, dtlW, dtlH, fr.joints[i][size_t(sk::ybot::RightArm)], qu, qv))
            continue;
        if (std::hypot(qu - pu, qv - pv) < 1.0) continue;   // the fit's line end-on to the camera: no angle
        d.push_back(shoulderLineAngleDiffDeg(l.u, l.v, r.u, r.v, pu, pv, qu, qv));
        if (out.frames == 0) lo = t - topUs;
        hi = t - topUs;
        ++out.frames;
    }
    if (d.empty()) return out;
    std::sort(d.begin(), d.end());
    const size_t n = d.size();
    out.medianDeg = (n & 1u) ? d[n / 2] : 0.5 * (d[n / 2 - 1] + d[n / 2]);
    out.fromUs = lo;
    out.toUs = hi;
    out.disagree = out.medianDeg > cfg.limitDeg;
    return out;
}

} // namespace pinpoint::analysis::body3d
