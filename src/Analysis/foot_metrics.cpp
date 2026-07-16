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

#include "foot_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pinpoint::analysis {
namespace {

// COCO-WholeBody foot keypoints (wholebody_pose_design.md §1.1): 17 L-bigtoe,
// 18 L-smalltoe, 19 L-heel, 20 R-bigtoe, 21 R-smalltoe, 22 R-heel. Only
// bigtoe + heel are used here (smalltoe is not needed for these quantities).
constexpr int kLBigToe = 17, kLHeel = 19;
constexpr int kRBigToe = 20, kRHeel = 22;

// Euclidean pixel distance between two already-de-normalized px points.
double distPx(const QPointF &a, const QPointF &b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    return std::sqrt(dx * dx + dy * dy);
}

// Angle (deg) of the image-plane vector a→b vs the horizontal (y-down convention).
double angleDeg(const QPointF &a, const QPointF &b)
{
    return std::atan2(b.y() - a.y(), b.x() - a.x()) * 57.29577951308232;
}

double medianOf(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Per-frame foot state from the heel + bigtoe keypoints of each foot.
FootState computeState(const PoseFrame2D &f, int frameW, int frameH, double confMin, bool leadIsLeft)
{
    FootState s;
    s.t_us = f.t_us;

    const int leadHeelIdx  = leadIsLeft ? kLHeel  : kRHeel;
    const int leadToeIdx   = leadIsLeft ? kLBigToe : kRBigToe;
    const int trailHeelIdx = leadIsLeft ? kRHeel  : kLHeel;
    const int trailToeIdx  = leadIsLeft ? kRBigToe : kLBigToe;

    const auto pxOf = [&](int idx) {
        return QPointF(f.kp[idx].x() * frameW, f.kp[idx].y() * frameH);
    };

    if (f.conf[leadHeelIdx] >= confMin && f.conf[leadToeIdx] >= confMin) {
        s.leadValid  = true;
        s.leadHeelPx = pxOf(leadHeelIdx);
        s.leadToePx  = pxOf(leadToeIdx);
        s.leadConf   = 0.5f * (f.conf[leadHeelIdx] + f.conf[leadToeIdx]);
    }
    if (f.conf[trailHeelIdx] >= confMin && f.conf[trailToeIdx] >= confMin) {
        s.trailValid  = true;
        s.trailHeelPx = pxOf(trailHeelIdx);
        s.trailToePx  = pxOf(trailToeIdx);
        s.trailConf   = 0.5f * (f.conf[trailHeelIdx] + f.conf[trailToeIdx]);
    }
    return s;
}

// Linear interp of an ascending sparse channel at t (hold at ends, bridge gaps).
double interpChannel(const std::vector<int64_t> &xs, const std::vector<double> &ys, int64_t x)
{
    if (xs.empty()) return 0.0;                 // guarded upstream (channel non-empty)
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back())  return ys.back();
    const auto it = std::lower_bound(xs.begin(), xs.end(), x);
    const size_t hi = size_t(it - xs.begin());
    const size_t lo = hi - 1;
    const int64_t span = xs[hi] - xs[lo];
    if (span <= 0) return ys[lo];               // coincident samples (defensive)
    const double f = double(x - xs[lo]) / double(span);
    return ys[lo] + (ys[hi] - ys[lo]) * f;
}

int64_t phaseTime(const std::vector<PhaseEvent> &phases, Phase p, int64_t fallback)
{
    for (const PhaseEvent &e : phases)
        if (e.phase == p) return e.t_us;
    return fallback;
}

int nearestIndex(const std::vector<int64_t> &grid, int64_t t)
{
    if (t <= grid.front()) return 0;
    if (t >= grid.back())  return int(grid.size()) - 1;
    const auto it = std::lower_bound(grid.begin(), grid.end(), t);
    const int hi = int(it - grid.begin());
    const int lo = hi - 1;
    return (t - grid[lo] <= grid[hi] - t) ? lo : hi;
}

} // namespace

