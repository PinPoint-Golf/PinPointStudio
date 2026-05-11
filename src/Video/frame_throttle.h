#pragma once

#ifdef HAVE_OPENCV

#include <QMutex>
#include <QObject>
#include <QVideoFrame>
#include <atomic>

// Sits between the camera and VideoPreprocessorOpenCV.
// Ensures the preprocessor (and therefore the pose estimator) only ever
// works on the freshest available frame.
//
// Thread model (all connections are DirectConnection):
//   offer()     — capture thread  (videoFrameReady → offer)
//   clearBusy() — pose thread     (estimationDone  → clearBusy)
//   frameReady  — QueuedConnection → preprocessor.processFrame
//
// While inference is in flight every incoming QVideoFrame overwrites
// m_latest, so only the most recent one is kept.  When clearBusy() fires
// it immediately emits m_latest (if any), keeping m_busy=true and starting
// the next inference without going idle.  This bounds skeleton lag to one
// inference cycle regardless of camera frame rate.
//
// The preprocessor never sees stale frames and its event queue never builds
// up a backlog.

class FrameThrottle : public QObject
{
    Q_OBJECT

public:
    explicit FrameThrottle(QObject *parent = nullptr);

    // Only forward every n-th offered frame to the preprocessor.
    // Default is 1 (every frame).  Set to 2 to halve the pose rate, etc.
    void setSkipFactor(int n);

public slots:
    void offer(const QVideoFrame &frame);
    void clearBusy();

signals:
    void frameReady(const QVideoFrame &frame);

private:
    std::atomic<bool> m_busy{false};
    QVideoFrame       m_latest;
    QMutex            m_mutex;
    int               m_skipFactor{1};
    int               m_offerCount{0};
};

#endif // HAVE_OPENCV
