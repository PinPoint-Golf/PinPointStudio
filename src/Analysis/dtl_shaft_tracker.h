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

// DtlShaftTracker — the SwingWindow layer of the down-the-line tracker
// (dtl_shaft_tracker_design.md §5.1), beside ShaftTracker and built the same way:
// derive the anchors from the DTL pose, build a frame-decode callback over the
// DTL camera (shaft_frame_io.h, shared), then hand both to dtlSolve/dtlPostSolve.
//
// One direction only (§5.10). The witness flows face-on → DTL and nothing flows
// back: this class never touches ShaftTrack2D, the face-on plane fit, or any
// face-on metric. A witness that has been fitted to the thing it testifies about
// is not one.
//
// SwingLab-only. Nothing in the app calls it, and ShotAnalyzer does not know it
// exists — which is why a swing with a DTL stream analyses byte-identically with
// this tracker present.
//
// ⚠ STUB. track() returns an invalid track.

#include "dtl_shaft_types.h"
#include "swing_analysis.h"   // PoseTrack2D

namespace pinpoint { class SwingWindow; }
struct ShotAnalysisJob;

namespace pinpoint::analysis {

class DtlShaftTracker {
public:
    // `dtlPose` must be posed on job.dtlSource. `witness` is null in truthOnly
    // mode — the DTL band lock is then generated without any face-on input, which
    // is the whole point of the instrument that grades the coupling (§6).
    static DtlShaftTrack2D track(const pinpoint::SwingWindow& window,
                                 const PoseTrack2D& dtlPose,
                                 const FaceOnWitness* witness,
                                 const ShotAnalysisJob& job,
                                 DtlDecideTrace* trace = nullptr);
};

} // namespace pinpoint::analysis
