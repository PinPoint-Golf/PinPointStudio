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

// Offline ball-detection replay over the face-on camera of a frozen SwingWindow
// (v3.4 design §9, plan §3 — the "important prerequisite" for archived swings
// captured before the app recorded a live ball stream). Mirrors PoseRunner
// exactly: run() replays the SAME production temporal-matched-filter core
// (ball_temporal.h TemporalBallTracker) that BallDetector::detectTemporal()
// drives live, over decoded frames from the frozen window; loadFromJson()
// loads an INJECTED ground-truth track for synthetic fixtures.

#include <QRectF>
#include <QString>
#include <cstdint>

#include "types.h"
#include "swing_analysis.h"   // BallSample2D / BallTrack2D (canonical output shapes)
#include "pose_runner.h"      // ShotAnalysisRunnerOptions (shared with PoseRunner)

namespace pinpoint { class SwingWindow; }

class BallRunner {
public:
    // SwingLab: load an INJECTED ground-truth BallTrack2D from a JSON file in
    // the swing.json "ball" block shape ({"frames":[{t_us,found,x,y,r,conf}],
    // "launch":{t_us,x,y}}). Used for synthetic fixtures — there is no real
    // ball for the temporal matched filter to lock onto in a synthetic scene,
    // exactly the reason PoseRunner::loadFromJson() exists for pose. Returns
    // an empty track on any parse problem.
    static pinpoint::analysis::BallTrack2D loadFromJson(const QString &file,
                                                         pinpoint::SourceId camera);

    // Decode + replay ball detection over the face-on camera's frames, using
    // the SAME frame range PoseRunner::run() covered (opt, including the
    // address-hold widening — plan §2) so both channels see the same span.
    // `pose` is the already-resolved offline pose track for this analysis —
    // used to derive the search-corridor prior (ball_detection_v2.md §4.1a's
    // stance corridor, computed offline instead of live) when no `searchRoi`
    // is given; falls back to a static bottom-of-frame band when pose coverage
    // is empty. `searchRoi` (full-frame normalized) is the hitting-area box the
    // live detector used, persisted with the swing — when non-empty it REPLACES
    // the stance corridor so re-analysis searches only the ball region (skipping
    // feet/shoe distractors), matching live detection. Returns an empty track
    // when the source has no frames, the format is undecodable, or too few
    // frames are available to seed the baseline.
    static pinpoint::analysis::BallTrack2D run(const pinpoint::SwingWindow &window,
                                               pinpoint::SourceId faceOnSource,
                                               const pinpoint::analysis::PoseTrack2D &pose,
                                               const ShotAnalysisRunnerOptions &opt,
                                               const QRectF &searchRoi = QRectF());
};
