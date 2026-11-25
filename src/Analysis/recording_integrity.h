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

// THE ONE INTEGRITY RULE of a Recording (refactor step 1.5; step 1 design §4.7, A-09).
//
//   integrityFor(recording) → { the IMU re-fusion verdict, the capture verdict }
//
// Before step 1.5 three copies decided these, and disagreed: the live join skipped the re-fusion
// check when the CURRENT SETTING was ESKF; re-analysis ran it only when the RECORDING said
// Madgwick; SwingLab's --doc-blocks copied re-analysis by hand. The capture check exempted the
// impact camera live, and the impact camera and the off-clock lanes on re-analysis. Now one rule,
// on the Recording alone, so a shot judged live and judged again a year later gets the same answer:
//
//   IMU re-fusion   only when the Recording says its host-fused lanes were fused with Madgwick
//                   (IntegrityInputs::hostOrientationFilter) — the only filter that warm-starts
//                   exactly; otherwise nullopt (no claim: the writer then removes the block).
//                   A wG3 lane is never checked (checkImuRefusion skips it).
//   capture         every camera lane's frame-timestamp holes, and its coverage of the RECORDED
//                   capture window (IntegrityInputs::windowStartUs/EndUs), the impact camera (its
//                   keep band) and the off-clock lanes (a phone clip) exempt from coverage. The
//                   impact is the recorded one; where the first hole opens (firstHoleUs) is
//                   window time. nullopt only with no window to read.
//
// Times: the Recording is window-relative and its window runs on its own clock (t0Us + relative:
// the ring clock live, 0 loaded), so the impact and the coverage window are taken there with t0Us.
//
// Called by the analysis service only (analysis_service.h): with every analysis, and on its own
// for a capture that ran no analysis (the corpus "skip analysis" capture).
//
// Rules ring: pure, no I/O.

#include <optional>

#include "../Vocabulary/Recording/recording.h"
#include "../IMU/imu_refusion_check.h"   // ImuRefusionVerdict, checkImuRefusion
#include "capture_integrity_check.h"     // CaptureIntegrityVerdict, checkCaptureIntegrity

namespace pinpoint::analysis {

struct IntegrityVerdicts {
    // nullopt = no claim (not checkable): the writer removes the block rather than keep an old one.
    std::optional<pinpoint::ImuRefusionVerdict>      imu;
    std::optional<pinpoint::CaptureIntegrityVerdict> capture;
};

IntegrityVerdicts integrityFor(const pinpoint::recording::Recording &recording);

} // namespace pinpoint::analysis
