#ifdef HAVE_OPENCV

#include "frame_throttle.h"

#include <QMutexLocker>

FrameThrottle::FrameThrottle(QObject *parent)
    : QObject(parent)
{}

void FrameThrottle::setSkipFactor(int n)
{
    m_skipFactor = qMax(1, n);
}

void FrameThrottle::offer(const QVideoFrame &frame)
{
    if (++m_offerCount % m_skipFactor != 0)
        return;

    if (m_busy.load(std::memory_order_relaxed)) {
        QMutexLocker lk(&m_mutex);
        m_latest = frame;
        return;
    }
    m_busy.store(true, std::memory_order_relaxed);
    emit frameReady(frame);
}

void FrameThrottle::clearBusy()
{
    QVideoFrame next;
    {
        QMutexLocker lk(&m_mutex);
        next    = m_latest;
        m_latest = QVideoFrame();
    }
    if (next.isValid()) {
        // Keep m_busy=true — immediately kick off next inference on the
        // freshest frame that arrived during the previous cycle.
        emit frameReady(next);
    } else {
        m_busy.store(false, std::memory_order_relaxed);
    }
}

#endif // HAVE_OPENCV
