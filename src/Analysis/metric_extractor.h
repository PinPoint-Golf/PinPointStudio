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

#include "imu_vision_fuser.h"   // FusedStreams
#include "swing_analysis.h"            // MetricSeries, PhaseEvent

namespace pinpoint::analysis {

// Build per-frame MetricSeries (the full curve over the TimeGrid + Address/Top/Impact
// phase samples) for the lead-arm wrist metrics, from the fused anatomical streams and
// the detected phase events. All values are degrees, Address-referenced.
//
// handedness: 0 unknown / 1 right / 2 left — selects lead-arm sign mirroring (provisional
// until verified on the wizard "check your sensors" page).
//
// leadWristFlexExt + leadWristRadUln need only forearm + hand. forearmPronation +
// leadArmFlexion (IMU elbow) are emitted only when the LeadUpperArm binding is present.
class MetricExtractor {
public:
    static std::vector<MetricSeries> extract(const FusedStreams &streams,
                                             const std::vector<PhaseEvent> &phases,
                                             int handedness);
};

} // namespace pinpoint::analysis
