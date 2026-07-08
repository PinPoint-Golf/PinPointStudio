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

#include "ball_anchor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "shot_analyzer.h"   // ShotAnalysisJob
#include "../Core/pp_debug.h"

namespace pinpoint::analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;

// First-cut thresholds (build + self-verify per plan; corpus-tuning owed —
// see docs/implementation/shaft_detection_v3_impl.md v3.4, "gates deferred
// to the post-release validation pass"). Sweepable later via job.tuningOverrides
// under the ball.* namespace if corpus data says these need moving.
constexpr double kTk0DepartDeg   = 25.0;   // theta-vs-theta_ball departure => tk0
constexpr double kAgreeTolDeg    = 15.0;   // agreement tolerance vs an existing Measured sample
constexpr double kMaxJumpNormPx  = 6.0;    // reject a found sample that jumped from the previous one (mis-lock)
constexpr float  kAnchorConf     = 0.5f;   // soft-anchor confidence (uncorrected — no delta lean-bias table yet)

double angDiffDeg(double aRad, double bRad)
{
    double d = (aRad - bRad) * 180.0 / kPi;
    while (d > 180.0)  d -= 360.0;
    while (d < -180.0) d += 360.0;
    return std::abs(d);
}

// Nearest ball sample to t_us (linear scan — nf is a few hundred to ~1000
// frames, this runs once per coverage frame in a single post-hoc pass, not
// in any hot loop). Returns nullptr when ball has no frames or the nearest
// sample is stale (> ~2 frame periods away).
const BallSample2D *nearestBallSample(const BallTrack2D &ball, int64_t tUs, int64_t toleranceUs)
{
    const BallSample2D *best = nullptr;
    int64_t bestDt = std::numeric_limits<int64_t>::max();
    for (const auto &s : ball.frames) {
        const int64_t dt = std::llabs(s.t_us - tUs);
        if (dt < bestDt) { bestDt = dt; best = &s; }
    }
    if (best && bestDt <= toleranceUs) return best;
    return nullptr;
}

} // namespace

