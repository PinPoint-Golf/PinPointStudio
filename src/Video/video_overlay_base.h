#pragma once

#include <QObject>
#include <QVideoFrame>

// Abstract base for classes that annotate video frames before display.
//
// Subclasses override overlayFrame(), draw into the frame, and emit
// frameReady() with the annotated result.  Move the subclass to a
// dedicated QThread and wire via QueuedConnection:
//
//   connect(videoInput, &VideoInputBase::videoFrameReady,
//           overlay,    &VideoOverlayBase::overlayFrame,
//           Qt::QueuedConnection);
//   connect(overlay, &VideoOverlayBase::frameReady,
//           controller, &VideoController::onAnnotatedFrame,
//           Qt::QueuedConnection);

class VideoOverlayBase : public QObject
{
    Q_OBJECT

public:
    explicit VideoOverlayBase(QObject *parent = nullptr);
    ~VideoOverlayBase() override = default;

public slots:
    virtual void overlayFrame(const QVideoFrame &frame) = 0;

signals:
    void frameReady(const QVideoFrame &frame);
};
