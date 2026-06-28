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

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <variant>

#include <QElapsedTimer>

#include "analysis_tuning.h"
#include "shaft_kinematics.h"
#include "imu_vision_fuser.h"
#include "pose_runner.h"
#include "shot_analyzer.h"
#include "swing_window.h"
#include "format_descriptor.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_profiler.h"
#include "../Export/frame_decode.h"

namespace pinpoint::analysis {
namespace {

// Assumed athlete stature for the prior-quality pixel scale (it only sizes
// the detection search radius, never a metric — C1 calibration replaces it).
constexpr double kAssumedStatureM = 1.70;

// COCO keypoint indices used for anchors/clutter masks.
constexpr int kLeftShoulder  = 5;
constexpr int kRightShoulder = 6;
constexpr int kLeftElbow  = 7;
constexpr int kRightElbow = 8;
constexpr float kKpMinConf = 0.30f;

// Nominal acromion→wrist length (m) — only the R1 arm-derived pixel scale uses
// it, and only as a prior-quality scale (never a metric). Population average.
constexpr double kArmNominalM = 0.52;
constexpr double kPi          = 3.14159265358979323846;
constexpr double kDeg2Rad     = kPi / 180.0;

struct AnchorState {
    cv::Point2f grip;
    bool  hasInterHand = false;
    float interHandRad = 0.f;
    int   numElbows    = 0;
    float elbowRad[2]  = { 0.f, 0.f };
    // Lead-arm geometry (K1): "arms hang from the shoulders to the hands."
    bool        hasArm    = false;
    float       phiArmRad = 0.f;    // image angle shoulder(s)→grip (the arm-hang direction)
    float       armPx     = 0.f;    // shoulder→grip length px (× forearm-scale on elbow fallback)
    cv::Point2f shoulderRef;        // shoulder midpoint / single shoulder / elbow fallback, image px
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

