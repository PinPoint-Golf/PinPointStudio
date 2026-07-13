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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

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
#include "../Pose/pose_model_selection.h"   // useVitPoseLarge (tier -> model)
#include "shaft_track_assembly.h"   // estimateSwingSpanUs / ShaftV3Config (Stage B span estimate)
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
    //
    // Tier -> model: "High" runs ViTPose++-L when the user has downloaded it,
    // otherwise ViTPose-B (Low/Medium always B). The choice degrades safely to B
    // whenever the L model is absent — including on machines that never fetched it.
    using ViTVariant = PoseEstimatorViTPose::ModelVariant;
    const bool useLarge = pinpoint::pose::useVitPoseLarge(
        opt.motionCaptureQuality,
        PoseEstimatorViTPose::isVariantAvailable(ViTVariant::WholeBodyLarge));
    PoseEstimatorViTPose estimator(useLarge ? ViTVariant::WholeBodyLarge
                                            : ViTVariant::WholeBodyB);
    estimator.load();
    if (!estimator.isReady()) {
        ppWarn() << "[PoseRunner] ViTPose unavailable (model missing or load failed) "
                    "— empty track";
        return track;
    }
    estimator.setDecodeHands(true);

    // inferPrepared() is synchronous and emits poseEstimated() inline on this
    // (consumer) thread — a direct connection captures the result before it
    // returns.
    PoseResult res;
    bool gotPose = false;
    QObject::connect(&estimator, &PoseEstimatorBase::poseEstimated, &estimator,
                     [&res, &gotPose](const PoseResult &r) {
                         res     = r;
                         gotPose = true;
                     },
                     Qt::DirectConnection);

    // Lead = left hand for right-handed (and unknown) golfers, right for left-handed.
    const bool leftLeads = (opt.handedness != 2);
    constexpr int   kLeftWrist    = 9;    // PoseJoint::LeftWrist
    constexpr int   kRightWrist   = 10;   // PoseJoint::RightWrist
    constexpr float kMinHandConf  = 0.3f;

    size_t  wristOk = 0;

    // ── Pipelined pose ──────────────────────────────────────────────────────────
    // decode + preprocess run on a producer thread while ORT inference runs on
    // this (consumer) thread — the ~25 ms decode/preprocess of frame N+1 hides
    // behind the ~82 ms inference of frame N. Output is byte-identical to the
    // old serial poseOne(): same frames, same order (a FIFO preserves it), same
    // math — only the execution overlaps.
    //
    // Frozen-window read contract: SwingPayloadSource keeps ONE frame resident
    // per source, so the producer is the SOLE payloadOf() caller and must fully
    // consume each frame BEFORE the next fetch. preprocess() reads the whole
    // decoded frame into an owned NCHW float buffer, so even the zero-copy BGR24
    // passthrough alias (decodeToBgr may hand back a Mat aliasing the payload
    // bytes) is safe — the alias never outlives the fetch.
    struct PipeItem {
        int64_t            t_us     = 0;
        float              progress = 0.f;
        bool               decodeOk = false;
        std::vector<float> input;   // NCHW tensor buffer (empty when decode failed)
    };

    // Run one materialized (entryIndex, progress) sequence through the pipeline,
    // appending posed frames to `out` in sequence order. Decode/inference
    // failures skip exactly as the old poseOne() did (decode fail → no
    // inference; a frame ViTPose can't estimate is dropped). progress() is
    // invoked on THIS worker thread for every sequence entry, matching the old
    // serial loops (it is never called on the producer thread).
    auto runPipeline = [&](const std::vector<std::pair<size_t, float>> &jobs,
                           PoseTrack2D &out) {
        if (jobs.empty())
            return;

        std::mutex              mtx;
        std::condition_variable cvNotFull, cvNotEmpty;
        std::deque<PipeItem>    queue;
        bool                    producerDone = false;
        bool                    stop         = false;   // consumer → producer abort
        constexpr size_t        kMaxDepth    = 3;

        std::thread producer([&] {
            for (const auto &job : jobs) {
                {
                    std::unique_lock<std::mutex> lk(mtx);
                    cvNotFull.wait(lk, [&] { return queue.size() < kMaxDepth || stop; });
                    if (stop)
                        return;
                }
                PipeItem item;
                item.t_us     = entries[job.first].timestamp_us;
                item.progress = job.second;
                cv::Mat frameBgr;   // fresh per frame — decodeToBgr may alias the payload
                const pinpoint::SourceRing::ReadHandle handle =
                    window.payloadOf(entries[job.first]);
                if (pinpoint::decodeToBgr(*cfmt, handle.data, handle.bytes, frameBgr)) {
                    // Fully consumes frameBgr into an owned buffer before the next
                    // payloadOf() invalidates the resident frame.
                    estimator.preprocess(frameBgr, item.input);
                    item.decodeOk = true;
                }
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    queue.push_back(std::move(item));
                }
                cvNotEmpty.notify_one();
            }
            {
                std::lock_guard<std::mutex> lk(mtx);
                producerDone = true;
            }
            cvNotEmpty.notify_one();
        });

        // Join the producer on EVERY exit from this scope (normal drain or an
        // exception from the consumer body) — no detached thread, no deadlock
        // with a producer parked on a full queue.
        struct Joiner {
            std::thread             &t;
            std::mutex              &m;
            std::condition_variable &cond;
            bool                    &stop;
            ~Joiner() {
                {
                    std::lock_guard<std::mutex> lk(m);
                    stop = true;
                }
                cond.notify_all();
                if (t.joinable())
                    t.join();
            }
        } joiner{producer, mtx, cvNotFull, stop};

        for (;;) {
            PipeItem item;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cvNotEmpty.wait(lk, [&] { return !queue.empty() || producerDone; });
                if (queue.empty())          // predicate guarantees producerDone here
                    break;
                item = std::move(queue.front());
                queue.pop_front();
            }
            cvNotFull.notify_one();

            if (opt.progress)
                opt.progress(item.progress);
            if (!item.decodeOk)
                continue;

            gotPose = false;
            estimator.inferPrepared(item.input);
            if (!gotPose)
                continue;

            PoseFrame2D f;
            f.t_us = item.t_us;
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
            out.frames.push_back(std::move(f));
        }
    };

    // Dense zone around impact (both scan paths share it): pose every
    // denseStride-th frame here, every sparseStride-th elsewhere in span.
    const int64_t denseLo = opt.impactUs - static_cast<int64_t>(opt.densePreMs)  * 1000;
    const int64_t denseHi = opt.impactUs + static_cast<int64_t>(opt.densePostMs) * 1000;

    // ── Two-pass pose (swing_span_bounding_plan.md §5) ──────────────────────────
    // Engaged only with no externally-supplied span (an IMU/G3 bound always
    // wins). Pass 1 poses a coarse full-window grid; estimateSwingSpanUs() over
    // that grip track yields [onset, finish]; pass 2 fills the span only. The
    // coarse frames stay in the track as address-hold coverage (subsumes
    // addressScanPadUs here). Progress: pass 1 → [0, 0.2], pass 2 → [0.2, 1.0].
    if (opt.twoPass && opt.scanEndUs <= opt.scanStartUs) {
        const size_t coarse = static_cast<size_t>(std::max(1, opt.coarseStride));

        // Pass 1 — coarse, whole window.
        std::vector<std::pair<size_t, float>> jobs1;
        jobs1.reserve(entries.size() / coarse + 1);
        for (size_t i = 0; i < entries.size(); i += coarse)
            jobs1.emplace_back(i, 0.2f * float(i + 1) / float(entries.size()));
        runPipeline(jobs1, track);
        const size_t pass1Posed = track.frames.size();

        // Coarse grip track in PIXELS (leadHand/trailHand are normalized [0,1])
        // + coarse frame rate (median inter-frame dt) for the span estimate.
        std::vector<double>  gx, gy;
        std::vector<int64_t> tUs;
        gx.reserve(pass1Posed); gy.reserve(pass1Posed); tUs.reserve(pass1Posed);
        const double frameW = double(cfmt->width), frameH = double(cfmt->height);
        for (const PoseFrame2D &f : track.frames) {
            gx.push_back(0.5 * (f.leadHand.x() + f.trailHand.x()) * frameW);
            gy.push_back(0.5 * (f.leadHand.y() + f.trailHand.y()) * frameH);
            tUs.push_back(f.t_us);
        }
        double fps = 0.0;
        if (tUs.size() >= 2) {
            std::vector<int64_t> dts;
            dts.reserve(tUs.size() - 1);
            for (size_t k = 1; k < tUs.size(); ++k)
                dts.push_back(tUs[k] - tUs[k - 1]);
            std::nth_element(dts.begin(), dts.begin() + dts.size() / 2, dts.end());
            const int64_t medDt = dts[dts.size() / 2];
            if (medDt > 0)
                fps = 1e6 / double(medDt);
        }
        const pinpoint::analysis::SwingSpanEstimate est =
            fps > 0.0 ? pinpoint::analysis::estimateSwingSpanUs(
                            gx, gy, tUs, fps, opt.impactUs,
                            pinpoint::analysis::ShaftV3Config{})
                      : pinpoint::analysis::SwingSpanEstimate{};

        // Pass 2 — fill. A good span scans only [onset − 150 ms, finish + 150 ms]
        // (§5's pre-pad for pass-1 coarse-onset error); the degenerate no-run
        // path falls back to a full-window single pass (log + degrade the
        // optimisation, never the result). Both reuse the pass-1 frames (skip
        // i % coarse == 0 — coarse frames are a subset of the sparse grid, and a
        // frame ViTPose failed in pass 1 fails identically here).
        constexpr int64_t kSpanPadUs = 150000;
        size_t pLo = 0, pHi = entries.size();
        if (est.ok) {
            const int64_t spanLo = est.startUs - kSpanPadUs;
            const int64_t spanHi = est.endUs   + kSpanPadUs;
            while (pLo < entries.size() && entries[pLo].timestamp_us < spanLo)
                ++pLo;
            while (pHi > pLo && entries[pHi - 1].timestamp_us > spanHi)
                --pHi;
        } else {
            ppWarn() << "[PoseRunner] two-pass: no swing span from the coarse pass "
                        "— falling back to a full-window single pass";
        }

        const size_t denseStep  = static_cast<size_t>(std::max(1, opt.denseStride));
        const size_t sparseStep = static_cast<size_t>(std::max(1, opt.sparseStride));
        const size_t span = pHi > pLo ? pHi - pLo : 1;
        std::vector<std::pair<size_t, float>> jobs2;
        jobs2.reserve(span);
        for (size_t i = pLo; i < pHi; ++i) {
            if ((i % coarse) == 0)   // posed in pass 1 — never re-pose a timestamp
                continue;
            const pinpoint::IndexEntry &e = entries[i];
            const bool dense = opt.impactUs >= 0
                            && e.timestamp_us >= denseLo && e.timestamp_us <= denseHi;
            if ((i % (dense ? denseStep : sparseStep)) != 0)
                continue;
            jobs2.emplace_back(i, 0.2f + 0.8f * float(i + 1 - pLo) / float(span));
        }
        runPipeline(jobs2, track);
        const size_t pass2Posed = track.frames.size() - pass1Posed;

        // Pass-1 (whole window) and pass-2 (span fill) interleave in time — merge.
        std::sort(track.frames.begin(), track.frames.end(),
                  [](const PoseFrame2D &a, const PoseFrame2D &b) { return a.t_us < b.t_us; });

        ppInfo() << "[PoseRunner] source" << faceOnSource << ": two-pass"
                 << track.frames.size() << "posed (" << pass1Posed << "coarse pass-1 +"
                 << pass2Posed << (est.ok ? "span-bounded pass-2)," : "full-window pass-2 fallback),")
                 << entries.size() << "in window," << wristOk
                 << "with both wrists conf > 0.3," << wall.elapsed() << "ms";
        return track;
    }

    // ── Single pass (today's behaviour) ─────────────────────────────────────────
    const int stride = std::max(1, opt.sparseStride);

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

    const int addrStride = std::max(1, opt.addressStride);

    track.frames.reserve(i1 - iAddr0);
    std::vector<std::pair<size_t, float>> jobs;
    jobs.reserve(i1 - iAddr0);
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
        jobs.emplace_back(i, float(i + 1 - iAddr0) / float(i1 - iAddr0));
    }
    const size_t sampled = jobs.size();
    runPipeline(jobs, track);

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
