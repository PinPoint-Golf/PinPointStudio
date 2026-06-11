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

#include <vector>

#include "shaft_track_assembly.h"   // ShaftTrack2D + assembly stages
#include "swing_analysis.h"         // PhaseEvent, SegmentRole

struct ShotAnalysisJob;

namespace pinpoint { class SwingWindow; }

namespace pinpoint::analysis {

struct FusedStreams;

// ShaftTracker stage S2 driver — runs the per-frame anchored radial detection
// (shaft_tracker_math) over EVERY face-on camera frame of the frozen window,
// with grip/elbow anchors interpolated between the PoseRunner's sampled
// frames, then assembles the smooth track (shaft_track_assembly: ŝ_hand
// calibration → Viterbi → wrap-aware 2-channel KF + RTS).
//
// Pure const reader of the frozen window — all reads finish before return.
// Returns an INVALID track (consumers must check .valid) when: the pose track
// is empty, the camera format is undecodable, fewer than the coverage gate's
// fraction of swing-span frames yield a measurement, or nothing is detectable
// at all. The IMU channel engages only when a LeadHand binding is present in
// the fused streams AND the per-shot ŝ_hand fit passes its residual gate —
// otherwise the assembly runs vision-only (degrade, never fabricate).
class ShaftTracker {
public:
    static ShaftTrack2D track(const pinpoint::SwingWindow &window,
                              const PoseTrack2D &pose,
                              const FusedStreams &streams,
                              const std::vector<PhaseEvent> &phases,
                              const ShotAnalysisJob &job);
};

} // namespace pinpoint::analysis
