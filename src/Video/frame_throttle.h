#pragma once

#ifdef HAVE_OPENCV

#include <QObject>
#include <opencv2/core.hpp>

// Sits between VideoPreprocessorOpenCV and PoseEstimatorBase.
// Passes a frame only when the previous inference has completed, preventing
// unbounded queue growth on the pose estimator thread.
//
// Thread model: move to the preprocessor thread.
//   - offer()     — connect with Qt::DirectConnection from framePreprocessed
//                   (same thread; inline filter, no queue overhead)
//   - clearBusy() — connect with Qt::QueuedConnection from estimationDone
//                   (pose thread → event posted to preprocessor thread)
//   - frameReady  — connect with Qt::QueuedConnection to estimatePose
//                   (preprocessor thread → pose thread)

class FrameThrottle : public QObject
{
    Q_OBJECT

public:
    explicit FrameThrottle(QObject *parent = nullptr);

public slots:
    void offer(const cv::Mat &frame);
    void clearBusy();

signals:
    void frameReady(const cv::Mat &frame);

private:
    bool m_busy = false;
};

#endif // HAVE_OPENCV
