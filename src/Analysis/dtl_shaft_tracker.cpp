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

// DtlShaftTracker — the SwingWindow layer (see dtl_shaft_tracker.h). Mirrors
// ShaftTracker::track's input building exactly (coverage entries inside the pose
// span, median-interval timebase, buildFrameCache, lerpPoseFrame per frame) and
// then hands the anchors to dtlSolve.
//
// The snap and the tier ladder live in dtl_shaft_post, not here: this class is
// the SwingWindow layer and nothing more, and the deciding halves stay testable
// over plain vectors. What is load-bearing at this level is the honesty rule —
// a frame in an end-on or quarantined span publishes NOTHING, and
// publishedInEndOn is counted rather than asserted, because a pin you assert is
// a pin you stopped measuring.

#include "dtl_shaft_tracker.h"

#include <opencv2/core.hpp>

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>
#include <vector>

#include "dtl_shaft_config.h"
#include "dtl_shaft_decide.h"
#include "dtl_shaft_post.h"
#include "format_descriptor.h"
#include "shaft_frame_io.h"     // lerpPoseFrame / buildFrameCache (shared with the face-on tracker)
#include "shot_analyzer.h"      // ShotAnalysisJob
#include "swing_window.h"
#include "../Core/pp_debug.h"

namespace pinpoint::analysis {
namespace {

// COCO joints for the body ROI: shoulders, hips, knees, ankles — the same eight
// the face-on tracker derives (prep_swing.py), so the two views describe the
// golfer with one skeleton.
constexpr int kBodyJoints[8] = {5, 6, 11, 12, 13, 14, 15, 16};
constexpr float kConfMin = 0.30f;   // codebase-wide keypoint confidence gate

inline double nan_() { return std::numeric_limits<double>::quiet_NaN(); }

// A keypoint in pixels, or NaN when the pose had no confident opinion. NaN is
// the datum here, not a hole: D2 skips a forearm it cannot see rather than
// vetoing along a guessed one.
inline cv::Point2d kpPx(const PoseFrame2D& pf, int j, int w, int h)
{
    if (pf.conf[size_t(j)] <= kConfMin) return { nan_(), nan_() };
    return { pf.kp[size_t(j)].x() * w, pf.kp[size_t(j)].y() * h };
}

} // namespace

DtlShaftTrack2D DtlShaftTracker::track(const pinpoint::SwingWindow& window,
                                       const PoseTrack2D& dtlPose,
                                       const FaceOnWitness* witness,
                                       const ShotAnalysisJob& job,
                                       DtlDecideTrace* traceIn)
{
    QElapsedTimer wall;
    wall.start();
    DtlShaftTrack2D out;
    out.camera = dtlPose.camera;

    const DtlShaftConfig cfg = DtlShaftConfig::fromOverrides(job.tuningOverrides);
    if (!cfg.enabled) { ppInfo() << "[DtlShaftTracker] disabled (shaft.dtl.enabled=0)"; return out; }
    if (dtlPose.frames.size() < 2) {
        ppWarn() << "[DtlShaftTracker] no usable DTL pose track — invalid";
        return out;
    }
    if (!cfg.truthOnly && !witness) {
        // Not a failure — it is the truth-only configuration arrived at by
        // accident, and the two must never be confused (§6).
        ppWarn() << "[DtlShaftTracker] no face-on witness and truthOnly is off — invalid";
        return out;
    }

    const pinpoint::FormatDescriptor& fd = window.formatOf(dtlPose.camera);
    const auto* cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt || cfmt->width == 0 || cfmt->height == 0) {
        ppWarn() << "[DtlShaftTracker] source" << dtlPose.camera << "has no camera format — invalid";
        return out;
    }
    const int w = int(cfmt->width), h = int(cfmt->height);
    out.frameWidth  = w;
    out.frameHeight = h;
    // §4.5 / Stage 0: the two streams share the host clock and the measured
    // inter-camera offset is applied as ZERO. The DTL frames sit ~3.2 ms out of
    // phase with the face-on ones and FaceOnWitness::at() interpolates across
    // that; a constant shift on top would be a second correction for one effect.
    out.clockOffsetUs = 0;

    // camera frames inside pose coverage — the face-on rule, unchanged
    const std::vector<pinpoint::IndexEntry> all = window.entriesFor(dtlPose.camera);
    const int64_t tLo = dtlPose.frames.front().t_us, tHi = dtlPose.frames.back().t_us;
    std::vector<pinpoint::IndexEntry> cov;
    for (const auto& e : all) if (e.timestamp_us >= tLo && e.timestamp_us <= tHi) cov.push_back(e);
    const int nf = int(cov.size());
    if (nf < 2) { ppWarn() << "[DtlShaftTracker] < 2 DTL frames inside pose coverage — invalid"; return out; }