FootMetricsResult trackFeet(const PoseTrack2D &pose, int frameW, int frameH, bool leadIsLeft,
                            int64_t addressUs, const FootMetricsConfig &cfg)
{
    FootMetricsResult res;
    res.frameW = frameW;
    res.frameH = frameH;
    if (frameW <= 0 || frameH <= 0)
        return res;

    // The smoothed companion track (parallel t_us, same normalized kp) is
    // preferred, exactly like head_track — falls back to raw frames on swings
    // analysed before the smoother existed.
    const std::vector<PoseFrame2D> &frames =
        pose.smoothed.empty() ? pose.frames : pose.smoothed;
    if (frames.empty())
        return res;

    res.states.reserve(frames.size());
    for (const PoseFrame2D &f : frames)
        res.states.push_back(computeState(f, frameW, frameH, cfg.confMin, leadIsLeft));

    // Robust (median) address reference over the lead-valid frames: prefer the
    // confident frames inside the Address-event window; if none (or no
    // event), fall back to the first N lead-valid frames — same shape as
    // head_track's reference resolution.
    std::vector<const FootState *> ref;
    if (addressUs >= 0) {
        for (const FootState &s : res.states)
            if (s.leadValid && std::llabs(s.t_us - addressUs) <= cfg.addrWindowUs)
                ref.push_back(&s);
    }
    if (ref.empty()) {
        for (const FootState &s : res.states) {
            if (!s.leadValid) continue;
            ref.push_back(&s);
            if (int(ref.size()) >= cfg.addrMinFrames) break;
        }
    }
    if (ref.empty())
        return res;   // no lead foot anywhere (e.g. a legacy 17-kp track) — leave valid == false

    std::vector<double> widths, leadFlares, trailFlares, toeLines, addrElev;
    for (const FootState *s : ref) {
        // stanceWidth / toeLine need BOTH feet valid on this reference frame.
        if (s->leadValid && s->trailValid) {
            widths.push_back(distPx(s->leadHeelPx, s->trailHeelPx));
            toeLines.push_back(angleDeg(s->leadToePx, s->trailToePx));
        }
        leadFlares.push_back(angleDeg(s->leadHeelPx, s->leadToePx));   // s->leadValid guaranteed by ref
        if (s->trailValid)
            trailFlares.push_back(angleDeg(s->trailHeelPx, s->trailToePx));
        addrElev.push_back(s->leadToePx.y() - s->leadHeelPx.y());
    }

    res.setup.stanceWidthValid = !widths.empty();
    if (res.setup.stanceWidthValid)
        res.setup.stanceWidthXFrame = medianOf(widths) / frameW;   // isotropic ×frame (single ref dim)
    res.setup.leadFlareValid = !leadFlares.empty();
    if (res.setup.leadFlareValid)
        res.setup.leadFlareDeg = medianOf(leadFlares);
    res.setup.trailFlareValid = !trailFlares.empty();
    if (res.setup.trailFlareValid)
        res.setup.trailFlareDeg = medianOf(trailFlares);
    res.setup.toeLineValid = !toeLines.empty();
    if (res.setup.toeLineValid)
        res.setup.toeLineDeg = medianOf(toeLines);

    const double addrElevPx = medianOf(addrElev);
    res.valid = true;

    // Address-referenced lead-heel-lift channel: elevDiff(t) − elevDiff(addr),
    // ×frame units (single reference dimension, matches head_track's sway/lift).
    for (const FootState &s : res.states) {
        if (!s.leadValid) continue;
        const double elevPx = s.leadToePx.y() - s.leadHeelPx.y();
        res.liftTUs.push_back(s.t_us);
        res.liftValue.push_back((elevPx - addrElevPx) / frameW);
    }
    return res;
}

std::vector<MetricSeries> buildFootSeries(const FootMetricsResult &res,
                                          const std::vector<PhaseEvent> &phases)
{
    std::vector<MetricSeries> out;
    if (!res.valid || res.states.empty())
        return out;

    // Full per-frame grid — the lead-heel-lift curve resamples onto it (gaps
    // bridged, never NaN); the setup scalars' single phaseSample uses its front
    // as the ultimate fallback "address" instant.
    std::vector<int64_t> grid;
    grid.reserve(res.states.size());
    for (const FootState &s : res.states) grid.push_back(s.t_us);

    const int64_t addrT = phaseTime(phases, Phase::Address, grid.front());

    // Setup scalars — see foot_metrics.h's header note on this representation:
    // an empty t_us/value curve + one Address phaseSample. swing_doc.cpp's
    // generic writer and every reader already loop over both arrays
    // independently with no non-empty-curve assumption, so this needs no
    // reader/writer change (docs/reference/swing_json_schema.md 2026-07-13).
    const auto pushScalar = [&](bool ok, const QString &key, const QString &label,
                                const QString &unit, double value) {
        if (!ok) return;
        MetricSeries m;
        m.key   = key;
        m.label = label;
        m.unit  = unit;
        m.phaseSamples.push_back({ Phase::Address, addrT, value, QString() });
        out.push_back(std::move(m));
    };
    pushScalar(res.setup.stanceWidthValid, QStringLiteral("stanceWidth"),
              QStringLiteral("Stance width"), QStringLiteral("×frame"), res.setup.stanceWidthXFrame);
    pushScalar(res.setup.leadFlareValid, QStringLiteral("leadFootFlare"),
              QStringLiteral("Lead foot flare"), QStringLiteral("°"), res.setup.leadFlareDeg);
    pushScalar(res.setup.trailFlareValid, QStringLiteral("trailFootFlare"),
              QStringLiteral("Trail foot flare"), QStringLiteral("°"), res.setup.trailFlareDeg);
    pushScalar(res.setup.toeLineValid, QStringLiteral("toeLineAngle"),
              QStringLiteral("Toe-line angle"), QStringLiteral("°"), res.setup.toeLineDeg);

    // leadHeelLift — full per-frame curve, resampled + gap-bridged exactly like
    // head_track's sway/lift (never NaN).
    if (!res.liftTUs.empty()) {
        std::vector<double> vals(grid.size());
        for (size_t i = 0; i < grid.size(); ++i)
            vals[i] = interpChannel(res.liftTUs, res.liftValue, grid[i]);

        MetricSeries m;
        m.key   = QStringLiteral("leadHeelLift");
        m.label = QStringLiteral("Lead heel lift");
        m.unit  = QStringLiteral("×frame");
        m.t_us  = grid;
        m.value = vals;
        for (const Phase p : { Phase::Address, Phase::Top, Phase::Impact }) {
            const int idx = nearestIndex(grid, phaseTime(phases, p, grid.front()));
            m.phaseSamples.push_back({ p, grid[idx], vals[idx], QString() });
        }
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace pinpoint::analysis