    // Lead-arm geometry: handedness-free — the shoulder midpoint → grip vector is
    // both the "arm hang" direction (R2 prior) and an arm-length proxy (R1 scale).
    // Forearm (elbow→grip × ~full-arm/forearm ratio) is the fallback when the
    // shoulders are occluded; nothing when neither is confident.
    {
        auto conf2 = [&](int j) { return std::min(a.conf[size_t(j)], b.conf[size_t(j)]); };
        auto kpPx  = [&](int j) { return toPx(lerp(a.kp[size_t(j)], b.kp[size_t(j)])); };
        const bool ls = conf2(kLeftShoulder)  >= kKpMinConf;
        const bool rs = conf2(kRightShoulder) >= kKpMinConf;
        cv::Point2f ref;
        bool  haveRef  = false;
        float lenScale = 1.f;
        if (ls && rs) {
            ref = 0.5f * (kpPx(kLeftShoulder) + kpPx(kRightShoulder));
            haveRef = true;
        } else if (ls || rs) {
            ref = kpPx(ls ? kLeftShoulder : kRightShoulder);
            haveRef = true;
        } else {
            const bool le = conf2(kLeftElbow)  >= kKpMinConf;
            const bool re = conf2(kRightElbow) >= kKpMinConf;
            if (le && re) { ref = 0.5f * (kpPx(kLeftElbow) + kpPx(kRightElbow)); haveRef = true; lenScale = 1.9f; }
            else if (le || re) { ref = kpPx(le ? kLeftElbow : kRightElbow); haveRef = true; lenScale = 1.9f; }
        }
        if (haveRef) {
            const cv::Point2f d = s.grip - ref;
            const float len = std::hypot(d.x, d.y);
            if (len > 8.f) {
                s.shoulderRef = ref;
                s.phiArmRad   = std::atan2(d.y, d.x);
                s.armPx       = len * lenScale;
                s.hasArm      = true;
            }
        }
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

// Median shoulder→grip ("arm hang") length in px over the track — the R1 arm
// pixel scale and R5 length floor source. 0 when shoulders + hands are never
// both confident (caller then falls back to the silhouette scale).
double medianArmPx(const PoseTrack2D &pose, int w, int h)
{
    auto toPx = [&](const QPointF &p) { return cv::Point2f(float(p.x() * w), float(p.y() * h)); };
    std::vector<double> lens;
    lens.reserve(pose.frames.size());
    for (const PoseFrame2D &f : pose.frames) {
        const bool ls = f.conf[size_t(kLeftShoulder)]  >= kKpMinConf;
        const bool rs = f.conf[size_t(kRightShoulder)] >= kKpMinConf;
        if (!ls && !rs)
            continue;
        const cv::Point2f sh = (ls && rs)
            ? 0.5f * (toPx(f.kp[size_t(kLeftShoulder)]) + toPx(f.kp[size_t(kRightShoulder)]))
            : toPx(f.kp[size_t(ls ? kLeftShoulder : kRightShoulder)]);
        const cv::Point2f grip = toPx(QPointF(0.5 * (f.leadHand.x() + f.trailHand.x()),
                                              0.5 * (f.leadHand.y() + f.trailHand.y())));
        const double len = std::hypot(grip.x - sh.x, grip.y - sh.y);
        if (len > 8.0)
            lens.push_back(len);
    }
    if (lens.empty())
        return 0.0;
    std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
    return lens[lens.size() / 2];
}

// Canonical swing-progress for each phase knot (matches the R6 seed table). −1
// for phases that are not progress anchors.
float progressForPhase(Phase p)
{
    switch (p) {
    case Phase::Address:       return 0.00f;
    case Phase::Takeaway:      return 0.15f;
    case Phase::MidBackswing:  return 0.35f;
    case Phase::Top:           return 0.50f;
    case Phase::Transition:    return 0.60f;
    case Phase::Delivery:      return 0.80f;
    case Phase::MaxSpeed:      return 0.85f;
    case Phase::Impact:        return 0.90f;
    case Phase::Release:       return 0.95f;
    case Phase::FollowThrough: return 0.98f;
    case Phase::Finish:        return 1.00f;
    default:                   return -1.f;
    }
}

// Map a frame timestamp to monotone swing-progress s ∈ [0,1] by interpolating
// between the segmentation event anchors. Returns false (no reliable s) when
// fewer than two anchors exist — the caller then falls back to the arm-hang dir.
bool swingProgressAt(const Segmentation &seg, int64_t t_us, float &sOut)
{
    struct Anchor { int64_t t; float s; };
    std::vector<Anchor> anchors;
    anchors.reserve(seg.events.size());
    for (const PhaseEvent &e : seg.events) {
        const float s = progressForPhase(e.phase);
        if (s >= 0.f)
            anchors.push_back({ e.t_us, s });
    }
    if (anchors.size() < 2)
        return false;
    std::sort(anchors.begin(), anchors.end(),
              [](const Anchor &x, const Anchor &y) { return x.t < y.t; });
    if (t_us <= anchors.front().t) { sOut = anchors.front().s; return true; }
    if (t_us >= anchors.back().t)  { sOut = anchors.back().s;  return true; }
    for (size_t i = 1; i < anchors.size(); ++i) {
        if (t_us <= anchors[i].t) {
            const Anchor &a = anchors[i - 1], &b = anchors[i];
            const float f = (b.t == a.t) ? 0.f
                                         : float(double(t_us - a.t) / double(b.t - a.t));
            sOut = a.s + (b.s - a.s) * f;
            return true;
        }
    }
    sOut = anchors.back().s;
    return true;
}

PoseFrame2D lerpPoseFrame(const PoseTrack2D &pose, size_t lo, size_t hi, int64_t t)
{
    const PoseFrame2D &a = pose.frames[lo];
    const PoseFrame2D &b = pose.frames[hi];
    const double f = (hi == lo || b.t_us == a.t_us)
                         ? 0.0
                         : std::clamp(double(t - a.t_us) / double(b.t_us - a.t_us), 0.0, 1.0);
    PoseFrame2D out;
    out.t_us = t;
    for (size_t i = 0; i < 17; ++i) {
        out.kp[i] = QPointF(a.kp[i].x() + (b.kp[i].x() - a.kp[i].x()) * f,
                            a.kp[i].y() + (b.kp[i].y() - a.kp[i].y()) * f);
        out.conf[i] = a.conf[i] + (b.conf[i] - a.conf[i]) * float(f);
    }
    out.leadHand  = QPointF(a.leadHand.x() + (b.leadHand.x() - a.leadHand.x()) * f,
                            a.leadHand.y() + (b.leadHand.y() - a.leadHand.y()) * f);
    out.trailHand = QPointF(a.trailHand.x() + (b.trailHand.x() - a.trailHand.x()) * f,
                            a.trailHand.y() + (b.trailHand.y() - a.trailHand.y()) * f);
    out.handConf  = a.handConf + (b.handConf - a.handConf) * float(f);
    return out;
}

void drawBodyMask(cv::Mat &mask, const PoseFrame2D &f, int w, int h)
{
    auto toPx = [&](const QPointF &p) {
        return cv::Point(int(p.x() * w), int(p.y() * h));
    };

    // COCO keypoint indices connections
    static const std::vector<std::pair<int, int>> connections = {
        {5, 6},     // shoulders
        {5, 7}, {7, 9},     // left arm
        {6, 8}, {8, 10},    // right arm
        {11, 12},           // hips
        {5, 11}, {6, 12},   // torso sides
        {11, 13}, {13, 15}, // left leg
        {12, 14}, {14, 16}  // right leg
    };

    int thickness = std::max(20, int(h * 0.10));
    for (const auto &conn : connections) {
        if (f.conf[size_t(conn.first)] >= kKpMinConf && f.conf[size_t(conn.second)] >= kKpMinConf) {
            cv::line(mask, toPx(f.kp[size_t(conn.first)]), toPx(f.kp[size_t(conn.second)]), cv::Scalar(255), thickness);
        }
    }

    // Also draw a circle around the hands anchor
    QPointF handsMid = 0.5 * (f.leadHand + f.trailHand);
    int handRadius = std::max(30, int(h * 0.12));
    cv::circle(mask, toPx(handsMid), handRadius, cv::Scalar(255), -1);
}

cv::Mat computeMedianLuma(const std::vector<pinpoint::IndexEntry> &entries,
                          const pinpoint::CameraFormat &cfmt,
                          const pinpoint::SwingWindow &window)
{
    std::vector<cv::Mat> bgFrames;
    size_t numBgFrames = 15;
    size_t step = std::max(size_t(1), entries.size() / numBgFrames);
    for (size_t i = 0; i < entries.size(); i += step) {
        const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(entries[i]);
        cv::Mat luma;
        if (pinpoint::decodeToLuma(cfmt, handle.data, handle.bytes, luma)) {
            bgFrames.push_back(luma.clone());
        }
        if (bgFrames.size() >= numBgFrames)
            break;
    }
    if (bgFrames.empty())
        return cv::Mat();

    int h = bgFrames[0].rows;
    int w = bgFrames[0].cols;
    cv::Mat medianFrame(h, w, CV_8UC1);
    size_t n = bgFrames.size();
    std::vector<uchar> vals(n);
    
    for (int r = 0; r < h; ++r) {
        uchar *outRow = medianFrame.ptr<uchar>(r);
        std::vector<const uchar*> rowPtrs(n);
        for (size_t i = 0; i < n; ++i) {
            rowPtrs[i] = bgFrames[i].ptr<uchar>(r);
        }
        for (int c = 0; c < w; ++c) {
            for (size_t i = 0; i < n; ++i) {
                vals[i] = rowPtrs[i][c];
            }
            std::sort(vals.begin(), vals.end());
            outRow[c] = vals[n / 2];
        }
    }
    return medianFrame;
}

} // namespace

ShaftTrack2D ShaftTracker::track(const pinpoint::SwingWindow &window,
                                 const PoseTrack2D &pose,
                                 const FusedStreams &streams,
                                 const Segmentation &segmentation,
                                 const ShotAnalysisJob &job,
                                 ShaftTrace *trace)
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

