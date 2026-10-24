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

#include "pose_smoother.h"

#include "../Core/kf3_smoother.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace pinpoint::analysis {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The Kalman core lives in Core/kf3_smoother.h (shared with the club rate filter).
using kf3::Kf3;

// Per-keypoint per-frame smoother result (pixel domain).
struct KpResult {
    double px = 0.0, py = 0.0;   // smoothed position (px) — valid iff hasSmoothed
    double sigma = 0.0;          // posterior σ (px)
    bool   hasSmoothed = false;  // a per-segment RTS produced a value here
    bool   accepted = false;     // the frame's measurement passed the gate
    bool   measRun = false;      // frame sits in a confirmed run (≥ runMin, single-hole tolerant)
};

// Phase-5 adaptive-window plumbing for ONE smoothKeypoint run. Default-constructed —
// or the whole pointer omitted — is the pre-phase-5 code path, byte for byte.
// `innov` and smoothKeypoint's `qScale` argument are mutually exclusive by
// construction (the driver below picks exactly one policy per keypoint).
struct KpAdaptIo {
    bool                 innov    = false;    // compute the scale INSIDE the forward pass
    double               innovRef = 1.0;      // divisor on the normalised innovation
    int                  innovRun = 1;        // window of ACCEPTED steps the max runs over
    double               minScale = 1.0;      // clamp floor for the innov policy
    std::vector<double> *aMagOut  = nullptr;  // |a| px/s² per frame (accel pass 1); 0 = no value
    std::vector<double> *scaleOut = nullptr;  // the scale ACTUALLY used per frame (test hook)
};

