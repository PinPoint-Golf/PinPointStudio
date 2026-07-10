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

// ShaftTracker v3.0-r1 — the SwingWindow layer. Derives per-frame grip anchors
// + lead-forearm φ + the 8-joint body skeleton from the offline pose (mirroring
// tools/shaftlab/prep_swing.py), builds a frame-decode callback over the face-on
// camera, and hands the lot to decideTrack() (shaft_track_assembly), the shared
// SwingWindow-free decide core. Vision-only (streams/segmentation unused).

#include "shaft_tracker.h"

#include <opencv2/imgproc.hpp>

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "ball_anchor.h"            // applyBallAnchor — the v3.4 post-hoc pass
#include "shot_analyzer.h"          // ShotAnalysisJob
#include "swing_window.h"
#include "format_descriptor.h"
#include "../Core/pp_debug.h"
#include "../Export/frame_decode.h"

namespace pinpoint::analysis {
namespace {

constexpr double kPi = 3.14159265358979323846;
// COCO joints for the body ROI: shoulders, hips, knees, ankles (prep_swing).
constexpr int kBodyJoints[8] = {5, 6, 11, 12, 13, 14, 15, 16};

// Linear-interpolate a pose frame between two samples at time t.
PoseFrame2D lerpPoseFrame(const PoseTrack2D& pose, size_t lo, size_t hi, int64_t t)
{
    const PoseFrame2D& a = pose.frames[lo];
    const PoseFrame2D& b = pose.frames[hi];
    const double f = (hi == lo || b.t_us == a.t_us)
                         ? 0.0
                         : std::clamp(double(t - a.t_us) / double(b.t_us - a.t_us), 0.0, 1.0);
    PoseFrame2D o; o.t_us = t;
    for (size_t i = 0; i < 17; ++i) {
        o.kp[i] = QPointF(a.kp[i].x() + (b.kp[i].x() - a.kp[i].x()) * f,
                          a.kp[i].y() + (b.kp[i].y() - a.kp[i].y()) * f);
        o.conf[i] = a.conf[i] + (b.conf[i] - a.conf[i]) * float(f);
    }
    o.leadHand  = QPointF(a.leadHand.x() + (b.leadHand.x() - a.leadHand.x()) * f,
                          a.leadHand.y() + (b.leadHand.y() - a.leadHand.y()) * f);
    o.trailHand = QPointF(a.trailHand.x() + (b.trailHand.x() - a.trailHand.x()) * f,
                          a.trailHand.y() + (b.trailHand.y() - a.trailHand.y()) * f);
    o.handConf  = a.handConf + (b.handConf - a.handConf) * float(f);
    return o;
}

// Decode one frozen-ring payload to CV_8UC1 grey, matching the Python path
// (demosaic → BGR2GRAY). Falls back to the luma fast path. Empty on undecodable
// formats. The result never aliases the ring (cvtColor/clone).
cv::Mat decodeGray(const pinpoint::SwingWindow& window, const pinpoint::IndexEntry& e,
                   const pinpoint::CameraFormat& cfmt)
{
    const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
    cv::Mat bgr;
    if (pinpoint::decodeToBgr(cfmt, handle.data, handle.bytes, bgr) && !bgr.empty()) {
        if (bgr.channels() == 1) return bgr.clone();
        cv::Mat g; cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
        return g;
    }
    cv::Mat luma;
    if (pinpoint::decodeToLuma(cfmt, handle.data, handle.bytes, luma) && !luma.empty())
        return luma.clone();
    return {};
}

} // namespace

ShaftTrack2D ShaftTracker::track(const pinpoint::SwingWindow& window, const PoseTrack2D& pose,
                                 const BallTrack2D& ball,
                                 const FusedStreams& /*streams*/, const Segmentation& /*segmentation*/,
                                 const ShotAnalysisJob& job, ShaftTrace* trace)
{
    QElapsedTimer wall;
    wall.start();
    ShaftTrack2D out;
    out.camera = pose.camera;
    if (pose.frames.size() < 2) { ppWarn() << "[ShaftTracker] no usable pose track — invalid"; return out; }

    const pinpoint::FormatDescriptor& fd = window.formatOf(pose.camera);
    const auto* cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt || cfmt->width == 0 || cfmt->height == 0) {
        ppWarn() << "[ShaftTracker] source" << pose.camera << "has no camera format — invalid";
        return out;
    }
    const int w = int(cfmt->width), h = int(cfmt->height);

    ShaftV3Config cfg = ShaftV3Config::fromOverrides(job.tuningOverrides);
    if (job.fullWindow)
        cfg.spanBound = false;   // explicit re-analysis: evidence over the whole window, not just the swing span

