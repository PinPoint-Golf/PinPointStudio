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

#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_vitpose.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QStandardPaths>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include "pp_debug.h"
#include "pp_model_path.h"   // bundle-vs-bare-exe model lookup
#include "pp_profiler.h"
#include "cpu_topology.h"   // pinpoint::physicalCoreCount() (pose.intraOpThreads == -1)

#include <onnxruntime_cxx_api.h>
#include "ort_log.h"
#include <opencv2/imgproc.hpp>

#ifdef WITH_COREML
#  include <coreml_provider_factory.h>
#endif

// Model I/O constants.
static constexpr int kInputH      = 256;
static constexpr int kInputW      = 192;
static constexpr int kHeatmapH    = 64;
static constexpr int kHeatmapW    = 48;
static constexpr int kTotalJoints = 133; // COCO-WholeBody output channels
static constexpr int kBodyJoints  = 17;  // COCO body joints consumed here

// ImageNet normalisation constants (RGB order).
static constexpr float kMean[3] = { 0.485f, 0.456f, 0.406f };
static constexpr float kStd[3]  = { 0.229f, 0.224f, 0.225f };

// Decode one [kHeatmapH, kHeatmapW] channel per the active mode (heatmap_decode.h),
// normalised to [0, 1] in input space (the input image was resized to exactly
// kInputW × kInputH, so heatmap coords divide directly by the heatmap dims).
// Argmax is byte-identical to the original inline body-joint decode; Dark adds
// DARK sub-pixel refinement. Shared by the body and whole-body decode loops.
void PoseEstimatorViTPose::decodeChannel(const float *hm, float &nx, float &ny, float &score)
{
    if (m_decodeMode == DecodeMode::Dark)
        pinpoint::pose::decodeDark(hm, kHeatmapW, kHeatmapH, m_decodeBlur.data(), nx, ny, score);
    else
        pinpoint::pose::decodeArgmax(hm, kHeatmapW, kHeatmapH, nx, ny, score);
}

// All ORT state lives here so onnxruntime_cxx_api.h is not pulled in via the header.
struct PoseEstimatorViTPose::OrtState {
    Ort::Env            env     { ORT_LOGGING_LEVEL_WARNING, "ViTPose", ppOrtLogCallback, nullptr };
    Ort::SessionOptions opts;
    Ort::RunOptions     runOpts;
    Ort::AllocatorWithDefaultOptions allocator;
    std::unique_ptr<Ort::Session>    session;

    std::string inputName;
    std::string outputName;

    QElapsedTimer wallTimer;

    int64_t modelBytes = 0;   // [seam] model-file size as an ONNX.Pose arena proxy

    // pose.ioBinding (inferBatch): the binding and its input tensor persist across
    // batches of one shape; on CUDA the input lives in pinned host memory (the
    // EP's "CudaPinned" allocator) and the output is bound to it too, so each batch
    // is one H→D and one D→H DMA. Elsewhere both are plain CPU memory.
    std::unique_ptr<Ort::IoBinding> binding;
    std::unique_ptr<Ort::Allocator> pinnedAlloc;   // null ⇒ CPU memory
    bool                            pinnedTried = false;
    Ort::Value                      boundInput{ nullptr };
    std::vector<float>              boundBuf;      // boundInput's bytes when not pinned
    int64_t                         boundBatch = 0;
};

// ---------------------------------------------------------------------------

// ViTPose++-L is never packaged (it is ~1.2 GB); it is downloaded on demand into
// the writable app-data dir. These coordinates are shared with the download
// controller (MotionCaptureProbe).
static constexpr char kLargeModelFile[] = "vitpose-l-wholebody.onnx";
static constexpr char kLargeModelUrl[]  =
    "https://huggingface.co/JunkyByte/easy_ViTPose/resolve/main/onnx/wholebody/"
    "vitpose-l-wholebody.onnx";

PoseEstimatorViTPose::PoseEstimatorViTPose(ModelVariant variant, QObject *parent)
    : PoseEstimatorBase(parent)
    , m_variant(variant)
{}

PoseEstimatorViTPose::~PoseEstimatorViTPose()
{
    if (m_ort && m_ort->modelBytes > 0)
        PP_PROFILE_MEM_SUB("ONNX.Pose", m_ort->modelBytes);
}

QString PoseEstimatorViTPose::largeModelFileName()
{
    return QString::fromLatin1(kLargeModelFile);
}

QString PoseEstimatorViTPose::largeModelDir()
{
    // Writable, survives rebuilds, never bundled (same convention as Kokoro TTS
    // voices in TtsController::modelDataDir()).
    //
    // The APP's data directory by name, not AppLocalDataLocation: that one is named after the
    // RUNNING executable (nothing sets an application name), so on Windows swinglab_run looked in
    // %LOCALAPPDATA%\swinglab_run, never found the L model the app downloaded, and ran every High
    // swing on ViTPose-B. 2026-09-29: that B pose put 16 Sept Wrist_02 s2's whole backswing on the
    // wrong structure; the same swing on L tracks cleanly. Generic + "PinPointStudio" is exactly
    // the app's own AppLocalDataLocation on macOS, Windows and Linux.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
         + QStringLiteral("/PinPointStudio/models/vitpose/");
}

