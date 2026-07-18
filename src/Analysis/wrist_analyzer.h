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

#include "shot_analyzer.h"
#include "analysis_stage.h"

// Real Wrist-session (SessionController::Type::Wrist == 1) analyzer. analyze() runs a
// capability-gated stage pipeline over a shared, typed AnalysisContext — the constrained
// blackboard of analysis_pipeline_fusion_architecture_proposal.md §10. The Wrist profile
// (wristProfile() in the .cpp) is an authored list of AnalysisStage objects — IMU fusion
// → segmentation → wrist metrics → pose/smooth/ball/shaft → resemblance/assessment —
// each gated by its CaptureCapabilities so absent devices skip stages instead of forking
// the control flow. runStages() executes the list in order on a QtConcurrent worker over
// the frozen window; projectResult() then flattens the context onto ShotAnalysisResult.
// Degrades gracefully by capability: an IMU-only capture yields wrist metrics + score, a
// camera-only capture yields pose/shaft products, and a window with neither returns
// ok=false (lands on the carousel with score 0) rather than crashing.
class WristAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override;
};

namespace pinpoint::analysis {

// Reusable camera-only profile that produces the kinematic display series (clubhead
// speed / hand speed / lag) for the non-Wrist session types. It reuses the Wrist
// profile's face-on camera stages — Pose → PoseSmooth → Ball → Shaft → SegResolve →
// BindDetail → Kinematics — with no IMU or scoring stages. KinematicsStage self-gates
// on kinematics.enabled, so the profile yields an empty detail when the feature is
// dark. The stage structs stay file-local to wrist_analyzer.cpp; this builder is the
// shared seam (developer guide §10.5 — share when the second session needs it).
SessionProfile cameraKinematicsProfile();

} // namespace pinpoint::analysis
