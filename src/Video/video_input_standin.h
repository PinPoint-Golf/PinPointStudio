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

// THE STAND-IN CAMERA (#394, step 81; docs/implementation/platform_d_capture_seam.md, T2): a camera back-end beside
// Spinnaker, Aravis and Apple that opens no device. It draws its own frames (a dark field with a bright bar that moves
// one step a frame, so no two neighbouring frames are alike) at a steady rate, on the thread it is started on, stamped
// with the event buffer's clock as they are drawn, and hands them on as every Qt-frame back-end does.
//
// ⚠ BUILT INTO THE CAPTURE TEST RIG ONLY. Its source is compiled into capture_rig_test and nowhere else
// (cmake/PinPointCaptureRig.cmake), and VideoInputFactory creates one only in that build (PP_STANDIN_CAMERA, from the
// rig's own generated header). The app, a Release build and an installer never hold one. A device is listed for it by
// registering it with the device enumerator (Backend::StandIn), which only the rig does.

#include "video_input_base.h"

#include <QSize>

class QTimer;

class VideoInputStandIn : public VideoInputBase
{
    Q_OBJECT

public:
    // The frames it draws: 320×240 BGRA at 60 fps.
    static constexpr int    kWidth  = 320;
    static constexpr int    kHeight = 240;
    static constexpr double kFps    = 60.0;

    explicit VideoInputStandIn(QObject *parent = nullptr);
    ~VideoInputStandIn() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop() override;
    void              suspend() override;
    void              resume() override;
    bool              isActive() const override;
    QVideoFrameFormat frameFormat() const override;
    CameraCapabilities queryCapabilities() const override;
    qint64            lastFrameInstantUs() const override { return m_lastFrameUs; }

    // What the enumerator lists for it (the rig registers it with these).
    static CameraCapabilities capabilities();

    // Frames drawn since start(): read by the rig.
    quint64 framesDrawn() const { return m_frames; }

    // THE CABLE PULLED (the rig's hook, for every stand-in at once): at its next frame each stand-in stops drawing and
    // reports what a real back-end does when its camera goes, an error and the Error state. Cleared, a new start()
    // draws again.
    static void setUnplugged(bool unplugged);

private:
    void drawFrame();

    QTimer *m_timer = nullptr;
    quint64 m_frames = 0;
    qint64  m_lastFrameUs = 0;
};
