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
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <QElapsedTimer>
#include <QFileInfo>
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
#include "pose_crop.h"             // PoseAccuracyConfig / computePoseCropRect (WB1)
#include "hand_axis.h"             // handCentroid / HandCentroid (shared with the smoothed-grip recompute)
#include "shaft_track_assembly.h"   // estimateSwingSpanUs / ShaftV3Config (Stage B span estimate)
#include "analysis_tuning.h"        // tuning::apply — pose.intraOpThreads resolution
#include "pose_pipeline.h"          // InstanceCache (session cache) + runOrderedPipeline (producer pool)
#include "../Core/pp_tuned_constants.h"   // pose::kSessionCache / kProducerThreads / kQueueDepth
#include "../Core/cpu_topology.h"   // physicalCoreCount() — pose.producerThreads auto
#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#endif

// lastTiming()'s store: one per thread (see pose_runner.h), overwritten by every run().
static thread_local pinpoint::pose::PoseTiming tl_lastPoseTiming;

pinpoint::pose::PoseTiming PoseRunner::lastTiming() { return tl_lastPoseTiming; }

#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)

namespace {

// handCentroid / HandCentroid are factored into hand_axis.h (WB4) — the exact
// same score-weighted-centroid math, now shared with the smoothed-hands grip
// recompute (pose.gripFromSmoothedHands). This raw path is byte-identical.

// Union bbox of the body keypoints (COCO 0–16) over the sampled frames, in
// normalized [0,1] coords — the WB1 swing-level person crop's source (design
// §3.2). A frame "contributes" when at least one body joint clears the score
// gate; `frames` is that contributing count (the crop fallback needs ≥ N).
struct BodyBboxAccum {
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    int    frames = 0;

    void addPoint(double x, double y) {
        x0 = std::min(x0, x); y0 = std::min(y0, y);
        x1 = std::max(x1, x); y1 = std::max(y1, y);
    }
    // From a stored PoseFrame2D (pass-1 whole-window frames stay in the track).
    void add(const std::array<QPointF, pinpoint::analysis::kWholeBodyJoints> &kp,
             const std::array<float, pinpoint::analysis::kWholeBodyJoints> &conf, float thr) {
        bool any = false;
        for (int j = 0; j < PoseResult::kNumKeypoints; ++j)
            if (conf[size_t(j)] >= thr) { any = true; addPoint(kp[size_t(j)].x(), kp[size_t(j)].y()); }
        if (any) ++frames;
    }
    // From a live PoseResult (mini-scan frames, discarded after bbox use).
    void addResult(const PoseResult &r, float thr) {
        bool any = false;
        for (int j = 0; j < PoseResult::kNumKeypoints; ++j)
            if (r.keypoints[j].score >= thr) { any = true; addPoint(r.keypoints[j].x, r.keypoints[j].y); }
        if (any) ++frames;
    }
};

// ── Step 1: one ViTPose session per process per model ───────────────────────
// Key: (model file, the build's EP cascade, the pose.intraOpThreads REQUEST).
// The provider load() ends up on is a function of the build flags and the host,
// so within one process the cascade tag stands for it; step 3's EP options (fp16,
// TensorRT) belong in that slot. The intra-op request (0 / -1 / n) resolves the
// same way every time, so it keys as well as the resolved count.
using EstimatorKey = std::tuple<std::string, std::string, int>;
using EstimatorCache = pinpoint::analysis::InstanceCache<EstimatorKey, PoseEstimatorViTPose>;

const char *epCascadeTag()
{
    return ""
#ifdef WITH_COREML
        "coreml>"
#endif
#ifdef WITH_CUDA
        "cuda>"
#endif
#ifdef WITH_DIRECTML
        "dml>"
#endif
        "cpu";
}

// Never destroyed, deliberately: the cached sessions live until the process
// exits, and tearing ORT sessions (CoreML/CUDA EPs) and QObjects down during
// static destruction — after QCoreApplication and possibly ORT's own statics are
// gone — buys nothing but an exit-time crash risk. The OS reclaims it.
EstimatorCache &estimatorCache()
{
    static EstimatorCache *cache = new EstimatorCache;
    return *cache;
}

PoseEstimatorViTPose::LoadOptions loadOptionsFor(bool fp16, bool coremlProgram, bool tensorrt,
                                                 bool staticBatch, int batchSize, bool logPartition)
{
    PoseEstimatorViTPose::LoadOptions o;
    o.fp16          = fp16;
    o.coremlProgram = coremlProgram;
    o.tensorrt      = tensorrt;
    o.staticBatch   = staticBatch ? batchSize : 0;
    o.maxBatch      = batchSize;
    o.logPartition  = logPartition;
    return o;
}

// The step-3 load options (precision, CoreML format, TensorRT, static batch) make a
// different session: they key it alongside the cascade.
EstimatorKey estimatorKey(PoseEstimatorViTPose::ModelVariant variant,
                          const PoseEstimatorViTPose::LoadOptions &loadOpts, int intraOpThreads)
{
    return EstimatorKey{ PoseEstimatorViTPose::modelPath(variant).toStdString(),
                         std::string(epCascadeTag()) + "|"
                             + PoseEstimatorViTPose::loadOptionsTag(loadOpts).toStdString(),
                         intraOpThreads };
}

} // namespace

