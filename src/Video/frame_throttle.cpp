/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

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

void FrameThrottle::setConsumerCount(int n)
{
    m_consumerCount = qMax(1, n);
}

void FrameThrottle::setRawConsumerCount(int n)
{
    m_rawConsumerCount = qMax(1, n);
}

void FrameThrottle::offer(const QVideoFrame &frame, qint64 tUs)
{
    if (++m_offerCount % m_skipFactor != 0)
        return;

    // The busy check and both busy transitions happen under the mutex:
    // a relaxed check outside it raced clearBusy() (busy read true here,
    // cleared there before our park) and left a frame stranded in m_latest
    // while the throttle sat idle — replayed out-of-order one cycle later.
    {
        QMutexLocker lk(&m_mutex);
        if (m_busy.load(std::memory_order_relaxed)) {
            m_latest    = frame;
            m_latestTUs = tUs;
            return;
        }
        m_busy.store(true, std::memory_order_relaxed);
    }
    emit frameReady(frame, tUs);
}

void FrameThrottle::clearBusy()
{
    if (++m_doneCount < m_consumerCount)
        return;
    m_doneCount.store(0, std::memory_order_relaxed);

    QVideoFrame next;
    qint64      nextTUs = -1;
    {
        QMutexLocker lk(&m_mutex);
        next        = m_latest;
        nextTUs     = m_latestTUs;
        m_latest    = QVideoFrame();
        m_latestTUs = -1;
        if (!next.isValid())
            m_busy.store(false, std::memory_order_relaxed);  // idle decision under the lock
    }
    if (next.isValid())
        emit frameReady(next, nextTUs);
}

void FrameThrottle::offerRaw(const RawVideoFrame &frame, qint64 tUs)
{
    if (++m_rawOfferCount % m_skipFactor != 0)
        return;

    // Same lock discipline as offer() — see comment there.
    {
        QMutexLocker lk(&m_rawMutex);
        if (m_rawBusy.load(std::memory_order_relaxed)) {
            m_rawLatest    = frame;
            m_rawLatestTUs = tUs;
            return;
        }
        m_rawBusy.store(true, std::memory_order_relaxed);
    }
    emit rawFrameReady(frame, tUs);
}

void FrameThrottle::clearRawBusy()
{
    if (++m_rawDoneCount < m_rawConsumerCount)
        return;
    m_rawDoneCount.store(0, std::memory_order_relaxed);

    RawVideoFrame next;
    qint64        nextTUs = -1;
    {
        QMutexLocker lk(&m_rawMutex);
        next           = m_rawLatest;
        nextTUs        = m_rawLatestTUs;
        m_rawLatest    = RawVideoFrame();
        m_rawLatestTUs = -1;
        if (next.isNull())
            m_rawBusy.store(false, std::memory_order_relaxed);
    }
    if (!next.isNull())
        emit rawFrameReady(next, nextTUs);
}

#endif // HAVE_OPENCV
