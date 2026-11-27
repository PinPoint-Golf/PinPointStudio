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

// THE RECORDING CONTRACT'S CAPTURE-TIME TYPES: the IMU segment bindings and the face-on ball inputs, fixed
// when the shot is captured and carried in the Recording (recording.h: Recording::imuBindings, BallInputs).
// They are Acquisition's (the Recording is its output contract), so they live here, beside recording.h,
// and not in Diagnostics' vocabulary. swing_analysis.h includes this header, so its readers see them
// as before. Moved out of swing_analysis.h unchanged: the same names, namespace and members in the same
// order (they are written to the swing file and read back).
//
// Vocabulary ring: value types, no rules.

#include <QByteArray>
#include <QPointF>
#include <QQuaternion>
#include <QRectF>
#include <QString>
#include <cstdint>
#include <vector>

#include "../../Buffer/types.h"               // pinpoint::SourceId, kInvalidSourceId
#include "../Common/segment_role.h"           // SegmentRole
#include "../Common/trunk_calib_provenance.h" // TrunkCalibProvenance (the trunk solve on a binding)

namespace pinpoint::analysis {

// Resolved on the UI thread from the role-keyed placement (ImuManager) + the live ImuInstance
// calibration snapshot (alignA/mountM are session-lifetime on the QObject — the
// worker can never read them, so they are copied into the job here).
struct ImuSegmentBinding {
    SourceId    source = kInvalidSourceId;
    SegmentRole role   = SegmentRole::Unknown;
    QQuaternion alignA;   // fusion-world  -> anatomical-world  (A)
    QQuaternion mountM;   // anatomical-body -> sensor-body     (M)
    // Calibration status snapshot (ImuInstance) — persisted so SwingLab can
    // filter a corpus by calibration provenance. `calibrated` is the composite
    // gate, ImuInstance::fullyCalibrated(): a whole calibration run passed for the sensor
    // (#417); anatCalibrated alone says an alignment was applied, perhaps by half a run.
    bool        anatCalibrated = false;
    bool        calibrated     = false;
    double      mountDeviationDeg    = 0.0;
    double      mountGravityErrorDeg = 0.0;
    QString     calibratedAtUtc;       // ISO8601 wallclock; empty = never calibrated
    double      calibAgeSec = -1.0;    // age at shot time; -1 = never calibrated
    // Present only when A/M came from the trunk stand-tall solve (trunk design §6.8): the δ tier,
    // σ_δ and the gate residuals. Absent = an arm routine's calibration, or none.
    TrunkCalibProvenance trunkCalib;

