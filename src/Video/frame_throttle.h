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

#pragma once

#ifdef HAVE_OPENCV

#include <QMutex>
#include <QObject>
#include <QVideoFrame>
#include <atomic>
#include "raw_video_frame.h"

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

    // Number of downstream consumers that must call clearBusy() before the
    // throttle releases the next frame.  Default is 1 (original behaviour).
    // Call before wiring any connections.
    void setConsumerCount(int n);
    void setRawConsumerCount(int n);

public slots:
    // QVideoFrame path — used by Qt Multimedia / Aravis.
    void offer(const QVideoFrame &frame);
    void clearBusy();

    // RawVideoFrame path — used by Bayer cameras (Spinnaker).
    void offerRaw(const RawVideoFrame &frame);
    void clearRawBusy();

signals:
    void frameReady(const QVideoFrame &frame);
    void rawFrameReady(const RawVideoFrame &frame);

private:
    std::atomic<bool> m_busy{false};
    QVideoFrame       m_latest;
    QMutex            m_mutex;

    std::atomic<bool> m_rawBusy{false};
    RawVideoFrame     m_rawLatest;
    QMutex            m_rawMutex;

    int m_skipFactor{1};
    int m_offerCount{0};
    int m_rawOfferCount{0};

    int                m_consumerCount{1};
    std::atomic<int>   m_doneCount{0};

    int                m_rawConsumerCount{1};
    std::atomic<int>   m_rawDoneCount{0};
};

#endif // HAVE_OPENCV
