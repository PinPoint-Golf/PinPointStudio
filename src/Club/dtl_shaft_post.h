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

// DTL deciding half, part 2 — the post-solve pass: segment probe and snap along
// the frozen solve direction, then tiering into the published track
// (dtl_shaft_tracker_design.md §5.9). It READS the DP output; it never re-solves
// it — the same contract applyBallAnchor has face-on.
//
// The tier ladder is the honesty mechanism. BAND and SEG publish θ + s + a
// terminus, RAY publishes θ alone, and END_ON / OCCLUDED / UNSEEN publish nothing
// but a reason. There is no PRED tier and there must not be one: inventing a θ_D
// to predict with is exactly how §2's impact frames happened.
//
// Two things about the pass are decisions, not detail:
//
//  · THE SNAP RUNS ON THE CONTRAST IMAGE, |frame − boxblur(k)|, not on the raw
//    frame. The taped shaft alternates black and white; a SIGNED ridge integral
//    cancels along it and a wide bright limb outscores it on a frame where the
//    shaft is in plain view. A raw-channel snap re-registers the club onto a leg.
//
//  · SEG IS NOT PRODUCED. The segment probe along the solved direction is
//    deferred whole rather than half-built: a terminus that had not been earned,
//    published at a measured tier, is the one failure §2 is a record of.

#include <cstdint>
#include <vector>

#include "dtl_shaft_config.h"
#include "dtl_shaft_types.h"
#include "shaft_track_assembly.h"   // FrameSource
#include "shaft_tracker_math.h"     // SegmentGeom
#include "../Analysis/gate_record.h" // GateRecord (dtlShaftGates, trust stage 6)

namespace pinpoint::analysis {

// `framesOwned`: frameAt returns owned Mats and may be called from any thread
// (the tracker's decode-once cache is live). Only then may the per-frame snap +
// run loop run in parallel (cfg.par.post); a test's or the over-cap serial
// frameAt keeps it serial.
// `state` is taken by non-const reference because the segment probe refines the
// solved θ in place before tiering reads it back.
DtlShaftTrack2D dtlPostSolve(const FrameSource& frameAt,
                             const std::vector<int64_t>& tUs,
                             const DtlAnchors& anchors,
                             const FaceOnWitness* witness,
                             DtlSolveState& state,
                             int frameW, int frameH,
                             const SegmentGeom& geom,
                             const DtlShaftConfig& cfg,
                             DtlDecideTrace* trace = nullptr,
                             bool framesOwned = false);

// THE DOWN-THE-LINE TRACK'S GATE RECORDS (trust stage 6, docs/design/trust_stage6_plan.md), read off
// the finished track (it is never reused, so this runs on every analysis). One summary each, subject
// track.clubDtl, effect masked:
//   clubDtl.topOutOfView — the top of the backswing left the picture (DtlShaftTrack2D::topOutOfView)
//   clubDtl.quarantine   — the frames whose grip anchor the cross-view check quarantined
//   clubDtl.lateEscape   — the corridor escapes after P8 the tracker refused to publish
// Times are ms from the witness's impact instant; the P4 and P8 instants come from its ladder, by the
// same rules dtlPostSolve used. No witness ⇒ no records (the tracker refused the whole track).
std::vector<GateRecord> dtlShaftGates(const DtlShaftTrack2D& track, const FaceOnWitness* witness,
                                      const DtlShaftConfig& cfg);

} // namespace pinpoint::analysis