PoseTrack2D PoseRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const ShotAnalysisRunnerOptions &opt)
{
    // Timing split (perf plan step 0): the estimator and the decode sites below
    // write into timingSink; on EVERY exit from run() the snapshot plus the wall
    // total becomes lastTiming() — an early-out publishes zeros, never the
    // previous camera's numbers.
    pinpoint::pose::PoseTimingSink timingSink;
    struct TimingPublisher {
        pinpoint::pose::PoseTimingSink &sink;
        QElapsedTimer                   wall;
        ~TimingPublisher() {
            pinpoint::pose::PoseTiming t = sink.snapshot();
            t.totalMs = double(wall.nsecsElapsed()) / 1e6;
            tl_lastPoseTiming = t;
        }
    } timingPublisher{ timingSink, {} };
    timingPublisher.wall.start();

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

    // ViTPose is offline-only; load() sizes its ORT intra-op pool to the core
    // count, and the post-shot pipeline sequences the x264 export AFTER this pose
    // pass (ShotProcessor::onAnalysisFinished), so the inference is no longer
    // starved by the encoder's threads (which inflated per-frame inference ~5×).
    //
    // Tier -> model: "High" runs ViTPose++-L when the user has downloaded it,
    // otherwise ViTPose-B (Medium always B). The choice degrades safely to B
    // whenever the L model is absent — including on machines that never fetched it.
    using ViTVariant = PoseEstimatorViTPose::ModelVariant;
    const bool useLarge = pinpoint::pose::useVitPoseLarge(
        opt.motionCaptureQuality,
        PoseEstimatorViTPose::isVariantAvailable(ViTVariant::WholeBodyLarge));
    const ViTVariant variant = useLarge ? ViTVariant::WholeBodyLarge : ViTVariant::WholeBodyB;
    // A High swing on B is a different pose from the one it was captured for — say so (once per
    // process). Silent, it cost a day on 2026-09-29: the studio's tools ran High swings on B unseen.
    if (!useLarge && opt.motionCaptureQuality.compare(QLatin1String("High"), Qt::CaseInsensitive) == 0) {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true)) {
            ppWarn() << "[PoseRunner] High quality swing but ViTPose-L is not at"
                     << PoseEstimatorViTPose::modelPath(ViTVariant::WholeBodyLarge)
                     << "— running ViTPose-B";
        }
    }

    // Offline ORT intra-op pool size (pose.intraOpThreads). Seed from the option
    // (default 0 = legacy heuristic) and let the override map win — resolved here
    // and applied BEFORE load() sizes the pool. 0 keeps today's
    // clamp(hardware_concurrency()/2, 1, 8); -1 opts into the physical-core
    // topology auto; > 0 pins. Empty overrides ⇒ thread-count-identical to history.
    int intraOpThreads = opt.intraOpThreads;
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.intraOpThreads", intraOpThreads);

    // Throughput knobs (perf plan steps 1–2; pp_tuned_constants.h pose::). None
    // changes a keypoint — OFF = sessionCache false, producerThreads 1, queueDepth 3.
    bool sessionCache   = pinpoint::tuned::pose::kSessionCache;
    int  producerThreads = pinpoint::tuned::pose::kProducerThreads;
    int  queueDepth      = pinpoint::tuned::pose::kQueueDepth;
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.sessionCache",    sessionCache);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.producerThreads", producerThreads);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.queueDepth",      queueDepth);
    if (producerThreads <= 0)
        producerThreads = std::min(4, std::max(1, pinpoint::physicalCoreCount() / 2));
    queueDepth = std::max(1, queueDepth);

    // Inference proper (perf plan step 3; pp_tuned_constants.h pose::). All OFF by
    // default; OFF = one frame per Run() through inferPrepared(), all 133 channels on
    // this thread, the fp32 model, CoreML NeuralNetwork / CUDA — today's path.
    namespace tp = pinpoint::tuned::pose;
    int  batchSize     = tp::kBatchSize;
    bool staticBatch   = tp::kStaticBatch;
    bool ioBinding     = tp::kIoBinding;
    int  decodeThreads = tp::kDecodeThreads;
    bool tensorrt      = tp::kTensorRT;
    bool logPartition  = tp::kLogPartition;
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.batchSize",     batchSize);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.staticBatch",   staticBatch);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.ioBinding",     ioBinding);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.decodeThreads", decodeThreads);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.tensorrt",      tensorrt);
    pinpoint::analysis::tuning::apply(opt.tuningOverrides, "pose.logPartition",  logPartition);
    // The three string-valued keys: an unrecognised value keeps the default (said once).
    auto strKey = [&](const char *key, bool dflt, const char *offVal, const char *onVal) {
        const auto it = opt.tuningOverrides.constFind(QLatin1String(key));
        if (it == opt.tuningOverrides.cend())
            return dflt;
        const QString v = it->toString();
        if (v.compare(QLatin1String(onVal), Qt::CaseInsensitive) == 0)  return true;
        if (v.compare(QLatin1String(offVal), Qt::CaseInsensitive) == 0) return false;
        ppWarn() << "[PoseRunner]" << key << "=" << v << "is not" << offVal << "or" << onVal
                 << "— keeping" << (dflt ? onVal : offVal);
        return dflt;
    };
    const bool bodyHandsOnly = strKey("pose.decodeChannels", tp::kDecodeBodyHands, "all", "bodyHands");
    const bool modelFp16     = strKey("pose.modelPrecision", tp::kModelFp16, "fp32", "fp16");
    const bool coremlProgram = strKey("pose.coreml", tp::kCoreMLProgram, "nn", "mlprogram");
    batchSize = std::clamp(batchSize, 1, 64);
    if (decodeThreads <= 0)
        decodeThreads = std::min(4, std::max(1, pinpoint::physicalCoreCount() / 2));
    const pinpoint::pose::ChannelSet decodeSet =
        bodyHandsOnly ? pinpoint::pose::ChannelSet::BodyHands : pinpoint::pose::ChannelSet::All;
    // The batched path carries every step-3 runtime option; with all of them off the
    // legacy inferPrepared() runs, untouched. (A static batch dim forces it even at
    // batch 1: inferPrepared() feeds [1,…], which a fixed batch of B would refuse.)
    const bool batchedPath = batchSize > 1 || staticBatch || ioBinding || bodyHandsOnly
                          || decodeThreads > 1;
    // B frames gathered while B more are prepared: the window must hold two batches
    // or the producers stall behind the consumer's Run().
    if (batchSize > 1)
        queueDepth = std::max(queueDepth, 2 * batchSize);

    const PoseEstimatorViTPose::LoadOptions loadOpts =
        loadOptionsFor(modelFp16, coremlProgram, tensorrt, staticBatch, batchSize, logPartition);

    // Step 1 — the estimator (and its ORT session) comes from the process-wide
    // cache: built on the first run() for this model, reused by every run() after
    // it — both cameras, every shot, every swinglab stage. Before, each run()
    // built its own: a 360 MB model load + EP init/compile, twice a shot. The
    // lease holds the entry for this whole run (InstanceCache's one-thread rule),
    // so a concurrent run() on the same model waits rather than sharing the
    // session and this run's per-run state (decode mode, whole-body flag, result
    // slot, timing sink, the poseEstimated hook below). sessionCache = false
    // builds a private estimator exactly as before.
    auto makeEstimator = [&]() {
        auto e = std::make_unique<PoseEstimatorViTPose>(variant);
        e->setIntraOpThreads(intraOpThreads);
        e->setLoadOptions(loadOpts);
        e->setTimingSink(&timingSink);   // the build lands in THIS run's sessionBuildMs
        e->load();
        return e;
    };
    EstimatorCache::Lease                 lease;
    std::unique_ptr<PoseEstimatorViTPose> privateEstimator;
    if (sessionCache) {
        // The step-3 load options (precision, CoreML format, TensorRT, static batch)
        // make a different session: they key it alongside the cascade.
        const EstimatorKey key = estimatorKey(variant, loadOpts, intraOpThreads);
        lease = estimatorCache().acquire(key, makeEstimator,
                                         [](const PoseEstimatorViTPose &e) { return e.isReady(); });
    } else {
        privateEstimator = makeEstimator();
    }
    PoseEstimatorViTPose &estimator = sessionCache ? *lease : *privateEstimator;
    estimator.setTimingSink(&timingSink);   // session build, preprocess, Run(), heatmaps, frames
    // The sink is this run's stack object — a cached estimator must not keep
    // pointing at it. Declared after the lease, so it runs before the lease
    // releases the entry.
    struct SinkDetach {
        PoseEstimatorViTPose &e;
        ~SinkDetach() { e.setTimingSink(nullptr); }
    } sinkDetach{ estimator };
    if (!estimator.isReady()) {
        ppWarn() << "[PoseRunner] ViTPose unavailable (model missing or load failed) "
                    "— empty track";
        return track;
    }
    estimator.setDecodeWholeBody(true);
    // The step-3 settings this run actually got, for the summary lines below.
    const QString step3 = QStringLiteral("%1 %2, batch %3%4, decode %5 × %6 thread(s)%7")
        .arg(estimator.executionProvider(),
             QFileInfo(estimator.resolvedModelFile()).fileName())
        .arg(batchedPath ? batchSize : 1)
        .arg(QLatin1String(staticBatch ? " static" : ""),
             QLatin1String(bodyHandsOnly ? "bodyHands" : "all"))
        .arg(batchedPath ? decodeThreads : 1)
        .arg(QLatin1String(ioBinding ? ", IO binding" : ""));

    // WB1 accuracy pass (wholebody_pose_design.md §3): DARK sub-pixel decode + a
    // swing-level person crop, both from the tuning map (frozen defaults ON). The
    // decode mode is set once and applies to every pass; the crop rect is computed
    // after a bbox scan and passed per-pass. pose.crop.enabled=false AND
    // pose.decode.dark=false reproduce the pre-WB1 full-frame + argmax pipeline
    // byte-for-byte. Set on EVERY run — a cached estimator carries the last run's.
    const pinpoint::analysis::PoseAccuracyConfig acc =
        pinpoint::analysis::PoseAccuracyConfig::fromOverrides(opt.tuningOverrides);
    estimator.setDecodeMode(acc.decodeDark ? PoseEstimatorViTPose::DecodeMode::Dark
                                           : PoseEstimatorViTPose::DecodeMode::Argmax);
    track.decode = acc.decodeDark ? QStringLiteral("dark") : QStringLiteral("argmax");

    // inferPrepared() is synchronous and emits poseEstimated() inline on this
    // (consumer) thread — a direct connection captures the result before it
    // returns. The hook captures this run's locals, so it is cut on every exit:
    // a cached estimator outlives the run.
    PoseResult res;
    bool gotPose = false;
    const QMetaObject::Connection poseHook =
        QObject::connect(&estimator, &PoseEstimatorBase::poseEstimated, &estimator,
                         [&res, &gotPose](const PoseResult &r) {
                             res     = r;
                             gotPose = true;
                         },
                         Qt::DirectConnection);
    struct HookCut {
        const QMetaObject::Connection &c;
        ~HookCut() { QObject::disconnect(c); }
    } hookCut{ poseHook };

    // Lead = left hand for right-handed (and unknown) golfers, right for left-handed.
    const bool leftLeads = (opt.handedness != 2);
    constexpr int   kLeftWrist    = 9;    // PoseJoint::LeftWrist
    constexpr int   kRightWrist   = 10;   // PoseJoint::RightWrist
    constexpr float kMinHandConf  = 0.3f;

    size_t  wristOk = 0;

    // ── Pipelined pose (perf plan step 2: a producer pool) ──────────────────────
    // decode + preprocess run on producer threads while ORT inference runs on
    // this (consumer) thread. One producer was the bottleneck once inference got
    // fast: a 688×1024 BayerRG8 frame is ~5–8 ms of edge-aware demosaic + resize
    // + normalise, against a Run() of a few ms on CoreML/CUDA. pose.producerThreads
    // producers (auto min(4, physical cores / 2)) now each prepare their own
    // frames; runOrderedPipeline hands them to inference strictly in job order
    // through a pose.queueDepth (8) window. Output is byte-identical to the old
    // single producer: same frames, same order, same math — only who does the CPU
    // work and how many at once. producerThreads 1 + queueDepth 3 IS the old
    // pipeline, schedule included.
    //
    // Frozen-window read contract: SwingPayloadSource keeps ONE frame resident
    // per source, and the MP4 re-analysis reader decodes forward (rewinding to
    // frame 0 on any back-seek), so payloadOf() is called by ONE producer at a
    // time, in job order (the pipeline's serialised fetch stage), and each frame
    // is fully consumed before the next fetch:
    //   1 producer  — the fetch stage does everything, as before: decode (the
    //                 BGR24 passthrough may alias the payload) + preprocess into
    //                 an owned NCHW buffer before the next fetch.
    //   N producers — the fetch stage copies the payload bytes out (a ~0.7 MB
    //                 Bayer frame, ~2 MB BGR24 from the MP4 reader: tens of µs),
    //                 and demosaic + preprocess run on the copy in parallel. On
    //                 re-analysis the H.264 decode itself stays in the serial
    //                 fetch; only what follows it fans out.
    struct PipeItem {
        int64_t            t_us     = 0;
        float              progress = 0.f;
        bool               decodeOk = false;
        std::vector<float> input;   // NCHW tensor buffer (empty when decode failed)
    };
    struct Fetched {
        PipeItem               item;
        bool                   prepared = false;   // 1 producer: item is final
        bool                   rawNull  = true;    // payloadOf() returned no bytes
        std::vector<std::byte> raw;                // N producers: owned payload copy
    };
    const bool serialFetch = producerThreads <= 1;

    // Run one materialized (entryIndex, progress) sequence through the pipeline,
    // appending posed frames to `out` in sequence order. Decode/inference
    // failures skip exactly as the old poseOne() did (decode fail → no
    // inference; a frame ViTPose can't estimate is dropped). progress() is
    // invoked on THIS worker thread for every sequence entry, matching the old
    // serial loops (it is never called on a producer thread).
    // cropRoi (nullptr = full frame) is the swing-level person crop applied to
    // every frame in this pass; the consumer back-projects the estimator's
    // crop-normalized peaks to full-frame normalized through the (constant) affine.
    auto runPipeline = [&](const std::vector<std::pair<size_t, float>> &jobs,
                           PoseTrack2D &out, const cv::Rect *cropRoi) {
        if (jobs.empty())
            return;

        const double fw = double(cfmt->width), fh = double(cfmt->height);

        // payload bytes → BGR → owned NCHW tensor. preprocess() is const and
        // touches no estimator state (the timing sink is mutex-guarded), so N
        // producers may run it at once alongside inferPrepared().
        auto prepare = [&](const std::byte *data, size_t bytes, PipeItem &item) {
            cv::Mat frameBgr;   // fresh per frame — decodeToBgr may alias the payload
            bool decoded;
            {
                // Demosaic → BGR (perf plan F3). With N producers this is summed
                // across threads, so decodeMs can exceed the run's wall time.
                pinpoint::pose::PoseScopedTimer decodeTimer(
                    &timingSink, &pinpoint::pose::PoseTiming::decodeMs);
                decoded = pinpoint::decodeToBgr(*cfmt, data, bytes, frameBgr);
            }
            if (decoded) {
                // The crop is a plain ROI view (no copy) — mathematically
                // equivalent to a warpAffine given the aspect-locked rect (design §3.2).
                if (cropRoi)
                    estimator.preprocess(frameBgr(*cropRoi), item.input);
                else
                    estimator.preprocess(frameBgr, item.input);
                item.decodeOk = true;
            }
        };

        const std::function<Fetched(size_t)> fetch = [&](size_t k) {
            Fetched f;
            f.item.t_us     = entries[jobs[k].first].timestamp_us;
            f.item.progress = jobs[k].second;
            pinpoint::SourceRing::ReadHandle handle;
            {
                // The payload fetch is decode time too: on re-analysis without a
                // raw sidecar it IS the H.264 decode (Mp4FrameReader, incl. its
                // rewind on a back-seek).
                pinpoint::pose::PoseScopedTimer fetchTimer(
                    &timingSink, &pinpoint::pose::PoseTiming::decodeMs);
                handle = window.payloadOf(entries[jobs[k].first]);
                if (!serialFetch && handle.data) {
                    f.rawNull = false;
                    f.raw.assign(handle.data, handle.data + handle.bytes);
                }
            }
            if (serialFetch) {   // consume the resident frame before the next fetch
                prepare(handle.data, handle.bytes, f.item);
                f.prepared = true;
            }
            return f;
        };
        const std::function<PipeItem(size_t, Fetched &&)> process = [&](size_t, Fetched &&f) {
            if (!f.prepared)
                prepare(f.rawNull ? nullptr : f.raw.data(), f.raw.size(), f.item);
            return std::move(f.item);
        };

        // One inferred frame → the track: body from the PoseResult, the tail from
        // the whole-body decode, the crop back-projection, the hand anchors. Shared
        // by the one-frame path and the batched one (perf plan step 3).
        auto appendPosed = [&](int64_t t_us, const PoseResult &pr, const WholeBodyResult &wb) {
            PoseFrame2D f;
            f.t_us = t_us;
            // Body 0–16 from the emitted PoseResult — the pre-wholebody source,
            // kept verbatim so the first-17 outputs are byte-identical by
            // construction (WholeBodyResult.kp[0..16] are copies of the same
            // decode, but `res` is the contract the old code read).
            for (int j = 0; j < PoseResult::kNumKeypoints; ++j) {
                f.kp[j]   = QPointF(pr.keypoints[j].x, pr.keypoints[j].y);
                f.conf[j] = pr.keypoints[j].score;
            }
            // Feet/face/hand tail (17–132) from the whole-body decode.
            if (wb.valid) {
                for (int j = PoseResult::kNumKeypoints;
                     j < pinpoint::analysis::kWholeBodyJoints; ++j) {
                    f.kp[size_t(j)]   = wb.kp[size_t(j)];
                    f.conf[size_t(j)] = wb.score[size_t(j)];
                }
            }

            // Back-project all 133 crop-normalized peaks to full-frame normalized
            // through the (constant) crop affine: x_full = (cropX + x·cropW)/frameW.
            // No-op — byte-identical — on the full-frame path (cropRoi == nullptr).
            // Transform the keypoints FIRST, then derive the hand centroids from
            // them (affine ⇒ transforming points then averaging == averaging then
            // transforming), so every persisted value stays full-frame normalized.
            if (cropRoi) {
                const double cx = cropRoi->x, cy = cropRoi->y;
                const double cw = cropRoi->width, ch = cropRoi->height;
                for (int j = 0; j < pinpoint::analysis::kWholeBodyJoints; ++j)
                    f.kp[size_t(j)] = QPointF((cx + f.kp[size_t(j)].x() * cw) / fw,
                                              (cy + f.kp[size_t(j)].y() * ch) / fh);
            }

            // Hand anchors: score-weighted knuckle centroids; fall back to the
            // COCO wrists (with handConf = 0) when either hand is unconvincing —
            // both anchors must come from the same source or the inter-hand
            // direction prior d̂ = trail − lead is meaningless. Read the (now
            // back-projected) 21-point hand ranges from f (channels 91–111 /
            // 112–132) — byte-identical to the retired wb read on the full-frame
            // path (f.kp/f.conf tail == wb.kp/wb.score there).
            pinpoint::analysis::HandCentroid lc, rc;
            if (wb.valid) {
                using pinpoint::analysis::kLeftHandFirstKp;
                using pinpoint::analysis::kRightHandFirstKp;
                lc = pinpoint::analysis::handCentroid(&f.kp[kLeftHandFirstKp],  &f.conf[kLeftHandFirstKp]);
                rc = pinpoint::analysis::handCentroid(&f.kp[kRightHandFirstKp], &f.conf[kRightHandFirstKp]);
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
        };

        // Batched path (perf plan step 3): frames gathered in job order, B per
        // Run(), results appended in the same order — so the track's frame order is
        // the one-frame path's. Progress is reported as each frame is gathered.
        pinpoint::analysis::BatchGatherer<PipeItem> gatherer(
            size_t(batchSize), [&](std::vector<PipeItem> &&batch) {
                std::vector<const std::vector<float> *> inputs;
                inputs.reserve(batch.size());
                for (const PipeItem &it : batch)
                    inputs.push_back(&it.input);
                std::vector<PoseEstimatorViTPose::FrameResult> results;
                if (!estimator.inferBatch(inputs, results, decodeSet, decodeThreads, ioBinding))
                    return;   // logged; the batch's frames drop as a failed frame does
                for (size_t i = 0; i < batch.size(); ++i)
                    appendPosed(batch[i].t_us, results[i].pose, results[i].wholeBody);
            });

        const std::function<void(size_t, PipeItem &&)> consume = [&](size_t, PipeItem &&item) {
            if (opt.progress)
                opt.progress(item.progress);
            if (!item.decodeOk)
                return;

            if (batchedPath) {
                gatherer.push(std::move(item));
                return;
            }

            gotPose = false;
            estimator.inferPrepared(item.input);
            if (!gotPose)
                return;
            appendPosed(item.t_us, res, estimator.lastWholeBody());
        };

        pinpoint::analysis::runOrderedPipeline<Fetched, PipeItem>(
            jobs.size(), producerThreads, size_t(queueDepth), fetch, process, consume);
        gatherer.finish();
    };

    // Turn an accumulated body bbox into the swing-level crop for the dense pass
    // (design §3.2) and record its provenance (track.cropRect, full-frame
    // normalized). Returns nullptr — the full-frame fallback — when cropping is
    // disabled or computePoseCropRect rejects the bbox (too sparse / no gain).
    // cropRectStore lives for the whole run so the returned pointer stays valid
    // through the pass that reads it.
    cv::Rect cropRectStore;
    auto makeCrop = [&](const BodyBboxAccum &bb) -> const cv::Rect * {
        if (!acc.crop.enabled)
            return nullptr;
        const auto pcr = pinpoint::analysis::computePoseCropRect(
            bb.x0, bb.y0, bb.x1, bb.y1, bb.frames, cfmt->width, cfmt->height, acc.crop);
        if (!pcr)
            return nullptr;
        cropRectStore = cv::Rect(pcr->x, pcr->y, pcr->w, pcr->h);
        track.cropRect = QRectF(double(pcr->x) / cfmt->width, double(pcr->y) / cfmt->height,
                                double(pcr->w) / cfmt->width, double(pcr->h) / cfmt->height);
        return &cropRectStore;
    };

    // Dense zone around impact (both scan paths share it): pose every
    // denseStride-th frame here, every sparseStride-th elsewhere in span.
    const int64_t denseLo = opt.impactUs - static_cast<int64_t>(opt.densePreMs)  * 1000;
    const int64_t denseHi = opt.impactUs + static_cast<int64_t>(opt.densePostMs) * 1000;
    // Entry timestamps for the pure selectors (pose_schedule.h), which pin the face-on
    // schedules to their pre-step-4 inline loops (pose_pipeline_test.cpp).
    std::vector<int64_t> entryTs;
    entryTs.reserve(entries.size());
    for (const pinpoint::IndexEntry &e : entries)
        entryTs.push_back(e.timestamp_us);

    // ── Two-pass pose (swing_span_bounding_plan.md §5) ──────────────────────────
    // Engaged only with no externally-supplied span (an IMU/G3 bound always
    // wins). Pass 1 poses a coarse full-window grid; estimateSwingSpanUs() over
    // that grip track yields [onset, finish]; pass 2 fills the span only. The
    // coarse frames stay in the track as address-hold coverage (subsumes
    // addressScanPadUs here). Progress: pass 1 → [0, 0.2], pass 2 → [0.2, 1.0].
    if (opt.twoPass && opt.scanEndUs <= opt.scanStartUs) {
        const size_t coarse = static_cast<size_t>(std::max(1, opt.coarseStride));

        // Pass 1 — coarse, whole window, always full-frame (its frames stay in the
        // track as address-hold coverage and its body keypoints seed the crop).
        std::vector<std::pair<size_t, float>> jobs1;
        jobs1.reserve(entries.size() / coarse + 1);
        for (size_t i : pinpoint::analysis::selectTwoPassCoarse(entries.size(), opt.coarseStride))
            jobs1.emplace_back(i, 0.2f * float(i + 1) / float(entries.size()));
        runPipeline(jobs1, track, nullptr);
        const size_t pass1Posed = track.frames.size();

        // Swing-level person crop from the union bbox of pass-1 body keypoints
        // (design §3.2). Pass-1 frames are already full-frame normalized — kept
        // as-is (mixed provenance is fine); only pass 2 runs cropped.
        BodyBboxAccum bb;
        for (size_t k = 0; k < pass1Posed; ++k)
            bb.add(track.frames[k].kp, track.frames[k].conf, 0.30f);
        const cv::Rect *cropPtr = makeCrop(bb);

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

        // i % coarse == 0 was posed in pass 1 — never re-pose a timestamp.
        const size_t span = pHi > pLo ? pHi - pLo : 1;
        std::vector<std::pair<size_t, float>> jobs2;
        jobs2.reserve(span);
        for (size_t i : pinpoint::analysis::selectTwoPassFill(entryTs, pLo, pHi, opt.coarseStride,
                                                              opt.denseStride, opt.sparseStride,
                                                              opt.impactUs, denseLo, denseHi))
            jobs2.emplace_back(i, 0.2f + 0.8f * float(i + 1 - pLo) / float(span));
        runPipeline(jobs2, track, cropPtr);
        const size_t pass2Posed = track.frames.size() - pass1Posed;

        // Pass-1 (whole window) and pass-2 (span fill) interleave in time — merge.
        std::sort(track.frames.begin(), track.frames.end(),
                  [](const PoseFrame2D &a, const PoseFrame2D &b) { return a.t_us < b.t_us; });

        ppInfo() << "[PoseRunner] source" << faceOnSource << ": two-pass"
                 << track.frames.size() << "posed (" << pass1Posed << "coarse pass-1 +"
                 << pass2Posed << (est.ok ? "span-bounded pass-2)," : "full-window pass-2 fallback),")
                 << entries.size() << "in window," << wristOk
                 << "with both wrists conf > 0.3," << wall.elapsed() << "ms (session"
                 << (sessionCache ? "cached," : "private,") << producerThreads << "producers, depth"
                 << queueDepth << "," << step3.toUtf8().constData() << ")";
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

    track.frames.reserve(i1 - iAddr0);
    std::vector<std::pair<size_t, float>> jobs;
    jobs.reserve(i1 - iAddr0);
    // Frames before G3's own bound [iAddr0, i0) keep the coarse address-hold sampling either way.
    // An explicit zone schedule (the DTL's, step 4) replaces only the dense/sparse choice inside
    // [i0, i1); without one this is the pre-step-4 selection exactly (pose_pipeline_test.cpp).
    std::vector<size_t> picked;
    if (!opt.zoneSchedule.empty()) {
        picked = pinpoint::analysis::selectSinglePass(entryTs, iAddr0, i0, i0, opt.addressStride,
                                                      stride, opt.impactUs, denseLo, denseHi);
        const std::vector<size_t> zoned = pinpoint::analysis::selectZoneSchedule(
            entryTs, i0, i1, opt.zoneSchedule, opt.restStride);
        picked.insert(picked.end(), zoned.begin(), zoned.end());
    } else {
        picked = pinpoint::analysis::selectSinglePass(entryTs, iAddr0, i0, i1, opt.addressStride,
                                                      stride, opt.impactUs, denseLo, denseHi);
    }
    for (size_t i : picked)
        jobs.emplace_back(i, float(i + 1 - iAddr0) / float(i1 - iAddr0));
    // WB1 person crop (design §3.2). No pass-1 bbox exists on this path, so run a
    // cheap mini-scan — 8 evenly-spaced full-frame inferences across the scan
    // range — purely to accumulate the body bbox. Those frames are DISCARDED (not
    // appended: they don't match the stride sampling semantics). The loop is
    // serial and fully-consuming (sole payloadOf caller, one resident frame at a
    // time, finished before runPipeline's producer starts), so the frozen-window
    // read contract holds. Skipped entirely when cropping is off ⇒ no extra
    // inference and a byte-identical flags-off path.
    const cv::Rect *cropPtr = nullptr;
    if (acc.crop.enabled && i1 > iAddr0) {
        BodyBboxAccum bb;
        constexpr int kMiniScanN = 8;
        const size_t span = i1 - iAddr0;
        size_t prev = entries.size();   // impossible index ⇒ dedup sentinel
        for (int k = 0; k < kMiniScanN; ++k) {
            const size_t idx = iAddr0
                + (span <= 1 ? 0 : size_t(uint64_t(k) * (span - 1) / (kMiniScanN - 1)));
            if (idx == prev)            // span < N ⇒ deduplicate repeated indices
                continue;
            prev = idx;
            cv::Mat frameBgr;
            bool decoded;
            {
                pinpoint::pose::PoseScopedTimer decodeTimer(
                    &timingSink, &pinpoint::pose::PoseTiming::decodeMs);
                const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(entries[idx]);
                decoded = pinpoint::decodeToBgr(*cfmt, handle.data, handle.bytes, frameBgr);
            }
            if (!decoded)
                continue;
            std::vector<float> buf;
            estimator.preprocess(frameBgr, buf);
            if (batchedPath) {   // same session options as the dense pass (static batch)
                std::vector<PoseEstimatorViTPose::FrameResult> one;
                if (estimator.inferBatch({ &buf }, one, decodeSet, decodeThreads, ioBinding)
                    && !one.empty())
                    bb.addResult(one.front().pose, 0.30f);
                continue;
            }
            gotPose = false;
            estimator.inferPrepared(buf);
            if (gotPose)
                bb.addResult(res, 0.30f);
        }
        cropPtr = makeCrop(bb);
    }

    const size_t sampled = jobs.size();
    runPipeline(jobs, track, cropPtr);

    ppInfo() << "[PoseRunner] source" << faceOnSource << ":" << track.frames.size()
             << "posed of" << sampled
             << (opt.zoneSchedule.empty() ? QStringLiteral("sampled (")
                                          : QStringLiteral("sampled by a %1-zone schedule, rest stride %2 (")
                                                .arg(opt.zoneSchedule.size()).arg(opt.restStride)).toUtf8().constData()
             << (i1 - i0) << "in span +"
             << (i0 - iAddr0) << "address-hold," << entries.size() << "in window)," << wristOk
             << "with both wrists conf > 0.3," << wall.elapsed() << "ms (session"
                 << (sessionCache ? "cached," : "private,") << producerThreads << "producers, depth"
                 << queueDepth << "," << step3.toUtf8().constData() << ")";
    return track;
}

#else // !(HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME)

PoseTrack2D PoseRunner::run(const pinpoint::SwingWindow &window,
                            pinpoint::SourceId faceOnSource,
                            const ShotAnalysisRunnerOptions &opt)
{
    Q_UNUSED(window)
    Q_UNUSED(opt)
    tl_lastPoseTiming = pinpoint::pose::PoseTiming{};
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

QString PoseRunner::modelIdentity(const QString &motionCaptureQuality)
{
#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)
    using ViTVariant = PoseEstimatorViTPose::ModelVariant;
    const bool useLarge = pinpoint::pose::useVitPoseLarge(
        motionCaptureQuality, PoseEstimatorViTPose::isVariantAvailable(ViTVariant::WholeBodyLarge));
    const QString path = PoseEstimatorViTPose::modelPath(useLarge ? ViTVariant::WholeBodyLarge
                                                                  : ViTVariant::WholeBodyB);
    const QFileInfo fi(path);
    return fi.fileName() + QLatin1Char('@') + QString::number(fi.exists() ? fi.size() : 0);
#else
    Q_UNUSED(motionCaptureQuality);
    return QStringLiteral("none");
#endif
}

pinpoint::analysis::PoseTrack2D PoseRunner::loadFromJson(const QString &file,
                                                         pinpoint::SourceId camera)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        pinpoint::analysis::PoseTrack2D track; track.camera = camera; return track;
    }
    return fromJsonObject(QJsonDocument::fromJson(f.readAll()).object(), camera);
}

