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

#include <QQuaternion>
#include <cstdint>
#include <vector>

#include "../swing_analysis.h"   // SegmentRole, ImuSegmentBinding

namespace pinpoint { class SwingWindow; }

namespace pinpoint::analysis {

// One segment's anatomical-orientation stream, sampled on the shared TimeGrid.
struct SegmentStream {
    SegmentRole role = SegmentRole::Unknown;
    std::vector<QQuaternion> qAnat;        // q_anat = A·q_raw·M at each TimeGrid index
};

// The orientation slice of the fusion layer: every bound IMU resampled onto one
// master timeline as anatomical quaternions. M1 Wrist is IMU-only — no camera
// fusion yet. Joint angles are relative quaternions between adjacent segments, so
// the shared (drifting) 6-axis yaw cancels and no per-shot re-zero is needed.
struct FusedStreams {
    std::vector<int64_t> timeGrid;          // ascending absolute t_us
    std::vector<SegmentStream> segments;    // one per bound IMU with a known role

    const SegmentStream *streamFor(SegmentRole r) const {
        for (const auto &s : segments)
            if (s.role == r) return &s;
        return nullptr;
    }
    bool has(SegmentRole r) const { return streamFor(r) != nullptr; }
};

class ImuVisionFuser {
public:
    // Build a fixed-rate master TimeGrid over the bound IMUs' common sample coverage
    // and, per binding, the anatomical quaternion stream q_anat(t) = A·q_raw(t)·M,
    // resampled (slerp) via SwingWindow::interpolateImu. Bindings whose role is
    // Unknown or that have < 2 in-window samples are skipped. Returns an empty grid
    // if nothing is fusable.
    static FusedStreams fuse(const SwingWindow &window,
                             const std::vector<ImuSegmentBinding> &bindings,
                             double gridHz = 200.0);
};

} // namespace pinpoint::analysis