QString PoseEstimatorViTPose::largeModelUrl()
{
    return QString::fromLatin1(kLargeModelUrl);
}

QString PoseEstimatorViTPose::modelPath(ModelVariant v)
{
    if (v == ModelVariant::WholeBodyLarge)
        return largeModelDir() + QString::fromLatin1(kLargeModelFile);

    return pinpoint::modelFilePath(QStringLiteral(VITPOSE_MODEL_FILE), QStringLiteral("vitpose"));
}

bool PoseEstimatorViTPose::isVariantAvailable(ModelVariant v)
{
    return QFile::exists(modelPath(v));
}

QString PoseEstimatorViTPose::loadOptionsTag(const LoadOptions &o)
{
    return QStringLiteral("%1|%2|%3|b%4|%5")
        .arg(QLatin1String(o.fp16 ? "fp16" : "fp32"),
             QLatin1String(o.coremlProgram ? "mlprogram" : "nn"),
             o.tensorrt ? QStringLiteral("trt%1").arg(o.maxBatch) : QStringLiteral("-"))
        .arg(o.staticBatch)
        .arg(QLatin1String(o.logPartition ? "log" : "-"));
}

// <dir>/<name>.fp16.onnx beside the fp32 model (tools/pose/convert_fp16.py).
static QString fp16Sibling(const QString &fp32Path)
{
    QString p = fp32Path;
    if (p.endsWith(QLatin1String(".onnx")))
        p.chop(5);
    return p + QStringLiteral(".fp16.onnx");
}

void PoseEstimatorViTPose::load()
{
    QString path = modelPath(m_variant);
    const char *variantTag = (m_variant == ModelVariant::WholeBodyLarge) ? "++-L" : "-B";
    if (!QFile::exists(path)) {
        ppError() << "[ViTPose" << variantTag << "] Model not found:" << path;
        return;
    }
    // pose.modelPrecision fp16: the converted file beside the fp32 one, else fp32 —
    // said, so a run that asked for fp16 and got fp32 is never read as an fp16 result.
    if (m_loadOpts.fp16) {
        const QString p16 = fp16Sibling(path);
        if (QFile::exists(p16)) {
            path = p16;
        } else {
            ppWarn() << "[ViTPose" << variantTag << "] fp16 model not found at" << p16
                     << "— running fp32 (tools/pose/convert_fp16.py makes it)";
        }
    }
    m_resolvedModel = path;

    m_ready = false;
    if (m_ort && m_ort->modelBytes > 0)   // reload: release the prior arena estimate
        PP_PROFILE_MEM_SUB("ONNX.Pose", m_ort->modelBytes);
    m_ort   = std::make_unique<OrtState>();

    // ViTPose is used only on the offline analysis path (PoseRunner), and the
    // post-shot pipeline now sequences the x264 export AFTER the pose pass
    // (ShotProcessor::onAnalysisFinished) — so the estimator has the machine to
    // itself and should spread the inference across cores. This model is heavily
    // single-thread-bound otherwise: measured ~337 ms/frame at 1 intra-op thread
    // versus ~83 ms at the physical-core count on a 6-core/12-thread CPU
    // (hyperthreads past the physical-core count regress).
    //
    // Pool size resolves three ways (m_intraOpThreads, from pose.intraOpThreads —
    // PoseRunner sets it before load()):
    //   > 0   → pinned exactly (manual override)
    //   == -1 → topology auto: physical-core count clamped [1,16] via
    //           cpu_topology.h — OPT-IN; a determinism A/B on the affected hardware
    //           (no-SMT / hybrid P/E / >16-logical) is owed before it can be the
    //           default (docs/implementation/shaft_tracker_impl.md S0 note)
    //    0    → (DEFAULT) the legacy proxy: hardware_concurrency() reports logical
    //           cores, so halve it and clamp [1,8] — UNCHANGED, so the default path
    //           is thread-count-identical to the historical behaviour
    // (The live 60 Hz path uses MoveNet, which stays pinned to 1.)
    int intraThreads;
    if (m_intraOpThreads > 0) {
        intraThreads = m_intraOpThreads;
    } else if (m_intraOpThreads == -1) {
        intraThreads = std::clamp(pinpoint::physicalCoreCount(), 1, 16);
    } else {
        const unsigned hwThreads = std::thread::hardware_concurrency();
        intraThreads = std::clamp(static_cast<int>(hwThreads ? hwThreads / 2 : 1), 1, 8);
    }
    const LoadOptions lo = m_loadOpts;

    // Everything but the providers. Re-applied to a fresh SessionOptions when a
    // TensorRT session fails to build and the cascade is retried without it.
    auto configure = [&](Ort::SessionOptions &opts) {
        opts.SetIntraOpNumThreads(intraThreads);
        opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        // pose.staticBatch: the graph's one free dim fixed, so its 28 Shape→Gather→
        // Concat chains fold to constants at optimisation time.
        if (lo.staticBatch > 0)
            opts.AddFreeDimensionOverrideByName("batch_size", lo.staticBatch);
        // pose.logPartition: the EP's "number of nodes supported" summary is an INFO
        // line (WARNING only when it splits the graph into > 1 partition).
        if (lo.logPartition)
            opts.SetLogSeverityLevel(ORT_LOGGING_LEVEL_INFO);
    };

    // Execution provider cascade — identical to PoseEstimatorMoveNet with every
    // step-3 option off.
    auto appendProviders = [&](Ort::SessionOptions &opts, bool withTrt) {
        QString epLabel;
        Q_UNUSED(withTrt)

#ifdef WITH_COREML
        if (epLabel.isEmpty() && lo.coremlProgram) {
            // pose.coreml mlprogram. The NeuralNetwork format below gave 43–64 ms a
            // frame on the M4 — the transformer mostly on the CPU EP. MLProgram is the
            // format CoreML compiles for the GPU/ANE, fp16-capable. The compiled model
            // is cached across processes (swinglab_run is one process per swing); the
            // directory carries the options and the model size because the EP keys its
            // cache on the model path only and never notices a changed model.
            const QString cacheDir =
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/PinPointStudio/cache/coreml/")
                + QFileInfo(path).completeBaseName() + QLatin1Char('-')
                + QString::number(QFileInfo(path).size()) + QStringLiteral("-b")
                + QString::number(lo.staticBatch);
            QDir().mkpath(cacheDir);
            try {
                std::unordered_map<std::string, std::string> o{
                    { "ModelFormat", "MLProgram" },
                    { "MLComputeUnits", "ALL" },
                    { "RequireStaticInputShapes", "0" },
                    { "EnableOnSubgraphs", "0" },
                    { "ModelCacheDirectory", cacheDir.toStdString() } };
                opts.AppendExecutionProvider("CoreML", o);
                epLabel = QStringLiteral("CoreML-MLProgram");
                ppInfo() << "[ViTPose] CoreML execution provider active (MLProgram, all compute units,"
                         << "cache" << cacheDir << ")";
            } catch (const Ort::Exception &e) {
                ppInfo() << "[ViTPose] CoreML MLProgram unavailable:" << e.what() << "— falling back";
            }
        }
        if (epLabel.isEmpty()) {
            try {
                Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
                    opts, COREML_FLAG_USE_NONE));
                epLabel = QStringLiteral("CoreML");
                ppInfo() << "[ViTPose] CoreML execution provider active";
            } catch (const Ort::Exception &e) {
                ppInfo() << "[ViTPose] CoreML unavailable:" << e.what() << "— falling back";
            }
        }