    // camera frames inside pose coverage
    const std::vector<pinpoint::IndexEntry> all = window.entriesFor(pose.camera);
    const int64_t tLo = pose.frames.front().t_us, tHi = pose.frames.back().t_us;
    std::vector<pinpoint::IndexEntry> cov;
    for (const auto& e : all) if (e.timestamp_us >= tLo && e.timestamp_us <= tHi) cov.push_back(e);
    const int nf = int(cov.size());
    if (nf < 2) { ppWarn() << "[ShaftTracker] < 2 frames inside pose coverage — invalid"; return out; }

    // timebase: median inter-frame interval (NOT container fps)
    std::vector<int64_t> diffs;
    for (int i = 1; i < nf; ++i) diffs.push_back(cov[i].timestamp_us - cov[i - 1].timestamp_us);
    std::nth_element(diffs.begin(), diffs.begin() + diffs.size() / 2, diffs.end());
    const double dtUs = double(diffs[diffs.size() / 2]);
    const double fps = dtUs > 0 ? 1e6 / dtUs : 30.0;

    // derive grip / φ / joints per frame from the pose (mirrors prep_swing.py)
    const int leadElbow = (job.handedness == 2) ? 8 : 7;   // right-lead=7(L), left-lead=8(R)
    std::vector<int64_t> tUs(nf);
    std::vector<double> gx(nf), gy(nf), phiRaw(nf);
    std::vector<std::vector<cv::Point2d>> rawJoints(nf, std::vector<cv::Point2d>(8));
    size_t poseIdx = 0;
    for (int i = 0; i < nf; ++i) {
        tUs[i] = cov[i].timestamp_us;
        while (poseIdx + 1 < pose.frames.size() && pose.frames[poseIdx + 1].t_us <= cov[i].timestamp_us) ++poseIdx;
        const PoseFrame2D pf = lerpPoseFrame(pose, poseIdx, std::min(poseIdx + 1, pose.frames.size() - 1),
                                             cov[i].timestamp_us);
        const double grx = 0.5 * (pf.leadHand.x() + pf.trailHand.x()) * w;
        const double gry = 0.5 * (pf.leadHand.y() + pf.trailHand.y()) * h;
        gx[i] = grx; gy[i] = gry;
        const double ex = pf.kp[size_t(leadElbow)].x() * w, ey = pf.kp[size_t(leadElbow)].y() * h;
        const double plen = std::hypot(grx - ex, gry - ey);
        phiRaw[i] = (pf.conf[size_t(leadElbow)] > 0.30f && plen > 8.0)
                        ? std::atan2(gry - ey, grx - ex) * 180.0 / kPi
                        : std::numeric_limits<double>::quiet_NaN();
        for (int j = 0; j < 8; ++j)
            rawJoints[i][j] = {pf.kp[size_t(kBodyJoints[j])].x() * w, pf.kp[size_t(kBodyJoints[j])].y() * h};
    }

    int impf = -1;
    if (job.impactUs >= 0) {
        int64_t best = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < nf; ++i) {
            const int64_t d = std::llabs(cov[i].timestamp_us - job.impactUs);
            if (d < best) { best = d; impf = i; }
        }
    }

    const FrameSource frameAt = [&](int i) -> cv::Mat { return decodeGray(window, cov[i], *cfmt); };
    // Persistent club-length prior (club_length_fusion.h) from the job — filled by
    // ShotProcessor from AppSettings (live) or SwingDiskLoader from swing.json
    // (re-analysis). Joined into the fusion only when matured (n ≥ 2); pass null
    // otherwise so the E-prior term contributes nothing. Read-only here — the
    // prior UPDATE happens in the caller after out.lengths is populated.
    LengthPriorState lengthPrior;
    lengthPrior.emaPx = job.priorClubLenPx;
    lengthPrior.varPx = (job.priorClubLenVarPx >= 0.0) ? job.priorClubLenVarPx : 0.0;
    lengthPrior.n     = job.priorClubLenN;
    const LengthPriorState* priorPtr = (job.priorClubLenN >= 2 && job.priorClubLenPx > 0.0)
                                           ? &lengthPrior : nullptr;
    // Pass the ball into decideTrack (A1) so out.measuredClubLenPx is measured
    // before head placement and can drive the length ladder; null when empty
    // (same emptiness notion as applyBallAnchor). θ is unaffected either way.
    out = decideTrack(frameAt, tUs, gx, gy, phiRaw, rawJoints, w, h, fps,
                      job.bandCentersMm, job.clubLengthM * 1000.0, impf, cfg, trace,
                      ball.frames.empty() ? nullptr : &ball, priorPtr);
    out.camera = pose.camera;

    // v3.4 (design §9): additive post-hoc ball anchor — reads the frozen DP
    // output above, never re-solves it. No-op when `ball` is empty.
    applyBallAnchor(out, ball, gx, gy, tUs, w, h, impf, job, trace);

    ppInfo() << "[ShaftTracker] v3 frames" << nf << "coverage" << out.coverage
             << (out.valid ? "VALID" : "invalid") << "," << wall.elapsed() << "ms";
    return out;
}

} // namespace pinpoint::analysis
