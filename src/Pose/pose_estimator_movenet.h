#pragma once

#if defined(HAVE_OPENCV) && defined(HAVE_MOVENET) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_base.h"
#include <array>
#include <memory>

class PoseEstimatorMoveNet : public PoseEstimatorBase
{
    Q_OBJECT

public:
    enum class ModelVariant {
        Lightning = 0,  // 192×192 input — faster, less accurate
        Thunder   = 1,  // 256×256 input — slower, more accurate
    };

    explicit PoseEstimatorMoveNet(QObject *parent = nullptr);
    ~PoseEstimatorMoveNet() override;

    static QString modelPath(ModelVariant v = ModelVariant::Lightning);
    static int     inputSize(ModelVariant v);
    static bool    isVariantAvailable(ModelVariant v);

    bool        isReady()   const { return m_ready; }
    ModelVariant variant()  const { return m_variant; }

public slots:
    void load();
    // Switches to a different model variant and reloads. Safe to call cross-thread
    // via QMetaObject::invokeMethod — estimatePose() is a no-op during the reload.
    void reloadModel(int variant);
    void estimatePose(const cv::Mat &frame) override;

private:
    ModelVariant m_variant = ModelVariant::Lightning;
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
