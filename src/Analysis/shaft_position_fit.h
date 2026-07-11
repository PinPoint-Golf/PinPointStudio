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

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>   // cv::warpAffine (shift-and-stack registration)

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "shaft_tracker_math.h"   // RidgeConfig (shared ridge line evaluation)
#include "shaft_positions.h"      // PositionFitConfig (the "positions.*" B-fit tuning)
#include "swing_analysis.h"       // QPointF / BallSample2D / ShaftPosition / PositionSource

// Position-first shaft measurement — Layer B "B-fit" (the milestone fitter),
// docs/design/shaft_position_first_design.md §2 Layer B. Header-only (inline);
// the OpenCV shift-and-stack means it cannot be Qt/OpenCV-free like
// shaft_positions.h, but it stays a single translation-unit-free header so
// decideTrack (shaft_track_assembly.cpp) and the standalone fit test share ONE
// definition. Nothing here touches samples[]/θ/coverage — the fit only upgrades
// the report-only positions[] block from a B1 TrackSample to a MilestoneFit.
//
// MECHANISM (per the empirical B1 baseline gate — this reshapes the naive spec):
//   1. SHIFT-AND-STACK registration. Register the ±k frames about the grip anchor
//      along the LOCALLY PREDICTED motion — θ advances by ω·dt (ω = the smoothed
//      angular rate), grip translates along its own smoothed path — and average
//      the de-rotated frames (√N noise/blur reduction). Crucially the registration
//      depends on ω (the rate) and the grip path, NOT on the absolute track θ, so a
//      FLIPPED-by-180° track θ (the impact-blur-gap pathology at P6/P7) does not
//      corrupt the stack: every frame's TRUE shaft is offset by the same ω·dt and
//      reinforces at its true (unknown) orientation. For a still position (P1) ω≈0
//      ⇒ a plain multi-frame average.
//   2. JOINT FIT on the stacked image. Search θ over the admitted sector (NARROW,
//      local about the track θ at P1–P4 where the DP is already excellent; WIDE,
//      global about the arm/ball direction at P5–P8 to escape the flip) × radial
//      length L over a plausible band, scoring by the SAME polarity-aware corridor
//      ridge integral the Layer A snap uses (shared here, not duplicated). A small
//      grip perturbation (±few px) is refined at the winning θ. The arm-plausibility
//      sector vetoes θ pointing into the lead forearm (φ+180 ± armVetoDeg). Ball
//      evidence at P7/P1: a multiplicative reward for lines whose head end passes
//      near the ball (the ball at/just-before impact constrains the clubhead).
//   3. ACCEPT iff the fit's ridge support (lineConf semantics) ≥ minFitConf AND the
//      arm veto admits θ; else REJECT and the caller keeps the B1 track sample
//      (never degrade). σθ/σL come from the near-max plateau half-width.

