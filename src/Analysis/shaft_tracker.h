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

#include <vector>

#include "shaft_track_assembly.h"   // ShaftV3Config + deciding stages
#include "swing_analysis.h"         // ShaftTrack2D, PoseTrack2D, Segmentation

struct ShotAnalysisJob;

namespace pinpoint { class SwingWindow; }

namespace pinpoint::analysis {

struct FusedStreams;

// ShaftTracker — the v3.0-r1 face-on club-shaft estimator (orchestrator). Runs
// the validated club_track_v3 method live: derive per-frame grip anchors + the
// lead-forearm direction φ + the 8-joint body skeleton from the offline pose
// (PoseRunner), decode the face-on camera over the swing span (span-bounded),
// gather E1+E2 evidence (shaft_tracker_math), then decide the globally-
// consistent θ track (shaft_track_assembly: emission → banded Viterbi DP →
// ψ-isotonic reconcile → tiering) and emit it as a ShaftTrack2D.
//
// VISION-ONLY: `streams` (IMU) and `segmentation` are accepted for call-site
// compatibility but unused — v3 runs its own hands-only phase model. Pure const
// reader of the frozen window; all reads finish before return.
//
// `ball` (v3.4 design §9) is an ADDITIVE companion applied AFTER decideTrack()
// returns — a post-hoc soft anchor at address/impact from the grip->ball line,
// never touching the DP/transition-band machinery. Empty ⇒ no-op, identical to
// today's output (design §9.6 — the ball can only improve the track, never
// degrade it).
//
// Returns an INVALID track (consumers must check .valid) when the pose track is
// empty, the camera format is undecodable, or fewer than coverageMin of the
// swing-span frames yield a direct measurement.
class ShaftTracker {
public:
    // SwingLab trace: the per-frame decide internals (emission/DP/reconcile),
    // filled only when a sink is passed — production callers pass nothing. The
    // decide core (shaft_track_assembly) owns the shape.
    using ShaftTrace = ShaftDecideTrace;

    static ShaftTrack2D track(const pinpoint::SwingWindow &window,
                              const PoseTrack2D &pose,
                              const BallTrack2D &ball,
                              const FusedStreams &streams,
                              const Segmentation &segmentation,
                              const ShotAnalysisJob &job,
                              ShaftTrace *trace = nullptr);
};

} // namespace pinpoint::analysis
