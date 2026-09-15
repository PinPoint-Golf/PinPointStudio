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

// ImpactRunner — the ball and the club in the impact camera's clip
// (impact_camera_design.md §7, §10.3), and the club's path through it.
//
// The clip is a 640×240-ish strip at ~600 fps aimed at the ball: a static
// scene (mat, resting ball) that the club enters from above and the ball
// leaves after impact. That structure is the whole detector:
//
//   background  the per-pixel median of the clip's opening frames (the resting
//               ball is part of it); a per-pixel spread over the same frames
//               masks anything that flickers (a lit edge at the frame border)
//   ball, rest  the bright round blob in the background — its diameter is
//               42.67 mm, so it also scales the frame (mmPerPx)
//   foreground  frame − background above a noise-scaled threshold
//   club        the largest foreground component that is not the ball; its
//               principal axis is the shaft, the off-axis mass is the head
//               (an iron blade or a driver head), and when there is no mass
//               the head is the shaft's low end (headKind 2)
//   departure   the first frame the resting ball's spot has gone dark
//   ball, gone  after departure, the round blob nearest where the ball is
//               heading (last position + last velocity)
//   path        a quadratic through the head positions around departure —
//               curved on purpose (§1: the arc turns several degrees per
//               frame near the ball, a line through the points is biased)
//
// Output is in the clip's normalised coordinates (ImpactSample2D). Runs in a
// few hundred ms; never reused across re-analyses (analysis_versions.h).

#include "swing_analysis.h"
#include <cstdint>

namespace pinpoint { class SwingWindow; }

class ImpactRunner {
public:
    // Decode + track over every frame the window holds for `impactSource`.
    // `impactHintUs` (the arbiter's instant, same clock as the entries; -1 =
    // none) only narrows where the path is fitted; the departure the frames
    // show wins when both exist. Empty / !valid on no frames, an undecodable
    // format, or a clip too short to build a background from.
    static pinpoint::analysis::ImpactTrack2D run(const pinpoint::SwingWindow &window,
                                                 pinpoint::SourceId impactSource,
                                                 int64_t impactHintUs);
};
