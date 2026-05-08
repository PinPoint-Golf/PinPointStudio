#pragma once

#ifdef HAVE_OPENCV

#include "video_preprocessor_base.h"
#include <opencv2/core.hpp>
#include <array>

// Concrete preprocessor that converts incoming QVideoFrames to BGR cv::Mat.
//
// cv::Mat is registered as a Qt metatype at runtime in the constructor via
// qRegisterMetaType<cv::Mat>() — sufficient for function-pointer QueuedConnection
// in Qt 6.  Q_DECLARE_METATYPE is intentionally omitted: MOC compiles all moc
// files into one translation unit and any prior implicit instantiation of
// QMetaTypeId<cv::Mat> (e.g. from PoseEstimatorBase's slot signature) would
// cause a "explicit specialisation after instantiation" error.
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