    PP_PROFILE_SCOPE("Shaft.track");   // whole per-shot tracker (config + per-frame loop + assembly)

    // Prior-quality pixel scale from the pose silhouette (sizes the search
    // radius only). Falls back to a half-frame radius when pose never sees a
    // full body. SwingLab "shaft.*" overrides apply first; maxRadiusPx is
    // computed below from geometry (override "shaft.maxRadiusPx" to pin it).
    ShaftDetectConfig dcfg;
    {
        namespace tn = tuning;
        const QVariantMap &ov = job.tuningOverrides;
        tn::apply(ov, "shaft.rhoMinPx",          dcfg.rhoMinPx);
        tn::apply(ov, "shaft.thetaBins",         dcfg.thetaBins);
        tn::apply(ov, "shaft.ridgeKernelPx",     dcfg.ridgeKernelPx);
        tn::apply(ov, "shaft.maxCandidates",     dcfg.maxCandidates);
        tn::apply(ov, "shaft.nmsSeparationDeg",  dcfg.nmsSeparationDeg);
        tn::apply(ov, "shaft.clutterMaskDeg",    dcfg.clutterMaskDeg);
        tn::apply(ov, "shaft.minVisibleLenPx",   dcfg.minVisibleLenPx);
        tn::apply(ov, "shaft.minScoreFrac",      dcfg.minScoreFrac);
        tn::apply(ov, "shaft.runStartGapPx",     dcfg.runStartGapPx);
        tn::apply(ov, "shaft.runMaxGapPx",       dcfg.runMaxGapPx);
        tn::apply(ov, "shaft.noiseSigmaK",       dcfg.noiseSigmaK);
        tn::apply(ov, "shaft.thresholdFloor",    dcfg.thresholdFloor);
        tn::apply(ov, "shaft.interHandSigmaDeg", dcfg.interHandSigmaDeg);
        tn::apply(ov, "shaft.priorFloor",        dcfg.priorFloor);
        tn::apply(ov, "shaft.wedgeMinSpanDeg",   dcfg.wedgeMinSpanDeg);
        // Skeleton-aware enhancement knobs (consumed from K2/K4; no-op until then).
        tn::apply(ov, "shaft.envelopeKSigma",    dcfg.envelopeKSigma);
        tn::apply(ov, "shaft.envelopeHardK",     dcfg.envelopeHardK);
        tn::apply(ov, "shaft.blurThreshScale",   dcfg.blurThreshScale);
        tn::apply(ov, "shaft.blurSnrMargin",     dcfg.blurSnrMargin);
    }
    // Skeleton-aware enhancement gates (K1; all default OFF → current behaviour).
    bool   useArmScale       = false;  // R1: arm-derived search radius
    double armNominalLenM    = kArmNominalM;
    double minLenFracOfArm   = 0.0;    // R5: arm-relative length floor (0 = disabled)
    bool   useKinematicPrior = false;  // R2/R6: arm-direction soft prior (K1 interim = arm-hang dir)
    double armAxisSigmaDeg   = 35.0;   // R2 interim prior width (fallback when no swing-progress)
    bool   useEnvelope       = false;  // R6: hard-reject kinematically impossible candidates
    int    chirality         = +1;     // handedness × mirror sign for predictClubAngle (K5/handedness refines)
    bool   useBlurMode       = false;  // R8: blur-first detection in the delivery→impact zone
    double expExposureUs     = 3000.0; // exposure-time estimate for the fan width (K4 two-pass refine later)
    bool   useBackgroundSub  = false;  // Rec 4: median background subtraction
    bool   twoPassCalibration = false; // Rec 1/2: two-pass calibration loop
    bool   autoChirality      = true;   // Rec 3: automated chirality detection
    {
        namespace tn = tuning;
        const QVariantMap &ov = job.tuningOverrides;
        tn::apply(ov, "shaft.useArmScale",        useArmScale);
        tn::apply(ov, "shaft.armNominalLenM",     armNominalLenM);
        tn::apply(ov, "shaft.minLenFracOfArm",    minLenFracOfArm);
        tn::apply(ov, "shaft.useKinematicPrior",  useKinematicPrior);
        tn::apply(ov, "shaft.armAxisSigmaDeg",    armAxisSigmaDeg);
        tn::apply(ov, "shaft.useEnvelope",        useEnvelope);
        tn::apply(ov, "shaft.chirality",          chirality);
        tn::apply(ov, "shaft.useBlurMode",        useBlurMode);
        tn::apply(ov, "shaft.expExposureUs",      expExposureUs);
        tn::apply(ov, "shaft.useBackgroundSub",   useBackgroundSub);
        tn::apply(ov, "shaft.twoPassCalibration", twoPassCalibration);
        tn::apply(ov, "shaft.autoChirality",      autoChirality);
    }

