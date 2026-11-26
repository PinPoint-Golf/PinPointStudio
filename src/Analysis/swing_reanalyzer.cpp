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

#include "swing_reanalyzer.h"
#include "../Vocabulary/Analysis/recorded_products.h"   // shaftTrackFromAnalysisJson / segmentationFromAnalysisJson
#include "../Vocabulary/Analysis/analysis_versions.h"
#include "analysis_job_builder.h"   // the one job builder
#include "analysis_service.h"      // the run and the integrity verdicts (step 1.5)
#include "effective_context.h"
#include "reuse_gate.h"           // the one reuse gate (step 1.6)
#include "../Ball/ball_runner.h"
#include "../Pose/pose_runner.h"

#include <QJsonObject>

#include <utility>

#include "../Core/pp_debug.h"

using namespace pinpoint;

namespace pinpoint::analysis {

namespace {

// The job from a loaded Recording, through the ONE builder, and the LoadedSwing around it.
LoadedSwing jobFromRecording(LoadedRecording lr)
{
    LoadedSwing out;
    if (!lr.ok) {
        out.error = lr.error;
        return out;
    }

    // THE CLUB THE GOLFER HIT (step 1.6; refactor 6.2, R-REC-6): the effective context takes the shot's one club — the
    // board's override, else the popover's club, else the device club follow-club stored, else the captured club —
    // effectiveContext() v2. The reuse gate sees the
    // club through the input stamp (reuse_gate.h), so a corrected shot re-runs its shaft track and
    // ladder while its pose and ball are reused.
    const EffectiveContext effective = effectiveContext(lr.recording.context, lr.review);
    RunOptions run;
    run.origin = QStringLiteral("replay");   // reanalyzeLoaded says otherwise for itself
    out.job = buildAnalysisJob(lr.recording, effective, run);
    out.clubCorrected = effective.club.corrected;

    out.review                = lr.review;
    out.contextRecorded       = lr.contextRecorded;
    out.readOnly              = lr.readOnly;
    out.readOnlyReason        = lr.readOnlyReason;
    out.analysisIn            = std::move(lr.analysisIn);
    out.analysisOriginUs      = lr.analysisOriginUs;
    out.usedRaw               = lr.usedRaw;
    out.loadGates             = std::move(lr.loadGates);
    out.hostOrientationFilter = lr.recording.integrity.hostOrientationFilter;
    out.captureWindowStartUs  = lr.recording.integrity.windowStartUs;
    out.captureWindowEndUs    = lr.recording.integrity.windowEndUs;
    out.offClockCameras       = lr.recording.integrity.offClockCameras;
    // The window's OWNER moves to the caller; the window itself never moves, so the Recording's
    // pointer into it stays valid however the LoadedSwing is moved afterwards.
    out.window           = std::move(lr.window);
    out.recording        = std::move(lr.recording);
    out.recording.window = out.window.get();
    out.ok = true;
    return out;
}

} // namespace

RecordingLoadOptions SwingDiskLoader::loadOptions(const SwingLoadOptions& opts)
{
    RecordingLoadOptions ro;
    ro.faceOnSubstring = opts.faceOnSubstring;
    ro.faceOnExplicit  = opts.faceOnExplicit;
    ro.tuningOverrides = opts.tuningOverrides;
    return ro;
}

LoadedSwing SwingDiskLoader::fromRecording(LoadedRecording loaded)
{
    return jobFromRecording(std::move(loaded));
}

// Through the shot's RecordingSource (refactor step 3.4b-ii): the one reader, whose store adapter is the Recording
// loader (src/Stores/recording_loader.*, step 1.2); this builds the job from the Recording through the ONE builder.
LoadedSwing SwingDiskLoader::load(const RecordingSource& source, const SwingLoadOptions& opts)
{
    return fromRecording(source.read(loadOptions(opts)));
}

ReanalyzeResult reanalyzeShot(const RecordingSource& source, const ReanalyzeOptions& opts)
{
    // The overrides reach the loader too: the MP4 readers it builds take the decode.*
    // keys (everything else in the map is applied by the stages, below).
    if (opts.stop && opts.stop->load(std::memory_order_acquire)) {   // #294: stopped before the read
        ReanalyzeResult stopped;
        stopped.stopped = true;
        stopped.error   = QStringLiteral("stopped: the app is quitting");
        return stopped;
    }
    SwingLoadOptions lopts;
    lopts.tuningOverrides = opts.tuningOverrides;
    return reanalyzeLoaded(SwingDiskLoader::load(source, lopts), opts, source.shot().toString());
}

ReanalyzeResult reanalyzeLoaded(LoadedSwing ls, const ReanalyzeOptions& opts, const QString& label)
{
    ReanalyzeResult out;
    if (opts.stop && opts.stop->load(std::memory_order_acquire)) {   // #294: stopped during the read
        out.stopped = true;
        out.error   = QStringLiteral("stopped: the app is quitting");
        return out;
    }
    if (!ls.ok) {
        out.error = ls.error;
        return out;
    }
    out.usedRaw = ls.usedRaw;
    // A document newer than this build (its schema, or its analysisContext's): re-analysing it would
    // write an older shape over it. Read-only — refused before anything runs.
    if (ls.readOnly) {
        out.error = QStringLiteral("This shot can't be re-analysed here: %1.").arg(ls.readOnlyReason);
        return out;
    }

    if (!opts.tuningOverrides.isEmpty())
        ls.job.tuningOverrides = opts.tuningOverrides;
    if (opts.sessionTypeOverride >= 0)
        ls.job.sessionType = opts.sessionTypeOverride;   // the input stamp names it (the pipeline stamps it)
    if (!opts.poseTrackPath.isEmpty()) {
        ls.job.poseTrackPath = opts.poseTrackPath;
        ls.job.poseTrackJson = opts.poseTrackJson;     // its content, as its namer read it (step 7.2)
    }
    ls.job.fullWindow = opts.fullWindow;
    ls.job.stop       = opts.stop;   // #294: the owner's stop reaches every stage, the pose runner and its lease
    ls.job.path.origin = QStringLiteral("reanalysis");
    ls.job.path.forced = opts.forceRerun;
    // skeleton3d values held fixed: SwingLab's --skeleton-calib only (the app never pools).
    if (opts.skeletonCalib)
        ls.job.skeletonCalib = opts.skeletonCalib;
    // ── Version-gated reuse (analysis_versions.h), decided by the one gate (reuse_gate.h) ──────
    // The recorded pose is reloaded when its stage version and scan scope are the ones that would
    // run now and no pose tuning override is in play — WHATEVER POSE MODEL FILE THIS HOST HAS (Mark's
    // pose rule, 14 Nov 2026); the recorded ball track likewise, but only on top of a reused pose (it
    // was computed from that pose), and the shaft track + ladder on top of both, only when the club
    // they were computed with is this run's (the input stamp's club inputs, step 1.6). The
    // down-the-line pose is reused under the face-on pose's rule; the DTL club track always re-runs.
    // Everything downstream is recomputed.
    ReuseGateFacts facts;
    facts.versions         = ls.analysisIn[QStringLiteral("versions")].toObject();
    facts.hasBallBlock     = ls.analysisIn.contains(QStringLiteral("ball"));
    facts.hasClubBlock     = ls.analysisIn.contains(QStringLiteral("club"));
    facts.hasPoseDtlBlock  = ls.analysisIn.contains(QStringLiteral("poseDtl"));
    facts.forceRerun       = opts.forceRerun;
    facts.tuningOverrides  = ls.job.tuningOverrides;
    facts.fullWindow       = ls.job.fullWindow;
    facts.hasFaceOnCamera  = !ls.job.cameraSources.empty();
    facts.hasDtlCamera     = ls.job.dtlSource != kInvalidSourceId;
    // The stamp this run writes: the builder's, with the session type this run uses (the pipeline
    // stamps it the same way).
    facts.inputsNow             = ls.job.inputs;
    facts.inputsNow.sessionType = ls.job.sessionType;
    facts.clubCorrectedNow      = ls.clubCorrected;
    const ReuseGate gate(facts);

    if (gate.mayReloadPose()) {
        ls.job.posePreloaded = PoseRunner::fromJsonObject(ls.analysisIn, ls.job.cameraSources.front(),
                                                          ls.analysisOriginUs);
        if (ls.job.posePreloaded.frames.empty()) {
            ls.job.posePreloaded = {};
        } else {
            ls.job.posePreloadedScope = gate.recordedPoseScope();
            ls.job.posePreloadedModel = gate.recordedPoseModel();
        }
    }
    // The DOWN-THE-LINE pose, independent of whether the face-on pose was reused (the two are
    // separate inference passes). fromJsonObject reads a `pose2d` member (and its decode/cropRect
    // provenance), so hand it the DTL block under that name.
    if (gate.mayReloadPoseDtl()) {
        ls.job.poseDtlPreloaded = PoseRunner::fromJsonObject(
            QJsonObject{ { QStringLiteral("pose2d"), ls.analysisIn[QStringLiteral("poseDtl")] } },
            ls.job.dtlSource, ls.analysisOriginUs);
        if (ls.job.poseDtlPreloaded.frames.empty()) ls.job.poseDtlPreloaded = {};
        else ls.job.poseDtlPreloadedModel = gate.recordedPoseDtlModel();
    }
    // The handedness the reused pose was labelled with leads with the other hand now: relabel its
    // lead/trail hands (the keypoints do not depend on it), never re-infer (Mark's pose rule).
    if (gate.poseLeadSwapped()) {
        for (PoseTrack2D *t : { &ls.job.posePreloaded, &ls.job.poseDtlPreloaded })
            for (PoseFrame2D &f : t->frames)
                std::swap(f.leadHand, f.trailHand);
    }
    if (gate.mayReloadBall(!ls.job.posePreloaded.frames.empty())) {
        ls.job.ballPreloaded = BallRunner::fromAnalysisJson(ls.analysisIn[QStringLiteral("ball")].toObject(),
                                                            ls.job.cameraSources.front(),
                                                            ls.analysisOriginUs);
        if (ls.job.ballPreloaded.frames.empty()) ls.job.ballPreloaded = {};
    }
    // ── The shaft track and the resolved ladder (2026-09-17) ─────────────────
    // Production re-analysis (the app, --write-back) sets no tuning override, which is exactly the
    // metrics-only case this exists for: the tracker must not run again on the swing's mp4 and
    // replace a good live track with a coasting one. A corrected club re-runs it (step 1.6).
    if (gate.mayReloadShaft(!ls.job.ballPreloaded.frames.empty())) {
        ls.job.shaftPreloaded = shaftTrackFromAnalysisJson(
            ls.analysisIn[QStringLiteral("club")].toObject(), ls.job.cameraSources.front(),
            ls.analysisOriginUs);
        if (!ls.job.shaftPreloaded.valid || ls.job.shaftPreloaded.samples.empty()) {
            ls.job.shaftPreloaded = {};
        } else {
            ls.job.ladderPreloaded = segmentationFromAnalysisJson(ls.analysisIn, ls.analysisOriginUs);
            if (!ls.job.ladderPreloaded || ls.job.ladderPreloaded->events.empty()) {
                // A track without its ladder is not reusable: the ladder was refined FROM this
                // track, and re-deriving it would run the very stages the reuse exists to hold
                // still. Re-run the lot.
                ls.job.shaftPreloaded  = {};
                ls.job.ladderPreloaded = std::nullopt;
            }
        }
    }
    // The same answers for the document, each with its first reason (analysis.path.reuse).
    ReuseReloaded reloaded;
    reloaded.pose    = !ls.job.posePreloaded.frames.empty();
    reloaded.poseDtl = !ls.job.poseDtlPreloaded.frames.empty();
    reloaded.ball    = !ls.job.ballPreloaded.frames.empty();
    reloaded.shaft   = !ls.job.shaftPreloaded.samples.empty();
    reloaded.ladder  = ls.job.ladderPreloaded.has_value();
    ls.job.path.reuse = gate.reasons(reloaded);
    {
        const QString stale = analysisInputsStaleness(facts.versions, facts.inputsNow);
        ppInfo() << "[Reanalysis]" << label << "inputs:"
                 << (stale.isEmpty() ? QStringLiteral("current") : QStringLiteral("stale (%1)").arg(stale))
                 << "club" << (facts.inputsNow.club.isEmpty() ? QStringLiteral("(undeclared)") : facts.inputsNow.club)
                 << (ls.clubCorrected ? "(corrected)" : "");
    }
    if (!opts.forceRerun)
        ppInfo() << "[Reanalysis]" << label << "reuse: pose"
                 << (reloaded.pose ? "recorded" : "re-run") << "ball"
                 << (reloaded.ball ? "recorded" : "re-run") << "shaft"
                 << (reloaded.shaft ? "recorded (synth refreshed)" : "re-run")
                 << "ladder" << (reloaded.ladder ? "recorded" : "re-run")
                 << "dtl pose" << (ls.job.dtlSource == kInvalidSourceId ? "n/a"
                                   : reloaded.poseDtl ? "recorded" : "re-run");
    if (ls.job.impactUs <= 0) {
        out.error = QStringLiteral("This shot has no recorded impact time, so it can't be re-analysed.");
        return out;
    }
    // Analysis needs at least ONE usable modality, whatever the session type (issue #111).
    // Without IMU A/M bindings the fuser produces nothing (wrist metrics can't be
    // re-derived), but a face-on camera still yields a valid vision-only analysis (pose +
    // shaft track + hands-only phase landmarks). Refuse only when NEITHER is present — that
    // path would return a degenerate result the caller would write back over the original.
    if (ls.job.imuBindings.empty() && ls.job.faceOnCameraCount == 0) {
        out.error = QStringLiteral("No motion sensor calibration and no face-on camera were saved with this "
                                   "shot, so it can't be re-analysed.");
        return out;
    }

    // THE ANALYSIS SERVICE: the run, and the Recording's two integrity verdicts by the one rule
    // (integrityFor: the IMU re-fusion only when the recording says Madgwick; the capture check
    // against the recorded window, the impact camera and off-clock lanes exempt) — the very code the
    // live capture runs (step 1.5). A FRESH verdict for this pass, never the one capture reached.
    AnalysedRecording run = AnalysisService().analyseJob(ls.recording, ls.job);
    if (run.stopped) {   // nothing of a stopped run is returned (no analysis, no verdict)
        out.stopped = true;
        out.error   = run.analysis.error;
        return out;
    }
    out.analysis = std::move(run.analysis);
    out.ok       = out.analysis.ok;
    out.error    = out.analysis.error;
    // The loader's own gate records (a HackMotion lane it could not bind) join the stages'.
    if (out.analysis.detail && !ls.loadGates.empty())
        out.analysis.detail->gates.insert(out.analysis.detail->gates.end(),
                                          ls.loadGates.begin(), ls.loadGates.end());
    out.imuIntegrity     = std::move(run.integrity.imu);
    out.captureIntegrity = std::move(run.integrity.capture);

    return out;
}

} // namespace pinpoint::analysis
