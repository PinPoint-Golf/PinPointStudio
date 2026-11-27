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

// ── The shot processor's timings, per source ────────────────────────────────
//
// ⚠ LIFTED WORD FOR WORD out of shot_processor.cpp's anonymous namespace (#394, step 81), where no test could reach
// them, as the join's decisions were lifted into shot_outcome.h. The values and their comments are unchanged; the
// processor reads them from here. Tested by shot_source_map_test.

// Post-trigger capture continuation, per source — the buffer keeps capturing
// for this long after the trigger so the follow-through lands in the ring
// before it freezes. Auto detectors use 1250 ms (segmentation v3: the finish
// is follow-through ~0.67 s + decay-to-still, physically truncated at 500 ms
// — design A.6; impact then sits ~2.75 s into the 4 s ring, leaving ample
// pre-impact room). Manual stays 500 ms: the user presses after the swing and
// impact is back-dated, so the finish is already in the ring.
inline constexpr int kPostRollManualMs   = 500;
inline constexpr int kPostRollImuMs      = 1250;
inline constexpr int kPostRollPoseMs     = 1250;
inline constexpr int kPostRollBallMs     = 1250;
inline constexpr int kPostRollAcousticMs = 1250;

// ── Deferred gather (deferred_sources_design.md §4.1, brief Phase E) ────────
//
// A pull takes about as long as its window spans and the library serialises
// them, so a 3 s pre / 1.5 s post request costs ~4.5 s — and it cannot start
// until the window's last sample exists, at impact + 1.5 s. Reserving at
// DETECTION hides most of that inside the post-roll we were taking anyway.
//
// ⚠ THE TWO DEADLINES ARE ORDERED ON PURPOSE. The library's deadline sits just
// INSIDE ours, so a slow pull materialises its own block — carrying whatever
// arrived plus its coverage — rather than being cancelled by our timeout. A
// cancelled request still produces a block, but one that says CANCELLED where
// the honest answer was TIMED_OUT with 60 % coverage.
inline constexpr qint64 kHistoryDeadlineUs = 7'000'000;   // past impact: library gives up here
inline constexpr int    kGatherDeadlineMs  = 6'000;       // past pause:  we give up here

inline constexpr int postRollMsFor(ShotController::Source s)
{
    switch (s) {
    case ShotController::Source::Manual:   return kPostRollManualMs;
    case ShotController::Source::Imu:      return kPostRollImuMs;
    case ShotController::Source::Pose:     return kPostRollPoseMs;
    case ShotController::Source::Ball:     return kPostRollBallMs;
    case ShotController::Source::Acoustic: return kPostRollAcousticMs;
    // ⚠ MANUAL'S VALUE, AND ON PURPOSE.  The post-roll exists to let a
    // back-dated estimate settle before the ring is frozen; a PPCP `t0` has
    // already waited out the arbiter's whole `issue_hold_ns` on the wire, plus
    // link latency and up to one pump tick, before it ever reaches this
    // process.  Adding the acoustic detector's 1250 ms on top of that would
    // push impact further from the window's trailing edge and eat the
    // pre-impact reach the analyzer needs, for no settling that has not already
    // happened.  Manual is the short value for exactly the same reason: it is
    // the one other Source whose instant is fully back-dated on arrival.
    case ShotController::Source::Ppcp:     return kPostRollManualMs;
    }
    return kPostRollManualMs;
}

} // namespace pinpoint
