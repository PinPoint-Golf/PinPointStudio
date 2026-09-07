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

#include "shot_controller.h"
#include "shot_arbiter.h"

// ShotController::Source ↔ the arbiter's modality set.
//
// ⚠ A SEAM, NOT A FIX — lifted verbatim out of shot_controller.cpp's anonymous
// namespace, where it could not be reached by any test. It matters more than
// its size suggests: ShotController::Source is PERSISTED into swing.json as
// `capture.shotSource`, and swing_reanalyzer gates the microphone
// time-of-flight de-bias on the value being 4 (Acoustic). Whatever a committed
// shot is labelled here is what a re-analysis a year from now will believe
// about how its impact instant was found.
//
// Tested by shot_source_map_test.
namespace pinpoint {

// Manual has no arbiter modality (it commits directly). Every detector that does
// have one has its OWN — see the warning on ArbSource: Pose used to ride Ball's
// slot, which cost it both the ability to corroborate and its own name on the
// commit. Returns false for a source that never enters the arbiter.
inline bool toArbSource(ShotController::Source s, ArbSource &out)
{
    switch (s) {
    case ShotController::Source::Acoustic: out = ArbSource::Acoustic; return true;
    case ShotController::Source::Imu:      out = ArbSource::Imu;      return true;
    case ShotController::Source::Ball:     out = ArbSource::Ball;     return true;
    case ShotController::Source::Pose:     out = ArbSource::Pose;     return true;
    // Neither has a local modality: Manual commits directly, and a PPCP Shot
    // has ALREADY been arbitrated by the time it is seen here — feeding it to
    // the local arbiter would be the second arbiter this design exists to
    // prevent.
    case ShotController::Source::Manual:
    case ShotController::Source::Ppcp:     break;
    }
    return false;
}

inline ShotController::Source fromArbSource(ArbSource a)
{
    switch (a) {
    case ArbSource::Acoustic: return ShotController::Source::Acoustic;
    case ArbSource::Imu:      return ShotController::Source::Imu;
    case ArbSource::Ball:     return ShotController::Source::Ball;
    case ArbSource::Pose:     return ShotController::Source::Pose;
    }
    return ShotController::Source::Manual;
}

} // namespace pinpoint
