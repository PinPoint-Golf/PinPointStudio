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

#include <QJsonObject>

#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>

#include "swing_window.h"
#include "shot_analyzer.h"
#include "../IMU/imu_refusion_check.h"
#include "capture_integrity_check.h"
#include "../Vocabulary/Analysis/gate_record.h"   // LoadedSwing::loadGates
#include "effective_context.h"   // ClubReview
#include "../Recording/recording.h"
#include "../DomainModel/recording_source.h"   // RecordingSource: how a stored shot is read (refactor step 3.4b-ii)

// SwingReanalyzer — re-run the production analyzer OFFLINE on a recorded shot, the same
// disk → SwingWindow → analyzer flow the SwingLab runner uses.
//
// THE SHOT IS READ THROUGH ITS RecordingSource (refactor step 3.4b-ii; recording_source.h: 04 step 3 item 8, "they
// ask the Shot"): the value the Shot hands out, which wraps the one RecordingReader, whose store adapter is the
// RECORDING LOADER (src/Export/recording_loader.h, step 1.2): a disk-backed window that STREAMS camera frames on
// demand (one reusable buffer per camera, raw sidecar else MP4; IMU samples and the index in RAM), and the
// Recording that describes it. This file reads no folder itself (P11): a test or tool that has only a folder reads it
// through src/Export/swing_folder_reanalysis.h (the Export ring), which then hands the loaded swing here. The
// job is then made by the ONE builder (buildAnalysisJob, analysis_job_builder.h) from the
// Recording and its effective context, and run through the analysis service (analysis_service.h),
// which also reaches the two integrity verdicts with the one rule (integrityFor) — the very code the
// live capture runs. SwingDiskLoader builds the job (the board's club correction applied, step 1.6);
// reanalyzeLoaded adds the reuse gate (reuse_gate.h), for every door a swing comes in by.
//
// Shared by the in-app re-analyse path (ReanalysisController) and the
// swinglab_run tool, so both exercise one tested reconstruction.

namespace pinpoint::analysis {

// Tunables for which camera is treated as face-on. Default uses the recorded
// per-stream setup.perspective; the offline tool's --face-on forces a substring
// match (escape hatch for mislabelled recordings).
struct SwingLoadOptions {
    QString faceOnSubstring = QStringLiteral("Face");
    bool    faceOnExplicit  = false;
    // Only the decode.* keys are read here (the MP4 readers' threads / hwAccel /
    // seek — pose_inference_performance_plan.md step 5); the job's own copy is
    // what the analysis stages apply.
    QVariantMap tuningOverrides;
};

// A swing reconstructed into a streaming, disk-backed SwingWindow, plus the
// ShotAnalysisJob the one builder made from its Recording (sessionType, face-on-first camera
// sources, IMU sources, serial-matched A/M bindings, impact, the analysis context).
struct LoadedSwing {
    bool                                 ok = false;
    QString                              error;
    // Disk-backed; streams on demand. HELD BY POINTER so it never moves: `recording.window` points at
    // it, and a LoadedSwing is moved around freely (returned, std::move'd) — an inline window would
    // leave that pointer dangling on the first move (step 1.5, review round 1).
    std::unique_ptr<pinpoint::SwingWindow> window;
    // The Recording the job was built from; its window is `window` above (step 1.5: the analysis
    // service judges the integrity verdicts on it). Stays valid when the LoadedSwing is moved.
    pinpoint::recording::Recording       recording;
    ShotAnalysisJob                      job;
    bool                                 usedRaw = false;  // any camera used the raw sidecar
    QJsonObject                          analysisIn;       // the recorded analysis block (version-gated reuse)
    // The origin the reuse readers take the recorded block's times in (analysisReadOriginUs,
    // swing_doc.h): 0 for a document stamped timeBase "window", else its clock.t0_us.
    int64_t                              analysisOriginUs = 0;

    // The shot's review facts as stored: the board's override and the popover's club. The job
    // above applies both through effectiveContext() (step 1.6): the override is the club the analysis
    // uses, the declared club sets the standard length.
    ClubReview                           review;
    // The job's club is the golfer's correction (EffectiveClub::corrected) — for the reuse gate.
    bool                                 clubCorrected = false;
    // true = the document's own analysisContext block; false = the legacy context (recording_header.h).
    bool                                 contextRecorded = false;
    // The document (or its analysisContext) is newer than this build: reanalyzeLoaded refuses.
    bool                                 readOnly = false;
    QString                              readOnlyReason;

    // The host-side orientation filter in force when this swing was CAPTURED, read
    // from streams[].device.orientationFilter of the host-fused lanes (a wG3 fuses
    // on-device and records none, so its lanes are not consulted). Empty when the
    // recording does not say, or when host-fused lanes disagree with each other.
    //
    // Only the re-fusion parity check needs this, and only to refuse: that check
    // re-runs Madgwick and compares to the STORED quaternion, so it is meaningful
    // only if the stored quaternion is itself a Madgwick fusion. Under ESKF — or
    // when the recording is silent — re-fusing proves nothing about the data, and
    // an assumed "probably Madgwick" would be a fabricated provenance claim.
    QString hostOrientationFilter;

