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
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include "pp_debug.h"
#include "pp_profiler.h"

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
static constexpr int kLeftHandCh  = 91;  // first left-hand channel (91–111)
static constexpr int kRightHandCh = 112; // first right-hand channel (112–132)

// ImageNet normalisation constants (RGB order).
static constexpr float kMean[3] = { 0.485f, 0.456f, 0.406f };
static constexpr float kStd[3]  = { 0.229f, 0.224f, 0.225f };

// Decode one [kHeatmapH, kHeatmapW] heatmap channel: argmax + ±0.25 sub-pixel
// shift toward the higher neighbour ('default' mode), normalised to [0, 1] in
// frame space (the full input image was resized to exactly kInputW × kInputH,
// so heatmap coords divide directly by heatmap dims).  Shared by the body and
// hand decodes — identical math to the original inline body-joint decode.
static void decodeHeatmapChannel(const float *hm, float &nx, float &ny, float &score)
{
    int   maxIdx = 0;
    float maxVal = hm[0];
    for (int i = 1; i < kHeatmapH * kHeatmapW; ++i) {
        if (hm[i] > maxVal) { maxVal = hm[i]; maxIdx = i; }
    }
    const int iHx = maxIdx % kHeatmapW;
    const int iHy = maxIdx / kHeatmapW;
    float hx = static_cast<float>(iHx);
    float hy = static_cast<float>(iHy);

    if (iHx > 0 && iHx < kHeatmapW - 1)
        hx += (hm[iHy * kHeatmapW + iHx + 1] > hm[iHy * kHeatmapW + iHx - 1])
              ? 0.25f : -0.25f;
    if (iHy > 0 && iHy < kHeatmapH - 1)
        hy += (hm[(iHy + 1) * kHeatmapW + iHx] > hm[(iHy - 1) * kHeatmapW + iHx])
              ? 0.25f : -0.25f;

    nx    = hx / kHeatmapW;
    ny    = hy / kHeatmapH;
    score = maxVal;
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
};

// ---------------------------------------------------------------------------

PoseEstimatorViTPose::PoseEstimatorViTPose(QObject *parent)
    : PoseEstimatorBase(parent)
{}

PoseEstimatorViTPose::~PoseEstimatorViTPose()
{
    if (m_ort && m_ort->modelBytes > 0)
        PP_PROFILE_MEM_SUB("ONNX.Pose", m_ort->modelBytes);
}

QString PoseEstimatorViTPose::modelPath()
{
    const QString file = QStringLiteral(VITPOSE_MODEL_FILE);
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/../Resources/models/") + file;
#else
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/models/") + file;
#endif
}

bool PoseEstimatorViTPose::isAvailable()
{
    return QFile::exists(modelPath());
}

void PoseEstimatorViTPose::load()
{
    const QString path = modelPath();
    if (!QFile::exists(path)) {
        ppError() << "[ViTPose] Model not found:" << path;
        return;
    }

    m_ready = false;
    if (m_ort && m_ort->modelBytes > 0)   // reload: release the prior arena estimate
        PP_PROFILE_MEM_SUB("ONNX.Pose", m_ort->modelBytes);
    m_ort   = std::make_unique<OrtState>();
    m_ort->opts.SetIntraOpNumThreads(1);
    m_ort->opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

    // Execution provider cascade — identical to PoseEstimatorMoveNet.
    QString epLabel;

#ifdef WITH_COREML
    if (epLabel.isEmpty()) {
        try {
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
                m_ort->opts, COREML_FLAG_USE_NONE));
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
        if (hasNv) {
            try {
                OrtCUDAProviderOptions cuda{};
                cuda.device_id = 0;
                m_ort->opts.AppendExecutionProvider_CUDA(cuda);
                epLabel = QStringLiteral("CUDA");
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
            m_ort->opts.AppendExecutionProvider("DML");
            epLabel = QStringLiteral("DirectML");
            ppInfo() << "[ViTPose] DirectML execution provider active";
        } catch (const Ort::Exception &e) {
            ppInfo() << "[ViTPose] DirectML unavailable:" << e.what() << "— falling back";
        }
    }
#endif

    if (epLabel.isEmpty())
        ppInfo() << "[ViTPose] No GPU EP available — using CPU";
    else
        ppInfo() << "[ViTPose] Using" << epLabel << "execution provider";

    try {
#ifdef Q_OS_WIN
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toStdWString().c_str(), m_ort->opts);
#else
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toUtf8().constData(), m_ort->opts);
#endif
    } catch (const Ort::Exception &e) {
        ppError() << "[ViTPose] Failed to load model:" << e.what();
        m_ort.reset();
        return;
    }

    m_ort->inputName  = m_ort->session->GetInputNameAllocated(0, m_ort->allocator).get();
    m_ort->outputName = m_ort->session->GetOutputNameAllocated(0, m_ort->allocator).get();

    m_ort->modelBytes = QFileInfo(path).size();   // [seam] file size as ORT arena proxy
    PP_PROFILE_MEM_ADD("ONNX.Pose", m_ort->modelBytes);

    ppInfo() << "[ViTPose] Loaded — input:" << m_ort->inputName.c_str()
             << "output:" << m_ort->outputName.c_str()
             << "size:" << kInputW << "×" << kInputH;

    m_ort->wallTimer.start();
    m_lastCallNs = -1;
    m_ready      = true;
    emit poseBackendReady(epLabel);
}

void PoseEstimatorViTPose::estimatePose(const cv::Mat &frame)
{
    // Hands from a previous frame must never outlive the call that produced
    // them — invalidate before any early-out. No-op on the live path.
    if (m_decodeHands)
        m_lastHands.valid = false;

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
        // channels always, plus the hand channels (91–132) when opted in.
        // The remaining face/feet channels are ignored.
        const float *heatmapData = outputs[0].GetTensorData<float>();

        PoseResult result;
        result.timestamp = QDateTime::currentMSecsSinceEpoch();

        for (int j = 0; j < kBodyJoints; ++j) {
            const float *hm = heatmapData + j * kHeatmapH * kHeatmapW;
            decodeHeatmapChannel(hm, result.keypoints[j].x,
                                 result.keypoints[j].y, result.keypoints[j].score);
        }

        float scoreSum = 0.f;
        for (int j = 0; j < kBodyJoints; ++j) scoreSum += result.keypoints[j].score;
        result.confidence = scoreSum / kBodyJoints;

        // COCO-WholeBody hand channels — same decode pass, opt-in only (the
        // live 60 Hz path never pays for this).
        if (m_decodeHands) {
            const auto decodeHand = [&](int firstCh, std::array<QPointF, 21> &pts,
                                        std::array<float, 21> &scores) {
                for (int k = 0; k < WholeBodyHands::kHandJoints; ++k) {
                    const float *hm = heatmapData + (firstCh + k) * kHeatmapH * kHeatmapW;
                    float nx = 0.f, ny = 0.f, score = 0.f;
                    decodeHeatmapChannel(hm, nx, ny, score);
                    pts[k]    = QPointF(nx, ny);
                    scores[k] = score;
                }
            };
            decodeHand(kLeftHandCh,  m_lastHands.left,  m_lastHands.leftScore);
            decodeHand(kRightHandCh, m_lastHands.right, m_lastHands.rightScore);
            m_lastHands.valid = true;
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

#endif // HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME
