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
#include <cstddef>
#include <vector>

// R8-T1 blur-wedge measurement (shaft_detection_skeleton_design.md §R8). At
// delivery the club rotates at up to ~1400°/s, so within one exposure the
// shaft images not as a line but as a FAN about the grip: a plateau in the
// per-angle ridge response S(θ) whose energy-weighted centroid is the
// mid-exposure angle and whose angular width measures ω·t_exp. The v3 tracker
// hunts a thin 90 px straight ridge and sees nothing here; this module reads
// the plateau instead — verification by integration over the R6-predicted
// envelope, not per-pixel peaks.
//
// Honesty contract (§R8 rule 4): the threshold is ABSOLUTE (threshScale ×
// the S1 evAbsFloor reference — deliberately below the frame floor, safe only
// because the search is confined to the tight kinematic envelope). Both
// channels below threshold ⇒ NO candidate — the wedge widens the band where
// vision still contributes; it never pretends to see a shaft that isn't there.
//
// Pure header: no Qt, no OpenCV — operates on the per-θ score rows a proximal
// ridgeSweep already produced. Standalone-testable.

namespace pinpoint::analysis {

// "shaft.wedge.*" keys via ShaftV3Config::fromOverrides. enabled=false ⇒ the
// tracker never triggers, sweeps, injects, or tiers — byte-identical output.
struct WedgeConfig {
    // FLIPPED ON 2026-08-10 (S2 corpus gate + re-validation, shaft_wedge_p6_impl.md:
    // P6 emissions 19→46/61, every truth-labelled P6 within 5 ms, track.valid
    // 58 ≥ baseline 55, P2/P8 unchanged).
    bool   enabled          = true;    // master gate; false = the pre-S2 tracker byte-for-byte
    double omegaMinDegS     = 720.0;   // |ω̂| trigger: below this the thin-line machinery is trusted
    double tExpBootstrapS   = 0.003;   // exposure bootstrap when no calibration frame exists (s)
    double calOmegaLoDegS   = 286.0;   // |ω̂| floor (≈5 rad/s) for a frame to enter the t_exp median
    double kSigma           = 3.0;     // envelope half-width in σ_β units
    double threshScale      = 0.5;     // plateau threshold = threshScale × evAbsFloor
    double minSpanDeg       = 2.0;     // narrower plateaus are line-like noise, not a fan
    double proximalRHiFrac  = 0.35;    // proximal sweep ceiling as a fraction of rmax
    double proximalMinLenPx = 40.0;    // R5 blur relaxation of RidgeConfig.minLenPx (proximal only)
    double wWell            = 6.0;     // emission well depth at the centroid (DP cost units)
    double conf             = 0.45;    // emitted sample confidence for the WEDGE tier
    double dpTolDeg         = 4.0;     // DP θ within σ_θ + this of the centroid ⇒ WEDGE tier
    // FLIPPED ON 2026-08-10 with enabled (the gate's deciding arm: kinCone
    // converted 3 further truth swings by defunding structure-backed short
    // paths — the S1 finding's lever — at the cost of 1 track.valid, 59→58).
    bool   kinCone          = true;    // off-envelope penalty on triggered frames
    double wKinCone         = 4.0;     // its weight (cf. cfg.wCone) — prior-as-constraint, never a tier
    // DARK. Swap the hand-authored wrist-cock table (indexed by swing progress)
    // for the corpus-fitted one indexed by seconds-before-impact
    // (shaft_kinematics.h kWristCockKnotsV2). Graded against hand-placed shaft
    // truth the fitted model cuts the p10–p90 residual 74.3° → 20.9° and removes
    // a −9.1° bias; the envelope is inflated so it stays as forgiving as the one
    // it replaces. false ⇒ the v1 table, byte-for-byte.
    //
    // MEASURED AND HELD DARK (2026-08-11 corpus A/B). Enabling it costs 38% of
    // the WEDGE stamps and 8-15 P-position emissions, because this table feeds
    // the wedge TRIGGER as well as its centre: the fitted curve holds its lag
    // flat and releases in 140 ms, so its time derivative clears omegaMinDegS on
    // 30% fewer frames than the v1 curve's steady decline. The centre got
    // better and the rate got worse. Separating the trigger from the centre is
    // the prerequisite for flipping this — see docs/research/wrist_cock_model.md.
    bool   kinModelV2       = false;