pinpoint::analysis::PoseTrack2D PoseRunner::fromJsonObject(const QJsonObject &root,
                                                           pinpoint::SourceId camera)
{
    using namespace pinpoint::analysis;
    PoseTrack2D track;
    track.camera = camera;
    // Provenance written by serializeAnalysis (decode mode when dark, the crop
    // rect when one was applied) — restored so a reloaded track re-serialises
    // identically to the run that produced it.
    if (!root.contains(QStringLiteral("frames"))) {
        const QJsonObject p2 = root[QStringLiteral("pose2d")].toObject();
        if (p2.contains(QStringLiteral("decode"))) track.decode = p2[QStringLiteral("decode")].toString();
        if (p2.contains(QStringLiteral("cropRect"))) {
            const QJsonObject r = p2[QStringLiteral("cropRect")].toObject();
            track.cropRect = QRectF(r[QStringLiteral("x")].toDouble(), r[QStringLiteral("y")].toDouble(),
                                    r[QStringLiteral("w")].toDouble(), r[QStringLiteral("h")].toDouble());
        }
    }
    const QJsonArray frames = (root.contains(QStringLiteral("frames"))
                                   ? root[QStringLiteral("frames")]
                                   : root[QStringLiteral("pose2d")]
                                         .toObject()[QStringLiteral("frames")]).toArray();
    for (const QJsonValue &fv : frames) {
        const QJsonObject o = fv.toObject();
        PoseFrame2D pf;
        pf.t_us = int64_t(o[QStringLiteral("t_us")].toDouble());
        const QJsonArray kp = o[QStringLiteral("kp")].toArray();
        // Bounded by BOTH the array and the struct width: old 51-float files
        // fill 0–16 and leave the wholebody tail default-initialized.
        for (int j = 0; j < kWholeBodyJoints && j * 3 + 2 < kp.size(); ++j) {
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

double PoseRunner::warmUp(const QString &motionCaptureQuality)
{
    namespace tp = pinpoint::tuned::pose;
    if (!tp::kSessionCache) return 0.0;
    using ViTVariant = PoseEstimatorViTPose::ModelVariant;
    const bool useLarge = pinpoint::pose::useVitPoseLarge(
        motionCaptureQuality, PoseEstimatorViTPose::isVariantAvailable(ViTVariant::WholeBodyLarge));
    const ViTVariant variant = useLarge ? ViTVariant::WholeBodyLarge : ViTVariant::WholeBodyB;
    if (!PoseEstimatorViTPose::isVariantAvailable(variant)) return 0.0;
    // The defaults run() resolves with no overrides — the same key, or the warm-up warms the
    // wrong session.
    const int batchSize = std::clamp(tp::kBatchSize, 1, 64);
    const PoseEstimatorViTPose::LoadOptions loadOpts =
        loadOptionsFor(tp::kModelFp16, tp::kCoreMLProgram, tp::kTensorRT, tp::kStaticBatch, batchSize,
                       tp::kLogPartition);
    const int intraOpThreads = 0;
    QElapsedTimer t;
    t.start();
    auto make = [&]() {
        auto e = std::make_unique<PoseEstimatorViTPose>(variant);
        e->setIntraOpThreads(intraOpThreads);
        e->setLoadOptions(loadOpts);
        e->load();
        return e;
    };
    {
        EstimatorCache::Lease lease = estimatorCache().acquire(
            estimatorKey(variant, loadOpts, intraOpThreads), make,
            [](const PoseEstimatorViTPose &e) { return e.isReady(); });
        if (!lease->isReady()) {
            ppWarn() << "[PoseRunner] warm-up: ViTPose did not load";
            return 0.0;
        }
    }
    const double ms = double(t.elapsed());
    ppInfo() << "[PoseRunner] warm-up:" << PoseEstimatorViTPose::modelPath(variant) << "session ready in"
             << ms << "ms";
    return ms;
}
