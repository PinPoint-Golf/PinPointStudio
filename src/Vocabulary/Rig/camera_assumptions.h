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

// AD-7's camera assumptions, with the camera's calibration (governance/controls/product/analysis-and-data.md AD-7;
// docs/design/rig_design.md RP-8, §4.3, §4.8; information_model.md §2.3, "Calibration"). The analysis makes two
// assumptions about where the cameras point that nothing measures yet. This header is their one home: the Rig records
// each on the camera it describes as an `extrinsics` record with method `assumed` (src/DomainModel/rig.cpp decides
// which camera gets which), and the analysis reads the down-the-line camera's yaw uncertainties from here
// (src/Club/shaft_direction.h, shaft_direction_series.h, src/Body/skeleton3d_body_series.h). The camera calibration
// programme (#74) later replaces these constants with the camera's own record.
//   - A face-on camera fixed in place is square to the target line and level: yaw, pitch and roll 0, with no
//     orientation uncertainty (AD-7: "no orientation σ"), so a σ of 0 on each.
//   - A down-the-line camera with no measured direction is the analysis's assumed placement, looking along the target
//     line and level: yaw, pitch and roll 0 (fusion::Config's defaults), with the yaw uncertain by 9° and no
//     uncertainty claimed for pitch or roll: theirs is absent, not 0.
// The Solution Architect, 27 Nov 2026 (the rulings for Step 10 on #377: rulings 2 and 4, and the ruling replacing 4).
//
// Vocabulary ring, context Acquisition; header-only, no rules, no includes but the standard library (rig_design.md §6
// rule 1). Diagnostics' readers may include it (a later context includes an earlier one's vocabulary). Changing a
// value here changes the analysis's bearings' σ (a stage version question) and every machine's RigStateId at its
// next launch.

#include <optional>

namespace pinpoint::rig {

// ── The down-the-line camera's yaw uncertainty (planned-metrics promotion, 12 Nov 2026) ──────────────────────────────
// Every BEARING read in the two-camera frame (a heading in the ground plane: where the shaft points, where the
// shoulders aim) moves one-for-one with the down-the-line camera's yaw, which nothing measures until a calibration
// reaches the job (job.dtlCameraCalib.calibrated). Tilts and inclinations do not carry it (≤ 1° over ±15° of yaw,
// shaft_fusion.h). Added in quadrature to the reading's own σ, with the camera-assumed trust record
// (club3d.cameraAssumed). Moved here from pp_tuned_constants.h (tuned::dtlCamera) with their values unchanged.
//
// Uncalibrated: the 4 July stick probe found the down-the-line camera turned 4.4–8.7° toward the golfer, by its
// distance (tools/shaftlab/dtl_yaw_probe.py; dtl_posture_design.md §4). That error is a one-way BIAS, not noise: it
// pushes P2 toward outside and the top toward laid off at once (4 July swing 8 read +9.4° / −11.0°). Mark, 12 Nov
// 2026: widen to 9°, the largest turn measured, so every uncalibrated bearing says plainly how weak it is until
// calibration lands (was 5°). A design value. The FACE-ON camera is taken as square to the target line (Mark, 12 Nov:
// a camera fixed to a wall is not pointed at an angle), so it carries no term of its own.
inline constexpr double kDtlYawSigmaUncalDeg = 9.0;
// Calibrated (camera_pose_sticks.h on the protocol's stick clips): a stated 1°. Provisional — no calibrated session
// has been graded against a second yaw measurement yet.
inline constexpr double kDtlYawSigmaCalDeg   = 1.0;

// ── What each assumed record says ────────────────────────────────────────────────────────────────────────────────
// One assumed orientation, in degrees, with each axis's stated uncertainty (none: no uncertainty claimed).
struct AssumedOrientation {
    double                yawDeg   = 0.0;
    double                pitchDeg = 0.0;
    double                rollDeg  = 0.0;
    std::optional<double> yawSigmaDeg;
    std::optional<double> pitchSigmaDeg;
    std::optional<double> rollSigmaDeg;
};

// The face-on camera fixed in place: square and level, no orientation uncertainty (AD-7).
inline constexpr AssumedOrientation kFaceOnSquare { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

// The down-the-line camera with no measured direction: along the target line and level, the yaw uncertain by 9°.
inline constexpr AssumedOrientation kDownTheLineUncalibrated { 0.0, 0.0, 0.0, kDtlYawSigmaUncalDeg, std::nullopt,
                                                              std::nullopt };

} // namespace pinpoint::rig
