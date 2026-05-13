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
#include <QCamera>
#include <QCameraDevice>
#include <QVideoFrameFormat>

class QMediaCaptureSession;
class QVideoSink;

// Qt6 Multimedia implementation of VideoInputBase.
//
// Wires a QCamera into a QMediaCaptureSession with a QVideoSink that
// forwards every decoded frame into the videoFrameReady signal.
//
// Device selection:
//   Pass an empty string (default) to use QMediaDevices::defaultVideoInput().
//   Pass a description string to match against QCameraDevice::description(),
//   or a device-id string to match against QCameraDevice::id().
//   Use VideoInput::availableDevices() to enumerate candidates.

class VideoInput : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInput(QObject *parent = nullptr);
    ~VideoInput() override;

    // Returns every available camera on this platform.
    static QList<QCameraDevice> availableDevices();

    // -----------------------------------------------------------------------
    // VideoInputBase interface
    // -----------------------------------------------------------------------

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;

    // -----------------------------------------------------------------------
    // Configuration (call before start())
    // -----------------------------------------------------------------------

    // Hint for resolution / pixel format; the camera may ignore it.
    void setPreferredFormat(const QVideoFrameFormat &format);

private slots:
    void onCameraActiveChanged(bool active);
    void onCameraErrorOccurred(QCamera::Error error, const QString &errorString);
    void onVideoFrameChanged(const QVideoFrame &frame);

private:
    QCamera              *m_camera  = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink           *m_sink    = nullptr;
    QVideoFrameFormat     m_preferredFormat;
};