// Smooth ONE keypoint's x/y pixel tracks across all frames: segmented KF (coast
// budget in time + 3σ gate), per-segment RTS, confirmed-run marking. Fills `out`.
// zx/zy are per-frame measurement px (only read where hasZ), dtSec[f] is the
// transition time into frame f (dtSec[0] unused). This is the runHeadTemporal
// body, minus the θ-jump segmentation and the ball/off-tier machinery.
//
// `qScale` (phase 5, optional) is the per-frame process-noise scale: qScale[f]
// applies to the predict INTO frame f (the transition f−1 → f). nullptr or an empty
// vector ⇒ 1.0 everywhere ⇒ byte-identical output. This ONE function is the whole
// smoother, so the accel policy's two passes are two calls to it — the segmentation,
// gate, budget and run-marking logic exists once and neither policy can drift from it.
void smoothKeypoint(const std::vector<double> &zx, const std::vector<double> &zy,
                    const std::vector<double> &sigMeas, const std::vector<char> &hasZ,
                    const std::vector<double> &dtSec, const PoseSmootherConfig &cfg,
                    std::vector<KpResult> &out,
                    const std::vector<double> *qScale = nullptr,
                    KpAdaptIo *io = nullptr)
{
    const int nf = int(zx.size());
    out.assign(std::size_t(nf), KpResult{});
    // Frames that never reach a predict — a segment's init frame, and any frame
    // outside every segment — keep scale 1.0 and |a| 0 by this initialisation.
    if (io && io->scaleOut) io->scaleOut->assign(std::size_t(std::max(0, nf)), 1.0);
    if (io && io->aMagOut)  io->aMagOut->assign(std::size_t(std::max(0, nf)), 0.0);
    if (nf == 0) return;

    const bool haveScaleIn = (qScale != nullptr) && !qScale->empty();
    const int  innovRunN   = (io && io->innov) ? std::max(1, io->innovRun) : 0;

    const double q = cfg.sigmaJerk * cfg.sigmaJerk;
    const double gate2 = cfg.gateSig * cfg.gateSig;
    const double p0 = cfg.initSigPPx * cfg.initSigPPx;
    const double v0 = cfg.initSigV   * cfg.initSigV;
    const double a0 = cfg.initSigA   * cfg.initSigA;

    // KF pass — segmented; a coasted tail beyond the TIME budget is trimmed and
    // the segment closed (its trimmed frames carry no info → cleared accepted).
    struct Segment { std::vector<int> frames; Kf3 kfx, kfy; };
    std::vector<Segment> segments;
    std::vector<char> accepted(std::size_t(nf), 0);

    std::unique_ptr<Kf3> kfx, kfy;
    std::vector<int> segFrames;
    double coastMs = 0.0;
    int    coastCount = 0;
    int    rejectRun = 0;      // consecutive gate-rejected confident detections (re-acquisition)

    // innov policy state: the normalised innovations of the last innovRun ACCEPTED
    // steps, as a tiny ring. Reset when a segment OPENS (a break must not carry
    // evidence across it — the filter re-inits there and its innovations mean nothing
    // until it has settled), and a coasted step contributes nothing because there was
    // no measurement to be surprised by.
    std::vector<double> innovRing(std::size_t(std::max(0, innovRunN)), 0.0);
    int innovHead = 0, innovCount = 0;

    auto closeSegment = [&]() {
        segments.push_back({ std::move(segFrames), *kfx, *kfy });
        kfx.reset(); kfy.reset(); segFrames.clear();
        coastMs = 0.0; coastCount = 0;
    };

    for (int f = 0; f < nf; ++f) {
        if (!kfx) {                                   // no open segment
            if (hasZ[f]) {
                kfx = std::make_unique<Kf3>(q, gate2, p0, v0, a0);
                kfy = std::make_unique<Kf3>(q, gate2, p0, v0, a0);
                kfx->init(zx[f]); kfy->init(zy[f]);
                segFrames = { f };
                accepted[std::size_t(f)] = 1;
                innovHead = 0; innovCount = 0;      // a new segment starts with no evidence
                rejectRun = 0;
            }
            // else: this frame joins no segment ⇒ stays Off (raw passthrough).
            continue;
        }
        const double dt = dtSec[f];

        // The q scale for the transition INTO this frame. Off/no vector ⇒ 1.0 (and
        // m_q · 1.0 == m_q bitwise, so the whole path is byte-identical).
        double sc = 1.0;
        if (innovRunN > 0) {
            // The first innovRun ACCEPTED steps of a segment run at 1.0: the max is
            // not defined yet, and a freshly re-inited filter's innovations are the
            // loose init priors talking, not motion.
            if (innovCount >= innovRunN) {
                double mx = 0.0;
                for (const double v : innovRing) mx = std::max(mx, v);
                sc = std::clamp(mx / io->innovRef, io->minScale, 1.0);
            }
        } else if (haveScaleIn && f < int(qScale->size())) {
            sc = (*qScale)[std::size_t(f)];
        }
        // ⚠ A STEP WITH NO MEASUREMENT NEVER GETS THE REDUCED q — BOTH POLICIES.
        // A coasted step is a bridge: the posterior IS the prediction, and its σ is the
        // only honest statement that the filter is guessing there. Shrinking q on such a
        // step shrinks that σ without adding one bit of information, which is exactly the
        // dishonesty this whole design exists to remove (a bridged sample would claim to
        // be more certain than the measured samples either side of it). The scale is a
        // statement about MOTION, and a frame with no measurement has no evidence of any.
        //
        // NB this is the CONFIDENCE-gated case (conf < confMeasMin), not the gate-rejected
        // one, and the asymmetry is forced rather than chosen: acceptance is not known
        // until after the predict (gatePass needs Pp), so a rejected measurement's own step
        // has already been taken. A rejected frame is a single-frame event whose σ the RTS
        // bridges from both sides; a conf hole is the multi-frame case that actually
        // matters, and it is decidable in advance.
        if (!hasZ[f]) sc = 1.0;
        kfx->predict(dt, sc); kfy->predict(dt, sc);
        if (io && io->scaleOut) (*io->scaleOut)[std::size_t(f)] = sc;

        bool acc = false;
        double R = 0.0;
        double ni = 0.0;                    // this step's normalised innovation (innov policy)
        if (hasZ[f]) {
            R = sigMeas[f] * sigMeas[f];
            // Joint 2D acceptance: a keypoint is a point — reject the whole frame
            // unless BOTH axes clear their 3σ gate (keeps x/y segments identical).
            acc = kfx->gatePass(zx[f], R) && kfy->gatePass(zy[f], R);
            // Re-acquisition: only a CONFIDENT detection counts towards the run, and only one can
            // end it. A rejected unconfident one breaks the run — it is not evidence the filter is lost.
            const bool confident = sigMeas[f] <= cfg.reacquireSigMaxPx;
            if (!acc && cfg.reacquireRun > 0 && confident && rejectRun >= cfg.reacquireRun) acc = true;
            rejectRun = (acc || !confident) ? 0 : rejectRun + 1;
        } else {
            rejectRun = 0;
        }
        if (hasZ[f]) {
            // Read the gate's own statistic here, on the SAME pending prediction the
            // gate just judged. Both axes share ONE scale — the MAX of the two
            // statistics — for exactly the reason the accept flag is shared: x and y
            // must stay in lock-step or the per-keypoint σ stops being a per-axis σ.
            if (innovRunN > 0)
                ni = std::max(kfx->normInnov(zx[f], R), kfy->normInnov(zy[f], R));
        }
        kfx->commit(acc, hasZ[f] ? zx[f] : 0.0, R);
        kfy->commit(acc, hasZ[f] ? zy[f] : 0.0, R);
        if (innovRunN > 0 && acc) {         // accepted steps only
            innovRing[std::size_t(innovHead)] = ni;
            innovHead = (innovHead + 1) % innovRunN;
            ++innovCount;
        }
        segFrames.push_back(f);
        accepted[std::size_t(f)] = acc ? 1 : 0;
        if (acc) { coastMs = 0.0; coastCount = 0; }
        else     { coastMs += dt * 1000.0; ++coastCount; }

        if (coastMs > cfg.coastBudgetMs) {            // budget overrun ⇒ trim + close
            const int trim = std::min(coastCount, int(segFrames.size()) - 1);
            for (int t = int(segFrames.size()) - trim; t < int(segFrames.size()); ++t)
                accepted[std::size_t(segFrames[t])] = 0;   // trimmed coast carries no info
            kfx->trimTail(trim); kfy->trimTail(trim);
            segFrames.resize(std::size_t(int(segFrames.size()) - trim));
            closeSegment();
        }
    }
    if (kfx && !segFrames.empty()) closeSegment();

    // Per-segment RTS (never across a break); segments < 2 steps contribute nothing.
    for (const Segment &seg : segments) {
        if (seg.frames.size() < 2) continue;
        std::vector<double> xs, xv, ys, yv, xa, ya;
        const bool wantA = (io && io->aMagOut);
        seg.kfx.rts(xs, xv, wantA ? &xa : nullptr);
        seg.kfy.rts(ys, yv, wantA ? &ya : nullptr);
        const int m = std::min<int>(int(seg.frames.size()),
                                    std::min<int>(int(xs.size()), int(ys.size())));
        for (int i = 0; i < m; ++i) {
            KpResult &r = out[std::size_t(seg.frames[i])];
            r.px = xs[i]; r.py = ys[i];
            r.hasSmoothed = true;
            if (wantA)   // isotropic magnitude: the policy is about SPEED of change, not direction
                (*io->aMagOut)[std::size_t(seg.frames[i])] = std::hypot(xa[std::size_t(i)],
                                                                        ya[std::size_t(i)]);
            // Per-axis posterior σ (px).
            //
            // ⚠ THIS IS AN EQUALITY, NOT AN AVERAGE, and a downstream consumer depends on it. The x and
            // y tracks are filtered by two Kf3 instances constructed with the SAME q, stepped with the
            // SAME dt, updated with the SAME R, and — the part that matters — sharing the ACCEPT FLAG:
            // `acc = kfx->gatePass(...) && kfy->gatePass(...)`, so a keypoint is accepted on both axes
            // or rejected on both, and `trimTail` trims the identical frames. The covariance recursion
            // touches none of the measurement VALUES, only q, dt, R and accept — so xv[i] == yv[i]
            // bit-for-bit, and the mean below is that common variance written the long way. Keep the
            // segmentation joint if this is ever revisited: split the accept flag per axis and the two
            // diverge, this scalar stops being a per-axis σ, and every isotropy step downstream becomes
            // an approximation instead of an identity.
            //
            // lower_body_metrics.cpp and upper_body_metrics.cpp read this scalar as a TRUE PER-AXIS σ:
            // they propagate it through formulas that use only a keypoint's y (a body-line tilt), only
            // its x (pelvis sway) and projections onto arbitrary unit directions (the plumb bob, the
            // thorax drift), all without rescaling. That is exact because of the equality above.
            const double var = 0.5 * (std::max(xv[i], 0.0) + std::max(yv[i], 0.0));
            r.sigma = std::sqrt(std::max(var, 1e-9));   // > 0 so 0 stays the "no value" sentinel
        }
    }

    // Confirmed runs (club flush): consecutive accepted frames tolerating a single
    // one-frame hole; runs ≥ runMin bless their accepted members as meas-tier.
    std::vector<int> run;
    auto flush = [&](std::vector<int> &r) {
        if (int(r.size()) >= cfg.runMin)
            for (int f : r) out[std::size_t(f)].measRun = true;
        r.clear();
    };
    int miss = 0;
    for (int f = 0; f < nf; ++f) {
        if (accepted[std::size_t(f)]) { run.push_back(f); miss = 0; }
        else if (++miss > 1) { flush(run); miss = 0; }
    }
    flush(run);

    for (int f = 0; f < nf; ++f) out[std::size_t(f)].accepted = accepted[std::size_t(f)];
}