#endif

#ifdef WITH_CUDA
        if (epLabel.isEmpty()) {
#  ifdef Q_OS_LINUX
            const bool hasNv = QFile::exists(QStringLiteral("/proc/driver/nvidia/version"));
#  elif defined(Q_OS_WIN)
            QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
            const bool hasNv = nvcuda.load();
            if (hasNv) nvcuda.unload();
#  else
            const bool hasNv = false;
#  endif
            // pose.tensorrt — UNTESTED (no nvinfer on the studio). Appended AHEAD of
            // CUDA: TensorRT takes the subgraphs it can build, CUDA the rest. The
            // provider library pulls nvinfer in when appended, so a machine without
            // TensorRT throws here and stays on CUDA; one whose engine build fails at
            // session creation is retried without it (below). fp16 engines, cached
            // under app data (a ViT-B engine build is minutes); the batch dim gets one
            // optimisation profile [1 … batch] so a partial batch needs no rebuild.
            QString trtLabel;
            if (hasNv && withTrt) {
                const OrtApi &api = Ort::GetApi();
                OrtTensorRTProviderOptionsV2 *trt = nullptr;
                try {
                    Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&trt));
                    const QString cacheDir =
                        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                        + QStringLiteral("/PinPointStudio/cache/tensorrt");
                    QDir().mkpath(cacheDir);
                    auto shape = [](int b) {
                        return "input_0:" + std::to_string(b) + "x3x" + std::to_string(kInputH)
                             + "x" + std::to_string(kInputW);
                    };
                    const std::string shp    = shape(1);
                    const std::string shpMax = shape(std::max(1, lo.maxBatch));
                    const std::string dir = cacheDir.toStdString();
                    const char *keys[] = { "device_id", "trt_fp16_enable",
                                           "trt_engine_cache_enable", "trt_engine_cache_path",
                                           "trt_profile_min_shapes", "trt_profile_opt_shapes",
                                           "trt_profile_max_shapes" };
                    const char *vals[] = { "0", "1", "1", dir.c_str(),
                                           shp.c_str(), shpMax.c_str(), shpMax.c_str() };
                    Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(trt, keys, vals, 7));
                    opts.AppendExecutionProvider_TensorRT_V2(*trt);
                    trtLabel = QStringLiteral("TensorRT+");
                    ppInfo() << "[ViTPose] TensorRT execution provider appended (fp16, engine cache"
                             << cacheDir << ")";
                } catch (const Ort::Exception &e) {
                    ppWarn() << "[ViTPose] TensorRT unavailable:" << e.what() << "— CUDA only";
                }
                if (trt)
                    api.ReleaseTensorRTProviderOptions(trt);
            }
            if (hasNv) {
                try {
                    OrtCUDAProviderOptions cuda{};
                    cuda.device_id = 0;
                    opts.AppendExecutionProvider_CUDA(cuda);
                    epLabel = trtLabel + QStringLiteral("CUDA");
                    ppInfo() << "[ViTPose] CUDA execution provider active";
                } catch (const Ort::Exception &e) {
                    ppInfo() << "[ViTPose] CUDA unavailable:" << e.what() << "— falling back";
                }
            }
        }