    // timebase: median inter-frame interval (NOT container fps)
    std::vector<int64_t> diffs;
    for (int i = 1; i < nf; ++i) diffs.push_back(cov[size_t(i)].timestamp_us - cov[size_t(i - 1)].timestamp_us);
    std::nth_element(diffs.begin(), diffs.begin() + diffs.size() / 2, diffs.end());
    const double dtUs = double(diffs[diffs.size() / 2]);
    const double fps = dtUs > 0 ? 1e6 / dtUs : 30.0;

    // ── anchors (§5.2) ──────────────────────────────────────────────────────
    // BOTH forearms from day one. The face-on tracker deferred the elbow once and
    // paid for it in the snap's one tail; here D2 needs elbow AND wrist on both
    // sides in every phase, so they are derived with the grip rather than after it.
    const int leadElbow  = (job.handedness == 2) ? 8 : 7;    // COCO 7 = left elbow, 8 = right
    const int trailElbow = (job.handedness == 2) ? 7 : 8;
    const int leadWrist  = (job.handedness == 2) ? 10 : 9;   // COCO 9 = left wrist, 10 = right
    const int trailWrist = (job.handedness == 2) ? 9 : 10;

    std::vector<int64_t> tUs(size_t(nf), 0);
    DtlAnchors an;
    an.gx.assign(size_t(nf), nan_());
    an.gy.assign(size_t(nf), nan_());
    an.leadElbow.assign(size_t(nf), {nan_(), nan_()});
    an.trailElbow.assign(size_t(nf), {nan_(), nan_()});
    an.leadWrist.assign(size_t(nf), {nan_(), nan_()});
    an.trailWrist.assign(size_t(nf), {nan_(), nan_()});
    an.quarantined.assign(size_t(nf), 0);
    an.joints.assign(size_t(nf), std::vector<cv::Point2d>(8));
    size_t poseIdx = 0;
    for (int i = 0; i < nf; ++i) {
        tUs[size_t(i)] = cov[size_t(i)].timestamp_us;
        while (poseIdx + 1 < dtlPose.frames.size()
               && dtlPose.frames[poseIdx + 1].t_us <= cov[size_t(i)].timestamp_us) ++poseIdx;
        const PoseFrame2D pf = lerpPoseFrame(dtlPose, poseIdx,
                                             std::min(poseIdx + 1, dtlPose.frames.size() - 1),
                                             cov[size_t(i)].timestamp_us);
        an.gx[size_t(i)] = 0.5 * (pf.leadHand.x() + pf.trailHand.x()) * w;
        an.gy[size_t(i)] = 0.5 * (pf.leadHand.y() + pf.trailHand.y()) * h;
        an.leadElbow[size_t(i)]  = kpPx(pf, leadElbow,  w, h);
        an.trailElbow[size_t(i)] = kpPx(pf, trailElbow, w, h);
        an.leadWrist[size_t(i)]  = kpPx(pf, leadWrist,  w, h);
        an.trailWrist[size_t(i)] = kpPx(pf, trailWrist, w, h);
        for (int j = 0; j < 8; ++j)
            an.joints[size_t(i)][size_t(j)] = kpPx(pf, kBodyJoints[j], w, h);
    }

    // ── decode-once span cache (shaft_frame_io.h, shared) ───────────────────
    // The cache vector stays OURS: buildFrameCache's callable closes over it by
    // reference, so it must outlive every frameAt() call below.
    std::vector<cv::Mat> frameCache;
    const FrameSource frameAt = buildFrameCache(window, cov, *cfmt, w, h, frameCache);

    DtlDecideTrace local;
    DtlDecideTrace* trace = traceIn ? traceIn : &local;
    DtlSolveState st = dtlSolve(frameAt, tUs, an, witness, w, h, fps,
                                job.bandCentersMm, job.clubLengthM * 1000.0, cfg, trace);

    // ── the post-solve half owns the snap and the tier ladder (§5.9) ────────
    // It lives in dtl_shaft_post so it is testable over plain vectors: this class
    // is the SwingWindow layer and nothing more. The club geometry is built the
    // way the face-on tracker builds it, from the same club record.
    SegmentGeom segGeom;
    segGeom.clubLenMm = job.clubLengthM * 1000.0;
    segGeom.hoselMm   = job.hoselFromButtMm > 0.0 ? job.hoselFromButtMm : segGeom.clubLenMm - 58.0;
    segGeom.gripEndMm = job.shaftLengthMm > 0.0 ? segGeom.hoselMm - job.shaftLengthMm : 265.0;
    segGeom.bandsMm   = job.bandCentersMm;