// ── phase 5: the two motion-adaptive policies ─────────────────────────────────
// Both produce ONE number per frame: a multiplier on q = σ_jerk² for the transition
// into that frame. Neither touches the 3σ gate, the coast budget, the segmentation,
// the confirmed-run marking or the per-group static scales, and neither persists
// anything. Read the exponent warning in pose_smoother.h first: the scale is on q, so
// window ∝ s^(−1/6) and stationary residual σ ∝ s^(+1/12).
//
// **accel** (two-pass, deterministic) — the policy the design wants, because it asks
// the question the failure was about: *is this joint accelerating right now?* Pass 1
// is the ordinary smoother for the keypoint (with the group's static scales), read
// only for its RTS acceleration |a| = hypot(a_x, a_y); pass 2 re-runs the same
// smoother with s = clamp((|a|/aRef)^expo, minScale, 1.0), symmetrically max-filtered
// over ±leadMs. Two passes, not one, because the smoothed acceleration is a
// NON-CAUSAL estimate — a causal one would learn about the impact after the corridor
// has read it.
//   * The max filter is SYMMETRIC on purpose: the window must already be short before
//     the acceleration arrives and stay short just after it. A causal max would smooth
//     the leading frames of the downswing with the address window.
//   * Frames with no smoothed value (outside every segment, or in a trimmed coast
//     tail) carry 1.0, so a segment break widens back to today's window rather than
//     inheriting the address scale — and, being 1.0, those frames also pull their
//     neighbours up through the max filter, which is the conservative direction.
//   * Pass 2's segmentation can in principle differ from pass 1's: a smaller Q shrinks
//     Pp, which shrinks S, which tightens the 3σ gate. The effect is tiny (S is
//     R-dominated: at σ_m ≈ 3.2 px, Pp[0][0] ≈ 1.7 against R ≈ 10.2), and the gate
//     POLICY is untouched — it is the same test on an honestly smaller covariance.
//
// **innov** (single forward pass) — the cheap alternative, registered so the C15 gate
// can reject it on evidence rather than on argument. s = clamp(max over the last
// innovRun ACCEPTED steps of (innov²/S) / innovRef, minScale, 1.0), where innov²/S is
// the gate's own statistic. ⚠ Its honest limit: for a CONSISTENT filter innov²/S is
// χ²(1) with mean 1 whatever the joint is doing, and the part of a real trajectory a
// constant-acceleration predictor cannot see over one dense step is ~jerk·dt³/6 —
// 0.03 px for a 4 Hz 40 px hip excursion at 150 fps, against σ_m ≈ 3.2 px. So at dense
// sampling this statistic is dominated by measurement noise, not by motion: it reads
// as a randomly modulated window with the noise's own ~5 % rate of 2σ excursions.
// Expect it to fail criterion (2); it is here to be measured, and it is exactly one
// forward pass, which is why it was worth registering at all.
inline double adaptScaleFromAccel(double aMag, double aRefEff, double expo,
                                  double minScale)
{
    const double r = (aRefEff > 0.0) ? (aMag / aRefEff) : 1.0;
    // expo == 1.0 is the shipped reading and the common case; skip the libm round-trip
    // so the linear policy is exactly the ratio, with no pow() rounding in it.
    const double u = (expo == 1.0) ? r : std::pow(r, expo);
    return std::clamp(u, minScale, 1.0);
}

