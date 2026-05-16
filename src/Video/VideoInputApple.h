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

// Private implementation — defined only in VideoInputApple.mm so that
// Objective-C types never leak into C++ translation units.
struct VideoInputApplePrivate;

// macOS-native camera backend using AVCaptureSession / AVCaptureVideoDataOutput.
//
// Completely bypasses Qt's QCamera so that Qt's QCameraPermission plugin
// (which is absent in the FFmpeg multimedia build) is never invoked.
// Frames arrive as BGRA CVPixelBuffers and are wrapped in a QVideoFrame
// before being emitted on videoFrameReady().
//
// AVFoundation permission must be obtained via requestCameraPermission()
// (macos_permissions.h) before calling start().

class VideoInputApple : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputApple(QObject *parent = nullptr);
    ~VideoInputApple() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;
    CameraCapabilities queryCapabilities() const override;

    // Called by the Obj-C sample-buffer delegate — not for external use.
    void onFrameCaptured(const QVideoFrame &frame);

private:
    VideoInputApplePrivate *d = nullptr;
};
