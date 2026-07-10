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

// Multi-estimator club-length fusion + persistent per-athlete·club·camera prior
// (plan: robust club length — starry-shimmying-wind). Pure header-only math over
// plain scalars — Qt types (QVariantMap) only for the tuning-override parse, no
// OpenCV, no SwingWindow, no I/O, no statics. Deterministic: identical inputs ⇒
// identical outputs.
//
// Four independent per-swing length estimators (px, grip→head at a FIXED face-on
// camera scale) are fused into one posterior with an explicit confidence:
//   E-ball  medianGripBallLenPx (ball @address)          — sigFrac ball
//   E-band  band scale × (clubLenMm − r0)  (taped clubs)  — sigFrac band
//   E-head  p95 of Stage-2 measured-head rOut             — sigFrac head
//   E-prior the persistent EMA state (joins when priorN ≥ 2) — its own EW σ
// E-pose (rung-3 stature surrogate) is a SANITY BOUND ONLY here — it reads ~33%
// short and is never fused (passed as poseBoundPx). The persistent prior is
// updated ONLY from the PRIOR-FREE fusion (no self-reinforcement).
//
// The whole product lives in the head/length half of the shaft tracker — it
// never enters the corpus-validated θ path (segmentPhases/emission/DP/reconcile).
// The abstain result (nSurvivors == 0) lets the caller fall back to today's
// length ladder byte-for-byte.

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "analysis_tuning.h"   // pinpoint::analysis::tuning::apply ("fusion.*" keys)

namespace pinpoint::analysis {

// Which estimator produced a candidate. E-pose never appears here (sanity bound
// only). Prior carries its own explicit σ; the instantaneous three derive σ from
// cfg.sigFrac*·value.
enum class LengthSource : uint8_t { Ball = 0, Band = 1, Head = 2, Prior = 3 };

// One length estimate fed to fuseClubLength. px ≤ 0 ⇒ "absent" (skipped).
// sigmaPx > 0 pins an explicit σ (the prior passes sqrt(varPx)); sigmaPx ≤ 0 ⇒
// derive σ from the per-source fraction (ball/band/head). All σ are floored at
// cfg.sigFloorPx so a near-zero measurement can never dominate the fuse.
struct LengthCandidate {
    LengthSource source = LengthSource::Ball;
    double       px      = -1.0;
    double       sigmaPx = -1.0;   // <0 ⇒ derive from cfg.sigFrac(source)·px
};

// All fusion constants are "fusion.<name>" dotted-key tunables (SwingLab sweeps).
// The sigFrac* starting fractions and spread0 are re-fit empirically from the
// fresh Phase-6 corpus — these are the validated-STARTING defaults, not frozen.
struct LengthFusionConfig {
    bool   enabled          = true;   // master gate; false ⇒ caller keeps today's ladder byte-identical

    // per-source measurement σ = max(sigFrac·value, sigFloorPx)
    double sigFracBall      = 0.04;   // ball @address is the tightest instantaneous source
    double sigFracBand      = 0.06;   // band scale — grip-correction noise
    double sigFracHead      = 0.10;   // measured-head p95 — foreshortening / streak noise
    double sigFloorPx       = 4.0;    // σ floor (px) — a starved estimate must not crush the fuse

    // leave-one-out outlier rejection (only when ≥ 3 survivors)
    double outlierFrac      = 0.15;   // relative deviation from the median-of-others above which a candidate is dropped

    // confidence: conf = confAgree · confSupport
    double spread0          = 0.10;   // confAgree = exp(−(relSpread/spread0)²); relSpread = (max−min)/fused
    double confBase         = 0.35;   // confSupport = min(1, confBase + confPerInstant·kInstant + confPriorWeight·min(priorN, priorNCap))
    double confPerInstant   = 0.25;   //   kInstant = #surviving instantaneous (non-prior) estimators
    double confPriorWeight  = 0.10;
    int    priorNCap        = 4;

    // sanity bounds (fuseClubLength step 1): candidate accepted iff
    //   px ∈ [max(armFloorPx, poseLoFactor·poseBoundPx), min(frameHFrac·frameH, poseHiFactor·poseBoundPx)]
    // absent bounds (poseBoundPx ≤ 0 / armFloorPx ≤ 0) drop their half of the clamp.
    double poseLoFactor     = 0.9;    // E-pose reads short ⇒ generous lower band
    double poseHiFactor     = 2.2;
    double frameHFrac       = 0.62;   // upper clamp as a fraction of frame height (matches the ladder's 0.62·frameH)