    // ── THE BLUR'S EDGES, NOT ITS CENTRE (2026-09-29) ─────────────────────────────────────────
    //
    // The proximal sweep is a RIDGE detector, and a blurred shaft is not a filled fan to it: it
    // answers at the fan's two ENDS — the shaft where the exposure started and where it ended —
    // as two separate peaks about ω·t_exp apart (08-18 s4 at impact: 106° and 88°, the tracker's
    // own θ moving 121 → 106 → 88 frame to frame). The centroid of the above-threshold run sits
    // near the larger, TRAILING peak, which is where the +13° impact shaft-lean bias came from.
    // Against 123 hand-marked downswing frames (32 corpus swings, 2026-09-29) the LEADING peak reads
    // −1.2° median (|4.6|), the tracker as it was +6.4° (|8.2|), the trailing peak +10.4°; at
    // 16°+/frame the tracker was +15.9° and the leading peak +1.6°.
    //
    // The leading edge is the shaft at the END of the exposure, which is the frame's timestamp. The
    // trailing edge is the shaft at its START (frame time − exposure), and their midpoint the
    // mid-exposure angle; both are real only when the second peak sits where the blur predicts —
    // at slow rotation the second peak stays ~12° away whatever the speed, i.e. it is the hands or
    // an arm, not the other end of a blur. So the trailing edge is kept only when its separation
    // from the leading one matches |ω̂|·t_exp.
    //
    // false ⇒ the centroid path above, byte-for-byte.
    bool   leadEdge         = true;
    double edgePeakRel      = 0.35;    // a peak must reach this fraction of the profile's maximum
    double edgeMinSepDeg    = 4.0;     // two peaks closer than this are one peak
    double edgeSigmaDeg     = 4.5;     // σ_θ of an edge (the hand-mark |median| is 4.6°)
    double trailTolDeg      = 4.0;     // trailing peak kept if |sep − |ω̂|·t_exp| ≤ max(this,
    double trailTolFrac     = 0.5;     //   this × |ω̂|·t_exp)
    // The camera's exposure (µs) as recorded with the stream; ≤ 0 ⇒ unknown, and the tracker takes
    // 99 % of the frame period — what every session that DID record it shows (6573.6 µs at
    // 150.713 fps, 6635 µs per frame, 2026-07-08 onward).
    double exposureUs       = 0.0;
};

// t_exp plausibility clamp (s): global-shutter golf capture sits within
// [1/1000, 1/125]; the median estimate is clamped here, and the bootstrap
// (3 ms) is used when no frame qualifies for calibration.
inline constexpr double kTExpLoS = 0.001;
inline constexpr double kTExpHiS = 0.008;

struct WedgeCandidate {
    bool   ok          = false;
    double centroidDeg = 0.0;   // energy-weighted circular centroid = mid-exposure angle
    double widthDeg    = 0.0;   // plateau angular extent ≈ ω·t_exp
    double energy      = 0.0;   // summed above-threshold response over the plateau
};

// Plateau finder over the envelope's per-θ score rows. rawScore/difScore are
// the proximal ridgeSweep responses at binDeg[j] (deg), in CONTIGUOUS arc
// order (the caller walks the envelope centre-out, so index adjacency == θ
// adjacency); difScore may be empty (no scene-median channel). absFloorRef is
// the S1 evAbsFloor (score units) — the absolute anchor the threshold scales
// from; <= 0 ⇒ no absolute reference exists ⇒ no candidate (never fabricate).
// phiSDeg/armVetoDeg: a centroid within armVetoDeg of φ+180 is the trail-arm
// smear, not the club — rejected.
inline WedgeCandidate measureWedge(const std::vector<float>& rawScore,
                                   const std::vector<float>& difScore,
                                   const std::vector<float>& binDeg,
                                   double phiSDeg, double armVetoDeg,
                                   double absFloorRef, const WedgeConfig& cfg)
{
    WedgeCandidate out;
    const size_t n = binDeg.size();
    if (n < 2 || rawScore.size() != n || (!difScore.empty() && difScore.size() != n))
        return out;
    if (absFloorRef <= 0.0) return out;
    const float thresh = float(cfg.threshScale * absFloorRef);

    // Per-bin response = best surviving channel; above = plateau membership.
    // Bin step from the arc (uniform by construction).
    auto wrapDeg = [](double d) {
        d = std::fmod(d + 180.0, 360.0);
        if (d < 0) d += 360.0;
        return d - 180.0;
    };
    const double stepDeg = std::abs(wrapDeg(double(binDeg[1]) - double(binDeg[0])));
    if (!(stepDeg > 0.0)) return out;

    // Best contiguous run of above-threshold bins by summed energy.
    size_t runStart = 0;
    bool   inRun = false;
    double bestEnergy = 0.0;
    size_t bestA = 0, bestB = 0;   // inclusive
    double runEnergy = 0.0;
    for (size_t j = 0; j <= n; ++j) {
        const bool above = j < n && (rawScore[j] >= thresh
                                     || (!difScore.empty() && difScore[j] >= thresh));
        if (above) {
            const float e = std::max(rawScore[j], difScore.empty() ? 0.f : difScore[j]);
            if (!inRun) { inRun = true; runStart = j; runEnergy = 0.0; }
            runEnergy += double(e);
        } else if (inRun) {
            inRun = false;
            const double spanDeg = double(j - 1 - runStart) * stepDeg;
            if (spanDeg >= cfg.minSpanDeg && runEnergy > bestEnergy) {
                bestEnergy = runEnergy; bestA = runStart; bestB = j - 1;
            }
        }
    }
    if (bestEnergy <= 0.0) return out;

    // Energy-weighted circular centroid over the winning run, computed as an
    // offset from the run's first bin so the arc's own wrap never bites.
    const double ref = double(binDeg[bestA]);
    double wSum = 0.0, oSum = 0.0;
    for (size_t j = bestA; j <= bestB; ++j) {
        const double e = double(std::max(rawScore[j], difScore.empty() ? 0.f : difScore[j]));
        if (e < thresh) continue;   // interior bins are above by construction; keep exact
        wSum += e;
        oSum += e * wrapDeg(double(binDeg[j]) - ref);
    }
    if (!(wSum > 0.0)) return out;
    double cen = std::fmod(ref + oSum / wSum, 360.0);
    if (cen < 0) cen += 360.0;

    // Trail-arm smear veto: the forearm sweeps with the club and produces its
    // own fan at φ+180 — never promote it to a club measurement.
    const double armDeg = std::fmod(phiSDeg + 180.0, 360.0);
    if (std::abs(wrapDeg(cen - armDeg)) < armVetoDeg) return out;

    out.ok          = true;
    out.centroidDeg = cen;
    out.widthDeg    = double(bestB - bestA) * stepDeg;
    out.energy      = bestEnergy;
    return out;
}

// The blur's edges (WedgeConfig::leadEdge). All angles in the image atan2 convention, degrees.
struct WedgeEdges {
    bool   ok       = false;
    double leadDeg  = 0.0;     // shaft at exposure END (the frame time)
    bool   hasTrail = false;
    double trailDeg = 0.0;     // shaft at exposure START — only when the separation fits the blur
    double midDeg   = 0.0;     // mid-exposure: the midpoint of the two, only with a trail
    double sepDeg   = 0.0;     // |lead − trail| of the two peaks considered (0 = one peak)
};

// Peaks of the same per-θ rows measureWedge reads (max of the two channels, [1 2 1]/4 smoothed,
// parabolic sub-bin), taken greedily by height at least edgeMinSepDeg apart. Of the two highest,
// the LEADING one is the one further along the direction of rotation `rotSign` (+1 = θ increasing
// with time, −1 = decreasing; a left-handed swing simply arrives with the other sign). The other is
// the trailing edge when |sep − expectedSweepDeg| is within tolerance. Same honesty contract as
// measureWedge: nothing reaches the absolute threshold ⇒ no candidate; a leading edge within
// armVetoDeg of φ+180 is the trail-arm smear ⇒ no candidate.
inline WedgeEdges measureWedgeEdges(const std::vector<float>& rawScore,
                                    const std::vector<float>& difScore,
                                    const std::vector<float>& binDeg,
                                    int rotSign, double expectedSweepDeg,
                                    double phiSDeg, double armVetoDeg,
                                    double absFloorRef, const WedgeConfig& cfg)
{
    WedgeEdges out;
    const size_t n = binDeg.size();
    if (n < 3 || rawScore.size() != n || (!difScore.empty() && difScore.size() != n)) return out;
    if (absFloorRef <= 0.0 || rotSign == 0) return out;
    auto wrapDeg = [](double d) {
        d = std::fmod(d + 180.0, 360.0);
        if (d < 0) d += 360.0;
        return d - 180.0;
    };
    std::vector<double> p(n);
    double pmax = 0.0;
    for (size_t j = 0; j < n; ++j) {
        p[j] = std::max(double(rawScore[j]), difScore.empty() ? 0.0 : double(difScore[j]));
        pmax = std::max(pmax, p[j]);
    }
    if (pmax < cfg.threshScale * absFloorRef) return out;   // never fabricate
    std::vector<double> s(n);
    for (size_t j = 0; j < n; ++j)
        s[j] = (p[j > 0 ? j - 1 : 0] + 2.0 * p[j] + p[j + 1 < n ? j + 1 : n - 1]) / 4.0;
    const double smax = *std::max_element(s.begin(), s.end());
    if (!(smax > 0.0)) return out;

    // Angles as offsets from bin 0 along the arc, so an arc across 0°/360° stays monotone.
    struct Pk { double off, h; };
    std::vector<Pk> pk;
    for (size_t j = 1; j + 1 < n; ++j) {
        if (!(s[j] >= s[j - 1] && s[j] > s[j + 1] && s[j] >= cfg.edgePeakRel * smax)) continue;
        const double a = s[j - 1], b = s[j], c = s[j + 1], den = a - 2.0 * b + c;
        const double sub = den != 0.0 ? std::clamp(0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        const double step = wrapDeg(double(binDeg[j + 1]) - double(binDeg[j]));
        pk.push_back({ wrapDeg(double(binDeg[j]) - double(binDeg[0])) + sub * step, b });
    }
    if (pk.empty()) return out;
    std::sort(pk.begin(), pk.end(), [](const Pk& x, const Pk& y) { return x.h > y.h; });
    std::vector<Pk> keep;
    for (const Pk& q : pk) {
        bool apart = true;
        for (const Pk& k : keep) apart = apart && std::abs(q.off - k.off) >= cfg.edgeMinSepDeg;
        if (apart) keep.push_back(q);
        if (keep.size() == 2) break;
    }
    const auto toDeg = [&](double off) {
        double d = std::fmod(double(binDeg[0]) + off, 360.0);
        return d < 0 ? d + 360.0 : d;
    };
    double leadOff = keep[0].off;
    if (keep.size() == 2) {
        const bool secondLeads = double(rotSign) * (keep[1].off - keep[0].off) > 0.0;
        leadOff = secondLeads ? keep[1].off : keep[0].off;
        const double trailOff = secondLeads ? keep[0].off : keep[1].off;
        out.sepDeg = std::abs(leadOff - trailOff);
        const double tol = std::max(cfg.trailTolDeg, cfg.trailTolFrac * std::abs(expectedSweepDeg));
        if (expectedSweepDeg > 0.0 && std::abs(out.sepDeg - std::abs(expectedSweepDeg)) <= tol) {
            out.hasTrail = true;
            out.trailDeg = toDeg(trailOff);
            out.midDeg   = toDeg(0.5 * (leadOff + trailOff));
        }
    }
    out.leadDeg = toDeg(leadOff);
    const double armDeg = std::fmod(phiSDeg + 180.0, 360.0);
    if (std::abs(wrapDeg(out.leadDeg - armDeg)) < armVetoDeg) return WedgeEdges{};
    out.ok = true;
    return out;
}

} // namespace pinpoint::analysis
