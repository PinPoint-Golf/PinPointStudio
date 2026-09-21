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
constexpr int kShaftStageVersion = 2;
// shaft — REUSABLE since 2026-09-17 (swing_reanalyzer.cpp): the recorded samples,
//         P-anchors, lengths and plane fit are reloaded when this version matches,
//         the pose and ball were themselves reused, and no tuning override is in
//         play; the resolved phase ladder comes back with them. What is NOT reused
//         is the Layer C synth tier — it is re-synthesised from the reloaded samples
//         (resynthesizeLayerC), so a change to the synth rule alone needs no bump.
//         Bump this when samples / anchors / lengths / plane change meaning. The
//         reason reuse exists: a metrics-only re-analysis used to re-run the tracker
//         on the swing's mp4 (no raw sidecar), and on a dark clip that replaced a
//         good live track with a coasting one.
// impact — kImpactStageVersion (bump when impact_runner changes its output).
//          Stamped; never reused — the stage is a few hundred ms and a
//          re-analysis is exactly when a better detector should get its chance.
constexpr int kImpactStageVersion = 1;
// poseDtl — kDtlPoseStageVersion: the down-the-line pose (DtlPoseStage). Reused on
//           re-analysis under pose2d's rule — this version AND the pose model identity
//           match, and no pose./ball./address override is in play. Bump when the DTL
//           scan span/density or its smoothing changes the track's OUTPUT.
// shaftDtl — kDtlShaftStageVersion: the down-the-line club track (DtlShaftStage).
//           Stamped; never reused — it is recomputed from the reused poses and face-on
//           shaft on every re-analysis. Bump when dtl_shaft_* changes its output.
constexpr int kDtlPoseStageVersion  = 1;
constexpr int kDtlShaftStageVersion = 1;
// shaftFusion — kShaftFusionStageVersion: the fused 3-D shaft (ShaftFusionStage,
//           shaft_fusion.h). Stamped; never reused — it is a few microseconds of
//           arithmetic on two tracks that are already in hand. Bump when
//           shaft_fusion.h changes its output.
constexpr int kShaftFusionStageVersion = 1;

struct AnalysisVersions {
    int     pose  = 0;          // 0 = unknown / not stamped
    int     ball  = 0;
    int     shaft = 0;
    int     impact = 0;
    int     poseDtl  = 0;       // 0 = the DTL pose stage did not run
    int     shaftDtl = 0;       // 0 = the DTL shaft stage did not run
    int     shaftFusion = 0;    // 0 = the shaft fusion stage did not run
    QString poseModel;          // "<file>@<bytes>" of the ViTPose model that ran
    QString poseScope;          // "span" | "full"
    QString poseDtlModel;       // "<file>@<bytes>" of the model that posed the DTL stream
    bool stamped() const { return pose > 0; }
};

} // namespace pinpoint::analysis
