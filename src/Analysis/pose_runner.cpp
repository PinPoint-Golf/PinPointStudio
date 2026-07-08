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

#include "pose_runner.h"

using pinpoint::analysis::PoseFrame2D;
using pinpoint::analysis::PoseTrack2D;

#include <algorithm>
#include <variant>

#include <QElapsedTimer>
#include <QObject>

#include "format_descriptor.h"
#include "swing_window.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_profiler.h"

#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
#include <opencv2/core.hpp>
#include "../Export/frame_decode.h"
#include "../Pose/pose_estimator_vitpose.h"
#endif

#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)

namespace {

// Score-weighted centroid of one hand's 21 keypoints (score > 0.2 only).
// conf = plain mean of the contributing scores; ok = false when no keypoint
// clears the floor.
struct HandCentroid {
    QPointF pt;
    float   conf = 0.f;
    bool    ok   = false;
};

HandCentroid handCentroid(const std::array<QPointF, 21> &pts,
                          const std::array<float, 21> &scores)
{
    constexpr float kMinKpScore = 0.2f;
    double sx = 0.0, sy = 0.0, sw = 0.0, sconf = 0.0;
    int n = 0;
    for (int k = 0; k < WholeBodyHands::kHandJoints; ++k) {
        if (scores[k] <= kMinKpScore)
            continue;
        sx    += scores[k] * pts[k].x();
        sy    += scores[k] * pts[k].y();
        sw    += scores[k];
        sconf += scores[k];
        ++n;
    }
    HandCentroid c;
    if (n == 0 || sw <= 0.0)
        return c;
    c.pt   = QPointF(sx / sw, sy / sw);
    c.conf = static_cast<float>(sconf / n);
    c.ok   = true;
    return c;
}

} // namespace

PoseTrack2D PoseRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const ShotAnalysisRunnerOptions &opt)
{
    PoseTrack2D track;
    track.camera = faceOnSource;

    const auto entries = window.entriesFor(faceOnSource);
    if (entries.empty()) {
        ppWarn() << "[PoseRunner] no frames for source" << faceOnSource << "— empty track";
        return track;
    }

    const pinpoint::FormatDescriptor &fd = window.formatOf(faceOnSource);
    const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt) {
        ppWarn() << "[PoseRunner] source" << faceOnSource << "is not a camera — empty track";
        return track;
    }
    if (!pinpoint::demosaicPlanFor(cfmt->pixel_format).supported) {
        ppWarn() << "[PoseRunner] unsupported pixel format for source" << faceOnSource
                 << "— empty track";
        return track;
    }

    QElapsedTimer wall;
    wall.start();

    PP_PROFILE_SCOPE("Analysis.PoseRunner.run");

    // Estimator built on this (worker) thread. ViTPose is offline-only; load()
    // sizes its ORT intra-op pool to the physical-core count, and the post-shot
    // pipeline sequences the x264 export AFTER this pose pass
    // (ShotProcessor::onAnalysisFinished), so the inference is no longer starved
    // by the encoder's threads (which inflated per-frame inference ~5×).
    PoseEstimatorViTPose estimator;
    estimator.load();
    if (!estimator.isReady()) {
        ppWarn() << "[PoseRunner] ViTPose unavailable (model missing or load failed) "
                    "— empty track";
        return track;
    }
    estimator.setDecodeHands(true);

    // estimatePose() is synchronous and emits poseEstimated() inline — a
    // direct connection on this thread captures the result before it returns.
    PoseResult res;
    bool gotPose = false;
    QObject::connect(&estimator, &PoseEstimatorBase::poseEstimated, &estimator,
                     [&res, &gotPose](const PoseResult &r) {
                         res     = r;
                         gotPose = true;
                     },
                     Qt::DirectConnection);

    // Adaptive sampling: every frame in the blur-critical dense zone around
    // impact, every sparseStride-th frame elsewhere.
    const int64_t denseLo = opt.impactUs - static_cast<int64_t>(opt.densePreMs)  * 1000;
    const int64_t denseHi = opt.impactUs + static_cast<int64_t>(opt.densePostMs) * 1000;
    const int     stride  = std::max(1, opt.sparseStride);

    // Scan bounds (v3 G3): restrict to the detected swing span. Entries are
    // per-source monotonic, so the span is a contiguous index range. Bounds
    // that exclude every frame (clock mismatch) fall back to the full window
    // — degrading the optimisation, never the result.
    size_t i0 = 0, i1 = entries.size();
    bool   bounded = false;
    if (opt.scanEndUs > opt.scanStartUs) {
        while (i0 < entries.size() && entries[i0].timestamp_us < opt.scanStartUs)
            ++i0;
        while (i1 > i0 && entries[i1 - 1].timestamp_us > opt.scanEndUs)
            --i1;
        if (i0 >= i1) {
            ppWarn() << "[PoseRunner] scan bounds exclude every frame — falling back "
                        "to the full window";
            i0 = 0;
            i1 = entries.size();
        } else {
            bounded = true;
        }
    }

    // Address-hold coverage (v3.4 plan §2): pull the coverage window back
    // further than G3's scanStartUs so a real still address is reachable at
    // all — see pose_runner.h's addressScanPadUs doc. iAddr0 == i0 (no-op)
    // when unbounded, addressScanPadUs <= 0, or the pad doesn't reach any
    // earlier entries.
    size_t iAddr0 = i0;
    if (bounded && opt.addressScanPadUs > 0) {
        const int64_t addrLo = opt.scanStartUs - opt.addressScanPadUs;
        while (iAddr0 > 0 && entries[iAddr0 - 1].timestamp_us >= addrLo)
            --iAddr0;
    }

    // Lead = left hand for right-handed (and unknown) golfers, right for left-handed.
    const bool leftLeads = (opt.handedness != 2);
    constexpr int   kLeftWrist    = 9;    // PoseJoint::LeftWrist
    constexpr int   kRightWrist   = 10;   // PoseJoint::RightWrist
    constexpr float kMinHandConf  = 0.3f;
    const int addrStride = std::max(1, opt.addressStride);

    track.frames.reserve(i1 - iAddr0);
    size_t sampled = 0, wristOk = 0;
    cv::Mat bgr;   // reused decode scratch
    for (size_t i = iAddr0; i < i1; ++i) {
        const pinpoint::IndexEntry &e = entries[i];
        const bool inAddressZone = i < i0;   // before G3's own bound: coarse address-hold sampling
        const bool dense = !inAddressZone && opt.impactUs >= 0
                        && e.timestamp_us >= denseLo && e.timestamp_us <= denseHi;
        if (inAddressZone) {
            if ((i % static_cast<size_t>(addrStride)) != 0)
                continue;
        } else if (!dense && (i % static_cast<size_t>(stride)) != 0) {
            continue;
        }
        ++sampled;
        if (opt.progress)
            opt.progress(float(i + 1 - iAddr0) / float(i1 - iAddr0));

        // Frozen-window contract: payloadOf() may hand back data == nullptr
        // (slot overwritten / mid-write) — decodeToBgr rejects null and short
        // payloads alike. The handle outlives estimatePose(), so a zero-copy
        // `bgr` alias is safe for the call.
        const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
        if (!pinpoint::decodeToBgr(*cfmt, handle.data, handle.bytes, bgr))
            continue;

        gotPose = false;
        estimator.estimatePose(bgr);   // live path feeds full BGR CV_8UC3 frames too
        if (!gotPose)
            continue;

        PoseFrame2D f;
        f.t_us = e.timestamp_us;
        for (int j = 0; j < PoseResult::kNumKeypoints; ++j) {
            f.kp[j]   = QPointF(res.keypoints[j].x, res.keypoints[j].y);
            f.conf[j] = res.keypoints[j].score;
        }

        // Hand anchors: score-weighted knuckle centroids; fall back to the
        // COCO wrists (with handConf = 0) when either hand is unconvincing —
        // both anchors must come from the same source or the inter-hand
        // direction prior d̂ = trail − lead is meaningless.
        const WholeBodyHands &hands = estimator.lastHands();
        HandCentroid lc, rc;
        if (hands.valid) {
            lc = handCentroid(hands.left,  hands.leftScore);
            rc = handCentroid(hands.right, hands.rightScore);
        }
        QPointF leftPt, rightPt;
        float   handConf = 0.f;
        if (lc.ok && rc.ok && std::min(lc.conf, rc.conf) >= kMinHandConf) {
            leftPt   = lc.pt;
            rightPt  = rc.pt;
            handConf = 0.5f * (lc.conf + rc.conf);
        } else {
            leftPt  = f.kp[kLeftWrist];
            rightPt = f.kp[kRightWrist];
        }
        f.leadHand  = leftLeads ? leftPt  : rightPt;
        f.trailHand = leftLeads ? rightPt : leftPt;
        f.handConf  = handConf;

        if (f.conf[kLeftWrist] > 0.3f && f.conf[kRightWrist] > 0.3f)
            ++wristOk;
        track.frames.push_back(std::move(f));
    }

    ppInfo() << "[PoseRunner] source" << faceOnSource << ":" << track.frames.size()
             << "posed of" << sampled << "sampled (" << (i1 - i0) << "in span +"
             << (i0 - iAddr0) << "address-hold," << entries.size() << "in window)," << wristOk
             << "with both wrists conf > 0.3," << wall.elapsed() << "ms";
    return track;
}

