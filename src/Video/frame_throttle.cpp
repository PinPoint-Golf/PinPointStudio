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
        next     = m_latest;
        m_latest = QVideoFrame();
    }
    if (next.isValid()) {
        emit frameReady(next);
    } else {
        m_busy.store(false, std::memory_order_relaxed);
    }
}

void FrameThrottle::offerRaw(const RawVideoFrame &frame)
{
    if (++m_rawOfferCount % m_skipFactor != 0)
        return;

    if (m_rawBusy.load(std::memory_order_relaxed)) {
        QMutexLocker lk(&m_rawMutex);
        m_rawLatest = frame;
        return;
    }
    m_rawBusy.store(true, std::memory_order_relaxed);
    emit rawFrameReady(frame);
}

void FrameThrottle::clearRawBusy()
{
    RawVideoFrame next;
    {
        QMutexLocker lk(&m_rawMutex);
        next        = m_rawLatest;
        m_rawLatest = RawVideoFrame();
    }
    if (!next.isNull()) {
        emit rawFrameReady(next);
    } else {
        m_rawBusy.store(false, std::memory_order_relaxed);
    }
}

#endif // HAVE_OPENCV
