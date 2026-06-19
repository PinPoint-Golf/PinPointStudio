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
 */

#pragma once

#include <algorithm>
#include <cmath>

// ShaftTracker R6 — lead-arm → club angle (double-pendulum wrist-cock model).
// Pure, header-only: no Qt, no OpenCV (the impact_detector.h pattern).
//
// The golf swing is a double pendulum: link 1 = the lead arm (shoulder→hands,
// image angle φ_arm), link 2 = the club (hands→head), hinged at the wrists. The
// wrist-cock angle β = φ_club − φ_arm follows a stereotyped curve through the
// swing (≈ 0° address → ≈ 90° top → released to ≈ 0° impact → recocked to the
// finish), so the club image angle is predictable from the lead-arm angle plus
// the monotone swing-progress s ∈ [0,1] (mapped from the segmentation ladder):
//
//     φ_club ≈ φ_arm + chirality · β̂(s)
//
// β̂ is *signed* in a canonical chirality (RH golfer, non-mirrored face-on):
// trail-side of the lead arm is +, lead-side is −, and the sign crosses zero at
// the release (just before impact). The caller supplies `chirality` (±1) from
// handedness × mirroredSource. The seed table below comes from golf
// biomechanics; K5 refines it empirically from the corpus / lead-hand IMU.
//
// s is monotone and single-valued, so the table needs no Backswing/Downthrough
// branch — that lives only in the segmentation-absent fallback (infer position
// from φ_arm + sign(φ̇_arm)), for which `Branch`/`branchFromProgress` are kept.
//
// See docs/design/shaft_detection_skeleton_design.md R6 and
// docs/implementation/shaft_detection_skeleton_impl.md phase K2.

namespace pinpoint::analysis::kinematics {

// Backswing vs downswing/through — NOT used by the s-keyed table (s disambiguates
// on its own); retained for the segmentation-absent fallback path.
enum class Branch { Backswing, Downthrough };

struct ClubAnglePrediction {
    float betaRad  = 0.f;  // SIGNED wrist cock (trail +, lead −) in the canonical chirality
    float sigmaRad = 0.f;  // σ_β — prior width / envelope half-width
    int   side     = 0;    // sign(betaRad): +1 trail, −1 lead, 0 ≈ collinear
};

namespace detail {
constexpr float kPi = 3.14159265358979323846f;
inline float d2r(float d) { return d * (kPi / 180.f); }
inline float wrapPi(float a)
{
    while (a >  kPi) a -= 2.f * kPi;
    while (a < -kPi) a += 2.f * kPi;
    return a;
}
// Seed knots: swing-progress s, SIGNED wrist cock (deg), σ_β (deg). Monotone in s;
// the signed β̂ passes through 0 at the release (between Delivery and Impact).
struct Knot { float s, betaDeg, sigmaDeg; };
constexpr Knot kKnots[] = {
    { 0.00f,  -5.f,  8.f },   // Address      — near-collinear, slight forward lean (lead)
    { 0.15f,  28.f, 15.f },   // Takeaway     — cock starting (trail)
    { 0.35f,  70.f, 20.f },   // MidBackswing — lead arm parallel (backswing)
    { 0.50f,  92.f, 22.f },   // Top          — full "L"
    { 0.60f, 100.f, 30.f },   // Transition   — lag (most variable across skill)
    { 0.80f,  48.f, 25.f },   // Delivery     — releasing (shaft parallel, downswing)
    { 0.90f,  -8.f, 10.f },   // Impact       — released, hands ahead (lead)
    { 0.95f, -28.f, 20.f },   // Release      — extension (lead)
    { 1.00f, -95.f, 30.f },   // Finish       — recocked, wrapped (lead)
};
constexpr int kNumKnots = int(sizeof(kKnots) / sizeof(kKnots[0]));
} // namespace detail

// Seed wrist-cock prediction at monotone swing-progress s ∈ [0,1] (clamped),
// linearly interpolated between knots. Single-valued — no Branch needed.
inline ClubAnglePrediction wristCockAt(float s)
{
    using namespace detail;
    s = std::clamp(s, 0.f, 1.f);
    float betaDeg, sigmaDeg;
    if (s <= kKnots[0].s) {
        betaDeg = kKnots[0].betaDeg;  sigmaDeg = kKnots[0].sigmaDeg;
    } else if (s >= kKnots[kNumKnots - 1].s) {
        betaDeg = kKnots[kNumKnots - 1].betaDeg;  sigmaDeg = kKnots[kNumKnots - 1].sigmaDeg;
    } else {
        betaDeg = kKnots[0].betaDeg;  sigmaDeg = kKnots[0].sigmaDeg;
        for (int i = 1; i < kNumKnots; ++i) {
            if (s <= kKnots[i].s) {
                const Knot &a = kKnots[i - 1], &b = kKnots[i];
                const float t = (s - a.s) / (b.s - a.s);
                betaDeg  = a.betaDeg  + (b.betaDeg  - a.betaDeg)  * t;
                sigmaDeg = a.sigmaDeg + (b.sigmaDeg - a.sigmaDeg) * t;
                break;
            }
        }
    }
    ClubAnglePrediction p;
    p.betaRad  = d2r(betaDeg);
    p.sigmaRad = d2r(sigmaDeg);
    p.side     = (betaDeg > 1.f) ? +1 : (betaDeg < -1.f ? -1 : 0);
    return p;
}

// Predicted club image angle: φ_arm + chirality · β̂(s), wrapped to (−π, π].
// `chirality` (±1) injects the handedness × mirror sign convention (default +1,
// the canonical RH non-mirrored face-on view).
inline float predictClubAngle(float phiArmRad, float s, int chirality = +1,
                              ClubAnglePrediction *out = nullptr)
{
    const ClubAnglePrediction p = wristCockAt(s);
    if (out) *out = p;
    return detail::wrapPi(phiArmRad + float(chirality) * p.betaRad);
}

// Deviation of a candidate angle from the prediction in σ_β units — the R6
// guardrail metric (the caller soft-penalises beyond kSoft·σ and hard-rejects
// beyond kHard·σ on the wrong side). σ floored at 1° to avoid divide-by-zero.
inline float envelopeDeviationSigma(float candThetaRad, float predThetaRad, float sigmaRad)
{
    const float s = std::max(sigmaRad, detail::d2r(1.f));
    return std::fabs(detail::wrapPi(candThetaRad - predThetaRad)) / s;
}

// Fallback branch from progress (segmentation-absent path inference helper).
inline Branch branchFromProgress(float s) { return s < 0.55f ? Branch::Backswing : Branch::Downthrough; }

} // namespace pinpoint::analysis::kinematics