    // pre-pass ladder gate
    double ladderConfMin    = 0.35;   // fused conf ≥ this ⇒ rung 0 (fused px feeds the ladder + Stage-2 search)

    // E-head extraction (post-pass, over post-top Stage-2 HeadTier::Meas frames)
    double headConfMin      = 0.5;    // uncapped head confidence floor for an E-head contribution
    int    headMinMeas      = 6;      // minimum qualifying frames before E-head is trusted
    double headPctile       = 0.95;   // p95 of the qualifying rOut set
    double headPinFrac      = 0.02;   // exclude E-head when p95 sits within this fraction of the frames' search ceiling rHi

    // persistent prior update (updateLengthPrior)
    double priorInitSigFrac = 0.06;   // seed/reset EW σ as this fraction of the seeding value
    double updateConfMin    = 0.5;    // only the PRIOR-FREE fusion with conf ≥ this updates the prior
    double alphaMin         = 0.15;   // EMA rate floor: α = max(alphaMin, 1/(n+1))
    int    nCap             = 50;     // n saturates here (α floored ⇒ the prior keeps adapting to camera drift)
    double kSigmaGate       = 3.0;    // |value − ema| > kSigmaGate·σ ⇒ a disagreement (gated out)
    int    disagreeReset    = 3;      // consecutive disagreements ⇒ prior resets to the new value (silent camera move self-heal)