#endif

#ifdef WITH_DIRECTML
        if (epLabel.isEmpty()) {
            try {
                opts.AppendExecutionProvider("DML");
                epLabel = QStringLiteral("DirectML");
                ppInfo() << "[ViTPose] DirectML execution provider active";
            } catch (const Ort::Exception &e) {
                ppInfo() << "[ViTPose] DirectML unavailable:" << e.what() << "— falling back";
            }
        }
#endif
        return epLabel;
    };

    configure(m_ort->opts);
    QString epLabel = appendProviders(m_ort->opts, lo.tensorrt);

    if (epLabel.isEmpty())
        ppInfo() << "[ViTPose] No GPU EP available — using CPU";
    else
        ppInfo() << "[ViTPose] Using" << epLabel << "execution provider";

    auto buildSession = [&]() {
        // The model load + EP init/compile (perf plan F1: paid twice a shot today).
        pinpoint::pose::PoseScopedTimer sessionTimer(m_timing,
                                                     &pinpoint::pose::PoseTiming::sessionBuildMs);
#ifdef Q_OS_WIN
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toStdWString().c_str(), m_ort->opts);
#else
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toUtf8().constData(), m_ort->opts);
#endif
    };
    try {
        buildSession();
    } catch (const Ort::Exception &e) {
        if (!epLabel.startsWith(QLatin1String("TensorRT"))) {
            ppError() << "[ViTPose] Failed to load model:" << e.what();
            m_ort.reset();
            return;
        }
        // Fail soft: a TensorRT engine that will not build must not cost the pose.
        ppWarn() << "[ViTPose] TensorRT session failed:" << e.what() << "— retrying on CUDA";
        m_ort->opts = Ort::SessionOptions{};
        configure(m_ort->opts);
        epLabel = appendProviders(m_ort->opts, false);
        try {
            buildSession();
        } catch (const Ort::Exception &e2) {
            ppError() << "[ViTPose] Failed to load model:" << e2.what();
            m_ort.reset();
            return;
        }
    }
    m_epLabel = epLabel.isEmpty() ? QStringLiteral("CPU") : epLabel;

    m_ort->inputName  = m_ort->session->GetInputNameAllocated(0, m_ort->allocator).get();
    m_ort->outputName = m_ort->session->GetOutputNameAllocated(0, m_ort->allocator).get();

    m_ort->modelBytes = QFileInfo(path).size();   // [seam] file size as ORT arena proxy
    PP_PROFILE_MEM_ADD("ONNX.Pose", m_ort->modelBytes);

    ppInfo() << "[ViTPose" << variantTag << "] Loaded —" << QFileInfo(path).fileName()
             << "input:" << m_ort->inputName.c_str()
             << "output:" << m_ort->outputName.c_str()
             << "size:" << kInputW << "×" << kInputH
             << "intraOpThreads:" << intraThreads;

    m_decodeBlur.assign(size_t(kHeatmapH) * kHeatmapW, 0.f);   // DARK scratch

    m_ort->wallTimer.start();
    m_lastCallNs = -1;
    m_ready      = true;
    emit poseBackendReady(epLabel);
}

