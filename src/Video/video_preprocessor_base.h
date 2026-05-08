#pragma once

#include <QObject>
#include <QVideoFrame>

// Abstract base for classes that pre-process raw video frames into a form
// suitable for downstream analysis (e.g. pose estimation).
//
// Subclasses override processFrame() and are connected to a VideoInputBase
// via:
//   connect(input, &VideoInputBase::videoFrameReady,
//           preprocessor, &VideoPreprocessorBase::processFrame,
//           Qt::QueuedConnection);
//
// Move the subclass to a dedicated QThread so conversion work stays off the
// GUI thread.  Each concrete subclass defines its own output signal carrying
// the converted frame data (e.g. cv::Mat for VideoPreprocessorOpenCV).

class VideoPreprocessorBase : public QObject
{
    Q_OBJECT

public:
    explicit VideoPreprocessorBase(QObject *parent = nullptr);
    ~VideoPreprocessorBase() override = default;

public slots:
    virtual void processFrame(const QVideoFrame &frame) = 0;

signals:
    // Rolling average preprocessing time in milliseconds, emitted each frame
    // once the measurement window is warm.  Crosses to the main thread via
    // Qt::QueuedConnection — connect in VideoController.
    void preprocessStatsUpdated(double avgMs);
};
