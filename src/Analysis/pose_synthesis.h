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

#include <QPointF>

#include "swing_analysis.h"   // PoseFrame2D / kWholeBodyJoints

// Motion-overlay pose synthesis — temporal upsampling of the offline-smoothed
// skeleton (pose_smoother.{h,cpp}) onto a dense fixed-rate grid; the body-side
// sibling of the shaft's Layer-C synth (shaft_synthesis.h). The dejitter emits
// one smoothed PoseFrame2D per (non-uniform) pose sample; this interpolates a
// smooth C¹ curve THROUGH those frames at cfg.rateHz (default 240 Hz) so the
// replay overlays (frame / trace / fan) scrub the skeleton smoothly instead of
// stepping at the source cadence.
//
// VISUALIZATION only, exactly like club.synth: nothing reads this series for
// metrics/scoring/estimands — the measured detections stay in PoseTrack2D.frames
// and the dejittered per-frame truth in .smoothed; this rides alongside in
// .smoothedSynth and is serialized as a lean pose2d.synth (kp only).
//
// Interpolation is per-coordinate cubic Hermite with non-uniform Catmull-Rom
// tangents — finite differences over the REAL t_us spacing (the pose sampler is
// non-uniform: dense stride-1 near impact, sparse elsewhere), degrading to the
// segment secant when a neighbour is missing. A keypoint that is Off (conf <= 0,
// the smoother's raw-passthrough marker) at EITHER bracketing frame is emitted
// Off (conf 0) in the synth frame — no bridging across a non-measured endpoint,
// so the overlay's conf-gate skips it exactly as it skips a real Off joint. Hands
// (leadHand/trailHand/handConf) are carried by plain linear interpolation.

namespace pinpoint::analysis {

struct PoseSynthConfig {
    bool   enabled = true;    // VIZ tier live; metrics never read the synth series
    double rateHz  = 240.0;   // dense grid cadence (Hz); <= 0 ⇒ empty (no synth)
};

namespace pose_synth_detail {

// Cubic Hermite value at unit parameter tau∈[0,1]. p0/p1 = endpoint values;
// m0/m1 = endpoint derivatives w.r.t. tau (i.e. dP/dtau). Value at tau=0/1 is
// p0/p1 EXACTLY, so the curve interpolates its endpoints (C⁰ at every frame).
inline double hermite(double p0, double p1, double m0, double m1, double tau)
{
    const double t2 = tau * tau, t3 = t2 * tau;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * p0 + (t3 - 2.0 * t2 + tau) * m0
         + (-2.0 * t3 + 3.0 * t2) * p1 + (t3 - t2) * m1;
}

} // namespace pose_synth_detail

// Interpolate `smoothed` onto a dense grid at cfg.rateHz over
// [front.t_us, back.t_us]. Result is ascending in t_us, all VIZ-tier. Empty when
// disabled, given < 2 frames, or rateHz <= 0. `smoothed` MUST be ascending in
// t_us (the smoother's per-frame output is).
inline std::vector<PoseFrame2D> synthesizePoseTrack(
    const std::vector<PoseFrame2D>& smoothed, const PoseSynthConfig& cfg)
{
    using namespace pose_synth_detail;
    std::vector<PoseFrame2D> out;
    const int n = int(smoothed.size());
    if (!cfg.enabled || n < 2 || cfg.rateHz <= 0.0) return out;

    const int64_t t0     = smoothed.front().t_us;
    const int64_t t1     = smoothed.back().t_us;
    const double  stepUs = 1.0e6 / cfg.rateHz;
    if (t1 <= t0 || stepUs < 1.0) return out;

    out.reserve(size_t(double(t1 - t0) / stepUs) + 2);

    int seg = 0;   // advancing bracket cursor: [seg, seg+1] brackets the target t
    for (double td = double(t0); td <= double(t1) + 0.5; td += stepUs) {
        const int64_t t = int64_t(td + 0.5);
        while (seg + 2 < n && smoothed[size_t(seg) + 1].t_us <= t) ++seg;
        const PoseFrame2D& a = smoothed[size_t(seg)];
        const PoseFrame2D& b = smoothed[size_t(seg) + 1];
        const double hSec = double(b.t_us - a.t_us) * 1e-6;
        if (hSec <= 0.0) continue;
        const double tau =
            std::clamp(double(t - a.t_us) / double(b.t_us - a.t_us), 0.0, 1.0);

        // Neighbours for the Catmull-Rom tangents (null at the track ends).
        const PoseFrame2D* p = (seg > 0)     ? &smoothed[size_t(seg) - 1] : nullptr;
        const PoseFrame2D* q = (seg + 2 < n) ? &smoothed[size_t(seg) + 2] : nullptr;

        PoseFrame2D f;
        f.t_us = t;
        for (int j = 0; j < kWholeBodyJoints; ++j) {
            const float ca = a.conf[size_t(j)], cb = b.conf[size_t(j)];
            if (ca <= 0.f || cb <= 0.f) { f.conf[size_t(j)] = 0.f; continue; }  // Off ⇒ Off
            const QPointF pa = a.kp[size_t(j)], pb = b.kp[size_t(j)];
            // Tangents (per second): Catmull-Rom over the real spacing when the
            // neighbour joint is valid, else the segment secant.
            const double sx = (pb.x() - pa.x()) / hSec;
            const double sy = (pb.y() - pa.y()) / hSec;
            double m0x = sx, m0y = sy, m1x = sx, m1y = sy;
            if (p && p->conf[size_t(j)] > 0.f) {
                const double dt = double(b.t_us - p->t_us) * 1e-6;
                if (dt > 0.0) {
                    m0x = (pb.x() - p->kp[size_t(j)].x()) / dt;
                    m0y = (pb.y() - p->kp[size_t(j)].y()) / dt;
                }
            }
            if (q && q->conf[size_t(j)] > 0.f) {
                const double dt = double(q->t_us - a.t_us) * 1e-6;
                if (dt > 0.0) {
                    m1x = (q->kp[size_t(j)].x() - pa.x()) / dt;
                    m1y = (q->kp[size_t(j)].y() - pa.y()) / dt;
                }
            }
            f.kp[size_t(j)]   = QPointF{ hermite(pa.x(), pb.x(), m0x * hSec, m1x * hSec, tau),
                                         hermite(pa.y(), pb.y(), m0y * hSec, m1y * hSec, tau) };
            f.conf[size_t(j)] = float(ca + (cb - ca) * tau);
        }
        // Hands — plain linear interpolation (viz shape parity; overlays read kp).
        f.leadHand  = QPointF{ a.leadHand.x()  + (b.leadHand.x()  - a.leadHand.x())  * tau,
                               a.leadHand.y()  + (b.leadHand.y()  - a.leadHand.y())  * tau };
        f.trailHand = QPointF{ a.trailHand.x() + (b.trailHand.x() - a.trailHand.x()) * tau,
                               a.trailHand.y() + (b.trailHand.y() - a.trailHand.y()) * tau };
        f.handConf  = float(a.handConf + (b.handConf - a.handConf) * tau);
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace pinpoint::analysis
