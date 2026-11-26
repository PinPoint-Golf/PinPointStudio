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

// The analysis job: the value one analysis run consumes. Moved out of shot_analyzer.h (refactor step 1,
// governance/artefacts/information_model.md §2) so the measurement rules that read it — ball_anchor,
// shaft_tracker, dtl_shaft_tracker — include a Rules-ring header instead of the analyzer
// interface (Application). It is Rules, not Vocabulary, because it carries SkeletonCalib.

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "types.h"
#include "../Vocabulary/Analysis/swing_analysis.h"
#include "../Skeleton3d/skeleton3d_fit.h"

// Value-type analysis job — everything is resolved before the job is handed to
// the worker (same rule as SwingExportJob): the worker must never touch
// AppSettings or controllers.
//
// THE ONE BUILDER is buildAnalysisJob() (analysis_job_builder.h): Recording +
// effective context + run options → this job, the same for live capture and
// re-analysis (information_model.md §2, the Recording contract). Since step 1.5
// it is the only builder: the live capture's Recording comes from its capture-side
// snapshot (src/Gui/shot/capture_context.h), a stored one from the Recording loader.
// Comments below that say "live" / "re-analysis" name where a fact comes from in
// the Recording, not a second builder.
struct ShotAnalysisJob {
    int     sessionType = -1;   // SessionController::Type (Swing 0, Wrist 1, GRF 2, Coach 3)
    int     shotSource  = 0;    // ShotController::Source as int (avoids a Gui header dep)
    qint64  impactUs    = -1;   // impact instant, on the window's own clock (windowT0Us + relative)
    // THE WINDOW'S START on its own clock — the Recording's t0Us (the ring clock live, 0 for a window
    // loaded from disk), set by the one builder. PipelineAnalyzer::analyze rebases the finished
    // analysis to window time with it, once (swing_analysis_rebase.h; refactor step 1.5). Unset (a job
    // built by hand, e.g. a test's) = the analysis stays on the window's clock and the document writer
    // takes it to window time with the document's clock.t0_us, as before.
    std::optional<int64_t> windowT0Us;
    // THE INPUT STAMP (analysis_versions.h AnalysisInputs; refactor step 1.6): the effective context this
    // job was built from, set by the one builder (analysisInputsFor). The pipeline writes it into
    // SwingAnalysis::versions.inputs, with the session type the run used. Unstamped (version 0) = a job
    // built by hand; its analysis carries no stamp.
    pinpoint::analysis::AnalysisInputs inputs;

    std::vector<pinpoint::SourceId> cameraSources;  // exported cameras, face-on first
    int faceOnCameraCount = 0;  // leading cameraSources entries that are face-on
    // The impact camera (perspective Impact, impact_camera_design.md §10): its
    // clip feeds the ImpactRunner and nothing else — it is never a face-on
    // candidate and never runs pose. Invalid when no camera holds the placement.
    pinpoint::SourceId impactSource = pinpoint::kInvalidSourceId;
    // The down-the-line camera (perspective DownTheLine, dtl_shaft_tracker_design.md
    // §5.1). Two consumers: the DTL SHAFT tracker, which is SwingLab's alone, and —
    // since 2026-09-20 — DtlPoseStage, which poses this stream in the production
    // pipeline so the kinematic sequence can read the trunk from the PAIRED face-on +
    // down-the-line separations (segment_rates.h, routeId "faceOn+dtl"). A swing with
    // a DTL stream therefore no longer analyses identically to one without; a swing
    // WITHOUT one is untouched. Never a face-on candidate and never the impact camera.
    // Invalid when no camera holds the placement (and when --face-on names the DTL
    // stream ITSELF, since it is then the face-on one).
    pinpoint::SourceId dtlSource = pinpoint::kInvalidSourceId;
    std::vector<pinpoint::SourceId> imuSources;     // IMU sources present in the window

