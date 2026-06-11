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

// Offline pose pass over the face-on camera of a frozen SwingWindow
// (ShaftTracker S0 — the anchor source). Runs ViTPose synchronously on the
// QtConcurrent analysis worker; honors the frozen-window read contract
// (null-checked payloads, shared frame_decode, const reads only).

#include <cstdint>
#include <functional>

#include "types.h"
#include "swing_analysis.h"   // PoseFrame2D / PoseTrack2D (canonical output shapes)

namespace pinpoint { class SwingWindow; }

// Pose-pass knobs, resolved on the UI thread alongside the rest of the
// analysis job (value type — the worker never touches settings/controllers).
struct ShotAnalysisRunnerOptions {
    int64_t impactUs    = -1;   // impact instant (EventBuffer::nowMicros domain)
    int     handedness  = 0;    // 0 unknown, 1 right, 2 left (ShotAnalysisJob convention)
    int     densePreMs  = 800;  // pose every frame within [impact − densePreMs, …
    int     densePostMs = 300;  //                          … impact + densePostMs]
    int     sparseStride = 4;   // every Nth frame outside the dense zone

    // Scan bounds (segmentation v3 G3): only frames inside
    // [scanStartUs, scanEndUs] are decoded/posed — the detected swing span
    // (plus pad) instead of the whole 5 s ring. 0/0 = unbounded. If the
    // bounds exclude every frame (clock mismatch), the runner falls back to
    // the full window rather than returning an empty track.
    int64_t scanStartUs = 0;
    int64_t scanEndUs   = 0;

    // Optional progress sink, 0..1 over this pose pass (span-relative scan
    // position, not posed-frame count — the sparse zone advances it in
    // stride jumps). Called from the worker thread; may be null.
    std::function<void(float)> progress;
};

class PoseRunner {
public:
    // Decode + pose the face-on camera's frames. Returns an empty track when
    // the source has no frames, the format is undecodable, or ViTPose is
    // unavailable (model missing / built without ORT). Constructs the
    // estimator on the calling (worker) thread; all reads finish before
    // return (frozen-window contract).
    static pinpoint::analysis::PoseTrack2D run(const pinpoint::SwingWindow &window,
                                               pinpoint::SourceId faceOnSource,
                                               const ShotAnalysisRunnerOptions &opt);
};
