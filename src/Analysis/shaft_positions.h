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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Position-first shaft measurement — Layer B "B-time location"
// (docs/design/shaft_position_first_design.md §2 Layer B). Header-only, standalone
// (no Qt / no OpenCV), unit-tested per the detector-math convention
// (impact_detector.h / onset_detector.h). Pure 1-D crossing detection on the
// already-smoothed per-frame θ(t) and lead-arm φ(t) signals the shaft tracker
// produces — far more stable than any per-frame measurement — returning the
// located coaching P-times P1–P8.
//
// The coaching P-system (design §1):
//   P1 Address · P2 Shaft parallel (backswing) · P3 Lead arm parallel (backswing)
//   P4 Top     · P5 Lead arm parallel (downswing) · P6 Shaft parallel (downswing)
//   P7 Impact  · P8 Shaft parallel (follow-through)
// P1/P4/P7 are anchored to the tracker's segmentation landmarks (address/top/
// impact frames). P2/P6/P8 are image-plane shaft-parallel crossings of θ(t);
// P3/P5 are lead-arm-parallel crossings of φ(t).
//
// PARALLEL PREDICATE (image plane, atan2 convention).
//   θ is the shaft direction grip→head as an image angle: dir = (cos θ, sin θ)
//   with image y DOWN (shaft_track_assembly.cpp: headPx = grip + L·(cos θ, sin θ)).
//   φ is the lead-arm direction in the same convention. The shaft/arm is
//   "parallel to the ground" when its direction is HORIZONTAL, i.e. its vertical
//   component sin(·) = 0. We fold the two horizontal directions (pointing left vs
//   right) together with an ELEVATION angle
//        elevationDeg(a) = atan2(sin a, |cos a|)   (deg)
//   which is 0 at horizontal, +90 pointing straight down (head below grip), −90
//   straight up — continuous through vertical, and monotone across each phase
//   window (down→up in the backswing, up→down in the downswing, down→up in the
//   follow-through). A parallel event is a zero-crossing of elevationDeg.
//   "Image-plane parallel", not true 3-D parallel — accepted coaching practice on
//   face-on video (design §1, "Accepted caveat").
//
// HYSTERESIS. A crossing is confirmed only after the signal has travelled beyond
// ±hysteresisDeg on BOTH sides of horizontal, so a sub-band wiggle around
// horizontal (noise) can neither arm nor double-fire; the reported time is the
// steepest zero-straddling frame pair between the two armed frames, interpolated
// sub-frame. Missing crossings (abbreviated swings, NaN φ) ⇒ that P is simply
// absent from the result — the caller reports per-P coverage n/8 honestly.

