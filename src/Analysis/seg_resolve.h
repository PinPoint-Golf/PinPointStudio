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

#include <optional>

#include "swing_analysis.h"   // Segmentation

// ---------------------------------------------------------------------------
// Which segmentation becomes the swing's ladder (SegResolveStage)
// ---------------------------------------------------------------------------
//
// In its own header for the reason stream_trim.h gives: the stages are static structs inside
// analysis_pipeline.cpp, out of reach of every test in the tree, and this rule is one a test must
// hold (trunk design §8.5, defect 3).
//
// THE DEFECT. SegResolve adopted the IMU segmentation whenever one EXISTED. PhaseSegmenter always
// returns one when there are streams, and on a job with no hand or forearm sensor — a trunk-only
// job, hip and chest sensors and a face-on camera — it is the clamp fallback, conf 0, "bounds are
// just the window". So a trunk-only job threw away the camera's ladder for three placeholder
// events, and EventRefine (gated on "no IMU segmentation") skipped its refine too.
//
// THE RULE. The IMU segmentation is ADOPTED only when its conf > 0. Otherwise the vision one wins
// when it has confidence, exactly as if there were no IMU segmentation. With neither, the conf-0
// IMU clamp fallback is kept rather than the empty default — both mean "bounds are just the
// window", and the fallback still carries the anchored Impact an IMU-only job always had, so that
// job is unchanged. A reused ladder outranks everything (see SegResolveStage).
namespace pinpoint::analysis {

inline bool imuSegmentationAdopted(const std::optional<Segmentation> &segImu)
{
    return segImu.has_value() && segImu->conf > 0.f;
}

inline Segmentation resolveSegmentation(const std::optional<Segmentation> &segImu,
                                        const std::optional<Segmentation> &segVision,
                                        const std::optional<Segmentation> &ladderPreloaded)
{
    if (ladderPreloaded) return *ladderPreloaded;
    if (imuSegmentationAdopted(segImu)) return *segImu;
    if (segVision && segVision->conf > 0.f) return *segVision;
    return segImu ? *segImu : Segmentation{};
}

} // namespace pinpoint::analysis
