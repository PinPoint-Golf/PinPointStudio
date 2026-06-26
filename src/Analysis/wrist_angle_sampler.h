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

#include "wrist_assessment_contract.h"
#include "../Core/pp_tuned_constants.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Wrist Motion assessment engine — Phase 0 windowed-median sampler.
//
// Turns an IWristAngleSource (continuous per-DOF time series + a P1–P8 timeline) into a
// PpWristAngleSet (one cell per DOF×position) by taking a SHORT WINDOWED MEDIAN about each Pn
// timestamp (design §3): robust to IMU noise and finite-confidence P-timestamps, and — unlike a
// mean — it never fabricates a value across a gap. Header-only, deterministic, no Qt-GUI.
//
// Per cell the sampler decides:
//   • Gap            — no series / DOF not present / timeline entry absent / too few valid samples.
//   • Indeterminate  — samples exist but the windowed pitch-proxy ≥ gimbalThresholdDeg (design §2.3).
//   • Ok             — windowed median of the values; left-handed values mirrored to canonical RH.
// The gimbal threshold is the SINGLE tunable knob for the Gap/Indeterminate boundary — it is not
// baked into the data, so the same source flips state as the threshold moves.

namespace pinpoint::analysis {

struct PpWristSamplingConfig {
    int64_t windowHalfUs       = pinpoint::tuned::sampler::kWindowHalfUs;       // ±15 ms about Pn (≈ ±1 frame-cluster). tunable
    double  gimbalThresholdDeg = pinpoint::tuned::sampler::kGimbalThresholdDeg; // windowed pitch-proxy ≥ this ⇒ Indeterminate (design §2.3)
    int     minValidSamples    = pinpoint::tuned::sampler::kMinValidSamples;    // fewer valid samples in the window ⇒ Gap
};

namespace detail {

// Median of a non-empty vector. nth_element makes this O(n) and order-independent, so the result
// is deterministic for a given multiset regardless of input order.
inline double medianOf(std::vector<double> v)
{
    const std::size_t n   = v.size();
    const std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double hi = v[mid];
    if (n % 2 == 1)
        return hi;
    std::nth_element(v.begin(), v.begin() + (mid - 1), v.end());
    const double lo = v[mid - 1];
    return 0.5 * (lo + hi);
}

} // namespace detail

struct WristAngleSampler {

    static PpWristAngleSet sample(const IWristAngleSource &source,
                                  const PpWristSamplingConfig &cfg = {})
    {
        PpWristAngleSet set;
        set.handedness = source.handedness();
        const PpSwingPositionTimeline timeline = source.timeline();
        const bool mirror = (source.handedness() == PpHandedness::Left);

        for (int d = 0; d < kNumDof; ++d) {
            const auto dof = static_cast<PpJointDof>(d);
            const PpJointAngleSeries *series = source.series(dof);

            for (int p = 0; p < kNumPos; ++p) {
                const auto pos = static_cast<PpSwingPosition>(p);
                PpWristAngleCell &out = set.cell(dof, pos);   // defaults to Gap

                const PpSwingPositionTimeline::Entry &entry = timeline.at(pos);
                if (series == nullptr || !series->present || !entry.present)
                    continue;                                 // Gap — no fabricated value

                // Gather available samples inside [t − halfWindow, t + halfWindow].
                std::vector<double> values;
                std::vector<double> pitch;
                double confSum = 0.0;
                for (const PpJointAngleSample &s : series->samples) {
                    if (!s.available)
                        continue;
                    if (std::llabs(s.t_us - entry.t_us) > cfg.windowHalfUs)
                        continue;
                    values.push_back(s.valueDeg);
                    pitch.push_back(s.pitchProxyDeg);
                    confSum += s.confidence;
                }

                if (static_cast<int>(values.size()) < cfg.minValidSamples)
                    continue;                                 // Gap — window empty / too sparse

                if (detail::medianOf(pitch) >= cfg.gimbalThresholdDeg) {
                    out.status = PpCellStatus::Indeterminate; // near gimbal — value suppressed
                    continue;
                }

                double v = detail::medianOf(values);
                if (mirror)
                    v *= mirrorSign(dof);                     // canonicalise to right-handed

                const double winConf = confSum / static_cast<double>(values.size());
                out.valueDeg   = v;
                out.status     = PpCellStatus::Ok;
                out.confidence = static_cast<float>(series->baseConfidence * entry.conf * winConf);
            }
        }
        return set;
    }
};

} // namespace pinpoint::analysis