void PoseEstimatorViTPose::estimatePose(const cv::Mat &frame)
{
    // Whole-body keypoints from a previous frame must never outlive the call
    // that produced them — invalidate before any early-out. No-op live.
    if (m_decodeWholeBody)
        m_lastWholeBody.valid = false;

    if (!isEnabled()) {     // disabled by method — release the throttle, skip inference
        emit estimationDone();
        return;
    }

    if (!m_ready || frame.empty()) {
        // Model missing/failed to load, or a degenerate frame — still release
        // the throttle, or the whole camera pipeline (pose AND ball) starves
        // permanently on the first frame.
        emit estimationDone();
        return;
    }

    PP_PROFILE_SCOPE("Pose.ViTPose.run");   // full per-frame estimate (preprocess + infer + decode)

    const qint64 nowNs = m_ort->wallTimer.nsecsElapsed();

    // Preprocess: resize → BGR→RGB → float32 [0,1] → ImageNet normalise → CHW.
    cv::Mat resized, rgb, rgbF;
    cv::resize(frame, resized, cv::Size(kInputW, kInputH));
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgbF, CV_32FC3, 1.0 / 255.0);

    // Split into channel planes and normalise each channel.
    std::vector<cv::Mat> planes(3);
    cv::split(rgbF, planes);
    for (int c = 0; c < 3; ++c)
        planes[c] = (planes[c] - kMean[c]) / kStd[c];

    // Build contiguous NCHW tensor buffer [1, 3, 256, 192].
    static constexpr size_t kPlaneSize = kInputH * kInputW;
    std::vector<float> inputBuf(3 * kPlaneSize);
    for (int c = 0; c < 3; ++c) {
        cv::Mat dst(kInputH, kInputW, CV_32FC1, inputBuf.data() + c * kPlaneSize);
        planes[c].copyTo(dst);
    }

    QElapsedTimer inferTimer;
    inferTimer.start();

    try {
        PP_PROFILE_SCOPE("Pose.ViTPose.infer");   // ORT Run() + heatmap decode

        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 4> inputShape{ 1, 3, kInputH, kInputW };
        auto inputTensor = Ort::Value::CreateTensor<float>(
            memInfo,
            inputBuf.data(), inputBuf.size(),
            inputShape.data(), inputShape.size());

        const char *inputNames[]  = { m_ort->inputName.c_str() };
        const char *outputNames[] = { m_ort->outputName.c_str() };
        Ort::Value  inputs[]      = { std::move(inputTensor) };

        auto outputs = m_ort->session->Run(
            m_ort->runOpts,
            inputNames, inputs, 1,
            outputNames, 1);

        // Output: [1, 133, 64, 48] — consume the first kBodyJoints (0–16)
        // channels always, plus ALL remaining feet/face/hand channels
        // (17–132) when whole-body decode is opted in.
        const float *heatmapData = outputs[0].GetTensorData<float>();

        PoseResult result;
        result.timestamp = QDateTime::currentMSecsSinceEpoch();

        for (int j = 0; j < kBodyJoints; ++j) {
            const float *hm = heatmapData + j * kHeatmapH * kHeatmapW;
            decodeChannel(hm, result.keypoints[j].x,
                                 result.keypoints[j].y, result.keypoints[j].score);
        }

        float scoreSum = 0.f;
        for (int j = 0; j < kBodyJoints; ++j) scoreSum += result.keypoints[j].score;
        result.confidence = scoreSum / kBodyJoints;

        // COCO-WholeBody tail channels — same decode pass, opt-in only (the
        // live 60 Hz path never pays for this). The body slots (0–16) are
        // COPIED from the PoseResult decode above — never re-decoded — so the
        // emitted PoseResult and WholeBodyResult.kp[0..16] are bit-identical.
        if (m_decodeWholeBody) {
            WholeBodyResult &wb = m_lastWholeBody;
            for (int j = 0; j < kBodyJoints; ++j) {
                wb.kp[size_t(j)]    = QPointF(result.keypoints[j].x, result.keypoints[j].y);
                wb.score[size_t(j)] = result.keypoints[j].score;
            }
            for (int j = kBodyJoints; j < kTotalJoints; ++j) {   // feet + face + hands
                const float *hm = heatmapData + j * kHeatmapH * kHeatmapW;
                float nx = 0.f, ny = 0.f, score = 0.f;
                decodeChannel(hm, nx, ny, score);
                wb.kp[size_t(j)]    = QPointF(nx, ny);
                wb.score[size_t(j)] = score;
            }
            wb.valid = true;
        }

        emit poseEstimated(result);

    } catch (const Ort::Exception &e) {
        ppError() << "[ViTPose] Inference error:" << e.what();
        emit estimationDone();
        return;
    }

    const double inferMs    = inferTimer.nsecsElapsed() / 1e6;
    const double intervalMs = (m_lastCallNs >= 0)
                              ? (nowNs - m_lastCallNs) / 1e6
                              : inferMs;
    m_lastCallNs = nowNs;

    // Update rolling circular buffers — O(1) per frame.
    m_inferenceSum -= m_inferenceSamples[m_timingIndex];
    m_intervalSum  -= m_intervalSamples[m_timingIndex];
    m_inferenceSamples[m_timingIndex] = inferMs;
    m_intervalSamples[m_timingIndex]  = intervalMs;
    m_inferenceSum += inferMs;
    m_intervalSum  += intervalMs;
    m_timingIndex   = (m_timingIndex + 1) % kWindowSize;
    if (m_timingCount < kWindowSize)
        ++m_timingCount;

    if (m_timingCount == kWindowSize) {
        const double avgInferMs  = m_inferenceSum / kWindowSize;
        const double avgInterval = m_intervalSum  / kWindowSize;
        const double fps = (avgInterval > 0.0) ? 1000.0 / avgInterval : 0.0;
        emit poseStatsUpdated(avgInferMs, fps);
    }

    emit estimationDone();
}

// Preprocess half of estimatePose() — identical math (lines above), factored so
// PoseRunner can run it on a producer thread. Touches no member state; the
// output `inputBuf` is fully self-owned (independent of `frame`'s pixels).
void PoseEstimatorViTPose::preprocess(const cv::Mat &frame, std::vector<float> &inputBuf) const
{
    pinpoint::pose::PoseScopedTimer prepTimer(m_timing, &pinpoint::pose::PoseTiming::preprocessMs);

    // Preprocess: resize → BGR→RGB → float32 [0,1] → ImageNet normalise → CHW.
    cv::Mat resized, rgb, rgbF;
    cv::resize(frame, resized, cv::Size(kInputW, kInputH));
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgbF, CV_32FC3, 1.0 / 255.0);

    // Split into channel planes and normalise each channel.
    std::vector<cv::Mat> planes(3);
    cv::split(rgbF, planes);
    for (int c = 0; c < 3; ++c)
        planes[c] = (planes[c] - kMean[c]) / kStd[c];

    // Build contiguous NCHW tensor buffer [1, 3, 256, 192].
    static constexpr size_t kPlaneSize = kInputH * kInputW;
    inputBuf.resize(3 * kPlaneSize);
    for (int c = 0; c < 3; ++c) {
        cv::Mat dst(kInputH, kInputW, CV_32FC1, inputBuf.data() + c * kPlaneSize);
        planes[c].copyTo(dst);
    }
}

