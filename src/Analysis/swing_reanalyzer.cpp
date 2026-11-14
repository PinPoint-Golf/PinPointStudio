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
#include "recorded_products.h"   // shaftTrackFromAnalysisJson / segmentationFromAnalysisJson
#include "analysis_versions.h"
#include "analysis_job_builder.h"   // the one job builder
#include "effective_context.h"
#include "../Export/recording_loader.h"   // the swing folder → the Recording (step 1.2)
#include "../Ball/ball_runner.h"
#include "../Pose/pose_runner.h"

#include <QJsonObject>

#include <exception>
#include <utility>

#include "../Core/pp_debug.h"

using namespace pinpoint;

namespace pinpoint::analysis {

// The Recording loader moved to the store (src/Export/recording_loader.*, step 1.2). What is left
// here builds the job from the Recording through the ONE builder, so re-analysis and SwingLab
// already run on buildAnalysisJob. Step 1.5 replaces this adapter with the analysis service.
LoadedSwing SwingDiskLoader::load(const QString& swingDir, const SwingLoadOptions& opts)
{
    LoadedSwing out;

    RecordingLoadOptions ro;
    ro.faceOnSubstring = opts.faceOnSubstring;
    ro.faceOnExplicit  = opts.faceOnExplicit;
    ro.tuningOverrides = opts.tuningOverrides;
    LoadedRecording lr = loadRecording(swingDir, ro);
    if (!lr.ok) {
        out.error = lr.error;
        return out;
    }

    // THE CLUB, AS TODAY'S RE-ANALYSIS USES IT: the declared club (review.club) sets the standard
    // length; the board's override is NOT applied yet. It changes the shaft, ladder and DTL club
    // inputs, which the reuse gate must then see (the input stamp, step 1.6) or a corrected shot
    // would reuse its old shaft track; the override is applied with that gate (1.5 / 1.6). The
    // loader hands both over (out.review) so nothing is lost.
    ClubReview asToday;
    asToday.declared = lr.review.declared;
    RunOptions run;
    run.origin = QStringLiteral("replay");   // reanalyzeSwingDir says otherwise for itself
    out.job = buildAnalysisJob(lr.recording, effectiveContext(lr.recording.context, asToday), run);

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
    // The Recording pointed into the loader's window; it is not used past this point, so the
    // window moves out to the caller.
    out.window.emplace(std::move(*lr.window));
    out.ok = true;
    return out;
}

ReanalyzeResult reanalyzeSwingDir(const QString& swingDir, const ReanalyzeOptions& opts)
{
    ReanalyzeResult out;

    // The overrides reach the loader too: the MP4 readers it builds take the decode.*
    // keys (everything else in the map is applied by the stages, below).
    SwingLoadOptions lopts;
    lopts.tuningOverrides = opts.tuningOverrides;
    LoadedSwing ls = SwingDiskLoader::load(swingDir, lopts);
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
        ls.job.sessionType = opts.sessionTypeOverride;
    if (!opts.poseTrackPath.isEmpty())
        ls.job.poseTrackPath = opts.poseTrackPath;
    ls.job.fullWindow = opts.fullWindow;
    ls.job.path.origin = QStringLiteral("reanalysis");
    ls.job.path.forced = opts.forceRerun;
    for (const char *k : { "pose", "ball", "shaft", "ladder", "poseDtl" })
        ls.job.path.reuse.insert(QString::fromLatin1(k), opts.forceRerun ? QStringLiteral("forced") : QString());
    // skeleton3d values held fixed: SwingLab's --skeleton-calib only (the app never pools).
    if (opts.skeletonCalib)
        ls.job.skeletonCalib = opts.skeletonCalib;
    // ── Version-gated reuse (analysis_versions.h) ────────────────────────────
    // The recorded pose is reloaded when its producer (model file, stage version,
    // scan scope) is the one that would run now and no pose tuning override is in
    // play; the recorded ball track likewise, but only on top of a reused pose
    // (it was computed from that pose), and the shaft track + ladder on top of both
    // (see below). The down-the-line pose is reused under the face-on pose's rule;
    // the DTL club track always re-runs. Everything downstream is recomputed.
    if (!opts.forceRerun) {
        const QJsonObject ver  = ls.analysisIn[QStringLiteral("versions")].toObject();
        const QJsonObject vp   = ver[QStringLiteral("pose")].toObject();
        const QJsonObject vb   = ver[QStringLiteral("ball")].toObject();
        const QString wantScope = ls.job.fullWindow ? QStringLiteral("full") : QStringLiteral("span");
        bool poseOverride = false;
        for (auto it = ls.job.tuningOverrides.cbegin(); it != ls.job.tuningOverrides.cend(); ++it)
            if (it.key().startsWith(QLatin1String("pose.")) || it.key().startsWith(QLatin1String("shaft.addressScan"))
                || it.key().startsWith(QLatin1String("shaft.addressStride")) || it.key().startsWith(QLatin1String("ball.")))
                poseOverride = true;
        const bool poseMatch = !poseOverride && !vp.isEmpty()
            && vp[QStringLiteral("code")].toInt() == kPoseStageVersion
            && vp[QStringLiteral("model")].toString() == PoseRunner::modelIdentity(ls.job.motionCaptureQuality)
            // a full-window pose covers a span request; a span pose covers only a span request
            && (vp[QStringLiteral("scope")].toString() == QLatin1String("full")
                || vp[QStringLiteral("scope")].toString() == wantScope)
            && !ls.job.cameraSources.empty();
        if (poseMatch) {
            ls.job.posePreloaded = PoseRunner::fromJsonObject(ls.analysisIn, ls.job.cameraSources.front(),
                                                              ls.analysisOriginUs);
            if (ls.job.posePreloaded.frames.empty()) ls.job.posePreloaded = {};
            else ls.job.posePreloadedScope = vp[QStringLiteral("scope")].toString();
        }
        // The DOWN-THE-LINE pose, under the face-on pose's rule: its own stage version, the
        // same model identity, no pose./ball./address override — and a DTL camera to put it
        // on. Independent of whether the face-on pose was reused (the two are separate
        // inference passes). The DTL club track is never reused: it is recomputed from the
        // (reused) poses and face-on shaft every time.
        const QJsonObject vpd = ver[QStringLiteral("poseDtl")].toObject();
        const bool poseDtlMatch = !poseOverride && !vpd.isEmpty()
            && vpd[QStringLiteral("code")].toInt() == kDtlPoseStageVersion
            && vpd[QStringLiteral("model")].toString() == PoseRunner::modelIdentity(ls.job.motionCaptureQuality)
            && ls.analysisIn.contains(QStringLiteral("poseDtl"))
            && ls.job.dtlSource != kInvalidSourceId;
        if (poseDtlMatch) {
            // fromJsonObject reads a `pose2d` member (and its decode/cropRect provenance), so
            // hand it the DTL block under that name.
            ls.job.poseDtlPreloaded = PoseRunner::fromJsonObject(
                QJsonObject{ { QStringLiteral("pose2d"), ls.analysisIn[QStringLiteral("poseDtl")] } },
                ls.job.dtlSource, ls.analysisOriginUs);
            if (ls.job.poseDtlPreloaded.frames.empty()) ls.job.poseDtlPreloaded = {};
        }
        const bool ballMatch = !ls.job.posePreloaded.frames.empty() && !vb.isEmpty()
            && vb[QStringLiteral("code")].toInt() == kBallStageVersion
            && ls.analysisIn.contains(QStringLiteral("ball"));
        if (ballMatch) {
            ls.job.ballPreloaded = BallRunner::fromAnalysisJson(ls.analysisIn[QStringLiteral("ball")].toObject(),
                                                                ls.job.cameraSources.front(),
                                                                ls.analysisOriginUs);
            if (ls.job.ballPreloaded.frames.empty()) ls.job.ballPreloaded = {};
        }
        // ── The shaft track and the resolved ladder (2026-09-17) ─────────────────
        // Reused only on top of a reused pose AND ball (the track was computed from
        // them), when the tracker that wrote it is the one that would run now, and
        // when NO tuning override is in play at all — the ladder stages that would
        // otherwise re-run (EventRefine, PositionsLadder, TimelineFusion) each have
        // their own knobs, and a sweep that sets any knob wants everything re-run.
        // Production re-analysis (the app, --write-back) sets none, which is exactly
        // the metrics-only case this exists for: the tracker must not run again on
        // the swing's mp4 and replace a good live track with a coasting one.
        const QJsonObject vs = ver[QStringLiteral("shaft")].toObject();
        const bool shaftMatch = ballMatch && !ls.job.ballPreloaded.frames.empty()
            && ls.job.tuningOverrides.isEmpty() && !vs.isEmpty()
            && vs[QStringLiteral("code")].toInt() == kShaftStageVersion
            && ls.analysisIn.contains(QStringLiteral("club"));
        if (shaftMatch) {
            ls.job.shaftPreloaded = shaftTrackFromAnalysisJson(
                ls.analysisIn[QStringLiteral("club")].toObject(), ls.job.cameraSources.front(),
                ls.analysisOriginUs);
            if (!ls.job.shaftPreloaded.valid || ls.job.shaftPreloaded.samples.empty()) {
                ls.job.shaftPreloaded = {};
            } else {
                ls.job.ladderPreloaded = segmentationFromAnalysisJson(ls.analysisIn, ls.analysisOriginUs);
                if (!ls.job.ladderPreloaded || ls.job.ladderPreloaded->events.empty()) {
                    // A track without its ladder is not reusable: the ladder was refined
                    // FROM this track, and re-deriving it would run the very stages the
                    // reuse exists to hold still. Re-run the lot.
                    ls.job.shaftPreloaded  = {};
                    ls.job.ladderPreloaded = std::nullopt;
                }
            }
        }
        // The same answers for the document, each with its first reason (analysis.path.reuse).
        const auto version = [](const QJsonObject &v, int want) {
            return QStringLiteral("version %1, want %2").arg(v[QStringLiteral("code")].toInt()).arg(want);
        };
        const auto scopeOf = [&vp] { return vp[QStringLiteral("scope")].toString(); };
        const bool anyOverride = !ls.job.tuningOverrides.isEmpty();
        QMap<QString, QString> &reuse = ls.job.path.reuse;
        const QString reused = QStringLiteral("reused");
        const QString noRecord = QStringLiteral("no record");
        reuse[QStringLiteral("pose")] =
              !ls.job.posePreloaded.frames.empty() ? reused
            : poseOverride ? QStringLiteral("tuning override")
            : vp.isEmpty() ? noRecord
            : vp[QStringLiteral("code")].toInt() != kPoseStageVersion ? version(vp, kPoseStageVersion)
            : vp[QStringLiteral("model")].toString() != PoseRunner::modelIdentity(ls.job.motionCaptureQuality) ? QStringLiteral("model changed")
            : (scopeOf() != QLatin1String("full") && scopeOf() != wantScope) ? QStringLiteral("scope %1, want %2").arg(scopeOf(), wantScope)
            : ls.job.cameraSources.empty() ? QStringLiteral("no face-on camera")
                                           : QStringLiteral("recorded track is empty");
        reuse[QStringLiteral("ball")] =
              !ls.job.ballPreloaded.frames.empty() ? reused
            : ls.job.posePreloaded.frames.empty() ? QStringLiteral("needs a reloaded pose")
            : vb.isEmpty() || !ls.analysisIn.contains(QStringLiteral("ball")) ? noRecord
            : vb[QStringLiteral("code")].toInt() != kBallStageVersion ? version(vb, kBallStageVersion)
                                                                     : QStringLiteral("recorded track is empty");
        reuse[QStringLiteral("shaft")] =
              !ls.job.shaftPreloaded.samples.empty() ? reused
            : ls.job.ballPreloaded.frames.empty() ? QStringLiteral("needs a reloaded ball")
            : anyOverride ? QStringLiteral("tuning override")
            : vs.isEmpty() || !ls.analysisIn.contains(QStringLiteral("club")) ? noRecord
            : vs[QStringLiteral("code")].toInt() != kShaftStageVersion ? version(vs, kShaftStageVersion)
            : shaftMatch ? QStringLiteral("no usable track, or no ladder with it")
                         : QStringLiteral("not reusable");
        reuse[QStringLiteral("ladder")] = ls.job.ladderPreloaded ? reused : reuse[QStringLiteral("shaft")];
        reuse[QStringLiteral("poseDtl")] =
              !ls.job.poseDtlPreloaded.frames.empty() ? reused
            : ls.job.dtlSource == kInvalidSourceId ? QStringLiteral("no down-the-line camera")
            : poseOverride ? QStringLiteral("tuning override")
            : vpd.isEmpty() || !ls.analysisIn.contains(QStringLiteral("poseDtl")) ? noRecord
            : vpd[QStringLiteral("code")].toInt() != kDtlPoseStageVersion ? version(vpd, kDtlPoseStageVersion)
            : vpd[QStringLiteral("model")].toString() != PoseRunner::modelIdentity(ls.job.motionCaptureQuality) ? QStringLiteral("model changed")
                                                                                                                : QStringLiteral("recorded track is empty");
        ppInfo() << "[Reanalysis]" << swingDir << "reuse: pose"
                 << (ls.job.posePreloaded.frames.empty() ? "re-run" : "recorded") << "ball"
                 << (ls.job.ballPreloaded.frames.empty() ? "re-run" : "recorded") << "shaft"
                 << (ls.job.shaftPreloaded.samples.empty() ? "re-run" : "recorded (synth refreshed)")
                 << "ladder" << (ls.job.ladderPreloaded ? "recorded" : "re-run")
                 << "dtl pose" << (ls.job.dtlSource == kInvalidSourceId ? "n/a"
                                   : ls.job.poseDtlPreloaded.frames.empty() ? "re-run" : "recorded");
    }
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

    try {
        auto analyzer = makeShotAnalyzer();
        out.analysis  = analyzer->analyze(*ls.window, ls.job);
        out.ok        = out.analysis.ok;
        out.error     = out.analysis.error;
        // The loader's own gate records (a HackMotion lane it could not bind) join the stages'.
        if (out.analysis.detail && !ls.loadGates.empty())
            out.analysis.detail->gates.insert(out.analysis.detail->gates.end(),
                                              ls.loadGates.begin(), ls.loadGates.end());
    } catch (const std::exception& e) {
        out.error = QStringLiteral("analyzer threw: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        out.error = QStringLiteral("analyzer threw an unknown exception");
    }

    // ── A FRESH DATA-INTEGRITY VERDICT, NOT THE ONE CAPTURE REACHED ─────────────
    //
    // The window is still alive here and its IMU payloads are pure RAM
    // (RamPayloadSource), so this is one Madgwick pass per host-fused lane and no
    // disk IO. It runs after analyze() only because there is nothing to gain by
    // running it earlier; it does not depend on the analysis.
    //
    // ⚠ ONLY WHEN THE RECORDING SAYS MADGWICK. Parity is a claim about whether the
    // stored quaternion is reproducible from the stored vectors, which is a
    // question one can only ask of a Madgwick host fusion — the sole filter that
    // warm-starts exactly. Under ESKF, or when the recording does not say, we leave
    // this nullopt and the write-back REMOVES the block: no claim beats a fabricated
    // one. A wG3-only swing reaches checkImuRefusion and comes back with
    // sourcesChecked == 0, which applyImuIntegrity also treats as no claim.
    if (ls.window
        && ls.hostOrientationFilter.compare(QStringLiteral("madgwick"), Qt::CaseInsensitive) == 0)
        out.imuIntegrity = pinpoint::checkImuRefusion(*ls.window);

    // Capture data-integrity (frame-timestamp holes in the camera lanes). The
    // loader's window is window-relative and so is ls.job.impactUs — the same
    // domain contract the analyzer follows, so the pre/post-impact split is exact.
    // The lanes' coverage is judged against the RECORDED window (the document's
    // `window` block), not the loaded one, which spans only the frames that exist;
    // the impact camera's band and phone clips (their own clock) are exempt.
    if (ls.window) {
        pinpoint::CaptureCoverageSpec cov;
        cov.judge         = true;
        cov.windowStartUs = ls.captureWindowStartUs;
        cov.windowEndUs   = ls.captureWindowEndUs;
        std::vector<SourceId> exempt = ls.offClockCameras;
        exempt.push_back(ls.job.impactSource);
        out.captureIntegrity = pinpoint::checkCaptureIntegrity(*ls.window, ls.job.impactUs, cov, exempt);
    }

    return out;
}

} // namespace pinpoint::analysis