void applyBallAnchor(ShaftTrack2D &out, const BallTrack2D &ball,
                     const std::vector<double> &gx, const std::vector<double> &gy,
                     const std::vector<int64_t> &tUs, int frameW, int frameH,
                     int impf, const ShotAnalysisJob & /*job*/, ShaftDecideTrace *trace)
{
    if (ball.frames.empty() || out.samples.empty() || out.samples.size() != tUs.size())
        return;   // no ball data, or shapes don't line up — additive-only, no-op

    const int nf = int(out.samples.size());
    const int64_t frameToleranceUs = nf > 1
        ? std::max<int64_t>(1, (tUs.back() - tUs.front()) / int64_t(nf - 1)) * 2
        : 200000;

    // Per-frame theta_ball(f) = atan2(B - G) wherever a stable ball sample is
    // available near this coverage frame (design §9.1). A "found" sample that
    // jumped a long way from the last accepted one is treated as a mis-lock,
    // not a moved ball (the ball is stationary by construction at this stage
    // — design §9.7's "constant plus one step").
    std::vector<bool>    haveBall(size_t(nf), false);
    std::vector<double>  thetaBall(size_t(nf), 0.0);
    std::vector<QPointF> ballPx(static_cast<size_t>(nf));
    QPointF lastAccepted;
    bool haveLast = false;
    for (int i = 0; i < nf; ++i) {
        const BallSample2D *s = nearestBallSample(ball, tUs[i], frameToleranceUs);
        if (!s || !s->found) continue;
        const QPointF centerPx(s->center.x() * frameW, s->center.y() * frameH);
        if (haveLast) {
            const double jump = std::hypot(centerPx.x() - lastAccepted.x(),
                                           centerPx.y() - lastAccepted.y());
            if (jump > kMaxJumpNormPx) continue;   // mis-lock — skip this sample
        }
        lastAccepted = centerPx;
        haveLast = true;
        ballPx[size_t(i)] = centerPx;
        thetaBall[size_t(i)] = std::atan2(centerPx.y() - gy[size_t(i)], centerPx.x() - gx[size_t(i)]);
        haveBall[size_t(i)] = true;
    }

    // ── Exploit 1 (design §9.2, refined 2026-07-08) — tk0: first departure
    // of the club's own tracked theta from theta_ball, scanned forward from
    // the first ball sample, capped at bs0 (the current default — no earlier
    // boundary to search for past it). The departure threshold is ADAPTIVE:
    // a frame still counts as "pointing at the ball" if it's within the
    // fixed floor OR no worse than bs0's own departure — bs0 is itself
    // already inside the grip-speed heuristic's lagging boundary, so its own
    // alignment is a permissive, self-calibrating reference (Mark).
    const int bs0 = out.addressPhaseFrame;
    double effectiveDepartDeg = kTk0DepartDeg;
    if (bs0 >= 0 && bs0 < nf && haveBall[size_t(bs0)]) {
        const double refDist = angDiffDeg(out.samples[size_t(bs0)].thetaRad, thetaBall[size_t(bs0)]);
        effectiveDepartDeg = std::max(effectiveDepartDeg, refDist);
    }
    int tk0 = -1;
    const int searchEnd = (bs0 >= 0 && bs0 < nf) ? bs0 : nf;
    for (int i = 0; i < searchEnd && tk0 < 0; ++i) {
        if (!haveBall[size_t(i)]) continue;
        if (angDiffDeg(out.samples[size_t(i)].thetaRad, thetaBall[size_t(i)]) > effectiveDepartDeg) {
            tk0 = i;
        }
    }
    // No earlier departure found within [0,bs0) => bs0 itself stands (no
    // override), but exploit 4 below still has an address-hold bound to work
    // with rather than skipping entirely.
    if (tk0 < 0 && bs0 >= 0) tk0 = bs0;
    if (trace) trace->ballTk0Frame = tk0;

    // Override the reported address boundary when the ball found a genuinely
    // earlier one. Only meaningful when a trace sink is present — that's the
    // vision Segmentation adopted for camera-only swings (wrist_analyzer.cpp's
    // !hasImu fallback); fused swings' reported Address still comes from the
    // IMU today, untouched here (a separate, later question — Mark).
    if (trace && tk0 >= 0 && bs0 >= 0 && tk0 < bs0) {
        for (PhaseEvent &ev : trace->segmentation.events)
            if (ev.phase == Phase::Address) ev.t_us = tUs[size_t(tk0)];
        trace->segmentation.swingStartUs = tUs[size_t(tk0)];
    }

    // ── Exploit 4 (design §9.4/§9.5) — address discrimination + scale floor.
    // Publish ball-anchored theta across [firstBallFrame, tk0) wherever the DP
    // did NOT already produce a real measurement there (spanBound leaves the
    // address hold as a flat/coasted bridge by design — plan §2 is what makes
    // these frames reachable at all). Gated on agreement wherever a real
    // measurement DOES already exist; on disagreement, abstain (touch
    // nothing) rather than override it.
    std::vector<double> lenSamples;
    const int addressEnd = tk0 >= 0 ? tk0 : nf;   // tk0 defaults to bs0 above; -1 only when bs0 itself is unset
    if (tk0 >= 0) {
        for (int i = 0; i < addressEnd; ++i) {
            if (!haveBall[size_t(i)]) continue;
            ShaftSample2D &sm = out.samples[size_t(i)];
            const bool measured = (sm.flags & ShaftMeasured) != 0;
            if (measured) {
                // Corroborate only — never overwrite a real measurement.
                // Disagreement (> tolerance) is an honesty signal: abstain,
                // i.e. do nothing further to this sample.
                if (angDiffDeg(sm.thetaRad, thetaBall[size_t(i)]) <= kAgreeTolDeg)
                    sm.flags |= ShaftBallAnchored;
                continue;
            }
            sm.thetaRad = thetaBall[size_t(i)];
            sm.gripPx   = QPointF(gx[size_t(i)], gy[size_t(i)]);
            sm.conf     = std::max(sm.conf, kAnchorConf);
            sm.flags   |= ShaftBallAnchored;
        }
    }

    // ── Measured club length (design §9.4) — grip-to-ball distance, from the
    // ball's own found samples across the address hold (the shaft is a
    // straight line grip->head and, at address, head ~= ball).
    for (int i = 0; i < addressEnd; ++i) {
        if (!haveBall[size_t(i)]) continue;
        const QPointF &b = ballPx[size_t(i)];
        lenSamples.push_back(std::hypot(b.x() - gx[size_t(i)], b.y() - gy[size_t(i)]));
    }
    if (!lenSamples.empty()) {
        std::nth_element(lenSamples.begin(), lenSamples.begin() + lenSamples.size() / 2, lenSamples.end());
        out.measuredClubLenPx = float(lenSamples[lenSamples.size() / 2]);
    }

    // ── Exploit 3 (design §9.3) — impact anchor. Last pre-launch frame: if
    // the DP didn't measure it directly (Coasted/ImuBridged/KinematicPredicted
    // — the §8.1 arm-witness bridge), soft-anchor from grip->ball. Ships
    // uncorrected (no delta lean-bias subtraction — that table is deferred).
    if (ball.launchTUs >= 0) {
        int fImp = -1;
        for (int i = 0; i < nf; ++i) {
            if (tUs[size_t(i)] <= ball.launchTUs) fImp = i; else break;
        }
        if (fImp >= 0) {
            ShaftSample2D &sm = out.samples[size_t(fImp)];
            if ((sm.flags & ShaftMeasured) == 0) {
                const double bx = ball.launchCenter.x() * frameW, by = ball.launchCenter.y() * frameH;
                sm.thetaRad = std::atan2(by - gy[size_t(fImp)], bx - gx[size_t(fImp)]);
                sm.gripPx   = QPointF(gx[size_t(fImp)], gy[size_t(fImp)]);
                sm.conf     = std::max(sm.conf, kAnchorConf);
                sm.flags   |= ShaftBallAnchored;
            }
        }
    }
    Q_UNUSED(impf)
}

} // namespace pinpoint::analysis