namespace pinpoint::analysis {

// B2 milestone-fit tuning (shaft_position_first §2 Layer B "B-fit"). Pure POD (no
// Qt / no OpenCV) so it lives here beside the B-time config; consumed by
// fitPosition (shaft_position_fit.h) which owns the OpenCV shift-and-stack. Keys
// are dotted "positions.*" via ShaftV3Config::fromOverrides. fitEnabled=false ⇒
// the fit never runs ⇒ ShaftTrack2D.positions stays the B1 TrackSample track
// byte-for-byte (soak contract). The empirical P-shape (B1 baseline gate) drives
// the two sector widths: at P1–P4 the DP track is already good (local polish);
// at P5–P8 it is 100+ px off with the direction sometimes FLIPPED ~180°, so the
// fit searches GLOBALLY over the arm-plausibility-admitted sector around the
// (reliable) grip anchor — a local refinement cannot recover there.
struct PositionFitConfig {
    bool   fitEnabled       = false;  // master gate — dark until its corpus gate flips it
    int    halfWindowFrames = 4;      // ±k frames stacked (shift-and-stack registration)
    double minFitConf       = 0.35;   // accept floor on ridge support under the fitted line
    double wideSectorDeg    = 170.0;  // θ search WIDTH at P5–P8 (global; centred on arm/ball)
    double narrowSectorDeg  = 15.0;   // θ search WIDTH at P1–P4 (local polish about the track θ)
    double gripSearchPx     = 6.0;    // grip perturbation half-range (px)
    // search resolution + plausible radial-length band (design §2 Layer B step 2)
    double thetaStepDeg     = 0.5;    // coarse θ grid step (deg)
    double lenStepPx        = 3.0;    // radial-length search step (px)
    double lenMinFracH      = 0.06;   // Lmin = max(armFloor, lenMinFracH·frameH)
    double lenMaxFracH      = 0.62;   // Lmax = lenMaxFracH·frameH (matches decideTrack's rmax)
    double gripStepPx       = 2.0;    // grip perturbation grid step (px)
    int    corridorHalfPx   = 2;      // lateral corridor half-width for the ridge integral (px)
    double ballBonus        = 0.6;    // P7/P1 head-near-ball multiplicative reward (0 = off)
    double ballSigmaPx      = 22.0;   // Gaussian falloff of the ball reward (px)
    double plateauFrac      = 0.9;    // σ = half-width of the ≥plateauFrac·max score plateau
};

// "positions.*" tuning namespace (design §4). Nested in ShaftV3Config as
// `positions`; enabled=false keeps position extraction dark (a positions-off run
// leaves ShaftTrack2D.positions empty ⇒ swing.json byte-identical).
struct PositionsConfig {
    bool   enabled       = false;   // master gate — dark until its corpus gate flips it
    double hysteresisDeg = 8.0;     // deadband each side of horizontal (arm + anti-double-fire)
    PositionFitConfig fit;          // B2 milestone fit (dark until fit.fitEnabled flips)
};

// One located P-time. `p` is the coaching P-index 1..8 (NOT a Phase enum value);
// `tUs` is the sub-frame-interpolated event time in the input tUs domain.
struct PTime {
    int     p   = 0;
    int64_t tUs = 0;
};

namespace positions_detail {

constexpr double kPi = 3.14159265358979323846;

// Elevation of an image-plane direction above/below horizontal (deg), folding the
// two horizontal directions together — 0 at horizontal, ±90 at vertical.
inline double elevationDeg(double angleDeg)
{
    const double r = angleDeg * kPi / 180.0;
    return std::atan2(std::sin(r), std::abs(std::cos(r))) * 180.0 / kPi;
}

struct Crossing {
    bool    found = false;
    int64_t tUs   = 0;
};

// The single hysteresis-confirmed horizontal crossing of `angleDeg` over the
// frame window [i0, i1] (inclusive). NaN frames are skipped. Returns not-found
// when the signal never arms on both sides (no genuine transit through
// horizontal). See the header preamble for the predicate + hysteresis contract.
inline Crossing findHorizontalCrossing(const std::vector<int64_t>& tUs,
                                       const std::vector<double>& angleDeg,
                                       int i0, int i1, double hystDeg)
{
    Crossing out;
    const int n = int(angleDeg.size());
    i0 = std::max(0, i0);
    i1 = std::min(n - 1, i1);
    if (i1 <= i0) return out;

    // Valid (non-NaN) frame indices in the window — crossings interpolate between
    // consecutive VALID frames so a NaN gap never fabricates a straddle.
    std::vector<int> vi;
    for (int i = i0; i <= i1; ++i)
        if (!std::isnan(angleDeg[i])) vi.push_back(i);
    if (int(vi.size()) < 2) return out;

    // Hysteresis state machine: find the first armed +→− or −→+ transition.
    // armedSign ∈ {−1,0,+1}; armA = valid-array index of the last armed frame.
    int armedSign = 0, armA = -1, fromA = -1, toA = -1;
    for (int a = 0; a < int(vi.size()); ++a) {
        const double e = elevationDeg(angleDeg[vi[a]]);
        if (e > hystDeg) {
            if (armedSign < 0) { fromA = armA; toA = a; break; }
            armedSign = 1; armA = a;
        } else if (e < -hystDeg) {
            if (armedSign > 0) { fromA = armA; toA = a; break; }
            armedSign = -1; armA = a;
        }
    }
    if (fromA < 0) return out;   // never armed both sides ⇒ no genuine crossing

    // Steepest zero-straddling consecutive-valid pair within [fromA, toA] — the
    // genuine transit through horizontal (noise crossings are shallower), then
    // interpolate the zero sub-frame.
    double  bestSlope = -1.0;
    int64_t bestT     = 0;
    bool    have      = false;
    for (int a = fromA; a < toA; ++a) {
        const double ea = elevationDeg(angleDeg[vi[a]]);
        const double eb = elevationDeg(angleDeg[vi[a + 1]]);
        const bool straddle = (ea >= 0.0 && eb < 0.0) || (ea <= 0.0 && eb > 0.0);
        if (!straddle) continue;
        const double slope = std::abs(ea - eb);
        if (slope <= bestSlope) continue;
        bestSlope = slope;
        const double frac = (ea == eb) ? 0.0 : ea / (ea - eb);   // ∈ [0,1]
        const int64_t ta = tUs[vi[a]], tb = tUs[vi[a + 1]];
        bestT = ta + int64_t(std::llround(frac * double(tb - ta)));
        have = true;
    }
    if (!have) { out.found = true; out.tUs = tUs[vi[toA]]; return out; }   // numerical fallback
    out.found = true;
    out.tUs   = bestT;
    return out;
}

} // namespace positions_detail

// Locate the coaching P-times P1–P8 from the per-frame reconciled θ(t) and
// lead-arm φ(t) (both DEGREES, image atan2 convention, one entry per frame; NaN
// permitted, e.g. φ where the pose lead-arm is absent). tUs[] is the per-frame
// time domain. addressFrame/topFrame/impactFrame are the tracker's P1/P4/P7
// landmark frame indices (segmentPhases bs0/top/impact); each < 0 or out of range
// simply omits that landmark. Windows: P2 ∈ (P1,P4), P6 ∈ (P4,P7), P8 after P7,
// P3 ∈ (P2,P4), P5 ∈ (P4,P6); a window whose bounding crossing is absent omits its
// dependent P (P3 needs P2, P5 needs P6). Returns the found P-times ORDERED by p
// (1..8), so the result is monotone in both p and tUs.
inline std::vector<PTime> locatePTimes(const std::vector<int64_t>& tUs,
                                       const std::vector<double>& thetaDeg,
                                       const std::vector<double>& phiDeg,
                                       int addressFrame, int topFrame, int impactFrame,
                                       const PositionsConfig& cfg)
{
    using namespace positions_detail;
    std::vector<PTime> out;
    const int nf = int(tUs.size());
    if (nf < 2 || int(thetaDeg.size()) != nf) return out;
    const double hyst = cfg.hysteresisDeg;
    const bool   phiOk = int(phiDeg.size()) == nf;

    auto inRange       = [&](int f) { return f >= 0 && f < nf; };
    auto frameAtOrAfter  = [&](int64_t t) { int i = 0;      while (i < nf && tUs[i] < t) ++i; return i; };
    auto frameAtOrBefore = [&](int64_t t) { int i = nf - 1; while (i > 0  && tUs[i] > t) --i; return i; };

    // P1 — address landmark.
    if (inRange(addressFrame)) out.push_back({1, tUs[addressFrame]});

    // P2 — shaft parallel in (P1, P4).
    Crossing p2;
    if (inRange(addressFrame) && inRange(topFrame) && topFrame > addressFrame)
        p2 = findHorizontalCrossing(tUs, thetaDeg, addressFrame, topFrame, hyst);
    if (p2.found) out.push_back({2, p2.tUs});

    // P3 — lead arm parallel in (P2, P4). Needs P2 and a φ signal.
    if (p2.found && phiOk && inRange(topFrame)) {
        const Crossing p3 = findHorizontalCrossing(tUs, phiDeg, frameAtOrAfter(p2.tUs), topFrame, hyst);
        if (p3.found) out.push_back({3, p3.tUs});
    }

    // P4 — top landmark.
    if (inRange(topFrame)) out.push_back({4, tUs[topFrame]});

    // P6 — shaft parallel in (P4, P7). Located before P5 (bounds its window).
    Crossing p6;
    if (inRange(topFrame) && inRange(impactFrame) && impactFrame > topFrame)
        p6 = findHorizontalCrossing(tUs, thetaDeg, topFrame, impactFrame, hyst);

    // P5 — lead arm parallel in (P4, P6). Needs P6 and a φ signal. Pushed BEFORE
    // P6 so the result stays ordered by p.
    if (p6.found && phiOk && inRange(topFrame)) {
        const Crossing p5 = findHorizontalCrossing(tUs, phiDeg, topFrame, frameAtOrBefore(p6.tUs), hyst);
        if (p5.found) out.push_back({5, p5.tUs});
    }
    if (p6.found) out.push_back({6, p6.tUs});

    // P7 — impact landmark.
    if (inRange(impactFrame)) out.push_back({7, tUs[impactFrame]});

    // P8 — shaft parallel after P7.
    if (inRange(impactFrame) && impactFrame < nf - 1) {
        const Crossing p8 = findHorizontalCrossing(tUs, thetaDeg, impactFrame, nf - 1, hyst);
        if (p8.found) out.push_back({8, p8.tUs});
    }
    return out;
}

} // namespace pinpoint::analysis