    double clubLengthM = 1.12;  // shaft search radius (driver default until club selection is real)
    // True when clubLengthM came from a club record (athlete club, capture.club.lengthMm, or
    // --club-length-mm) rather than the 1.12 m default above. The uncertainty budgets read it:
    // a defaulted length carries σ 0.12 m into every speed (shaft uncertainty design Q1).
    bool   clubLengthKnown = false;
    // The STANDARD length (m) of the swing's club (club_standards.h) when no record had a length,
    // else 0. Two stages read it, and only while clubLengthKnown is false: DtlPosture (the ball's
    // distance, with the standard-length σ) and the 3-D skeleton (a looser hold than a record's).
    // clubLengthM keeps its driver default for the shaft tracker and the speeds, which were graded
    // on it.
    double standardClubLengthM = 0.0;
    // The athlete's standing height (m), 0 = unknown. The skeleton3d fit's scale prior: bone
    // lengths start at the Y-bot's proportions × height / Y-bot height. Filled from the athlete
    // profile on the LIVE path and from the recorded athlete block on RE-ANALYSIS.
    double athleteHeightM = 0.0;
    // skeleton3d values held fixed in the fit (swing_3d_viz_design.md §13.7): set only by SwingLab's
    // --skeleton-calib, through ReanalyzeOptions. The app never pools (§13.9).
    std::optional<pinpoint::skeleton3d::SkeletonCalib> skeletonCalib;
    // Retro-band geometry for the v3 E1 band matcher (taped clubs only). Band
    // centres measured from the butt (mm) + shaft type, taken from the athlete's
    // active club record (athlete_controller.h bandCentersMm/shaftType). Empty ⇒
    // untaped club: the shaft tracker runs E2 (ray) evidence only, no band tier.
    // From the effective club's geometry (the Recording's analysis context), by the one builder.
    std::vector<double> bandCentersMm;
    QString             shaftType;   // "steel" | "graphite" | "" (unknown)
    double hoselFromButtMm = 0.0;  // hosel offset from the butt (mm), athlete club record; 0 = unknown
    // Exposed shaft, bottom of grip → top of hosel (mm), athlete club record
    // (markerless_club_tracker_design.md §4.1); 0 = unknown ⇒ the tracker assumes
    // a 265 mm grip. Grip end from the butt = hoselFromButtMm − shaftLengthMm.
    double shaftLengthMm = 0.0;
    // Where the golfer's hands end on the grip (mm from the butt), club record;
    // 0 = unknown ⇒ 180. The steel-segment lock's proximal landmark (§4.8).
    double handsEndMm = 0.0;

    // Persistent club-length prior (club_length_fusion.h). clubName is the
    // canonical club-vocabulary id — half the prior key (athleteUuid|clubName|
    // cameraKey; the rest resolved in ShotProcessor). priorClubLen* carry the
    // persisted LengthPriorState so decideTrack can join E-prior into the fuse:
    // taken by the one builder from the Recording's analysis context (club.lengthPrior):
    // resolved from AppSettings once, at the window freeze, on the LIVE path (the
    // capture snapshot), read back from swing.json on RE-ANALYSIS (deterministic,
    // never AppSettings). Defaults (N=0) ⇒ no prior joins.
    QString clubName;
    double  priorClubLenPx    = -1.0;  // LengthPriorState::emaPx (px); <0 ⇒ uninitialised
    double  priorClubLenVarPx = -1.0;  // LengthPriorState::varPx (px²); <0 ⇒ absent
    int     priorClubLenN     = 0;     // LengthPriorState::n (updates folded in); 0 ⇒ no prior

    // Resolved IMU -> anatomical-segment bindings (placement slot + the live
    // calibration A/M snapshot), filled on the UI thread — the worker cannot
    // read the live ImuInstance. Empty when no IMU is bound.
    std::vector<pinpoint::analysis::ImuSegmentBinding> imuBindings;
    int     handedness = 0;     // 0 unknown, 1 right, 2 left (lead-arm sign)
    QString swingDir;           // swing folder for persistence (set at the join)

    // Motion-capture quality tier ("Medium"/"High") selecting the offline pose
    // model: "High" runs ViTPose++-L when downloaded, everything else runs
    // ViTPose-B (pose_model_selection.h::useVitPoseLarge). From the Recording's
    // capture facts: the setting read once at the window freeze on the LIVE path
    // (and written to swing.json from that same value), the recorded value on
    // RE-ANALYSIS (deterministic, never AppSettings). Empty ⇒ ViTPose-B (legacy/default).
    QString motionCaptureQuality;

    // Optional progress sink, 0..1 over the whole analysis. Called from the
    // WORKER thread — the installer must marshal to its own thread (the
    // ShotProcessor lambda posts a queued invoke). May be null; analyzers
    // must check before calling.
    std::function<void(float)> progress;

    // THE STOP (#294): set by the job's owner when the app quits (ReanalysisController::stopForQuit). The analysis
    // checks it before each stage starts, between pose frames and while it waits for the pose session, and ends there
    // by throwing AnalysisStopped (analysis_stage.h); nothing it computed is returned, so nothing is written. Like
    // `progress`, it lives only while the job runs. Null (a live shot, SwingLab): never stopped.
    std::shared_ptr<const std::atomic<bool>> stop;
    bool stopRequested() const { return stop && stop->load(std::memory_order_acquire); }