    // Rec 3: Automated Chirality Detection
    if (autoChirality) {
        bool resolved = false;
        const PhaseEvent *addrEvent = segmentation.eventFor(Phase::Address);
        const PhaseEvent *topEvent = segmentation.eventFor(Phase::Top);
        if (addrEvent && topEvent) {
            double xAddr = 0.0, xTop = 0.0;
            bool haveAddr = false, haveTop = false;
            for (const PoseFrame2D &f : pose.frames) {
                if (std::abs(f.t_us - addrEvent->t_us) < 10000) {
                    xAddr = 0.5 * (f.leadHand.x() + f.trailHand.x()) * w;
                    haveAddr = true;
                }
                if (std::abs(f.t_us - topEvent->t_us) < 10000) {
                    xTop = 0.5 * (f.leadHand.x() + f.trailHand.x()) * w;
                    haveTop = true;
                }
            }
            if (haveAddr && haveTop) {
                double dx = xTop - xAddr;
                if (std::abs(dx) > 5.0) {
                    chirality = (dx > 0.0) ? +1 : -1;
                    resolved = true;
                }
            }
        }
        if (!resolved && pose.frames.size() >= 10) {
            double xStart = 0.0;
            int nStart = std::min(5, int(pose.frames.size() / 2));
            for (int i = 0; i < nStart; ++i) {
                const auto &f = pose.frames[size_t(i)];
                xStart += 0.5 * (f.leadHand.x() + f.trailHand.x()) * w;
            }
            xStart /= nStart;
            double maxDx = 0.0;
            int halfSize = int(pose.frames.size() / 2);
            for (int i = nStart; i < halfSize; ++i) {
                const auto &f = pose.frames[size_t(i)];
                double xVal = 0.5 * (f.leadHand.x() + f.trailHand.x()) * w;
                double dx = xVal - xStart;
                if (std::abs(dx) > std::abs(maxDx)) {
                    maxDx = dx;
                }
            }
            if (std::abs(maxDx) > 5.0) {
                chirality = (maxDx > 0.0) ? +1 : -1;
                resolved = true;
            }
        }
        if (!resolved) {
            chirality = (job.handedness == 2) ? -1 : +1;
        }

        // Hand centroid sequence verification
        if (!pose.frames.empty()) {
            const PoseFrame2D &firstF = pose.frames.front();
            double leadX = firstF.leadHand.x() * w;
            double trailX = firstF.trailHand.x() * w;
            if (firstF.handConf > 0.f && std::abs(trailX - leadX) > 4.0) {
                bool mismatch = (chirality == +1 && trailX < leadX) || (chirality == -1 && trailX > leadX);
                if (mismatch) {
                    ppWarn() << "[ShaftTracker] Hand centroid sequence mismatch with resolved chirality"
                             << chirality << "leadX:" << leadX << "trailX:" << trailX;
                }
            }
        }
    }
    chirality = chirality < 0 ? -1 : +1;
    const double armPxMed = medianArmPx(pose, w, h);

