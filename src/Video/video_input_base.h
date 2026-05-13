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

#include <QObject>
#include <QVideoFrameFormat>
#include "raw_video_frame.h"

// Abstract base for camera / video capture.
//
// Subclasses implement the transport (Qt6 QCamera, platform-specific, etc.)
// and emit videoFrameReady() whenever a decoded frame arrives.
//
// Typical usage:
//   VideoInput *in = new VideoInput(this);
//   in->start();                           // default camera
//   in->start("Front Camera");             // named device

class VideoInputBase : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Active,
        Suspended,
        Error,
    };
    Q_ENUM(State)

    explicit VideoInputBase(QObject *parent = nullptr);
    ~VideoInputBase() override = default;

    // -----------------------------------------------------------------------
    // Transport control
    // -----------------------------------------------------------------------

    // Start capture on the named device; empty string selects the system default.
    // deviceId is matched against QCameraDevice::id() (byte string) or description().
    virtual bool start(const QString &deviceId = {}) = 0;
    virtual void stop() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;

    // -----------------------------------------------------------------------
    // Status
    // -----------------------------------------------------------------------

    virtual bool              isActive()      const = 0;
    virtual QVideoFrameFormat frameFormat()   const = 0;
    virtual State             state()         const;
    // Returns true when the backend emits rawVideoFrameReady (raw Bayer bytes)
    // rather than videoFrameReady (pre-decoded frames).  Valid after start().
    virtual bool              emitsRawBayer() const { return false; }

signals:
    // Emitted for every decoded camera frame (non-Bayer backends).
    void videoFrameReady(const QVideoFrame &frame);

    // Emitted by Bayer backends instead of videoFrameReady.  Data is packed
    // (stride == width) so the GPU upload path needs no row-stride adjustment.
    void rawVideoFrameReady(const RawVideoFrame &frame);

    void stateChanged(VideoInputBase::State state);
    void errorOccurred(const QString &message);

protected:
    State m_state = State::Stopped;
};