// Inference half of estimatePose() — ORT Run() + heatmap decode + emit, over a
// buffer already produced by preprocess(). Caller guarantees the estimator is
// ready. `inputBuf` is not modified (ORT's CreateTensor wants a non-const
// pointer; the buffer is only read during Run()).
void PoseEstimatorViTPose::inferPrepared(std::vector<float> &inputBuf)
{
    // Whole-body keypoints from a previous frame must never outlive the call
    // that produced them.
    if (m_decodeWholeBody)
        m_lastWholeBody.valid = false;

    if (!m_ready) {
        emit estimationDone();
        return;
    }

    PP_PROFILE_SCOPE("Pose.ViTPose.run");

    const qint64 nowNs = m_ort->wallTimer.nsecsElapsed();

    QElapsedTimer inferTimer;
    inferTimer.start();

    try {
        PP_PROFILE_SCOPE("Pose.ViTPose.infer");   // ORT Run() + heatmap decode

        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 4> inputShape{ 1, 3, kInputH, kInputW };
        auto inputTensor = Ort::Value::CreateTensor<float>(
            memInfo,
            inputBuf.data(), inputBuf.size(),
            inputShape.data(), inputShape.size());

        const char *inputNames[]  = { m_ort->inputName.c_str() };
        const char *outputNames[] = { m_ort->outputName.c_str() };
        Ort::Value  inputs[]      = { std::move(inputTensor) };

        std::vector<Ort::Value> outputs;
        {
            pinpoint::pose::PoseScopedTimer runTimer(m_timing, &pinpoint::pose::PoseTiming::runMs);
            outputs = m_ort->session->Run(
                m_ort->runOpts,
                inputNames, inputs, 1,
                outputNames, 1);
        }
        if (m_timing)
            m_timing->addFrame();

        // The 17 (or 133, whole-body) channel decodes, argmax or DARK — timed to
        // the emit so the slot's own work is not billed here.
        pinpoint::pose::PoseScopedTimer heatmapTimer(m_timing,
                                                     &pinpoint::pose::PoseTiming::heatmapDecodeMs);

        const float *heatmapData = outputs[0].GetTensorData<float>();

        PoseResult result;
        result.timestamp = QDateTime::currentMSecsSinceEpoch();

        for (int j = 0; j < kBodyJoints; ++j) {
            const float *hm = heatmapData + j * kHeatmapH * kHeatmapW;
            decodeChannel(hm, result.keypoints[j].x,
                                 result.keypoints[j].y, result.keypoints[j].score);
        }

        float scoreSum = 0.f;
        for (int j = 0; j < kBodyJoints; ++j) scoreSum += result.keypoints[j].score;
        result.confidence = scoreSum / kBodyJoints;

        // Body slots copied from the PoseResult decode (bit-identical), tail
        // channels decoded fresh — mirrors estimatePose() exactly.
        if (m_decodeWholeBody) {
            WholeBodyResult &wb = m_lastWholeBody;
            for (int j = 0; j < kBodyJoints; ++j) {
                wb.kp[size_t(j)]    = QPointF(result.keypoints[j].x, result.keypoints[j].y);
                wb.score[size_t(j)] = result.keypoints[j].score;
            }
            for (int j = kBodyJoints; j < kTotalJoints; ++j) {   // feet + face + hands
                const float *hm = heatmapData + j * kHeatmapH * kHeatmapW;
                float nx = 0.f, ny = 0.f, score = 0.f;
                decodeChannel(hm, nx, ny, score);
                wb.kp[size_t(j)]    = QPointF(nx, ny);
                wb.score[size_t(j)] = score;
            }
            wb.valid = true;
        }
        heatmapTimer.stop();

        emit poseEstimated(result);

    } catch (const Ort::Exception &e) {
        ppError() << "[ViTPose] Inference error:" << e.what();
        emit estimationDone();
        return;
    }

    const double inferMs    = inferTimer.nsecsElapsed() / 1e6;
    const double intervalMs = (m_lastCallNs >= 0)
                              ? (nowNs - m_lastCallNs) / 1e6
                              : inferMs;
    m_lastCallNs = nowNs;

    m_inferenceSum -= m_inferenceSamples[m_timingIndex];
    m_intervalSum  -= m_intervalSamples[m_timingIndex];
    m_inferenceSamples[m_timingIndex] = inferMs;
    m_intervalSamples[m_timingIndex]  = intervalMs;
    m_inferenceSum += inferMs;
    m_intervalSum  += intervalMs;
    m_timingIndex   = (m_timingIndex + 1) % kWindowSize;
    if (m_timingCount < kWindowSize)
        ++m_timingCount;

    if (m_timingCount == kWindowSize) {
        const double avgInferMs  = m_inferenceSum / kWindowSize;
        const double avgInterval = m_intervalSum  / kWindowSize;
        const double fps = (avgInterval > 0.0) ? 1000.0 / avgInterval : 0.0;
        emit poseStatsUpdated(avgInferMs, fps);
    }

    emit estimationDone();
}

