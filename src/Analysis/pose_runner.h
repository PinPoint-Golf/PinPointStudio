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

#include <QString>
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
    int     densePreMs  = 500;  // pose every denseStride-th frame within
    int     densePostMs = 250;  // [impact − densePreMs, impact + densePostMs]
    int     denseStride  = 1;   // stride inside the dense zone (2 = 75 Hz here)
    int     sparseStride = 4;   // every Nth frame outside the dense zone

    // Scan bounds (segmentation v3 G3): only frames inside
    // [scanStartUs, scanEndUs] are decoded/posed — the detected swing span
    // (plus pad) instead of the whole 5 s ring. 0/0 = unbounded. If the
    // bounds exclude every frame (clock mismatch), the runner falls back to
    // the full window rather than returning an empty track.
    int64_t scanStartUs = 0;
    int64_t scanEndUs   = 0;

    // Address-hold coverage (v3.4 design §9, plan §2): G3's scanStartUs is
    // pinned close to Takeaway (Address − a few hundred ms), so a real,
    // multi-second still address sits almost entirely BEFORE it — invisible
    // to PoseRunner and therefore to ShaftTracker (which is strictly bounded
    // to pose coverage). addressScanPadUs pulls the coverage window back
    // further (capped at the window start by the caller), sampled coarsely
    // via addressStride since the arm barely moves during a still address —
    // just enough to establish the arm-cone direction, not a full ViTPose
    // pass. 0 = no widening (today's behaviour). Ignored when scanStartUs/
    // scanEndUs are both 0 (already-unbounded run).
    int64_t addressScanPadUs = 0;
    int     addressStride    = 15;

    // Two-pass pose (swing_span_bounding_plan.md §5). The camera-only path has
    // no IMU-derived span (G3's scanStartUs/EndUs need segmentation conf > 0),
    // so the pose pass would otherwise decode+pose the whole 5 s ring. twoPass
    // breaks that chicken-and-egg: pass 1 poses every coarseStride-th frame over
    // the whole window and runs estimateSwingSpanUs() over the coarse grip track;
    // pass 2 then fills only [onset − 150 ms, finish + 150 ms] (dense zone stride
    // denseStride, sparseStride elsewhere), reusing the pass-1 frames rather than
    // re-posing them. The coarse frames stay in the returned track as the
    // address-hold coverage — this subsumes addressScanPadUs on the two-pass
    // path. Ignored when scanStartUs/scanEndUs are already set (an IMU/G3 bound
    // wins). On the degenerate no-run coarse pass the runner falls back to a
    // full-window single pass — never a worse result, only a lost optimisation.
    bool    twoPass      = false;
    int     coarseStride = 12;   // pass-1 stride (≈ 80 ms grid at 150 fps)

    // Optional progress sink, 0..1 over this pose pass (span-relative scan
    // position, not posed-frame count — the sparse zone advances it in
    // stride jumps). Called from the worker thread; may be null.
    std::function<void(float)> progress;
};

class PoseRunner {
public:
    // SwingLab: load a PoseTrack2D from a JSON file in the swing.json pose2d
    // shape ({"frames":[{t_us, kp:[x,y,c]×17, lead, trail, handConf}]}). Used
    // to inject synthetic tracks and to re-run shaft tuning without paying
    // for the pose pass. Returns an empty track on any parse problem.
    static pinpoint::analysis::PoseTrack2D loadFromJson(const QString &file,
                                                        pinpoint::SourceId camera);

    // Decode + pose the face-on camera's frames. Returns an empty track when
    // the source has no frames, the format is undecodable, or ViTPose is
    // unavailable (model missing / built without ORT). Constructs the
    // estimator on the calling (worker) thread; all reads finish before
    // return (frozen-window contract).
    static pinpoint::analysis::PoseTrack2D run(const pinpoint::SwingWindow &window,
                                               pinpoint::SourceId faceOnSource,
                                               const ShotAnalysisRunnerOptions &opt);
};
