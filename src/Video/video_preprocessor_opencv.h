#pragma once

#ifdef HAVE_OPENCV

#include "video_preprocessor_base.h"
#include <opencv2/core.hpp>
#include <array>

// cv::Mat uses shared reference counting, so it is safe to pass through Qt's
// queued signal/slot mechanism.  The metatype is registered in the constructor
// so QueuedConnection delivery to the pose-estimator thread works without an
// explicit qRegisterMetaType<cv::Mat>() call at the use site.
Q_DECLARE_METATYPE(cv::Mat)

// Concrete preprocessor that converts incoming QVideoFrames to BGR cv::Mat.
//
// Wire the output signal to the pose estimator:
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           poseEstimator, &PoseEstimatorBase::estimatePose,
//           Qt::QueuedConnection);

class VideoPreprocessorOpenCV : public VideoPreprocessorBase
{
    Q_OBJECT

public:
    explicit VideoPreprocessorOpenCV(QObject *parent = nullptr);

public slots:
    void processFrame(const QVideoFrame &frame) override;

signals:
    void framePreprocessed(const cv::Mat &mat);

private:
    static constexpr int kWindowSize = 30;
    std::array<double, kWindowSize> m_timingSamples{};
    double m_timingSum   = 0.0;
    int    m_timingIndex = 0;
    int    m_timingCount = 0;
};

#endif // HAVE_OPENCV