    // R1 scale ladder: arm length (robust, framing-independent) → silhouette
    // (needs the full body) → half-frame. Only the *source* of pxPerM changes;
    // maxRadiusPx = 1.25 · clubLengthM · pxPerM as before.
    const double bodyFrac = medianPoseHeight(pose);
    double      pxPerM    = 0.0;
    const char *scaleRung = "halfframe";
    if (useArmScale && armPxMed > 5.0 && armNominalLenM > 0.0) {
        pxPerM    = armPxMed / armNominalLenM;
        scaleRung = "arm";
    } else if (bodyFrac > 0.05) {
        pxPerM    = bodyFrac * h / kAssumedStatureM;
        scaleRung = "silhouette";
    }
    const double radius = pxPerM > 0.0 ? 1.25 * job.clubLengthM * pxPerM
                                       : 0.5 * std::min(w, h);
    dcfg.maxRadiusPx = float(std::clamp(radius, 80.0, double(std::min(w, h))));
    tuning::apply(job.tuningOverrides, "shaft.maxRadiusPx", dcfg.maxRadiusPx);

    // Predicted full club length in px (R7 predicted-series headPx). Falls back
    // to the search radius when there is no pixel scale at all.
    const double clubLenPx = (pxPerM > 0.0) ? job.clubLengthM * pxPerM
                                            : double(dcfg.maxRadiusPx) / 1.25;

    // R5: a shaft cannot image shorter than the club (≈ 2 arms); one arm is the
    // generous floor (≈ 50% occlusion headroom). 0 frac → current behaviour.
    dcfg.minShaftLenPx = dcfg.minVisibleLenPx;
    if (minLenFracOfArm > 0.0 && armPxMed > 5.0)
        dcfg.minShaftLenPx = std::max(dcfg.minVisibleLenPx, float(minLenFracOfArm * armPxMed));
    const float sharpMinShaftLenPx = dcfg.minShaftLenPx;   // R8 relaxes from this in blur frames

    const auto entries = window.entriesFor(pose.camera);

    QElapsedTimer wall;
    wall.start();

    // Rec 4: background difference luma map preparation
    cv::Mat medianBg;
    if (useBackgroundSub) {
        medianBg = computeMedianLuma(entries, *cfmt, window);
        if (medianBg.empty()) {
            ppWarn() << "[ShaftTracker] Median background estimation failed";
        }
    }

    // Rec 1 & 2: Two-Pass Calibration pre-pass (Pass 1)
    std::vector<double> expSamples;
    double pass1ResidSumSq = 0.0;
    int pass1ResidN = 0;