// Symmetric running MAX over the frames within ±leadMs of each frame, out of place (an
// in-place pass would feed its own output forward and smear the scale over the track).
//
// ⚠ A DURATION, NOT A FRAME COUNT. PoseRunner's grid is non-uniform — ≈27 ms in the
// coarse address region, 6.7 ms in the dense zone that opens ≈500 ms before impact — so
// ±3 FRAMES would have meant ±81 ms at the address and ±20 ms through impact: widest
// exactly where the lead is least needed, narrowest where it protects the corridor
// samples. The flip side, stated so nobody rediscovers it as a bug: at a 27 ms grid a
// 20 ms lead reaches NO neighbour, so this filter is a no-op out at the address and only
// bites in the dense zone. tSec must be non-decreasing (the frame time base).
void maxFilterSymmetricMs(std::vector<double> &s, const std::vector<double> &tSec, double leadMs)
{
    if (!(leadMs > 0.0) || s.size() < 2 || tSec.size() != s.size()) return;
    const double w = leadMs * 1e-3;
    const int n = int(s.size());
    std::vector<double> out(s.size(), 0.0);
    int lo = 0, hi = 0;
    for (int f = 0; f < n; ++f) {
        while (lo < f && tSec[std::size_t(f)] - tSec[std::size_t(lo)] > w) ++lo;
        if (hi < f) hi = f;
        while (hi + 1 < n && tSec[std::size_t(hi + 1)] - tSec[std::size_t(f)] <= w) ++hi;
        double mx = s[std::size_t(lo)];
        for (int j = lo + 1; j <= hi; ++j) mx = std::max(mx, s[std::size_t(j)]);
        out[std::size_t(f)] = mx;
    }
    s.swap(out);
}

} // namespace

