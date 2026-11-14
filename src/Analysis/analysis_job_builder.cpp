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

#include "analysis_job_builder.h"

namespace pinpoint::analysis {

namespace {

namespace rec = pinpoint::recording;

// Camera roles from the one perspective mapping. The order rule is the one both old builders
// applied: every face-on stream is inserted at the FRONT (so the last face-on listed leads), the
// rest are appended; the last Impact stream is the impact camera; the FIRST DownTheLine stream is
// the down-the-line camera, and the pick never moves once made.
void applyCameras(const rec::RecordingSources &sources, ShotAnalysisJob &job)
{
    for (const rec::CameraStream &c : sources.cameras) {
        if (c.perspective == rec::Perspective::FaceOn) {
            job.cameraSources.insert(job.cameraSources.begin(), c.source);
            ++job.faceOnCameraCount;
            if (!c.frames.isEmpty())
                job.path.framesFaceOn = c.frames;     // the last one, as it becomes the front
        } else {
            job.cameraSources.push_back(c.source);
        }
        if (c.perspective == rec::Perspective::Impact)
            job.impactSource = c.source;
        if (c.perspective == rec::Perspective::DownTheLine && job.dtlSource == pinpoint::kInvalidSourceId) {
            job.dtlSource = c.source;
            if (!c.frames.isEmpty())
                job.path.framesDtl = c.frames;
        }
    }
    job.imuSources = sources.imuLanes;
}

// The club the analysis uses — the EFFECTIVE one. A recorded length makes it known; otherwise the
// job keeps its driver default and carries the standard length for the stages that read it.
void applyClub(const EffectiveClub &club, ShotAnalysisJob &job)
{
    job.clubName = club.name;
    const rec::ClubGeometry &g = club.geometry;
    if (g.lengthKnown && g.lengthMm > 0.0) {
        job.clubLengthM     = g.lengthMm / 1000.0;
        job.clubLengthKnown = true;
    } else if (club.standardLengthMm > 0.0) {
        job.standardClubLengthM = club.standardLengthMm / 1000.0;
    }
    job.bandCentersMm   = g.bandCentersMm;
    job.shaftType       = g.shaftType;
    job.hoselFromButtMm = g.hoselFromButtMm;
    job.shaftLengthMm   = g.shaftLengthMm;
    job.handsEndMm      = g.handsEndMm;
    if (club.lengthPrior.present()) {
        job.priorClubLenPx    = club.lengthPrior.px;
        job.priorClubLenVarPx = club.lengthPrior.varPx;
        job.priorClubLenN     = club.lengthPrior.n;
    }
}

// The ball inputs, rebased onto the window's clock. D5: a valid empty-mat baseline makes the
// BallRunner re-run authoritative, so the recorded stream is not handed over.
void applyBall(const rec::BallInputs &ball, int64_t t0Us, ShotAnalysisJob &job)
{
    job.ballSearchRoi = ball.searchRoi;
    job.ballBaseline  = ball.baseline;
    if (ball.baseline.isValid() || ball.track.frames.empty())
        return;
    job.ballTrack = ball.track;
    for (BallSample2D &s : job.ballTrack.frames)
        s.t_us += t0Us;
    if (job.ballTrack.launchTUs >= 0)
        job.ballTrack.launchTUs += t0Us;
}

void applyRunOptions(const RunOptions &o, ShotAnalysisJob &job)
{
    job.path.origin = o.origin;
    job.path.forced = o.forceRerun;
    job.path.reuse  = o.reuse;
    job.fullWindow  = o.fullWindow;
    if (o.sessionTypeOverride >= 0)
        job.sessionType = o.sessionTypeOverride;
    job.tuningOverrides    = o.tuningOverrides;
    job.poseTrackPath      = o.poseTrackPath;
    job.poseDtlTrackPath   = o.poseDtlTrackPath;
    job.ballTrackPath      = o.ballTrackPath;
    job.skeletonCalib      = o.skeletonCalib;
    job.dtlCameraCalib     = o.dtlCameraCalib;
    job.progress           = o.progress;
    job.posePreloaded      = o.posePreloaded;
    job.posePreloadedScope = o.posePreloadedScope;
    job.poseDtlPreloaded   = o.poseDtlPreloaded;
    job.ballPreloaded      = o.ballPreloaded;
    job.shaftPreloaded     = o.shaftPreloaded;
    job.ladderPreloaded    = o.ladderPreloaded;
}

} // namespace

ShotAnalysisJob buildAnalysisJob(const pinpoint::recording::Recording &recording,
                                 const EffectiveContext &effective,
                                 const RunOptions &options)
{
    ShotAnalysisJob job;

    const rec::CaptureFacts &cap = recording.capture;
    job.sessionType          = cap.sessionType;
    job.shotSource           = cap.shotSource;
    job.impactUs             = cap.impactUs >= 0 ? recording.t0Us + cap.impactUs : -1;
    job.motionCaptureQuality = cap.motionCaptureQuality;

    applyCameras(recording.sources, job);
    job.imuBindings = recording.imuBindings;

    job.handedness     = static_cast<int>(effective.handedness);
    job.athleteHeightM = effective.heightM;
    applyClub(effective.club, job);

    applyBall(recording.ball, recording.t0Us, job);
    applyRunOptions(options, job);
    return job;
}

} // namespace pinpoint::analysis