    // The capture window the lanes were cut from — the document's `window` block, in the
    // loader's (window-relative) domain. The window above spans only the frames that exist, so
    // it cannot say a clip is short; the capture check's coverage half reads this instead
    // (issue #53). end <= start = the document does not say.
    int64_t captureWindowStartUs = 0;
    int64_t captureWindowEndUs   = -1;
    // Camera lanes that are not on the window's clock (SwingStore::streamOnWindowClock): a phone
    // clip, whose frame times start at its own first frame (ppcp_clip_filer.cpp). The capture
    // check judges their holes but not their coverage.
    std::vector<pinpoint::SourceId> offClockCameras;

    // Gate records decided while LOADING (trust stage 6): a HackMotion lane the loader could not
    // bind (hm_binding_recon.h refusalRecords). reanalyzeLoaded appends them to the analysis'
    // gates, so they reach analysis.gates beside the stages' own; rebuilt on every load.
    std::vector<GateRecord> loadGates;
};

class SwingDiskLoader {
public:
    // Reconstruct the shot through its RecordingSource (the Shot's; a tool's composition root makes one over
    // its own reader), then build the job (buildAnalysisJob, origin "replay"). Never opens the live
    // EventBuffer. On failure returns { ok=false, error set, window=nullopt }.
    static LoadedSwing load(const pinpoint::RecordingSource& source, const SwingLoadOptions& opts = {});
    // The job from a Recording already loaded (whichever door it came in by), through the ONE builder.
    static LoadedSwing fromRecording(pinpoint::LoadedRecording loaded);
    // The loader's options for these.
    static pinpoint::RecordingLoadOptions loadOptions(const SwingLoadOptions& opts);
};

struct ReanalyzeOptions {
    QVariantMap tuningOverrides;            // "<area>.<field>" → numeric (SwingLab)
    int         sessionTypeOverride = -1;   // -1 → use swing.json capture.sessionType
    QString     poseTrackPath;              // inject pose JSON, skip ViTPose (tests / --pose)
    std::optional<QJsonObject> poseTrackJson; // …its content, read by whoever named it (nullopt = unreadable; 7.2)
    // Forwarded to ShotAnalysisJob::fullWindow — disables the swing-span heavy-
    // stage bound so the whole captured window (not just the padded swing span)
    // is scanned. The in-app ReanalysisController sets this true on every
    // explicit re-analyse; SwingLab leaves it false to keep sweeps comparable
    // to production's live-capture bound unless a run opts in.
    bool        fullWindow = false;
    // Version-gated reuse (reuse_gate.h): when the recorded analysis block's pose
    // (stage version + scope — never the model file, Mark's pose rule) matches what
    // would run now, the recorded pose is reloaded and ViTPose is not run; likewise
    // the ball track when its version matches AND the pose is reused, and the shaft
    // track + ladder when the club inputs match too. true ⇒ always re-run everything.
    bool        forceRerun = false;
    // skeleton3d values held fixed in the fit (swing_3d_viz_design.md §13.7) — SwingLab's
    // --skeleton-calib only. The app never pools: §13.9 measured no benefit.
    std::optional<pinpoint::skeleton3d::SkeletonCalib> skeletonCalib;
};

struct ReanalyzeResult {
    bool               ok = false;
    QString            error;
    ShotAnalysisResult analysis;            // score / metrics / trace / detail
    bool               usedRaw = false;

    // A FRESH IMU data-integrity verdict for this pass, or nullopt when the swing
    // was not checkable — which is why this is an optional and not a plain verdict.
    // The caller writing swing.json back must pass it to pinpoint::applyImuIntegrity,
    // which removes the block on nullopt. Carrying the capture-time block forward
    // instead is what made a false ⚠ permanent; see swing_doc.h.
    std::optional<pinpoint::ImuRefusionVerdict> imuIntegrity;
    // Frame-timestamp verdict for the camera lanes (capture_integrity_check.h),
    // nullopt when the window could not be loaded. Same write-back contract as
    // imuIntegrity: pinpoint::applyCaptureIntegrity, which removes on nullopt.
    std::optional<pinpoint::CaptureIntegrityVerdict> captureIntegrity;
};

// The app's re-analysis: load the shot through its RecordingSource, then run makeShotAnalyzer()->analyze().
// Self-contained — touches no live device. The analyzer call is wrapped so a
// thrown analyzer degrades to { ok=false, error } rather than propagating. A read-only document
// (LoadedSwing::readOnly: newer than this build) is refused before anything runs. What the app's
// re-analysis and SwingLab's --write-back run (reanalyse_and_persist.h). (A folder: swing_folder_reanalysis.h.)
ReanalyzeResult reanalyzeShot(const pinpoint::RecordingSource& source, const ReanalyzeOptions& opts = {});
// The re-analysis of a swing already loaded: everything after the read, whichever door it came in by. `label`
// names the swing in the log (its folder, or its ShotRef).
ReanalyzeResult reanalyzeLoaded(LoadedSwing loaded, const ReanalyzeOptions& opts, const QString& label);

} // namespace pinpoint::analysis
