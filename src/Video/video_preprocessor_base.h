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
#include <QVideoFrame>
#include "raw_video_frame.h"

// Abstract base for classes that pre-process raw video frames into a form
// suitable for downstream analysis (e.g. pose estimation).
//
// Subclasses override processFrame() and are connected to a VideoInputBase
// via:
//   connect(input, &VideoInputBase::videoFrameReady,
//           preprocessor, &VideoPreprocessorBase::processFrame,
//           Qt::QueuedConnection);
//
// Move the subclass to a dedicated QThread so conversion work stays off the
// GUI thread.  Each concrete subclass defines its own output signal carrying
// the converted frame data (e.g. cv::Mat for VideoPreprocessorOpenCV).

class VideoPreprocessorBase : public QObject
{
    Q_OBJECT

public:
    explicit VideoPreprocessorBase(QObject *parent = nullptr);
    ~VideoPreprocessorBase() override = default;

public slots:
    virtual void processFrame(const QVideoFrame &frame) = 0;
    virtual void processRawFrame(const RawVideoFrame &frame) {}

signals:
    // Rolling average preprocessing time in milliseconds, emitted each frame
    // once the measurement window is warm.  Crosses to the main thread via
    // Qt::QueuedConnection — connect in VideoController.
    void preprocessStatsUpdated(double avgMs);

    // Rolling average camera arrival rate in fps, emitted each frame once
    // the measurement window is warm.
    void cameraFpsUpdated(double fps);
};
