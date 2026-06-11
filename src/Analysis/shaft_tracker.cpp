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

#include "shaft_tracker.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include <QElapsedTimer>

#include "imu_vision_fuser.h"
#include "pose_runner.h"
#include "shot_analyzer.h"
#include "swing_window.h"
#include "format_descriptor.h"
#include "../Core/pp_debug.h"
#include "../Export/frame_decode.h"

namespace pinpoint::analysis {
namespace {

// Assumed athlete stature for the prior-quality pixel scale (it only sizes
// the detection search radius, never a metric — C1 calibration replaces it).
constexpr double kAssumedStatureM = 1.70;

// COCO keypoint indices used for anchors/clutter masks.
constexpr int kLeftElbow  = 7;
constexpr int kRightElbow = 8;
constexpr float kKpMinConf = 0.30f;

struct AnchorState {
    cv::Point2f grip;
    bool  hasInterHand = false;
    float interHandRad = 0.f;
    int   numElbows    = 0;
    float elbowRad[2]  = { 0.f, 0.f };
};

// Linear interpolation of the per-frame anchor between bracketing pose
// frames (the sparse zone poses every 4th frame; hands move smoothly and the
// detector tolerates ±3 px of anchor error by design).
AnchorState anchorAt(const PoseTrack2D &pose, size_t lo, size_t hi, int64_t t,
                     int w, int h)
{
    const PoseFrame2D &a = pose.frames[lo];
    const PoseFrame2D &b = pose.frames[hi];
    const double f = (hi == lo || b.t_us == a.t_us)
                         ? 0.0
                         : std::clamp(double(t - a.t_us) / double(b.t_us - a.t_us), 0.0, 1.0);
    auto lerp = [&](const QPointF &p, const QPointF &q) {
        return QPointF(p.x() + (q.x() - p.x()) * f, p.y() + (q.y() - p.y()) * f);
    };
    auto toPx = [&](const QPointF &p) {
        return cv::Point2f(float(p.x() * w), float(p.y() * h));
    };

    AnchorState s;
    const QPointF lead  = lerp(a.leadHand,  b.leadHand);
    const QPointF trail = lerp(a.trailHand, b.trailHand);
    s.grip = toPx(QPointF(0.5 * (lead.x() + trail.x()), 0.5 * (lead.y() + trail.y())));

    // Inter-hand direction prior is only meaningful from real hand centroids
    // (paired wrist-fallback frames carry handConf = 0 and near-coincident
    // points).
    if (std::min(a.handConf, b.handConf) > 0.f) {
        const cv::Point2f d = toPx(trail) - toPx(lead);
        if (std::hypot(d.x, d.y) > 4.f) {
            s.hasInterHand = true;
            s.interHandRad = std::atan2(d.y, d.x);
        }
    }
    for (int e : { kLeftElbow, kRightElbow }) {
        if (std::min(a.conf[size_t(e)], b.conf[size_t(e)]) < kKpMinConf)
            continue;
        const cv::Point2f el = toPx(lerp(a.kp[size_t(e)], b.kp[size_t(e)]));
        const cv::Point2f d  = el - s.grip;
        if (std::hypot(d.x, d.y) < 8.f)
            continue;
        s.elbowRad[s.numElbows++] = std::atan2(d.y, d.x);
        if (s.numElbows == 2)
            break;
    }
    return s;
}

// Median confident-keypoint bounding-box height over the track, normalized.
double medianPoseHeight(const PoseTrack2D &pose)
{
    std::vector<double> heights;
    heights.reserve(pose.frames.size());
    for (const PoseFrame2D &f : pose.frames) {
        double lo = 1.0, hi = 0.0;
        int n = 0;
        for (int j = 0; j < 17; ++j) {
            if (f.conf[size_t(j)] < kKpMinConf)
                continue;
            lo = std::min(lo, f.kp[size_t(j)].y());
            hi = std::max(hi, f.kp[size_t(j)].y());
            ++n;
        }
        if (n >= 10 && hi > lo)
            heights.push_back(hi - lo);
    }
    if (heights.empty())
        return 0.0;
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    return heights[heights.size() / 2];
}

} // namespace

ShaftTrack2D ShaftTracker::track(const pinpoint::SwingWindow &window,
                                 const PoseTrack2D &pose,
                                 const FusedStreams &streams,
                                 const Segmentation &segmentation,
                                 const ShotAnalysisJob &job)
{
    ShaftTrack2D out;
    out.camera = pose.camera;
    if (pose.frames.size() < 2) {
        ppWarn() << "[ShaftTracker] no usable pose track — invalid";
        return out;
    }

    const pinpoint::FormatDescriptor &fd = window.formatOf(pose.camera);
    const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt || cfmt->width == 0 || cfmt->height == 0) {
        ppWarn() << "[ShaftTracker] source" << pose.camera << "has no camera format — invalid";
        return out;
    }
    const int w = int(cfmt->width), h = int(cfmt->height);

