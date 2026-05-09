#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_movenet.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

#ifdef WITH_COREML
#  include <coreml_provider_factory.h>
#endif
#ifdef WITH_DIRECTML
#  include <dml_provider_factory.h>
#endif

// All ORT state lives here so onnxruntime_cxx_api.h is not pulled in via the header.
struct PoseEstimatorMoveNet::OrtState {
    Ort::Env            env     { ORT_LOGGING_LEVEL_WARNING, "MoveNet" };
    Ort::SessionOptions opts;
    Ort::RunOptions     runOpts;
    Ort::AllocatorWithDefaultOptions allocator;
    std::unique_ptr<Ort::Session>    session;

    std::string inputName;
    std::string outputName;

    // Persistent nanosecond-resolution timer for interval tracking.
    QElapsedTimer wallTimer;
};

// ---------------------------------------------------------------------------

PoseEstimatorMoveNet::PoseEstimatorMoveNet(QObject *parent)
    : PoseEstimatorBase(parent)
{}

PoseEstimatorMoveNet::~PoseEstimatorMoveNet() = default;

QString PoseEstimatorMoveNet::modelPath()
{
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/../Resources/models/")
         + QStringLiteral(MOVENET_MODEL_FILE);
#else
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/models/")
         + QStringLiteral(MOVENET_MODEL_FILE);
#endif
}

void PoseEstimatorMoveNet::load()
{
    const QString path = modelPath();
    if (!QFile::exists(path)) {
        qWarning() << "[MoveNet] Model not found:" << path;
        return;
    }

    m_ready = false;
    m_ort   = std::make_unique<OrtState>();
    m_ort->opts.SetIntraOpNumThreads(1);
    m_ort->opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

    // Execution provider cascade — mirrors KokoroTTSEngine.
    QString epLabel;

#ifdef WITH_COREML
    if (epLabel.isEmpty()) {
        try {
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
                m_ort->opts, COREML_FLAG_USE_NONE));
            epLabel = QStringLiteral("CoreML");
            qDebug() << "[MoveNet] CoreML execution provider active";
        } catch (const Ort::Exception &e) {
            qDebug() << "[MoveNet] CoreML unavailable:" << e.what() << "— falling back";
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
                qDebug() << "[MoveNet] CUDA execution provider active";
            } catch (const Ort::Exception &e) {
                qDebug() << "[MoveNet] CUDA unavailable:" << e.what() << "— falling back";
            }
        }
    }
#endif

#ifdef WITH_DIRECTML
    if (epLabel.isEmpty()) {
        try {
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(m_ort->opts, 0));
            epLabel = QStringLiteral("DirectML");
            qDebug() << "[MoveNet] DirectML execution provider active";
        } catch (const Ort::Exception &e) {
            qDebug() << "[MoveNet] DirectML unavailable:" << e.what() << "— falling back";
        }
    }
#endif

    if (epLabel.isEmpty())
        qDebug() << "[MoveNet] No GPU EP available — using CPU";
    else
        qDebug() << "[MoveNet] Using" << epLabel << "execution provider";

    try {
#ifdef Q_OS_WIN
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toStdWString().c_str(), m_ort->opts);
#else
        m_ort->session = std::make_unique<Ort::Session>(
            m_ort->env, path.toUtf8().constData(), m_ort->opts);
#endif
    } catch (const Ort::Exception &e) {
        qWarning() << "[MoveNet] Failed to load model:" << e.what();
        m_ort.reset();
        return;
    }

    // Query actual input/output names from the graph.
    m_ort->inputName  = m_ort->session->GetInputNameAllocated(0, m_ort->allocator).get();
    m_ort->outputName = m_ort->session->GetOutputNameAllocated(0, m_ort->allocator).get();

    qDebug() << "[MoveNet] Loaded — input:" << m_ort->inputName.c_str()
             << "output:" << m_ort->outputName.c_str();

    m_ort->wallTimer.start();
    m_lastCallNs = -1;
    m_ready      = true;
    emit poseBackendReady(epLabel);
}

void PoseEstimatorMoveNet::estimatePose(const cv::Mat &frame)
{
    if (!m_ready || frame.empty())
        return;

    // Record arrival time for FPS (interval between successive calls).
    const qint64 nowNs = m_ort->wallTimer.nsecsElapsed();

    // Preprocess: resize → RGB → int32 [0, 255].
    // The Xenova/Transformers.js MoveNet ONNX export expects raw pixel values
    // as int32, not normalised float32.
    cv::Mat resized, rgb, rgb32;
    cv::resize(frame, resized, cv::Size(192, 192));
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb32, CV_32SC3);  // uint8 [0,255] → int32 [0,255], no scale

    // Inference.
    QElapsedTimer inferTimer;
    inferTimer.start();

    try {
        Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 4> inputShape{ 1, 192, 192, 3 };
        auto inputTensor = Ort::Value::CreateTensor<int32_t>(
            memInfo,
            rgb32.ptr<int32_t>(), 192 * 192 * 3,
            inputShape.data(), inputShape.size());

        const char *inputNames[]  = { m_ort->inputName.c_str() };
        const char *outputNames[] = { m_ort->outputName.c_str() };
        Ort::Value  inputs[]      = { std::move(inputTensor) };

        auto outputs = m_ort->session->Run(
            m_ort->runOpts,
            inputNames, inputs, 1,
            outputNames, 1);

        // Parse [1, 1, 17, 3] → PoseResult.  Each keypoint is (y, x, score).
        const float *out = outputs[0].GetTensorData<float>();
        PoseResult result;
        result.timestamp = QDateTime::currentMSecsSinceEpoch();
        for (int j = 0; j < PoseResult::kNumKeypoints; ++j) {
            result.keypoints[j].y     = out[j * 3 + 0];
            result.keypoints[j].x     = out[j * 3 + 1];
            result.keypoints[j].score = out[j * 3 + 2];
        }
        float scoreSum = 0.f;
        for (const auto &kp : result.keypoints) scoreSum += kp.score;
        result.confidence = scoreSum / PoseResult::kNumKeypoints;

        emit poseEstimated(result);

    } catch (const Ort::Exception &e) {
        qWarning() << "[MoveNet] Inference error:" << e.what();
        return;
    }

    const double inferMs   = inferTimer.nsecsElapsed() / 1e6;
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
        const double avgInferMs = m_inferenceSum / kWindowSize;
        const double avgInterval = m_intervalSum / kWindowSize;
        const double fps = (avgInterval > 0.0) ? 1000.0 / avgInterval : 0.0;
        emit poseStatsUpdated(avgInferMs, fps);
    }
}

#endif // HAVE_OPENCV && HAVE_MOVENET && HAVE_ONNXRUNTIME
