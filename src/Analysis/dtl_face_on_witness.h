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

// The face-on witness the down-the-line tracker reads (dtl_shaft_tracker_design.md
// §5.3, §5.7), built from the face-on ShaftTrack2D of the SAME swing. ONE builder
// for the app (ShaftStage → DtlShaftStage) and swinglab_run --dtl, so the witness
// the corpus gates grade is the one the app hands the tracker.
//
// READ-ONLY and one-directional (§5.10): it carries face-on's schedule into the DTL
// search and nothing comes back.

#include <cstdint>

#include "dtl_shaft_types.h"        // FaceOnWitness
#include "shaft_track_assembly.h"   // ShaftDecideTrace
#include "swing_analysis.h"         // ShaftTrack2D

namespace pinpoint::analysis {

// Every column is read off `fo` and, when given, `trace` — which must be the SAME
// ShaftTracker::track run's decide trace (one entry per emitted sample, in order).
// A witness assembled from two runs is registered against neither.
//
// Tier and phase come from the trace when its lengths align with fo.samples. With
// no trace (a re-analysis that REUSED the recorded shaft track — there is no decide
// run to trace), or a misaligned one, the tier is derived from each sample's flags:
// a vision measurement (ShaftMeasured, or ShaftWedge → Wedge) not coasted reads as a
// measured tier, anything else PRED; phase is −1 and chir 0. The tracker consumes
// tier only as "measured or not" (dtl_shaft_decide.cpp, wMeasured) and neither phase
// nor chir at all, so that is the whole of what the fall-back must get right — and it
// is an approximation: a Stage-2 measured head on a PRED/RECON frame carries
// ShaftMeasured too.
//
// `impactUs` is the job's impact instant, as SwingLab has always passed it.
FaceOnWitness buildFaceOnWitness(const ShaftTrack2D& fo, const ShaftDecideTrace* trace,
                                 int64_t impactUs);

} // namespace pinpoint::analysis
