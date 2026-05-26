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

#include "video_input_base.h"

// Spinnaker-based backend for Teledyne/FLIR industrial cameras.
// Only supported on Windows.
//
// Uses the Spinnaker C++ API to capture frames.

class VideoInputSpinnaker : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputSpinnaker(QObject *parent = nullptr);
    ~VideoInputSpinnaker() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()      const override;
    QVideoFrameFormat frameFormat()   const override;
    bool              emitsRawBayer() const override { return m_emitRaw; }
    CameraCapabilities queryCapabilities() const override;

private:
    void captureLoop();

    void *m_system     = nullptr; // Spinnaker::SystemPtr*
    void *m_camera     = nullptr; // Spinnaker::CameraPtr*
    void *m_logHandler = nullptr; // SpinLogHandler* (Windows/HAVE_SPINNAKER only)
    bool  m_streaming = false;
    bool  m_abort     = false;
    int   m_bayerPattern = 0;   // RawVideoFrame::BayerPattern int, valid when Bayer format selected
    bool  m_emitRaw      = false; // true when camera runs a Bayer pixel format
};