    if (twoPassCalibration) {
        size_t tempPoseIdx = 0;
        float tempPrevPredDir = 0.f;
        int64_t tempPrevPredT = 0;
        bool tempHavePrevPred = false;
        cv::Mat tempLuma;
        for (const pinpoint::IndexEntry &e : entries) {
            if (e.timestamp_us < pose.frames.front().t_us || e.timestamp_us > pose.frames.back().t_us)
                continue;
            while (tempPoseIdx + 1 < pose.frames.size() && pose.frames[tempPoseIdx + 1].t_us <= e.timestamp_us)
                ++tempPoseIdx;
            const size_t hiIdx = std::min(tempPoseIdx + 1, pose.frames.size() - 1);
            const AnchorState a = anchorAt(pose, tempPoseIdx, hiIdx, e.timestamp_us, w, h);

            float predDirRad = 0.f, predSigmaRad = 0.f;
            int   predSide   = 0;
            float frameS     = -1.f;
            bool  havePred   = false;
            if (a.hasArm) {
                float s = 0.f;
                if (swingProgressAt(segmentation, e.timestamp_us, s)) {
                    frameS = s;
                    kinematics::ClubAnglePrediction pr;
                    predDirRad   = kinematics::predictClubAngle(a.phiArmRad, s, chirality, &pr);
                    predSigmaRad = pr.sigmaRad;
                    predSide     = chirality * pr.side;
                } else {
                    predDirRad   = a.phiArmRad;
                    predSigmaRad = float(armAxisSigmaDeg * kDeg2Rad);
                }
                havePred = true;
            }

            ShaftDetectConfig fdcfg = dcfg;
            fdcfg.blurMode = false;
            fdcfg.predFanHalfRad = 0.f;
            fdcfg.minShaftLenPx = sharpMinShaftLenPx;
            double tempExposureUs = 3000.0; // default 3ms for Pass 1

            float omega = 0.f;
            if (useBlurMode && havePred) {
                if (tempHavePrevPred && e.timestamp_us > tempPrevPredT) {
                    const double dt  = double(e.timestamp_us - tempPrevPredT) * 1e-6;
                    const double dth = std::remainder(double(predDirRad) - double(tempPrevPredDir), 2.0 * kPi);
                    omega = float(std::abs(dth) / std::max(dt, 1e-4));
                }
                const float fanWidthRad = omega * float(tempExposureUs * 1e-6);
                const bool inBlurPhase = frameS >= 0.78f && frameS <= 0.96f;
                if (fanWidthRad > float(4.0 * kDeg2Rad) || inBlurPhase) {
                    fdcfg.blurMode = true;
                    fdcfg.predFanHalfRad = 0.5f * fanWidthRad;
                    const float proximal = (fanWidthRad > 1e-3f)
                        ? 0.5f * float(fdcfg.ridgeKernelPx) / fanWidthRad
                        : sharpMinShaftLenPx;
                    fdcfg.minShaftLenPx = std::clamp(proximal, fdcfg.minVisibleLenPx, sharpMinShaftLenPx);
                }
            }
            if (havePred) { tempPrevPredDir = predDirRad; tempPrevPredT = e.timestamp_us; tempHavePrevPred = true; }

            const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
            if (pinpoint::decodeToLuma(*cfmt, handle.data, handle.bytes, tempLuma)) {
                cv::Mat diffImage;
                if (useBackgroundSub && !medianBg.empty() && tempLuma.size() == medianBg.size()) {
                    cv::absdiff(tempLuma, medianBg, diffImage);
                    cv::Mat bodyMask = cv::Mat::zeros(tempLuma.rows, tempLuma.cols, CV_8UC1);
                    PoseFrame2D f = lerpPoseFrame(pose, tempPoseIdx, hiIdx, e.timestamp_us);
                    drawBodyMask(bodyMask, f, w, h);
                    diffImage.setTo(0, bodyMask);
                }

                AnchorPrior prior;
                prior.gripPx          = a.grip;
                prior.hasInterHandDir = a.hasInterHand;
                prior.interHandDirRad = a.interHandRad;
                prior.numElbowDirs    = a.numElbows;
                prior.elbowDirRad[0]  = a.elbowRad[0];
                prior.elbowDirRad[1]  = a.elbowRad[1];
                if (havePred) {
                    prior.kinematicDirRad   = predDirRad;
                    prior.kinematicSigmaRad = predSigmaRad;
                    prior.armSide           = predSide;
                    prior.hasKinematicDir   = false; // prior-free
                }

                std::vector<ShaftCandidate> cands = detectShaft(tempLuma, fdcfg, prior, diffImage);
                if (useEnvelope && havePred && !cands.empty()) {
                    const float hardK = fdcfg.envelopeHardK;
                    cands.erase(
                        std::remove_if(cands.begin(), cands.end(),
                            [&](const ShaftCandidate &c) {
                                return kinematics::envelopeDeviationSigma(c.thetaRad, predDirRad, predSigmaRad) > hardK;
                            }),
                        cands.end());
                }

                if (!cands.empty() && cands.front().wedge && omega >= 5.f) {
                    double obsWedgeWidth = 2.0 * cands.front().sigmaThetaRad;
                    expSamples.push_back(obsWedgeWidth / double(omega));
                }

                if (havePred && !cands.empty()) {
                    const double d = std::remainder(double(cands.front().thetaRad) - double(predDirRad), 2.0 * kPi);
                    pass1ResidSumSq += d * d;
                    ++pass1ResidN;
                }
            }
        }

        // Calibrate parameters
        if (!expSamples.empty()) {
            std::nth_element(expSamples.begin(), expSamples.begin() + expSamples.size() / 2, expSamples.end());
            double medT = expSamples[expSamples.size() / 2];
            expExposureUs = std::clamp(medT * 1e6, 500.0, 10000.0);
            ppInfo() << "[ShaftTracker] Two-pass exposure calibrated to" << expExposureUs << "us";
        }
        if (pass1ResidN > 0) {
            double residDeg = std::sqrt(pass1ResidSumSq / double(pass1ResidN)) * 180.0 / kPi;
            ppInfo() << "[ShaftTracker] Two-pass residual calculated:" << residDeg << "deg";
            if (residDeg > 5.0) {
                double errFrac = std::clamp((residDeg - 5.0) / 10.0, 0.0, 1.0);
                dcfg.envelopeKSigma *= float(1.0 + errFrac);
                dcfg.envelopeHardK *= float(1.0 + errFrac);
                dcfg.blurThreshScale = float(double(dcfg.blurThreshScale) + (1.0 - double(dcfg.blurThreshScale)) * errFrac);
                ppInfo() << "[ShaftTracker] Adjusted blur mode aggressiveness. envelopeKSigma:" << dcfg.envelopeKSigma
                         << "envelopeHardK:" << dcfg.envelopeHardK << "blurThreshScale:" << dcfg.blurThreshScale;
            }
        }
    }

    // Per-frame detection over EVERY camera frame in the window, anchors
    // interpolated between pose samples. qHand from the fused LeadHand stream
    // (nearest 200 Hz grid sample) when bound.
    const SegmentStream *handStream = streams.streamFor(SegmentRole::LeadHand);

    std::vector<ShaftFrameObs> obs;
    obs.reserve(entries.size());
    std::vector<ShaftSample2D> predicted;        // R7: pure model, one per frame with a prediction
    double residSumSq = 0.0;                      // R7 residual (prior-free measured vs predicted)
    int    residN     = 0;
    float   prevPredDir  = 0.f;                   // R8 ω̂ finite-difference state
    int64_t prevPredT    = 0;
    bool    havePrevPred = false;
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

