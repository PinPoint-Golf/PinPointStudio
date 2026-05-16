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

// Aravis-based backend for industrial cameras (GigE Vision / USB3 Vision).
//
// Uses the Aravis 0.8 C API to capture frames. Since industrial cameras often
// output raw Bayer data, this backend typically emits frames in Format_Grayscale8
// (representing the raw Bayer mosaic) and relies on a GPU shader for debayering.

class VideoInputAravis : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputAravis(QObject *parent = nullptr);
    ~VideoInputAravis() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;
    CameraCapabilities queryCapabilities() const override;

private:
    void captureLoop();

    void *m_camera    = nullptr; // ArvCamera*
    void *m_stream    = nullptr; // ArvStream*
    bool  m_streaming = false;
    bool  m_abort     = false;
};