#else // !(HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME)

PoseTrack2D PoseRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const ShotAnalysisRunnerOptions &opt)
{
    Q_UNUSED(window)
    Q_UNUSED(opt)
    ppWarn() << "[PoseRunner] built without ViTPose/ONNX Runtime — empty track";
    PoseTrack2D track;
    track.camera = faceOnSource;
    return track;
}

#endif // HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME


#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

pinpoint::analysis::PoseTrack2D PoseRunner::loadFromJson(const QString &file,
                                                         pinpoint::SourceId camera)
{
    using namespace pinpoint::analysis;
    PoseTrack2D track;
    track.camera = camera;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return track;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray frames = (root.contains(QStringLiteral("frames"))
                                   ? root[QStringLiteral("frames")]
                                   : root[QStringLiteral("pose2d")]
                                         .toObject()[QStringLiteral("frames")]).toArray();
    for (const QJsonValue &fv : frames) {
        const QJsonObject o = fv.toObject();
        PoseFrame2D pf;
        pf.t_us = int64_t(o[QStringLiteral("t_us")].toDouble());
        const QJsonArray kp = o[QStringLiteral("kp")].toArray();
        for (int j = 0; j < 17 && j * 3 + 2 < kp.size(); ++j) {
            pf.kp[size_t(j)]   = QPointF(kp[j * 3].toDouble(), kp[j * 3 + 1].toDouble());
            pf.conf[size_t(j)] = float(kp[j * 3 + 2].toDouble());
        }
        const QJsonArray lead = o[QStringLiteral("lead")].toArray();
        const QJsonArray trail = o[QStringLiteral("trail")].toArray();
        if (lead.size() == 2)  pf.leadHand  = QPointF(lead[0].toDouble(), lead[1].toDouble());
        if (trail.size() == 2) pf.trailHand = QPointF(trail[0].toDouble(), trail[1].toDouble());
        pf.handConf = float(o[QStringLiteral("handConf")].toDouble());
        track.frames.push_back(std::move(pf));
    }
    return track;
}
