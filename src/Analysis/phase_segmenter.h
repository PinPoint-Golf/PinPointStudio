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

#include <cstdint>
#include <vector>

#include "imu_vision_fuser.h"   // FusedStreams
#include "swing_analysis.h"            // Phase, PhaseEvent

namespace pinpoint::analysis {

// First-pass swing-phase segmentation from the fused IMU streams (M1). Emits
// Address / Top / Impact / Finish:
//   * Impact = the hard ShotMarker anchor passed in (clamped to the grid).
//   * Address = settle just before sustained lead-hand motion onset.
//   * Top = the lead-hand orientation FURTHEST from Address (backswing apex) —
//           far more robust than angular-velocity valley hunting.
// Pure over FusedStreams (unit-testable). A learned event detector (toe-up /
// parallel P-positions) is deferred; this heuristic is the always-available base.
class PhaseSegmenter {
public:
    static std::vector<PhaseEvent> segment(const FusedStreams &streams, int64_t impactUs);
};

} // namespace pinpoint::analysis