    // Prior-quality pixel scale from the pose silhouette (sizes the search
    // radius only). Falls back to a half-frame radius when pose never sees a
    // full body.
    ShaftDetectConfig dcfg;
    const double bodyFrac = medianPoseHeight(pose);
    const double pxPerM   = bodyFrac > 0.05 ? bodyFrac * h / kAssumedStatureM : 0.0;
    const double radius   = pxPerM > 0.0 ? 1.25 * job.clubLengthM * pxPerM
                                         : 0.5 * std::min(w, h);
    dcfg.maxRadiusPx = float(std::clamp(radius, 80.0, double(std::min(w, h))));

    QElapsedTimer wall;
    wall.start();

    // Per-frame detection over EVERY camera frame in the window, anchors
    // interpolated between pose samples. qHand from the fused LeadHand stream
    // (nearest 200 Hz grid sample) when bound.
    const SegmentStream *handStream = streams.streamFor(SegmentRole::LeadHand);
    const auto entries = window.entriesFor(pose.camera);

    std::vector<ShaftFrameObs> obs;
    obs.reserve(entries.size());
    size_t poseIdx = 0, scanned = 0;
    int detected = 0;
    cv::Mat luma;
    for (const pinpoint::IndexEntry &e : entries) {
        if (job.progress)
            job.progress(float(++scanned) / float(entries.size()));
        // Only frames inside pose coverage have a defensible anchor.
        if (e.timestamp_us < pose.frames.front().t_us
            || e.timestamp_us > pose.frames.back().t_us)
            continue;
        while (poseIdx + 1 < pose.frames.size()
               && pose.frames[poseIdx + 1].t_us <= e.timestamp_us)
            ++poseIdx;
        const size_t hiIdx = std::min(poseIdx + 1, pose.frames.size() - 1);
        const AnchorState a = anchorAt(pose, poseIdx, hiIdx, e.timestamp_us, w, h);

        ShaftFrameObs o;
        o.t_us   = e.timestamp_us;
        o.gripPx = a.grip;

        const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
        if (pinpoint::decodeToLuma(*cfmt, handle.data, handle.bytes, luma)) {
            AnchorPrior prior;
            prior.gripPx          = a.grip;
            prior.hasInterHandDir = a.hasInterHand;
            prior.interHandDirRad = a.interHandRad;
            prior.numElbowDirs    = a.numElbows;
            prior.elbowDirRad[0]  = a.elbowRad[0];
            prior.elbowDirRad[1]  = a.elbowRad[1];
            o.candidates = detectShaft(luma, dcfg, prior);
            if (!o.candidates.empty())
                ++detected;
        }

        if (handStream && !streams.timeGrid.empty()) {
            const auto &grid = streams.timeGrid;
            const auto it = std::lower_bound(grid.begin(), grid.end(), e.timestamp_us);
            size_t gi = size_t(it - grid.begin());
            if (gi > 0 && (gi == grid.size()
                           || grid[gi] - e.timestamp_us > e.timestamp_us - grid[gi - 1]))
                --gi;
            if (gi < handStream->qAnat.size()
                && std::abs(grid[gi] - e.timestamp_us) < 25000) {   // within ~5 grid steps
                o.qHand      = handStream->qAnat[gi];
                o.qHandValid = true;
            }
        }
        obs.push_back(std::move(o));
    }
    if (obs.empty()) {
        ppWarn() << "[ShaftTracker] no decodable frames inside pose coverage — invalid";
        return out;
    }

    // Coverage span for the validity gate: the detected swing (Address→Finish
    // from the segmentation ladder) when found, else the whole obs range.
    int64_t spanLo = obs.front().t_us, spanHi = obs.back().t_us;
    if (const PhaseEvent *a = segmentation.eventFor(Phase::Address))
        spanLo = std::max(spanLo, a->t_us);
    if (const PhaseEvent *f = segmentation.eventFor(Phase::Finish))
        spanHi = std::min(spanHi, f->t_us);
    if (spanHi <= spanLo) {
        spanLo = obs.front().t_us;
        spanHi = obs.back().t_us;
    }

    ShaftTrack2D track = ShaftTrackAssembly::assemble(obs, spanLo, spanHi);
    track.camera      = pose.camera;
    track.frameWidth  = w;
    track.frameHeight = h;

    ppInfo() << "[ShaftTracker] frames" << qint64(obs.size())
             << "detected" << detected
             << "coverage" << track.coverage
             << "valid" << track.valid
             << "imuVisionCorr" << track.imuVisionCorr
             << "(" << wall.elapsed() << "ms )";
    return track;
}

} // namespace pinpoint::analysis