PoseSmootherOutput smoothPoseTrack(const std::vector<PoseFrame2D> &frames,
                                   int frameW, int frameH, const PoseSmootherConfig &cfg)
{
    PoseSmootherOutput result;
    const int nf = int(frames.size());
    result.smoothed.resize(std::size_t(std::max(0, nf)));
    result.aux.resize(std::size_t(std::max(0, nf)));
    if (nf == 0) return result;

    const double W = std::max(1, frameW);
    const double H = std::max(1, frameH);

    // Phase 5: the acceleration reference is quoted at a REFERENCE FORMAT
    // (tuned::…::adapt::kARefFrameWidthPx × kARefFrameHeightPx = 1280×1024, the format
    // the corpus numbers were measured at) because |a| is a pixel quantity: the same hip
    // motion filmed smaller reads fewer px/s² and a fixed threshold would score it quiet.
    // The scaling is the GEOMETRIC MEAN of the two axes, NOT the width: the corpus's own
    // other format is 720×1024 — the SAME height — so a width-only rule would move the
    // threshold 44 % while a vertical motion's px/s² did not move at all. See the
    // constant's comment for the residual (a single isotropic threshold cannot undo a
    // non-square scale change exactly; section 17 of the test measures what it does).
    const double aRefEff = cfg.adapt.aRefPxS2
                         * std::sqrt((W * H)
                                     / (pinpoint::tuned::pose::smoother::adapt::kARefFrameWidthPx
                                        * pinpoint::tuned::pose::smoother::adapt::kARefFrameHeightPx));
    // The C14 test hooks. Off ⇒ these stay empty and no scale vector is ever built.
    if (cfg.adapt.mode != AdaptMode::Off && cfg.adapt.emitScalesForTest) {
        result.adaptScale.assign(std::size_t(kWholeBodyJoints), {});
        result.adaptAccel.assign(std::size_t(kWholeBodyJoints), {});
    }

    // Per-frame transition dt (seconds); dtSec[0] is unused (segment init never
    // predicts). Non-monotonic/zero deltas clamp to a tiny positive.
    std::vector<double> dtSec(std::size_t(nf), 0.0);
    for (int f = 1; f < nf; ++f) {
        double dt = double(frames[f].t_us - frames[f - 1].t_us) * 1e-6;
        if (!(dt > 0.0)) dt = 1e-6;
        dtSec[std::size_t(f)] = dt;
    }
    // Absolute frame times (s) — the adapt lead filter is a DURATION, so it needs the
    // grid itself, not the step deltas. Built once for the whole track, not per keypoint.
    std::vector<double> tSec(std::size_t(nf), 0.0);
    for (int f = 0; f < nf; ++f) tSec[std::size_t(f)] = double(frames[std::size_t(f)].t_us) * 1e-6;

    // Per-keypoint scratch (px measurements + per-frame σ_meas + hasZ mask).
    const std::size_t NF = std::size_t(nf);
    std::vector<double> zx(NF), zy(NF), sigMeas(NF);
    std::vector<char>   hasZ(NF);
    std::vector<KpResult> kres;

    // Seed the output frames (t_us + hands copied through; kp filled per keypoint).
    for (int f = 0; f < nf; ++f) {
        result.smoothed[std::size_t(f)].t_us      = frames[std::size_t(f)].t_us;
        result.smoothed[std::size_t(f)].leadHand  = frames[std::size_t(f)].leadHand;
        result.smoothed[std::size_t(f)].trailHand = frames[std::size_t(f)].trailHand;
        result.smoothed[std::size_t(f)].handConf  = frames[std::size_t(f)].handConf;
    }

    for (int k = 0; k < kWholeBodyJoints; ++k) {
        // Per-group scales (additive — see the header doc): body 0–10 always
        // runs the frozen base constants (scale 1.0; ×1.0 is exact, so that
        // output is byte-identical to a 17-wide run); the feet/face/hand tail
        // and the legs (11–16, phase 4.1) scale the measurement-σ constants and
        // sigmaJerk multiplicatively. Every branch is mutually exclusive and the
        // legs one is last because the tail tests are all `>= 17`.
        double sigScale = 1.0, jerkScale = 1.0;
        if (k >= kLeftHandFirstKp) {
            sigScale = cfg.handSigmaScale;  jerkScale = cfg.handJerkScale;
        } else if (k >= kFaceFirstKp) {
            sigScale = cfg.faceSigmaScale;  jerkScale = cfg.faceJerkScale;
        } else if (k >= kFootFirstKp) {
            sigScale = cfg.feetSigmaScale;  jerkScale = cfg.feetJerkScale;
        } else if (isLegKeypoint(k)) {
            sigScale = cfg.legsSigmaScale;  jerkScale = cfg.legsJerkScale;
        }
        const double measBase  = cfg.measSigBasePx  * sigScale;
        const double measSlope = cfg.measSigSlopePx * sigScale;
        PoseSmootherConfig kcfg = cfg;
        kcfg.sigmaJerk = cfg.sigmaJerk * jerkScale;
        kcfg.reacquireSigMaxPx = measBase + (1.0 - cfg.reacquireConfMin) * measSlope;

        for (int f = 0; f < nf; ++f) {
            const PoseFrame2D &in = frames[std::size_t(f)];
            const double conf = in.conf[std::size_t(k)];
            zx[std::size_t(f)] = in.kp[std::size_t(k)].x() * W;
            zy[std::size_t(f)] = in.kp[std::size_t(k)].y() * H;
            hasZ[std::size_t(f)] = (conf >= cfg.confMeasMin) ? 1 : 0;
            sigMeas[std::size_t(f)] = measBase + (1.0 - conf) * measSlope;
        }
        // ── phase 5: the motion-adaptive window, or the frozen path ──────────
        // mode off ⇒ appliesTo() is false for every keypoint ⇒ this is the single
        // pre-phase-5 call with no scale vector anywhere in the process: the
        // byte-identical promise holds by construction, not by an identity.
        const bool adaptHere = cfg.adapt.appliesTo(k);
        const bool emitScales = adaptHere && cfg.adapt.emitScalesForTest;
        if (!adaptHere) {
            smoothKeypoint(zx, zy, sigMeas, hasZ, dtSec, kcfg, kres);
        } else if (cfg.adapt.mode == AdaptMode::Accel) {
            // Pass 1: the ordinary run, read only for its RTS acceleration.
            std::vector<double> aMag;
            KpAdaptIo io1;
            io1.aMagOut = &aMag;
            smoothKeypoint(zx, zy, sigMeas, hasZ, dtSec, kcfg, kres, nullptr, &io1);
            const std::vector<KpResult> pass1 = kres;   // the guard's reference verdicts

            std::vector<double> s(NF, 1.0);
            for (int f = 0; f < nf; ++f)
                if (kres[std::size_t(f)].hasSmoothed)   // no value ⇒ 1.0 (today's window)
                    s[std::size_t(f)] = adaptScaleFromAccel(aMag[std::size_t(f)], aRefEff,
                                                            cfg.adapt.expo, cfg.adapt.minScale);
            maxFilterSymmetricMs(s, tSec, cfg.adapt.leadMs);

            // Pass 2: the same smoother, same segmentation logic, scaled q.
            KpAdaptIo io2;
            if (emitScales) io2.scaleOut = &result.adaptScale[std::size_t(k)];
            smoothKeypoint(zx, zy, sigMeas, hasZ, dtSec, kcfg, kres, &s, emitScales ? &io2 : nullptr);

            // ── the divergence guard: THE ADAPTIVE WINDOW NEVER REMOVES A SAMPLE ──
            // Pass 2 re-decides segmentation from scratch, and a smaller q shrinks Pp,
            // which shrinks S, which TIGHTENS the 3σ gate. At the shipped minScale that
            // is negligible, but the C15 grid goes down to 0.0025 — σ_jerk a full 10×
            // below the collapse knee the derivation block above measured, where the
            // gate starts rejecting valid fast measurements and segments collapse. So
            // instead of arguing the effect is small, compare the two passes' verdicts
            // (accepted[] and hasSmoothed[] — measRun is derived from accepted, so it
            // follows) and on ANY difference keep pass 1's output for this keypoint and
            // count it. A non-zero count is a REJECTED sweep setting, not a warning.
            bool diverged = (kres.size() != pass1.size());
            for (std::size_t f = 0; f < kres.size() && !diverged; ++f)
                diverged = (kres[f].accepted    != pass1[f].accepted)
                        || (kres[f].hasSmoothed != pass1[f].hasSmoothed);
            if (diverged) {
                kres = pass1;
                ++result.adaptFallbacks;
                // The emitted scale must describe the output that was KEPT, and pass 1
                // is the unadapted run, so the row reads 1.0 everywhere.
                if (emitScales)
                    result.adaptScale[std::size_t(k)].assign(NF, 1.0);
            }
            if (emitScales) result.adaptAccel[std::size_t(k)] = aMag;
        } else {   // AdaptMode::Innov — one forward pass, the scale computed inside it
            KpAdaptIo io;
            io.innov    = true;
            io.innovRef = std::max(cfg.adapt.innovRef, 1e-12);   // 0 would divide by zero
            io.innovRun = cfg.adapt.innovRun;
            io.minScale = cfg.adapt.minScale;
            if (emitScales) io.scaleOut = &result.adaptScale[std::size_t(k)];
            smoothKeypoint(zx, zy, sigMeas, hasZ, dtSec, kcfg, kres, nullptr, &io);
        }

        for (int f = 0; f < nf; ++f) {
            const PoseFrame2D &in = frames[std::size_t(f)];
            PoseFrame2D &sm = result.smoothed[std::size_t(f)];
            PoseKpAux   &ax = result.aux[std::size_t(f)];
            const KpResult &r = kres[std::size_t(f)];
            const float rawConf = in.conf[std::size_t(k)];

            if (!r.hasSmoothed) {                          // Off — raw passthrough
                sm.kp[std::size_t(k)]   = in.kp[std::size_t(k)];
                sm.conf[std::size_t(k)] = 0.0f;
                ax.tier[std::size_t(k)]  = uint8_t(PoseTier::Off);
                ax.sigma[std::size_t(k)] = 0.0f;
                continue;
            }
            sm.kp[std::size_t(k)] = QPointF(r.px / W, r.py / H);
            ax.sigma[std::size_t(k)] = float(r.sigma);
            const bool meas = r.accepted && r.measRun;
            if (meas) {
                ax.tier[std::size_t(k)]  = uint8_t(PoseTier::Meas);
                sm.conf[std::size_t(k)]  = rawConf;
            } else {
                ax.tier[std::size_t(k)]  = uint8_t(PoseTier::Pred);
                sm.conf[std::size_t(k)]  = std::max(rawConf, 0.5f);
            }
        }
    }

    return result;
}

} // namespace pinpoint::analysis