namespace pinpoint::analysis {

// ── shared ridge line evaluation ─────────────────────────────────────────────
// Promoted from shaft_track_assembly.cpp's anonymous namespace so the Layer A
// snap pass (there) and the milestone fit (here) share ONE definition instead of
// duplicating the polarity-aware corridor math. The arithmetic of
// ridgeLineIntegral is unchanged (snap stays byte-identical).

// Nearest-neighbour clamped sample of a CV_32F image (matches ridgeSweep's `at`).
inline float sampleClamp(const cv::Mat& img, double fx, double fy)
{
    int x = int(fx), y = int(fy);
    if (x < 0) x = 0; else if (x >= img.cols) x = img.cols - 1;
    if (y < 0) y = 0; else if (y >= img.rows) y = img.rows - 1;
    return img.at<float>(y, x);
}

// median of exactly 4 (numpy even-count median = mean of the two middle values).
inline float med4(float a, float b, float c, float d)
{
    if (a > b) std::swap(a, b);
    if (c > d) std::swap(c, d);
    if (a > c) std::swap(a, c);
    if (b > d) std::swap(b, d);
    return 0.5f * (b + c);
}

// Polarity-aware ridge evidence at one on-line sample (cx,cy) with unit direction
// (ux,uy): the MEAN of a ±corridorHalf px lateral corridor credited over a
// background = median of four samples just outside it, split by the bgHi polarity
// (a bright line over dark bg, or a dark shaft over blown mat when bg>bgHi). This
// is the exact per-sample block ridgeLineIntegral used inline — factored so
// ridgeLineFit reuses it byte-for-byte.
inline double ridgeCorridorEvidence(const cv::Mat& g32, double cx, double cy,
                                    double ux, double uy, int corridorHalf, const RidgeConfig& rc)
{
    const double bgNear = corridorHalf + 7.0, bgFar = corridorHalf + 10.0;   // normal = (-uy, ux)
    const float b0 = sampleClamp(g32, cx - (-bgFar)  * uy, cy + (-bgFar)  * ux);
    const float b1 = sampleClamp(g32, cx - (-bgNear) * uy, cy + (-bgNear) * ux);
    const float b2 = sampleClamp(g32, cx - ( bgNear) * uy, cy + ( bgNear) * ux);
    const float b3 = sampleClamp(g32, cx - ( bgFar)  * uy, cy + ( bgFar)  * ux);
    const double bg = med4(b0, b1, b2, b3);
    double onSum = 0.0; int onN = 0;
    for (int o = -corridorHalf; o <= corridorHalf; ++o) {
        onSum += sampleClamp(g32, cx - double(o) * uy, cy + double(o) * ux);
        ++onN;
    }
    const double on = onSum / double(onN);
    return (bg > rc.bgHi)
        ? std::clamp(bg - on - 12.0, double(-rc.eClipNeg), double(rc.eClipPos))
        : std::clamp(on - bg - 12.0, double(-rc.eClipNeg), double(rc.eClipPos));
}

// Polarity-aware ridge line-integral of `g32` along the line through (ax,ay) in
// direction thetaRad, r ∈ [rLo, rEnd) step rStep. Returns the MEAN per-sample
// evidence over the FULL drawn extent (the snap objective — rewards a line lying
// on the shaft along its whole length) and, via `support`, the fraction of
// supported samples (e>8) — the lineConf metric ∈ [0,1]. Math unchanged from the
// pre-promotion definition in shaft_track_assembly.cpp (soak-byte contract).
inline double ridgeLineIntegral(const cv::Mat& g32, double ax, double ay, double thetaRad,
                                double rLo, double rEnd, double rStep, int corridorHalf,
                                const RidgeConfig& rc, double& support)
{
    const double ux = std::cos(thetaRad), uy = std::sin(thetaRad);
    double cum = 0.0;
    int n = 0, posCount = 0;
    for (double r = rLo; r < rEnd; r += rStep) {
        const double cx = ax + ux * r, cy = ay + uy * r;
        const double e = ridgeCorridorEvidence(g32, cx, cy, ux, uy, corridorHalf, rc);
        cum += e;
        ++n;
        if (e > 8.0) ++posCount;
    }
    support = (n > 0) ? double(posCount) / double(n) : 0.0;
    return (n > 0) ? cum / double(n) : -std::numeric_limits<double>::infinity();
}

namespace position_fit_detail {

constexpr double kPi = 3.14159265358979323846;

inline double wrap180(double deg)
{
    return std::fmod(std::fmod(deg + 180.0, 360.0) + 360.0, 360.0) - 180.0;
}

// Linear interpolation of a per-frame scalar to time t over the frame timebase.
// Clamps to the boundary sample outside [tUs.front(), tUs.back()].
inline double interpScalar(int64_t t, const std::vector<int64_t>& tUs,
                           const std::vector<double>& v)
{
    const int nf = int(tUs.size());
    if (nf == 0) return 0.0;
    int b = 0;
    while (b < nf && tUs[b] < t) ++b;
    if (b <= 0) return v[0];
    if (b >= nf) return v[nf - 1];
    const double denom = double(tUs[b] - tUs[b - 1]);
    const double frac  = denom > 0.0 ? double(t - tUs[b - 1]) / denom : 0.0;
    return v[b - 1] + (v[b] - v[b - 1]) * frac;
}

// Wrap-aware interpolation of a per-frame angle (deg) — interpolate the shortest
// signed delta so a 179°→-179° step does not sweep the long way round.
inline double interpAngleDeg(int64_t t, const std::vector<int64_t>& tUs,
                             const std::vector<double>& deg)
{
    const int nf = int(tUs.size());
    if (nf == 0) return 0.0;
    int b = 0;
    while (b < nf && tUs[b] < t) ++b;
    if (b <= 0) return deg[0];
    if (b >= nf) return deg[nf - 1];
    const double denom = double(tUs[b] - tUs[b - 1]);
    const double frac  = denom > 0.0 ? double(t - tUs[b - 1]) / denom : 0.0;
    return deg[b - 1] + wrap180(deg[b] - deg[b - 1]) * frac;
}

// Cumulative-peak ridge fit along a ray: integrates the same polarity-aware
// corridor evidence as ridgeLineIntegral, but tracks the RUNNING cumulative sum
// and returns the extent L* that maximises it (the shaft terminus — extending
// past it dilutes the cumulative with background), the cumulative evidence there
// (the joint-fit score), and the support fraction (e>8 samples / samples) up to
// L*. `cumProfile`/`rProfile` (optional) receive the per-step cumulative curve so
// the caller can extract σL from its near-peak plateau.
inline double ridgeLineFit(const cv::Mat& g32, double ax, double ay, double thetaRad,
                           double rLo, double rMax, double rStep, int corridorHalf,
                           const RidgeConfig& rc, double& lStar, double& support,
                           std::vector<double>* cumProfile = nullptr,
                           std::vector<double>* rProfile = nullptr)
{
    const double ux = std::cos(thetaRad), uy = std::sin(thetaRad);
    double cum = 0.0, cumMax = -std::numeric_limits<double>::infinity();
    double lAtMax = rLo;
    int nSoFar = 0, posCount = 0, nAtMax = 0, posAtMax = 0;
    for (double r = rLo; r < rMax; r += rStep) {
        const double cx = ax + ux * r, cy = ay + uy * r;
        const double e = ridgeCorridorEvidence(g32, cx, cy, ux, uy, corridorHalf, rc);
        cum += e; ++nSoFar;
        if (e > 8.0) ++posCount;
        if (cumProfile) { cumProfile->push_back(cum); rProfile->push_back(r + rStep); }
        if (cum > cumMax) { cumMax = cum; lAtMax = r + rStep; nAtMax = nSoFar; posAtMax = posCount; }
    }
    lStar   = lAtMax;
    support = (nAtMax > 0) ? double(posAtMax) / double(nAtMax) : 0.0;
    return (nSoFar > 0) ? cumMax : 0.0;
}

// Half-width (in the profile's x-units) of the ≥plateauFrac·max plateau straddling
// the profile's argmax — the σ estimate for θ (x = deg) or L (x = px). x is
// assumed uniformly stepped by `step`. Floors at one step so a single-cell peak
// still reports a finite σ.
inline double plateauHalfWidth(const std::vector<double>& score, double step,
                               double plateauFrac)
{
    const int n = int(score.size());
    if (n == 0) return step;
    int imax = 0;
    for (int i = 1; i < n; ++i) if (score[i] > score[imax]) imax = i;
    const double thresh = plateauFrac * score[imax];
    int lo = imax, hi = imax;
    while (lo - 1 >= 0 && score[lo - 1] >= thresh) --lo;
    while (hi + 1 < n  && score[hi + 1] >= thresh) ++hi;
    const double half = 0.5 * double(hi - lo) * step;
    return std::max(step, half);
}

} // namespace position_fit_detail

// The ±k-frame milestone fit result. accepted=false ⇒ the caller keeps its B1
// TrackSample (the fit never degrades the track). gripPx/headPx are IMAGE px (the
// ShaftPosition convention). σθ/σL come from the near-max plateau half-widths.
struct PositionFitResult {
    bool    accepted      = false;
    QPointF gripPx, headPx;
    double  thetaRad      = 0.0;
    double  lenPx         = 0.0;
    float   conf          = 0.f;   // ridge support under the fitted line (lineConf ∈ [0,1])
    float   sigmaThetaDeg = -1.f;
    float   sigmaLenPx    = -1.f;
    int     stackN        = 0;     // frames that entered the shift-and-stack
};

// Fit one coaching P-position by shift-and-stack + joint (grip, θ, L) refinement.
//
//   frameAt         decode hook — CV_8UC1 grey for a coverage frame index (empty ⇒
//                   undecodable; that neighbour is simply dropped from the stack).
//   pIndex          coaching P 1..8 — selects the NARROW (P1–P4) vs WIDE (P5–P8)
//                   search sector.
//   centerFrame     coverage frame nearest tUs (the stack reference frame).
//   tUs             the located P-time (sub-frame); grip/θ/ω are interpolated to it.
//   frameTUs        per-frame timebase (µs), length nf.
//   gx,gy           per-frame smoothed grip anchor path (px), length nf (NaN ok).
//   thetaDeg        per-frame reconciled track θ (deg, image atan2) — the narrow
//                   search centre and the ω-derivation source; a FLIPPED value here
//                   does not corrupt the stack (see the header preamble).
//   omegaDegPerSec  per-frame LOCALLY SMOOTHED angular rate (deg/s) — the stack
//                   registration predictor; length nf.
//   phiDeg          per-frame smoothed lead-arm φ (deg) — the arm-plausibility
//                   sector centre/veto; empty or NaN ⇒ no arm witness.
//   armFloorPx      plausible-length floor (grip→head), 0 ⇒ use lenMinFracH·frameH.
//   fusedClubLenPx  the swing's fused club length (px); >0 tightens the L band about
//                   it, ≤0 ⇒ the plain [Lmin, Lmax] band.
//   ballNear        ball sample near the P-time (P7/P1), or nullptr. When found, its
//                   grip→ball direction anchors the WIDE search centre and its
//                   position rewards lines whose head lands on it.
//   frameW,frameH   camera dims (search bounds / ball px conversion).
//   rc              the ridge evidence config (shared with the DP/snap).
//   cfg             the "positions.*" fit tuning.
//   armVetoDeg      ShaftV3Config::armVetoDeg — the same φ+180 exclusion the DP uses.
inline PositionFitResult fitPosition(const std::function<cv::Mat(int)>& frameAt,
                                     int pIndex, int centerFrame, int64_t tUs,
                                     const std::vector<int64_t>& frameTUs,
                                     const std::vector<double>& gx, const std::vector<double>& gy,
                                     const std::vector<double>& thetaDeg,
                                     const std::vector<double>& omegaDegPerSec,
                                     const std::vector<double>& phiDeg,
                                     double armFloorPx, double fusedClubLenPx,
                                     const BallSample2D* ballNear,
                                     int frameW, int frameH,
                                     const RidgeConfig& rc, const PositionFitConfig& cfg,
                                     double armVetoDeg)
{
    using namespace position_fit_detail;
    PositionFitResult out;

    const int nf = int(frameTUs.size());
    if (nf < 2 || int(gx.size()) != nf || int(gy.size()) != nf
        || int(thetaDeg.size()) != nf || int(omegaDegPerSec.size()) != nf) return out;
    if (centerFrame < 0 || centerFrame >= nf) return out;

    // Reference geometry at the P-instant (interpolated to tUs) — the stack frame.
    const double gx0 = interpScalar(tUs, frameTUs, gx);
    const double gy0 = interpScalar(tUs, frameTUs, gy);
    if (std::isnan(gx0) || std::isnan(gy0)) return out;
    const double theta0Deg = interpAngleDeg(tUs, frameTUs, thetaDeg);
    const double omega0     = interpScalar(tUs, frameTUs, omegaDegPerSec);   // deg/s
    const bool   phiOk      = int(phiDeg.size()) == nf && !std::isnan(phiDeg[centerFrame]);
    const double phiCenter  = phiOk ? interpAngleDeg(tUs, frameTUs, phiDeg) : 0.0;

    // ── (1) shift-and-stack registration ──────────────────────────────────────
    // Warp each ±k neighbour into the reference (P-instant) frame: de-rotate by
    // dθ = ω·(t_f − t0) about grip0 and translate grip_f → grip0. WARP_INVERSE_MAP
    // means M maps dst(ref)→src(frame f): src = R(dθ)·(p − grip0) + grip_f.
    const int k  = std::max(0, cfg.halfWindowFrames);
    const int lo = std::max(0, centerFrame - k);
    const int hi = std::min(nf - 1, centerFrame + k);
    cv::Mat acc;
    int stackN = 0;
    for (int f = lo; f <= hi; ++f) {
        if (std::isnan(gx[f]) || std::isnan(gy[f])) continue;
        cv::Mat g8 = frameAt(f);
        if (g8.empty()) continue;
        cv::Mat g32; g8.convertTo(g32, CV_32F);
        if (acc.empty()) acc = cv::Mat::zeros(g32.size(), CV_32F);
        if (g32.size() != acc.size()) continue;
        const double dThetaRad = omega0 * double(frameTUs[f] - tUs) * 1e-6 * kPi / 180.0;
        const double c = std::cos(dThetaRad), s = std::sin(dThetaRad);
        const double m13 = gx[f] - (c * gx0 - s * gy0);
        const double m23 = gy[f] - (s * gx0 + c * gy0);
        const cv::Matx23d M(c, -s, m13, s, c, m23);
        cv::Mat warped;
        cv::warpAffine(g32, warped, cv::Mat(M), g32.size(),
                       cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
        acc += warped;
        ++stackN;
    }
    if (stackN == 0) return out;
    cv::Mat stacked = acc / double(stackN);

    // ── (2) joint (grip, θ, L) fit on the stacked image ───────────────────────
    const bool   narrow = (pIndex >= 1 && pIndex <= 4);
    const bool   ballOk = ballNear && ballNear->found;
    const double ballPx = ballOk ? ballNear->center.x() * double(frameW) : 0.0;
    const double ballPy = ballOk ? ballNear->center.y() * double(frameH) : 0.0;

    // Search centre + width. NARROW: local polish about the track θ. WIDE: global,
    // centred on the most reliable direction available (ball > arm > track). No
    // witness at all ⇒ the whole circle (both polarities), so a 180° flip can't hide.
    double centerDeg, sectorWidth;
    if (narrow) {
        centerDeg   = theta0Deg;
        sectorWidth = cfg.narrowSectorDeg;
    } else if (ballOk) {
        centerDeg   = std::atan2(ballPy - gy0, ballPx - gx0) * 180.0 / kPi;
        sectorWidth = cfg.wideSectorDeg;
    } else if (phiOk) {
        centerDeg   = phiCenter;
        sectorWidth = cfg.wideSectorDeg;
    } else {
        centerDeg   = theta0Deg;
        sectorWidth = 360.0;
    }
    const double half = 0.5 * sectorWidth;

    // Plausible radial-length band (design §2 Layer B step 2): floor to the lead
    // arm (a club is always longer than the arm), ceil at the frame-height cap;
    // tighten about the fused club length when the swing has one.
    double lMin = std::max(armFloorPx, cfg.lenMinFracH * double(frameH));
    double lMax = cfg.lenMaxFracH * double(frameH);
    if (fusedClubLenPx > 0.0) {
        lMin = std::max(lMin, 0.65 * fusedClubLenPx);
        lMax = std::min(lMax, 1.35 * fusedClubLenPx);
    }
    if (lMax <= lMin + double(rc.rStep)) lMax = lMin + 4.0 * double(rc.rStep);

    const double armDir = phiOk ? std::fmod(phiCenter + 180.0, 360.0) : 0.0;
    auto armAdmits = [&](double thDeg) {
        return !phiOk || std::abs(wrap180(thDeg - armDir)) >= armVetoDeg;
    };
    // Head-near-ball multiplicative reward (P7/P1): favours a polarity/length whose
    // head end lands on the ball, breaking residual flip/length ambiguity.
    auto ballFactor = [&](double thRad, double L) {
        if (!ballOk || cfg.ballBonus <= 0.0) return 1.0;
        const double hx = gx0 + L * std::cos(thRad), hy = gy0 + L * std::sin(thRad);
        const double d2 = (hx - ballPx) * (hx - ballPx) + (hy - ballPy) * (hy - ballPy);
        const double sig = std::max(1.0, cfg.ballSigmaPx);
        return 1.0 + cfg.ballBonus * std::exp(-d2 / (2.0 * sig * sig));
    };
    const double rLo   = double(rc.rLo);
    const double rStep = std::max(0.5, cfg.lenStepPx);
    const int    corridorHalf = std::max(1, cfg.corridorHalfPx);

    auto scoreLine = [&](double gxA, double gyA, double thDeg,
                         double& lStar, double& support,
                         std::vector<double>* cumP, std::vector<double>* rP) {
        const double thRad = thDeg * kPi / 180.0;
        double cum = ridgeLineFit(stacked, gxA, gyA, thRad, rLo, lMax, rStep,
                                  corridorHalf, rc, lStar, support, cumP, rP);
        if (lStar < lMin) cum = -std::numeric_limits<double>::infinity();   // implausibly short
        if (cum > -std::numeric_limits<double>::infinity())
            cum *= ballFactor(thRad, lStar);
        return cum;
    };

    // Stage 1 — coarse θ sweep at grip0 (records the profile for σθ).
    const double thStep = std::max(0.1, cfg.thetaStepDeg);
    std::vector<double> thetaProfile;            // score per coarse θ (for σθ)
    double bestScore = -std::numeric_limits<double>::infinity();
    double bestTheta = centerDeg, bestL = lMin, bestSup = 0.0;
    for (double dth = -half; dth <= half + 1e-9; dth += thStep) {
        const double thDeg = centerDeg + dth;
        if (!armAdmits(thDeg)) { thetaProfile.push_back(0.0); continue; }
        double lStar = lMin, sup = 0.0;
        const double sc = scoreLine(gx0, gy0, thDeg, lStar, sup, nullptr, nullptr);
        thetaProfile.push_back(std::isfinite(sc) ? std::max(0.0, sc) : 0.0);
        if (sc > bestScore) { bestScore = sc; bestTheta = thDeg; bestL = lStar; bestSup = sup; }
    }
    if (!std::isfinite(bestScore)) return out;

    // Stage 2 — grip perturbation at the winning θ, then a fine θ refine at the
    // winning grip (recording the L-cumulative profile for σL). The perturbation
    // is PERPENDICULAR to θ only (the along-shaft grip component is degenerate with
    // L — a ridge alone cannot fix where along it the grip sits — so an unconstrained
    // 2-D search would trade grip-back for a longer L). This mirrors the Layer A
    // snap's ⊥-offset registration: it lands the anchor laterally on the club.
    double bestGx = gx0, bestGy = gy0;
    const double gStep = std::max(0.5, cfg.gripStepPx);
    const double nX = -std::sin(bestTheta * kPi / 180.0), nY = std::cos(bestTheta * kPi / 180.0);
    for (double d = -cfg.gripSearchPx; d <= cfg.gripSearchPx + 1e-9; d += gStep) {
        const double gxc = gx0 + d * nX, gyc = gy0 + d * nY;
        double lStar = lMin, sup = 0.0;
        const double sc = scoreLine(gxc, gyc, bestTheta, lStar, sup, nullptr, nullptr);
        if (sc > bestScore) { bestScore = sc; bestGx = gxc; bestGy = gyc; bestL = lStar; bestSup = sup; }
    }
    std::vector<double> cumProfile, rProfile;
    for (double dth = -2.0 * thStep; dth <= 2.0 * thStep + 1e-9; dth += 0.5 * thStep) {
        const double thDeg = bestTheta + dth;
        if (!armAdmits(thDeg)) continue;
        std::vector<double> cp, rp;
        double lStar = lMin, sup = 0.0;
        const double sc = scoreLine(bestGx, bestGy, thDeg, lStar, sup, &cp, &rp);
        if (sc > bestScore) {
            bestScore = sc; bestTheta = thDeg; bestL = lStar; bestSup = sup;
            cumProfile = std::move(cp); rProfile = std::move(rp);
        }
    }
    if (cumProfile.empty()) {   // fine refine never improved — recompute the winner's profile
        double lStar = lMin, sup = 0.0;
        scoreLine(bestGx, bestGy, bestTheta, lStar, sup, &cumProfile, &rProfile);
        bestL = lStar; bestSup = sup;
    }

    // ── (3) accept / reject + σ extraction ────────────────────────────────────
    if (bestSup < cfg.minFitConf || !armAdmits(bestTheta)) {
        out.accepted = false;   // caller keeps the B1 track sample
        return out;
    }
    const double thRad = bestTheta * kPi / 180.0;
    out.accepted      = true;
    out.gripPx        = QPointF(bestGx, bestGy);
    out.thetaRad      = thRad;
    out.lenPx         = bestL;
    out.headPx        = QPointF(bestGx + bestL * std::cos(thRad), bestGy + bestL * std::sin(thRad));
    out.conf          = float(std::clamp(bestSup, 0.0, 1.0));
    out.stackN        = stackN;
    out.sigmaThetaDeg = float(plateauHalfWidth(thetaProfile, thStep, cfg.plateauFrac));
    out.sigmaLenPx    = float(plateauHalfWidth(cumProfile, rStep, cfg.plateauFrac));
    return out;
}

} // namespace pinpoint::analysis
