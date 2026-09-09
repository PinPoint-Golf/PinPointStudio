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

#include <QString>

// Producer versions stamped into swing.json `analysis.versions` (2026-09-09,
// Mark: re-analysis re-ran ViTPose, the ball detector and the shaft tracker
// every time although nothing had changed — 11 s of pose per swing on a CPU
// host for nothing). Re-analysis reuses a recorded stage output when the stage
// that produced it is the same as the one that would run now:
//
//   pose  — the model file (name@bytes) AND kPoseStageVersion AND the scan
//           scope (span-bounded live capture vs the full window an explicit
//           re-analyse asks for). Bump kPoseStageVersion whenever pose_runner /
//           the ViTPose decode/crop/two-pass logic changes its OUTPUT.
//   ball  — kBallStageVersion (bump when ball_runner changes its output). Reused
//           only when the pose it was computed from is reused too.
//   shaft — kShaftStageVersion (bump when shaft_tracker / shaft_track_assembly /
//           clubhead_track / the ball anchor change their output). Stamped now;
//           NOT yet reused — the club block is lossy (onsetFloorFrame,
//           addressPhaseFrame) and has no deserialiser.
//
// A recorded swing without a versions block (everything before 2026-09-09) never
// matches, so it re-runs once and is stamped by the write-back.
namespace pinpoint::analysis {

constexpr int kPoseStageVersion  = 1;
constexpr int kBallStageVersion  = 1;
constexpr int kShaftStageVersion = 1;

struct AnalysisVersions {
    int     pose  = 0;          // 0 = unknown / not stamped
    int     ball  = 0;
    int     shaft = 0;
    QString poseModel;          // "<file>@<bytes>" of the ViTPose model that ran
    QString poseScope;          // "span" | "full"
    bool stamped() const { return pose > 0; }
};

} // namespace pinpoint::analysis
