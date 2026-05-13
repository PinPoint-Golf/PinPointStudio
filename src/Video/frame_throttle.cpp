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