    const pinpoint::SourceId cam = out.camera;
    out = dtlPostSolve(frameAt, tUs, an, witness, st, w, h, segGeom, cfg, trace);
    out.camera = cam;
    // §4.5 / Stage 0 again: shared host clock, no constant shift applied — the
    // witness interpolates across the ~3.2 ms phase difference.
    out.clockOffsetUs = 0;

    int tierCount[6] = {0, 0, 0, 0, 0, 0};
    int vetoedAtSolve = 0, escapes = 0, snapN = 0, gated = 0, limbVetoed = 0, bound = 0;
    std::vector<double> snapOffsets;
    for (int i = 0; i < int(out.samples.size()); ++i) {
        ++tierCount[int(out.samples[size_t(i)].tier)];
        if (out.samples[size_t(i)].corridorEscape) ++escapes;
        if (out.samples[size_t(i)].rhoSrc == DtlRhoSrc::Bound) ++bound;
        if (i < int(st.solved.size()) && st.solved[size_t(i)]
            && i < int(trace->limbVetoJoint.size()) && trace->limbVetoJoint[size_t(i)] >= 0)
            ++vetoedAtSolve;
        if (i < int(st.ballGate.size()) && st.ballGate[size_t(i)]) ++gated;
        if (i < int(trace->snapAccepted.size()) && trace->snapAccepted[size_t(i)]) {
            ++snapN;
            snapOffsets.push_back(std::abs(trace->snapOffsetPx[size_t(i)]));
        }
    }
    limbVetoed = vetoedAtSolve;
    double snapMedian = -1.0;
    if (!snapOffsets.empty()) {
        std::nth_element(snapOffsets.begin(), snapOffsets.begin() + snapOffsets.size() / 2,
                         snapOffsets.end());
        snapMedian = snapOffsets[snapOffsets.size() / 2];
    }

    // One log line per band, because the report Mark reads is per band and never
    // pooled (§6) — and because the corridor SIGN table is the thing §4.2 says is
    // not established, so it has to come out of every run rather than be asked for.
    ppInfo() << "[DtlShaftTracker] frames" << nf << "span" << st.spanFrames
             << "bands" << int(out.bands.size())
             << "| BAND" << tierCount[int(DtlTier::Band)]
             << "RAY" << tierCount[int(DtlTier::Ray)]
             << "UNSEEN" << tierCount[int(DtlTier::Unseen)]
             << "END_ON" << tierCount[int(DtlTier::EndOn)]
             << "OCCLUDED" << tierCount[int(DtlTier::Occluded)]
             << "| sightedFrac" << out.sightedFrac
             << "publishedInEndOn" << out.publishedInEndOn
             << "| ball" << (st.ball.found ? "yes" : "no") << st.ball.x << st.ball.y
             << qPrintable(st.ball.reason)
             << "| L_D" << st.lFullPx << qPrintable(st.lFullSource)
             << "| rowFit a" << st.rowFitA << "b" << st.rowFitB
             << "| D2 vetoed-at-solve" << limbVetoed << "escapes" << escapes
             << "| ballGated" << gated << "ρ̂ bound" << bound
             << "| snap" << snapN << "median" << snapMedian << "px"
             << "," << wall.elapsed() << "ms";
    for (int b = 0; b < int(out.bands.size()); ++b) {
        int nB = 0, nPub = 0, sPlus = 0, sMinus = 0, esc = 0;
        for (int i = out.bands[size_t(b)].lo; i <= out.bands[size_t(b)].hi; ++i) {
            ++nB;
            if (i < int(st.corrSignTaken.size())) {
                if (st.corrSignTaken[size_t(i)] > 0) ++sPlus;
                else if (st.corrSignTaken[size_t(i)] < 0) ++sMinus;
            }
            if (i < int(st.corridorEscape.size()) && st.corridorEscape[size_t(i)]) ++esc;
        }
        for (const DtlSample& s : out.samples)
            if (s.band == b && s.tier >= DtlTier::Ray) ++nPub;
        ppInfo() << "[DtlShaftTracker]   band" << b << qPrintable(out.bands[size_t(b)].name)
                 << "frames" << nB << "published" << nPub
                 << "corrSign +/-" << sPlus << sMinus << "escapes" << esc;
    }
    if (!out.valid)
        ppWarn() << "[DtlShaftTracker] no published sample — track invalid";
    return out;
}

} // namespace pinpoint::analysis
