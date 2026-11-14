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

// THE RECORDING — everything Analysis is allowed to know about a captured swing, and nothing else
// (docs/developer/information_model.md §2, "The Recording contract"). A SOURCE FACT: made once by
// capture at the window freeze, or loaded by the repository from the swing file; never regenerated,
// immutable once made. Live analysis and re-analysis consume the same value, so the same Recording
// and the same effective context build the same job (buildAnalysisJob, src/Analysis/
// analysis_job_builder.h).
//
// ONE TIME BASE. Every time in here is WINDOW-RELATIVE (µs from the capture window's start), plus
// t0Us, that start on the window's own clock: the ring clock live, 0 for a window loaded from disk
// (whose entries are already relative). The job builder adds t0Us back, because the stages run in
// the window's own clock; nothing else converts.
//
// Who makes it (step 1): the live capture-side snapshot (src/Gui/shot, step 1.5) and the repository's
// reader (src/Export, step 1.2). Until then the two old job builders still read live objects and the
// file directly; this header is the contract they move to.
//
// Vocabulary ring: value types and one code mapping, no rules.

#include <QRectF>
#include <QString>
#include <cstdint>
#include <vector>

#include "../Buffer/types.h"               // pinpoint::SourceId, kInvalidSourceId
#include "../Analysis/swing_analysis.h"    // ImuSegmentBinding, BallTrack2D, BallBaselineRef
#include "analysis_context.h"

namespace pinpoint { class SwingWindow; }

namespace pinpoint::recording {

// ── The one perspective mapping ───────────────────────────────────────────────────────────────
// A camera's placement. The codes are what the swing file stores in streams[].setup.perspective and
// what CameraInstance::Perspective holds; every reader goes through this mapping instead of
// comparing magic ints (2 = face-on, 4 = impact, 1 = down the line).
enum class Perspective : int { None = 0, DownTheLine = 1, FaceOn = 2, Other = 3, Impact = 4 };

inline Perspective perspectiveFromCode(int code)
{
    switch (code) {
    case 1:  return Perspective::DownTheLine;
    case 2:  return Perspective::FaceOn;
    case 3:  return Perspective::Other;
    case 4:  return Perspective::Impact;
    default: return Perspective::None;
    }
}
inline int perspectiveCode(Perspective p) { return static_cast<int>(p); }

// ── Sources by role ───────────────────────────────────────────────────────────────────────────
// One camera stream in the window, in the order the capture (or the file) lists them. Which camera
// is face-on, impact or down the line is decided from `perspective` by the job builder's one rule.
struct CameraStream {
    pinpoint::SourceId source = pinpoint::kInvalidSourceId;
    Perspective        perspective = Perspective::None;
    // Rig facts per stream. There is no rig reference yet: Studio/Rig has no identity until the
    // platform refactor (capture), so a Recording keeps each stream's setup (information_model §2).
    bool               mirrored = false;
    bool               fixedInPlace = false;
    // What backs the frames: "" for the live ring, "raw" (raw sidecar) or "mp4" for a loaded window.
    // Telemetry only (analysis.path.framesFaceOn / framesDtl).
    QString            frames;
};

struct RecordingSources {
    std::vector<CameraStream>       cameras;
    // The IMU and HackMotion lanes present in the window (a lane with no samples is not listed).
    std::vector<pinpoint::SourceId> imuLanes;
};

// ── Capture facts ─────────────────────────────────────────────────────────────────────────────
struct CaptureFacts {
    int     sessionType = -1;   // SessionController::Type as persisted (capture.sessionType); -1 = unknown
    int     shotSource  = 0;    // ShotController::Source as persisted (capture.shotSource)
    int64_t impactUs    = -1;   // the impact instant, WINDOW-RELATIVE; < 0 = unknown
    // The motion-capture quality tier the swing was captured under ("Medium" / "High"); selects the
    // offline pose model. Empty = ViTPose-B (the legacy default).
    QString motionCaptureQuality;
};

// ── Ball inputs ───────────────────────────────────────────────────────────────────────────────
struct BallInputs {
    // The face-on ball stream as captured (the accumulator snapshot frozen at the window), times
    // WINDOW-RELATIVE (frames[].t_us, launchTUs). Empty = none recorded.
    pinpoint::analysis::BallTrack2D     track;
    QRectF                              searchRoi;   // hitting-area box, full-frame normalised; empty = none
    pinpoint::analysis::BallBaselineRef baseline;    // the live empty-mat baseline; invalid = none
};

// ── Integrity inputs ──────────────────────────────────────────────────────────────────────────
// What the capture and IMU integrity checks are judged against (integrityFor, step 1.5).
struct IntegrityInputs {
    // The host-side orientation filter the IMU lanes were fused with at capture ("madgwick",
    // "eskf"); empty when the recording does not say or its lanes disagree. The re-fusion check is
    // meaningful only for Madgwick.
    QString                         hostOrientationFilter;
    // The capture window the lanes were cut from, window-relative. end <= start = not recorded.
    int64_t                         windowStartUs = 0;
    int64_t                         windowEndUs   = -1;
    // Camera lanes not on the window's clock (a phone clip): their holes are judged, not their coverage.
    std::vector<pinpoint::SourceId> offClockCameras;
};

// ── The Recording ─────────────────────────────────────────────────────────────────────────────
struct Recording {
    // The frozen window the streams are read from: the paused ring live, a disk-backed window when
    // loaded. NOT OWNED — whoever made the Recording keeps the window alive for the analysis run.
    // Null in a test that builds a job without streams.
    const pinpoint::SwingWindow *window = nullptr;
    int64_t                      t0Us   = 0;      // the window's start on its own clock (0 loaded)

    RecordingSources sources;
    CaptureFacts     capture;
    BallInputs       ball;
    // Each IMU / HackMotion lane's anatomical binding: role, the A/M calibration snapshot and its
    // provenance, as they stood at capture.
    std::vector<pinpoint::analysis::ImuSegmentBinding> imuBindings;
    IntegrityInputs  integrity;
    AnalysisContext  context;                    // the analysis context recorded (analysis_context.h)
};

} // namespace pinpoint::recording
