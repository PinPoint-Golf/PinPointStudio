#pragma once

#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_base.h"
#include <array>
#include <memory>

// Concrete PoseEstimatorBase backed by MoveNet Lightning (ONNX Runtime).
//
// Lifecycle:
//   auto *e = new PoseEstimatorMoveNet();
//   e->moveToThread(poseThread);
//   connect(poseThread, &QThread::started, e, &PoseEstimatorMoveNet::load);
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           e, &PoseEstimatorBase::estimatePose, Qt::QueuedConnection);
//
// load() is idempotent; re-calling it after a failure retries the load.
// estimatePose() is a no-op until load() succeeds.

class PoseEstimatorMoveNet : public PoseEstimatorBase
{
    Q_OBJECT

public:
    explicit PoseEstimatorMoveNet(QObject *parent = nullptr);
    ~PoseEstimatorMoveNet() override;

    // Platform-aware path to the bundled model file.
    static QString modelPath();

    bool isReady() const { return m_ready; }

public slots:
    void load();
    void estimatePose(const cv::Mat &frame) override;

private:
    bool m_ready = false;

    // 30-sample rolling windows — same pattern as VideoPreprocessorOpenCV.
    static constexpr int kWindowSize = 30;
    std::array<double, kWindowSize> m_inferenceSamples{};
    std::array<double, kWindowSize> m_intervalSamples{};
    double m_inferenceSum = 0.0;
    double m_intervalSum  = 0.0;
    int    m_timingIndex  = 0;
    int    m_timingCount  = 0;
    qint64 m_lastCallNs   = -1; // -1 = first frame not yet seen

    // ONNX Runtime state isolated in OrtState so its headers stay out of this
    // header (same pimpl pattern as KokoroTTSEngine).
    struct OrtState;
    std::unique_ptr<OrtState> m_ort;
};

#endif // HAVE_OPENCV && HAVE_MOVENET && HAVE_ONNXRUNTIME