        // R6 prediction φ_club_pred = φ_arm + chirality·β̂(s) — pose-driven, so
        // computed for every frame with a visible arm *independent of*
        // useKinematicPrior (it feeds the prior, the R7 predicted series, and the
        // residual). Falls back to the arm-hang direction when swing-progress is
        // unavailable.
        float predDirRad = 0.f, predSigmaRad = 0.f;
        int   predSide   = 0;
        float frameS     = -1.f;
        bool  havePred   = false;
        if (a.hasArm) {
            float s = 0.f;
            if (swingProgressAt(segmentation, e.timestamp_us, s)) {
                frameS = s;
                kinematics::ClubAnglePrediction pr;
                predDirRad   = kinematics::predictClubAngle(a.phiArmRad, s, chirality, &pr);
                predSigmaRad = pr.sigmaRad;
                predSide     = chirality * pr.side;
            } else {
                predDirRad   = a.phiArmRad;
                predSigmaRad = float(armAxisSigmaDeg * kDeg2Rad);
            }
            havePred = true;
        }

        // R8 blur trigger (per-frame dcfg; detectShaft consumes blurMode in K4b).
        // ω̂ from the model prediction's frame-to-frame rate (always available);
        // fan angular width = ω̂·t_exp. Enter blur mode in the delivery→impact span
        // or when the fan exceeds the ridge kernel, and relax the R5 length floor
        // to the proximal-crisp extent (only the near-grip shaft stays sharp).
        // Defaults reset every frame; gated OFF by default.
        dcfg.blurMode       = false;
        dcfg.predFanHalfRad = 0.f;
        dcfg.minShaftLenPx  = sharpMinShaftLenPx;
        if (useBlurMode && havePred) {
            float omega = 0.f;                         // rad/s
            if (havePrevPred && e.timestamp_us > prevPredT) {
                const double dt  = double(e.timestamp_us - prevPredT) * 1e-6;
                const double dth = std::remainder(double(predDirRad) - double(prevPredDir), 2.0 * kPi);
                omega = float(std::abs(dth) / std::max(dt, 1e-4));
            }
            const float fanWidthRad = omega * float(expExposureUs * 1e-6);
            const bool  inBlurPhase = frameS >= 0.78f && frameS <= 0.96f;   // Delivery→Release
            if (fanWidthRad > float(4.0 * kDeg2Rad) || inBlurPhase) {
                dcfg.blurMode       = true;
                dcfg.predFanHalfRad = 0.5f * fanWidthRad;
                const float proximal = (fanWidthRad > 1e-3f)
                    ? 0.5f * float(dcfg.ridgeKernelPx) / fanWidthRad
                    : sharpMinShaftLenPx;
                dcfg.minShaftLenPx = std::clamp(proximal, dcfg.minVisibleLenPx, sharpMinShaftLenPx);
            }
        }
        if (havePred) { prevPredDir = predDirRad; prevPredT = e.timestamp_us; havePrevPred = true; }

        const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
        bool decoded;
        {
            PP_PROFILE_SCOPE("Shaft.decode");   // per-frame frame → luma decode
            decoded = pinpoint::decodeToLuma(*cfmt, handle.data, handle.bytes, luma);
        }
        if (decoded) {
            // Rec 4: background difference luma map
            cv::Mat diffImage;
            if (useBackgroundSub && !medianBg.empty() && luma.size() == medianBg.size()) {
                cv::absdiff(luma, medianBg, diffImage);
                cv::Mat bodyMask = cv::Mat::zeros(luma.rows, luma.cols, CV_8UC1);
                PoseFrame2D f = lerpPoseFrame(pose, poseIdx, hiIdx, e.timestamp_us);
                drawBodyMask(bodyMask, f, w, h);
                diffImage.setTo(0, bodyMask);
            }

            AnchorPrior prior;
            prior.gripPx          = a.grip;
            prior.hasInterHandDir = a.hasInterHand;
            prior.interHandDirRad = a.interHandRad;
            prior.numElbowDirs    = a.numElbows;
            prior.elbowDirRad[0]  = a.elbowRad[0];
            prior.elbowDirRad[1]  = a.elbowRad[1];
            // Carry the prediction on the prior whenever we have one (the R8 blur
            // window needs the envelope centre even with the soft prior off); the
            // soft bump in buildThetaWeights stays gated on useKinematicPrior.
            if (havePred) {
                prior.kinematicDirRad   = predDirRad;
                prior.kinematicSigmaRad = predSigmaRad;
                prior.armSide           = predSide;
                prior.hasKinematicDir   = useKinematicPrior;
            }
            {
                PP_PROFILE_SCOPE("Shaft.detect");   // per-frame radial detection (blur/fan/SNR cues)
                o.candidates = detectShaft(luma, dcfg, prior, diffImage);
            }
            // R6 envelope guardrail: drop kinematically impossible candidates
            // (> envelopeHardK·σ_β from the prediction). Independent of the soft
            // prior — it can hard-reject off-axis clutter without biasing the
            // measurement. σ_β is wide at the turn points, so legitimate
            // wide-club frames are not rejected.
            if (useEnvelope && havePred && !o.candidates.empty()) {
                const float hardK = dcfg.envelopeHardK;
                o.candidates.erase(
                    std::remove_if(o.candidates.begin(), o.candidates.end(),
                        [&](const ShaftCandidate &c) {
                            return kinematics::envelopeDeviationSigma(c.thetaRad, predDirRad, predSigmaRad) > hardK;
                        }),
                    o.candidates.end());
            }
            if (!o.candidates.empty())
                ++detected;
            // R7 residual: prior-free vision measurement vs the model prediction
            // (only meaningful when the prior did not bias the measurement).
            if (!useKinematicPrior && havePred && !o.candidates.empty()) {
                const double d = std::remainder(double(o.candidates.front().thetaRad)
                                                - double(predDirRad), 2.0 * kPi);
                residSumSq += d * d;
                ++residN;
            }
        }