    static LengthFusionConfig fromOverrides(const QVariantMap& ov)
    {
        using namespace tuning;
        LengthFusionConfig c;
        apply(ov, "fusion.enabled", c.enabled);
        apply(ov, "fusion.sigFracBall", c.sigFracBall);
        apply(ov, "fusion.sigFracBand", c.sigFracBand);
        apply(ov, "fusion.sigFracHead", c.sigFracHead);
        apply(ov, "fusion.sigFloorPx", c.sigFloorPx);
        apply(ov, "fusion.outlierFrac", c.outlierFrac);
        apply(ov, "fusion.spread0", c.spread0);
        apply(ov, "fusion.confBase", c.confBase);
        apply(ov, "fusion.confPerInstant", c.confPerInstant);
        apply(ov, "fusion.confPriorWeight", c.confPriorWeight);
        apply(ov, "fusion.priorNCap", c.priorNCap);
        apply(ov, "fusion.poseLoFactor", c.poseLoFactor);
        apply(ov, "fusion.poseHiFactor", c.poseHiFactor);
        apply(ov, "fusion.frameHFrac", c.frameHFrac);
        apply(ov, "fusion.ladderConfMin", c.ladderConfMin);
        apply(ov, "fusion.headConfMin", c.headConfMin);
        apply(ov, "fusion.headMinMeas", c.headMinMeas);
        apply(ov, "fusion.headPctile", c.headPctile);
        apply(ov, "fusion.headPinFrac", c.headPinFrac);
        apply(ov, "fusion.priorInitSigFrac", c.priorInitSigFrac);
        apply(ov, "fusion.updateConfMin", c.updateConfMin);
        apply(ov, "fusion.alphaMin", c.alphaMin);
        apply(ov, "fusion.nCap", c.nCap);
        apply(ov, "fusion.kSigmaGate", c.kSigmaGate);
        apply(ov, "fusion.disagreeReset", c.disagreeReset);
        return c;
    }
};

// Fusion posterior. abstained ⇒ no survivors: the caller must keep today's path.
struct LengthFused {
    double fusedPx    = -1.0;   // inverse-variance posterior mean (px); <0 ⇒ abstained
    double sigmaPx    = -1.0;   // posterior σ (px), χ²-disagreement inflated
    double conf       = 0.0;    // confAgree · confSupport ∈ [0, 1]
    double spread     = 0.0;    // relative survivor spread (max−min)/fused — diagnostic
    int    nUsed      = 0;      // surviving INSTANTANEOUS estimators (non-prior)
    int    nSurvivors = 0;      // total survivors incl. the prior
    bool   abstained  = true;
};

// Persistent per-athlete·club·camera length prior. Round-tripped through the
// swing.json / AppSettings by P3/P4; decideTrack reads emaPx/varPx/n only.
// disagreeRun is carried so the reset-after-N self-heal survives serialization.
struct LengthPriorState {
    double emaPx       = -1.0;  // running EMA of the PRIOR-FREE fused length (px); <0 ⇒ uninitialised
    double varPx       = 0.0;   // EW variance (px²)
    int    n           = 0;     // #updates folded in (capped nCap)
    int    disagreeRun = 0;     // consecutive kσ-gate failures (reset-after gate)
};

// σ for one candidate: explicit when > 0 (prior), else per-source fraction. Both
// floored at sigFloorPx.
inline double lengthCandidateSigma(const LengthCandidate& c, const LengthFusionConfig& cfg)
{
    double s = c.sigmaPx;
    if (!(s > 0.0)) {
        double frac = cfg.sigFracBall;
        switch (c.source) {
            case LengthSource::Ball:  frac = cfg.sigFracBall; break;
            case LengthSource::Band:  frac = cfg.sigFracBand; break;
            case LengthSource::Head:  frac = cfg.sigFracHead; break;
            case LengthSource::Prior: frac = cfg.sigFracBand; break;   // unreached: prior always carries explicit σ
        }
        s = frac * c.px;
    }
    return std::max(s, cfg.sigFloorPx);
}

// Fuse the candidates into one posterior length + confidence.
//   poseBoundPx  E-pose rung-3 surrogate (px); ≤0 ⇒ no pose sanity band.
//   armFloorPx   1.05× still shoulder→grip (px); ≤0 ⇒ no lower clamp.
//   frameH       frame height (px) for the frameHFrac upper clamp.
//   priorN       the prior's support count (for confSupport's prior term); pass 0
//                for the PRIOR-FREE fusion. A Prior candidate in `cands` supplies
//                the value/σ; priorN supplies the confidence weight — they are
//                decoupled so a prior rejected by leave-one-out contributes 0.
// Steps: (1) sanity bounds, (2) leave-one-out outlier rejection while ≥3 survive,
// (3) inverse-variance fuse with χ² disagreement inflation, (4) conf.
inline LengthFused fuseClubLength(const std::vector<LengthCandidate>& cands,
                                  double poseBoundPx, double armFloorPx, double frameH,
                                  int priorN, const LengthFusionConfig& cfg)
{
    LengthFused r;

    // (1) sanity bounds — reject non-positive + out-of-plausibility candidates.
    double lo = 0.0, hi = std::numeric_limits<double>::infinity();
    if (armFloorPx > 0.0)                 lo = std::max(lo, armFloorPx);
    if (poseBoundPx > 0.0)                lo = std::max(lo, cfg.poseLoFactor * poseBoundPx);
    if (frameH > 0.0)                     hi = std::min(hi, cfg.frameHFrac * frameH);
    if (poseBoundPx > 0.0)                hi = std::min(hi, cfg.poseHiFactor * poseBoundPx);

    std::vector<LengthCandidate> surv;
    surv.reserve(cands.size());
    for (const LengthCandidate& c : cands) {
        if (!(c.px > 0.0)) continue;
        if (c.px < lo || c.px > hi) continue;
        surv.push_back(c);
    }

    // (2) leave-one-out: while ≥3 survive, drop the single worst candidate whose
    // relative deviation from the median of the OTHERS exceeds outlierFrac.
    while (int(surv.size()) >= 3) {
        int    worst = -1;
        double worstDev = cfg.outlierFrac;
        for (size_t i = 0; i < surv.size(); ++i) {
            std::vector<double> others;
            others.reserve(surv.size() - 1);
            for (size_t j = 0; j < surv.size(); ++j) if (j != i) others.push_back(surv[j].px);
            std::sort(others.begin(), others.end());
            const size_t m = others.size();
            const double med = (m % 2) ? others[m / 2] : 0.5 * (others[m / 2 - 1] + others[m / 2]);
            if (!(med > 0.0)) continue;
            const double dev = std::abs(surv[i].px - med) / med;
            if (dev > worstDev) { worstDev = dev; worst = int(i); }
        }
        if (worst < 0) break;
        surv.erase(surv.begin() + worst);
    }

    r.nSurvivors = int(surv.size());
    if (surv.empty()) return r;    // abstained
    r.abstained = false;

    // (3) inverse-variance fuse + χ² (Birge-ratio) disagreement inflation.
    double sumW = 0.0, sumWX = 0.0, minV = surv[0].px, maxV = surv[0].px;
    bool   priorSurvived = false;
    int    kInstant = 0;
    for (const LengthCandidate& c : surv) {
        const double s = lengthCandidateSigma(c, cfg);
        const double w = 1.0 / (s * s);
        sumW  += w;
        sumWX += w * c.px;
        minV = std::min(minV, c.px);
        maxV = std::max(maxV, c.px);
        if (c.source == LengthSource::Prior) priorSurvived = true; else ++kInstant;
    }
    const double fused = sumWX / sumW;
    double chi2 = 0.0;
    for (const LengthCandidate& c : surv) {
        const double s = lengthCandidateSigma(c, cfg);
        const double d = c.px - fused;
        chi2 += (d * d) / (s * s);
    }
    const double baseVar   = 1.0 / sumW;
    const double inflation = (r.nSurvivors > 1) ? std::max(1.0, chi2 / double(r.nSurvivors - 1)) : 1.0;

    r.fusedPx = fused;
    r.sigmaPx = std::sqrt(baseVar * inflation);
    r.nUsed   = kInstant;

    // (4) confidence. relSpread ∈ [0, .] drives agreement; support rewards more
    // independent estimators + a matured prior.
    r.spread = (fused > 0.0) ? (maxV - minV) / fused : 0.0;
    const double confAgree = std::exp(-(r.spread / cfg.spread0) * (r.spread / cfg.spread0));
    const int    priorNeff = priorSurvived ? std::max(0, priorN) : 0;
    const double confSupport = std::min(1.0,
        cfg.confBase + cfg.confPerInstant * kInstant
                     + cfg.confPriorWeight * std::min(priorNeff, cfg.priorNCap));
    r.conf = confAgree * confSupport;
    return r;
}

// Fold one PRIOR-FREE instant fusion into the persistent prior. Updates ONLY when
// instantConf ≥ updateConfMin and fusedInstantPx > 0 (no self-reinforcement — the
// prior never sees its own contribution). EMA α = max(alphaMin, 1/(n+1)) with the
// exponentially-weighted variance recurrence
//   varPx ← (1−α)·(varPx + α·(value − ema)²).
// A kσ outlier gate rejects a value beyond kSigmaGate·σ of the current EMA;
// disagreeReset consecutive rejections RESET the prior to the new value (a silent
// camera move self-heals). A gated-in value clears disagreeRun.
inline void updateLengthPrior(LengthPriorState& st, double fusedInstantPx, double instantConf,
                              const LengthFusionConfig& cfg)
{
    if (!(fusedInstantPx > 0.0) || instantConf < cfg.updateConfMin) return;

    // seed / reset: an uninitialised prior takes the value outright.
    if (st.n <= 0 || !(st.emaPx > 0.0)) {
        const double s = cfg.priorInitSigFrac * fusedInstantPx;
        st.emaPx = fusedInstantPx;
        st.varPx = s * s;
        st.n = 1;
        st.disagreeRun = 0;
        return;
    }

    // kσ gate — sd floored at sigFloorPx so a crushed variance can't lock the gate.
    const double sd  = std::max(std::sqrt(std::max(st.varPx, 0.0)), cfg.sigFloorPx);
    const double dev = std::abs(fusedInstantPx - st.emaPx);
    if (dev > cfg.kSigmaGate * sd) {
        ++st.disagreeRun;
        if (st.disagreeRun >= cfg.disagreeReset) {   // sustained disagreement ⇒ reset (camera moved)
            const double s = cfg.priorInitSigFrac * fusedInstantPx;
            st.emaPx = fusedInstantPx;
            st.varPx = s * s;
            st.n = 1;
            st.disagreeRun = 0;
        }
        return;   // a gated-out value never contributes to the EMA
    }
    st.disagreeRun = 0;

    const double alpha = std::max(cfg.alphaMin, 1.0 / double(st.n + 1));
    const double incr  = fusedInstantPx - st.emaPx;
    st.emaPx += alpha * incr;
    st.varPx  = (1.0 - alpha) * (st.varPx + alpha * incr * incr);
    st.n = std::min(st.n + 1, cfg.nCap);
}

} // namespace pinpoint::analysis