// Batched half of the offline path (perf plan step 3). See the header. Per frame the
// result is assembled exactly as inferPrepared() assembles it — body 0–16 into the
// PoseResult, the mean body score as its confidence, the WholeBodyResult's body
// slots copied from it and the tail from the same decode — so with batch 1, all
// channels and one thread the two paths agree bit for bit on the same heatmaps.
bool PoseEstimatorViTPose::inferBatch(const std::vector<const std::vector<float> *> &inputs,
                                      std::vector<FrameResult> &out,
                                      pinpoint::pose::ChannelSet channels, int decodeThreads,
                                      bool ioBinding)
{
    out.clear();
    if (inputs.empty())
        return true;
    if (!m_ready) {
        ppWarn() << "[ViTPose] inferBatch on an estimator that is not ready";
        return false;
    }

    PP_PROFILE_SCOPE("Pose.ViTPose.batch");

    static constexpr size_t kFrameFloats = size_t(3) * kInputH * kInputW;
    const int64_t n = int64_t(inputs.size());
    // A static batch dim (pose.staticBatch) takes exactly that many frames: a short
    // batch is zero-padded and the padding's heatmaps never decoded.
    const int64_t b = m_loadOpts.staticBatch > 0 ? std::max<int64_t>(m_loadOpts.staticBatch, n) : n;
    const std::array<int64_t, 4> inputShape{ b, 3, kInputH, kInputW };

    try {
        std::vector<Ort::Value> outputs;
        const float *heatmaps = nullptr;
        int64_t hmCh = kTotalJoints, hmH = kHeatmapH, hmW = kHeatmapW;
        {
            pinpoint::pose::PoseScopedTimer runTimer(m_timing, &pinpoint::pose::PoseTiming::runMs);

            const char *inputNames[]  = { m_ort->inputName.c_str() };
            const char *outputNames[] = { m_ort->outputName.c_str() };

            if (ioBinding) {
                OrtState &s = *m_ort;
                if (!s.pinnedTried) {
                    s.pinnedTried = true;
                    if (m_epLabel.contains(QLatin1String("CUDA"))) {
                        try {
                            Ort::MemoryInfo pinned("CudaPinned", OrtDeviceAllocator, 0,
                                                   OrtMemTypeCPUOutput);
                            s.pinnedAlloc = std::make_unique<Ort::Allocator>(*s.session, pinned);
                        } catch (const Ort::Exception &e) {
                            ppWarn() << "[ViTPose] no pinned allocator:" << e.what()
                                     << "— IO binding on plain host memory";
                        }
                    }
                }
                if (!s.binding || s.boundBatch != b) {
                    s.binding.reset();
                    s.boundInput = Ort::Value{ nullptr };
                    if (s.pinnedAlloc) {
                        s.boundInput = Ort::Value::CreateTensor<float>(
                            *s.pinnedAlloc, inputShape.data(), inputShape.size());
                    } else {
                        s.boundBuf.assign(size_t(b) * kFrameFloats, 0.f);
                        const Ort::MemoryInfo cpu =
                            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                        s.boundInput = Ort::Value::CreateTensor<float>(
                            cpu, s.boundBuf.data(), s.boundBuf.size(),
                            inputShape.data(), inputShape.size());
                    }
                    s.binding = std::make_unique<Ort::IoBinding>(*s.session);
                    if (s.pinnedAlloc) {
                        s.binding->BindOutput(outputNames[0], s.pinnedAlloc->GetInfo());
                    } else {
                        const Ort::MemoryInfo cpu =
                            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                        s.binding->BindOutput(outputNames[0], cpu);
                    }
                    s.boundBatch = b;
                }
                float *dst = s.boundInput.GetTensorMutableData<float>();
                for (int64_t i = 0; i < n; ++i)
                    std::memcpy(dst + size_t(i) * kFrameFloats, inputs[size_t(i)]->data(),
                                kFrameFloats * sizeof(float));
                if (b > n)
                    std::memset(dst + size_t(n) * kFrameFloats, 0,
                                size_t(b - n) * kFrameFloats * sizeof(float));
                // Re-bound EVERY batch: BindInput copies a host tensor to the device
                // the graph wants AT BIND TIME, so a binding made once and refilled fed
                // the first batch's frames to every Run() on CUDA (6 Oct, studio: body
                // median 80–207 px off, while the Mac's CPU-memory binding was
                // bit-identical). The bind is this batch's one H→D copy.
                s.binding->BindInput(inputNames[0], s.boundInput);
                m_ort->session->Run(m_ort->runOpts, *s.binding);
                s.binding->SynchronizeOutputs();
                outputs = s.binding->GetOutputValues();
            } else {
                m_batchInput.resize(size_t(b) * kFrameFloats);
                for (int64_t i = 0; i < n; ++i)
                    std::memcpy(m_batchInput.data() + size_t(i) * kFrameFloats,
                                inputs[size_t(i)]->data(), kFrameFloats * sizeof(float));
                if (b > n)
                    std::fill(m_batchInput.begin() + ptrdiff_t(size_t(n) * kFrameFloats),
                              m_batchInput.end(), 0.f);
                const Ort::MemoryInfo memInfo =
                    Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                    memInfo, m_batchInput.data(), m_batchInput.size(),
                    inputShape.data(), inputShape.size());
                Ort::Value ins[] = { std::move(inputTensor) };
                outputs = m_ort->session->Run(m_ort->runOpts, inputNames, ins, 1, outputNames, 1);
            }
        }
        if (outputs.empty())
            throw Ort::Exception("no output", ORT_FAIL);
        const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[0] < n || shape[1] != kTotalJoints
            || shape[2] != kHeatmapH || shape[3] != kHeatmapW) {
            ppError() << "[ViTPose] unexpected heatmap shape — batch dropped";
            return false;
        }
        hmCh = shape[1]; hmH = shape[2]; hmW = shape[3];
        heatmaps = outputs[0].GetTensorData<float>();
        if (m_timing)
            for (int64_t i = 0; i < n; ++i)
                m_timing->addFrame();

        // Decode: the 17 body channels always; the tail only for whole-body runs,
        // and of that only what `channels` keeps. (frame, channel) pairs are split in
        // contiguous runs over the threads; each writes its own slots with its own
        // DARK scratch, so the result does not depend on the thread count.
        pinpoint::pose::PoseScopedTimer heatmapTimer(m_timing,
                                                     &pinpoint::pose::PoseTiming::heatmapDecodeMs);
        const int chEnd = m_decodeWholeBody ? int(hmCh) : kBodyJoints;
        const size_t plane = size_t(hmH) * size_t(hmW);
        std::vector<std::pair<int, int>> work;
        work.reserve(size_t(n) * size_t(chEnd));
        for (int f = 0; f < int(n); ++f)
            for (int c = 0; c < chEnd; ++c)
                if (pinpoint::pose::channelDecoded(channels, c))
                    work.emplace_back(f, c);
        const size_t nSlots = size_t(n) * size_t(kTotalJoints);
        std::vector<float> nx(nSlots, 0.f), ny(nSlots, 0.f), sc(nSlots, 0.f);
        const DecodeMode mode = m_decodeMode;
        auto decodeRange = [&](size_t a, size_t e) {
            std::vector<float> blur(plane, 0.f);
            for (size_t k = a; k < e; ++k) {
                const auto [f, c] = work[k];
                const size_t slot = size_t(f) * kTotalJoints + size_t(c);
                pinpoint::pose::decodeOne(mode, heatmaps + (size_t(f) * size_t(hmCh) + size_t(c)) * plane,
                                          int(hmW), int(hmH), blur.data(), nx[slot], ny[slot], sc[slot]);
            }
        };
        const size_t nThreads = std::min<size_t>(size_t(std::max(1, decodeThreads)), work.size());
        if (nThreads <= 1) {
            decodeRange(0, work.size());
        } else {
            std::vector<std::thread> pool;
            pool.reserve(nThreads - 1);
            const size_t per = (work.size() + nThreads - 1) / nThreads;
            for (size_t t = 1; t < nThreads; ++t) {
                const size_t a = std::min(work.size(), t * per);
                const size_t e = std::min(work.size(), a + per);
                pool.emplace_back(decodeRange, a, e);
            }
            decodeRange(0, std::min(work.size(), per));
            for (std::thread &th : pool)
                th.join();
        }

        out.resize(size_t(n));
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int f = 0; f < int(n); ++f) {
            FrameResult &r = out[size_t(f)];
            const size_t base = size_t(f) * kTotalJoints;
            r.pose.timestamp = now;
            for (int j = 0; j < kBodyJoints; ++j) {
                r.pose.keypoints[j].x     = nx[base + size_t(j)];
                r.pose.keypoints[j].y     = ny[base + size_t(j)];
                r.pose.keypoints[j].score = sc[base + size_t(j)];
            }
            float scoreSum = 0.f;
            for (int j = 0; j < kBodyJoints; ++j) scoreSum += r.pose.keypoints[j].score;
            r.pose.confidence = scoreSum / kBodyJoints;

            WholeBodyResult &wb = r.wholeBody;
            wb.valid = false;
            if (m_decodeWholeBody) {
                for (int j = 0; j < kBodyJoints; ++j) {
                    wb.kp[size_t(j)]    = QPointF(r.pose.keypoints[j].x, r.pose.keypoints[j].y);
                    wb.score[size_t(j)] = r.pose.keypoints[j].score;
                }
                for (int j = kBodyJoints; j < kTotalJoints; ++j) {
                    wb.kp[size_t(j)]    = QPointF(nx[base + size_t(j)], ny[base + size_t(j)]);
                    wb.score[size_t(j)] = sc[base + size_t(j)];
                }
                wb.valid = true;
            }
        }
        heatmapTimer.stop();
    } catch (const Ort::Exception &e) {
        ppError() << "[ViTPose] Batch inference error (" << n << "frames):" << e.what();
        out.clear();
        return false;
    }
    return true;
}

#endif // HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME
