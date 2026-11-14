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

// THE ONE JOB BUILDER (docs/developer/information_model.md §2: "One builder makes the analysis job
// from the Recording and its effective context, for live capture and re-analysis alike").
//
//   buildAnalysisJob(recording, effective, options) → ShotAnalysisJob
//
// Every field of the job comes from one of the three inputs and from nothing else — no athlete, no
// session, no settings, no review block, no file. So the same Recording and the same effective
// context give the same job whether the shot was captured a second ago or a year ago; only the
// run options (origin, reuse, SwingLab's knobs) differ between a live run and a re-analysis.
//
// THE RULES IT APPLIES
//   - Camera roles from the one perspective mapping: face-on streams first (each inserted at the
//     front, as both old builders did), faceOnCameraCount = their number; the impact camera is the
//     last Impact stream; the down-the-line camera is the FIRST DownTheLine stream.
//   - Times: the Recording is window-relative; the job is in the window's own clock, so t0Us is
//     added to the impact and the ball stream (the stages assume the window's clock).
//   - The club from the EFFECTIVE context: its recorded length makes clubLengthKnown; otherwise the
//     driver default stays and the standard length (if any) is handed to the stages that read it.
//   - The ball (D5, Mark 14 Nov 2026): today's re-analysis rule on every path — a valid empty-mat
//     baseline makes the BallRunner re-run authoritative, so the recorded stream is not passed.
//   - markerSourceId is never set (read nowhere).
//
// NOT YET CALLED BY THE APP. Step 1.5 switches the two old builders' callers onto it and deletes
// them: ShotProcessor::captureWindowAndLaunch (live; ShotProcessor::buildAnalysisJob goes), and the
// callers of SwingDiskLoader::load — reanalyzeSwingDir (ReanalysisController) and swinglab_run —
// through the analysis service (the job half of the loader goes; its Recording half moves to the
// store in 1.2).
//
// Rules ring: pure, no I/O, linkable without ONNX, OpenCV video or Qt Multimedia.

#include <QMap>
#include <QString>
#include <QVariantMap>
#include <functional>
#include <optional>

#include "shot_analysis_job.h"
#include "effective_context.h"
#include "../Recording/recording.h"

namespace pinpoint::analysis {

// What a run adds that is not a fact about the swing. Nothing from the athlete or the settings.
struct RunOptions {
    QString origin;                         // AnalysisPath::origin: "live" | "reanalysis" | "replay"
    bool    forceRerun = false;             // AnalysisPath::forced (SwingLab --force-rerun)
    bool    fullWindow = false;             // skip the swing-span bound (explicit re-analysis)
    int     sessionTypeOverride = -1;       // SwingLab; -1 = the recorded session type
    QVariantMap tuningOverrides;            // SwingLab "<area>.<field>" → number; empty in production
    // Injected tracks (SwingLab / tests): a pose / DTL pose / ball JSON instead of running the model.
    QString poseTrackPath;
    QString poseDtlTrackPath;
    QString ballTrackPath;
    std::optional<pinpoint::skeleton3d::SkeletonCalib> skeletonCalib;   // SwingLab --skeleton-calib
    ShotAnalysisJob::DtlCameraCalib dtlCameraCalib;                       // SwingLab --dtl-calib
    std::function<void(float)> progress;    // called from the worker; may be null

    // The reuse gate's products (version-gated re-analysis, analysis_versions.h) and its reasons.
    PoseTrack2D                 posePreloaded;
    QString                     posePreloadedScope;
    PoseTrack2D                 poseDtlPreloaded;
    BallTrack2D                 ballPreloaded;
    ShaftTrack2D                shaftPreloaded;
    std::optional<Segmentation> ladderPreloaded;
    QMap<QString, QString>      reuse;      // AnalysisPath::reuse
};

ShotAnalysisJob buildAnalysisJob(const pinpoint::recording::Recording &recording,
                                 const EffectiveContext &effective,
                                 const RunOptions &options);

} // namespace pinpoint::analysis