    // SwingLab: when set, the analyzer takes the face-on PoseTrack2D from
    // this JSON file instead of running ViTPose — synthetic-corpus injection
    // and pose-cache reuse during shaft tuning. Empty in production. The path
    // marks the injection (the run's source, path.pose "injected"); the file's
    // content rides beside it, read by whoever named the file (SwingLab), never
    // by the stage (P11, refactor step 7.2): nullopt = it could not be opened,
    // which gives an empty track, as before.
    QString poseTrackPath;
    std::optional<QJsonObject> poseTrackJson;
    // Version-gated re-analysis (analysis_versions.h): a recorded pose / ball
    // track reloaded from the swing's own analysis block when the producer that
    // wrote it is the one that would run now. Non-empty ⇒ the stage reuses it and
    // does not run the model / replay. Empty in live capture.
    pinpoint::analysis::PoseTrack2D posePreloaded;
    // The scope the reused pose was recorded with. It describes those frames, so the write-back keeps
    // it: stamping this run's scope would call a full-window pose "span" and re-pose it next time.
    QString posePreloadedScope;
    // …and the model identity it was recorded with (versions.pose.model): provenance, kept as recorded.
    // Mark's pose rule reuses a saved pose whatever model file this host has, so stamping this host's
    // identity would misname the model that made those frames. Empty = stamp this host's identity.
    QString posePreloadedModel;
    // Where this run came from and what it could reuse. The reanalyzer fills it; a live shot keeps
    // the defaults. The pipeline completes it into SwingAnalysis::path.
    pinpoint::analysis::AnalysisPath path;
    pinpoint::analysis::BallTrack2D ballPreloaded;
    // The DOWN-THE-LINE pose, the same two ways (DtlPoseStage). `poseDtlPreloaded` is a track
    // already in hand; `poseDtlTrackPath` is SwingLab's `--dtl-pose`, which pins the DTL pose so a
    // corpus pass is deterministic (pose inference is not). Precedence: preloaded, then the path,
    // then run the model. Both empty in live capture. The file's content rides beside the path, as
    // poseTrackJson does (step 7.2).
    pinpoint::analysis::PoseTrack2D poseDtlPreloaded;
    QString                         poseDtlPreloadedModel;   // its recorded model identity (as above)
    QString                         poseDtlTrackPath;
    std::optional<QJsonObject>      poseDtlTrackJson;
    // The DTL camera's pose relative to the face-on camera, MEASURED (camera_pose_sticks.h
    // on the two-camera protocol's stick clips) — dtl_continuous_track_design_update.md §4
    // item 1. The shaft fusion and the 3-D synthetic shaft read it instead of assuming a
    // level camera on the target line; `calibrated` false is that assumption. SwingLab fills
    // it from `--dtl-calib <json>`; the app has no producer yet (no protocol session exists).
    struct DtlCameraCalib {
        bool   calibrated = false;
        double yawDeg = 0.0, pitchDeg = 0.0, rollDeg = 0.0;
        double offsetM[3] = { 0.0, 0.0, 0.0 };   // DTL centre − face-on centre, fusion frame
        QString source;                          // where the record came from, for the log
    } dtlCameraCalib;
    // The recorded shaft track and the RESOLVED phase ladder, reloaded together under the
    // same gate (2026-09-17, recorded_products.h): the Shaft stage adopts the track and
    // re-synthesises only its visualisation tier, and the ladder stages (SegResolve,
    // EventRefine, PositionsLadder, TimelineFusion) hand the recorded ladder through
    // untouched. Non-empty samples ⇒ reuse; the ladder is only ever set beside it.
    pinpoint::analysis::ShaftTrack2D shaftPreloaded;
    std::optional<pinpoint::analysis::Segmentation> ladderPreloaded;

    // Face-on ball track (v3.4 design §9), resolved on the UI thread from the live
    // CameraInstance's ball accumulator or a recorded swing.json "ball" block.
    // Empty ⇒ the shaft tracker's offline ball-anchor pass falls back to replaying
    // the production ball detector over the frozen window (BallRunner::run) —
    // never a failure, just no anchor for that swing (design §9.6, additive-only).
    pinpoint::analysis::BallTrack2D ballTrack;

    // SwingLab: when set, the analyzer loads an INJECTED ground-truth BallTrack2D
    // from this JSON file instead of ballTrack/BallRunner — synthetic-fixture
    // injection, mirrors poseTrackPath (its content in ballTrackJson, the same way). Empty in production.
    QString ballTrackPath;
    std::optional<QJsonObject> ballTrackJson;

    // Hitting-area ROI (full-frame normalized) captured with the swing. When set,
    // BallRunner searches this box instead of the pose-derived stance corridor,
    // so offline ball detection matches the live detector and skips out-of-box
    // distractors (feet/shoes). Empty ⇒ fall back to the stance corridor.
    QRectF ballSearchRoi;

    // The live empty-mat baseline: from swing.json setup.ballDetection.baseline on
    // re-analysis, from the detector's learned cache (in memory) live — since step
    // 1.5 both paths (D5: a valid baseline makes the BallRunner re-run authoritative,
    // and the recorded ball stream is not passed). Empty ⇒ BallRunner self-seeds
    // over the window as before.
    pinpoint::analysis::BallBaselineRef ballBaseline;

    // SwingLab tuning overrides (docs/implementation/swinglab_impl.md):
    // "<area>.<field>" → numeric value, applied onto the config structs at
    // analysis time (e.g. "shaft.ridgeKernelPx", "assembly.coverageMin",
    // "seg.backswingMinUs"). Empty in production — the app never sets it;
    // the offline runner fills it from a params JSON so the lab can iterate
    // without rebuilds. Unknown keys are logged and ignored.
    QVariantMap tuningOverrides;

    // Skip the heavy-stage swing-span bounding (v3 G3 pose/ball scan window +
    // shaft.spanBound) and process the entire captured window instead. Live
    // capture always leaves this false (throughput matters, the padded span
    // already covers address→finish); explicit re-analysis sets it true
    // (correctness over speed — see ReanalyzeOptions::fullWindow).
    bool fullWindow = false;
};