    // ⚠ THIS IS NOT THE INSTRUMENT DISCRIMINATOR §0 #8 ASKED FOR, AND PHASE F CUT.
    // That one existed to let two instruments claim ONE role simultaneously, so that
    // streamFor(role) could pick between them. The app will never run a wG3 and a
    // Witmotion at once — they interfere at wrist-and-hand spacing — so that problem
    // does not exist and its solution was deleted rather than built.
    //
    // This flag exists for an unrelated and much smaller reason: ONE INSTRUMENT'S RAW
    // QUATERNION NEEDS CONJUGATING BEFORE THE SHARED COMPOSITION. A wG3 streams
    // world->body, our pipeline composes A·q_raw·M expecting body->world, and
    // hm_frame.h's contract is q_anat = q_hm* ⊗ R_ph — i.e. toAnatomical() with the
    // raw quaternion ALREADY conjugated. imu_calibration::toAnatomical does not
    // conjugate and must not learn to; hm_sample_convert.h refuses to conjugate at
    // storage time and that refusal is correct. So the one site that composes has to
    // be told which convention it is holding, and this is how it is told.
    //
    // ⚠ Getting this wrong is INVISIBLE. Every four-candidate frame and both
    // composition orders score exactly zero cross-talk (hm_frame.h:69-108); the
    // wrist ANGLE is convention-blind. A missing conjugate produces a quaternion
    // that tracks the wrist convincingly with every sign inverted.
    bool        hackMotion = false;
};

// One frame of ball-detector output (src/Pose/ball_detector.h BallDetection,
// same normalized [0,1] full-frame convention as PoseFrame2D — no transform
// needed between the two). found=false frames still carry a t_us so gaps are
// distinguishable from "never sampled here" (v3.4 design §9.7 — a
// deliberately low-entropy "constant plus one step" stream).
struct BallSample2D {
    int64_t t_us       = 0;
    bool    found       = false;
    QPointF center;             // normalized [0,1], full frame
    float   radiusNorm  = 0.f;  // normalized to frame width
    float   conf        = 0.f;
    // W3 club-corridor activity (ball_runner.cpp; ball.clubActivity): mean
    // |crop − medRef| / σ over an annulus around the locked ball centre — high
    // when the clubhead is moving beside a still ball (a fidget bob). -1 = absent
    // (never computed: dark ball.clubActivity, the first refFrames warm-up frames,
    // or a frame where the ball wasn't found). TWO-CONSUMER CONTRACT (widened from
    // the W3 single consumer when EventRefine landed): this field feeds ONLY the
    // named pair (1) addressHoldEndFrame's club-quiet mask (shaft_positions.h) and
    // (2) the EventRefine Tier-B at-ball activity gate (event_refine.h) — still
    // never tk0, length, launch, or any DP evidence (ball_anchor_test asserts
    // applyBallAnchor is invariant to it). Persisted as "act" ONLY when >= 0, so a
    // dark run serialises byte-identically.
    float   clubActivity = -1.f;
};

// Face-on ball track: either populated live (CameraInstance's accumulator /
// a recorded swing.json "ball" stream) or replayed offline (BallRunner) over
// an archived swing that predates live recording. Empty ⇒ no ball data ⇒ the
// shaft tracker's ball-anchor pass is a no-op (additive-only, never degrades
// the existing track — design §9.6).
struct BallTrack2D {
    pinpoint::SourceId camera     = pinpoint::kInvalidSourceId;
    std::vector<BallSample2D> frames;
    int64_t launchTUs  = -1;    // the collapse-cliff instant (design §9.3); -1 = no launch observed
    QPointF launchCenter;       // ball position at the last pre-launch frame
};

// The persisted LIVE empty-mat baseline, resolved from swing.json
// setup.ballDetection.baseline. Lets offline re-analysis (BallRunner) reconstruct
// the exact baseline the studio session learned instead of self-seeding over the
// swing window's opening frames — where the ball is already placed (which bakes the
// ball into the baseline it subtracts against). The blob (row-major float32 B,
// w*h*4 bytes) is read by the Recording loader into `bytes` (refactor step 7.2, P11:
// the ball stage opens no file); BallRunner takes the bytes (ballBaselineFromBytes).
// `path` is resolved to absolute by the loader and kept as provenance (where the bytes
// came from, for the runner's warning); `roi` is full-frame normalized
// (the box B covers); `fps` is PROVENANCE ONLY — the offline tracker re-measures its
// own pushed-frame rate (plan risk R2). cv-free by design (Qt-only header).
// LIVE (refactor step 1.5, D5): the capture's Recording carries the same baseline the live detector
// learned, IN MEMORY (`bytes`), because the analysis runs before the export writes the blob — so the
// live path follows the re-analysis ball rule too. The bytes are exactly what the exporter writes.
struct BallBaselineRef {
    QString    path;            // absolute path to the .ballbase.f32 blob (a recorded swing): provenance
    QByteArray bytes;           // the blob itself, w*h*4 bytes (live, or read by the Recording loader)
    int     w = 0, h = 0;       // ROI px dims B was seeded over
    QRectF  roi;                // full-frame normalized ROI
    double  rHat   = 0.0;       // radiusForWidth at seed time (provenance)
    double  fps    = 0.0;       // NOT used to build the tracker (see above)
    double  noise0 = 1.0;       // robustNoise fallback for the tracker
    bool isValid() const { return (!path.isEmpty() || !bytes.isEmpty()) && w > 0 && h > 0 && !roi.isEmpty(); }
};

} // namespace pinpoint::analysis