        // R7 predicted series: the pure model output for this frame, emitted
        // regardless of useKinematicPrior so it can be overlaid against the
        // (prior-free) actual track. Flagged ShaftKinematicPredicted.
        if (havePred) {
            ShaftSample2D ps;
            ps.t_us         = e.timestamp_us;
            ps.gripPx       = QPointF(a.grip.x, a.grip.y);
            ps.headPx       = QPointF(a.grip.x + clubLenPx * std::cos(predDirRad),
                                      a.grip.y + clubLenPx * std::sin(predDirRad));
            ps.thetaRad     = predDirRad;
            ps.visibleLenPx = clubLenPx;
            const double sigDeg = predSigmaRad * 180.0 / kPi;
            ps.conf  = float(std::clamp(1.0 - sigDeg / 60.0, 0.1, 0.9));
            ps.flags = ShaftKinematicPredicted;
            predicted.push_back(ps);
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

    AssemblyConfig acfg;
    {
        namespace tn = tuning;
        const QVariantMap &ov = job.tuningOverrides;
        tn::apply(ov, "assembly.calibMinFrames",      acfg.calibMinFrames);
        tn::apply(ov, "assembly.calibMinSpanRad",     acfg.calibMinSpanRad);
        tn::apply(ov, "assembly.calibSlowRateRadS",   acfg.calibSlowRateRadS);
        tn::apply(ov, "assembly.calibAcceptRad",      acfg.calibAcceptRad);
        tn::apply(ov, "assembly.missingPenalty",      acfg.missingPenalty);
        tn::apply(ov, "assembly.nodeScoreFloor",      acfg.nodeScoreFloor);
        tn::apply(ov, "assembly.transSigmaBaseRad",   acfg.transSigmaBaseRad);
        tn::apply(ov, "assembly.transAccSlackRadS2",  acfg.transAccSlackRadS2);
        tn::apply(ov, "assembly.transAccSlackImu",    acfg.transAccSlackImu);
        tn::apply(ov, "assembly.transNoRateExtraRad", acfg.transNoRateExtraRad);
        tn::apply(ov, "assembly.lenSigmaFrac",        acfg.lenSigmaFrac);
        tn::apply(ov, "assembly.lenSigmaFloorPx",     acfg.lenSigmaFloorPx);
        tn::apply(ov, "assembly.jerkPsd",             acfg.jerkPsd);
        tn::apply(ov, "assembly.visionSigmaFloorRad", acfg.visionSigmaFloorRad);
        tn::apply(ov, "assembly.imuSigmaFloorRad",    acfg.imuSigmaFloorRad);
        tn::apply(ov, "assembly.confSigmaRefRad",     acfg.confSigmaRefRad);
        tn::apply(ov, "assembly.coverageMin",         acfg.coverageMin);
    }
    ShaftTrack2D track;
    {
        PP_PROFILE_SCOPE("Shaft.assemble");   // Viterbi association + wrap-aware KF/RTS smoother
        track = ShaftTrackAssembly::assemble(
            obs, spanLo, spanHi, acfg, trace ? &trace->assembly : nullptr);
    }
    if (trace)
        trace->obs = obs;
    track.camera      = pose.camera;
    track.frameWidth  = w;
    track.frameHeight = h;
    // R7 dual output: the pure-model series + its agreement with the prior-free
    // vision measurement (−1 when biased by the prior or no measured frames).
    track.predicted = std::move(predicted);
    track.modelVisionResidualDeg =
        (!useKinematicPrior && residN > 0)
            ? float(std::sqrt(residSumSq / double(residN)) * 180.0 / kPi)
            : -1.f;

    ppInfo() << "[ShaftTracker] frames" << qint64(obs.size())
             << "detected" << detected
             << "coverage" << track.coverage
             << "valid" << track.valid
             << "imuVisionCorr" << track.imuVisionCorr
             << "scale" << scaleRung << "armPx" << armPxMed
             << "minShaftLenPx" << dcfg.minShaftLenPx
             << "predicted" << qint64(track.predicted.size())
             << "modelVisionResidualDeg" << track.modelVisionResidualDeg
             << "(" << wall.elapsed() << "ms )";
    return track;
}

} // namespace pinpoint::analysis
