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

#include "video_frame_processor_base.h"
#include <QImage>

// Concrete VideoFrameProcessorBase that converts each arriving QVideoFrame
// to a QImage and emits it.  Intended to run on a dedicated thread via
// VideoInputBase::connectProcessor() + moveToThread().

class VideoFrameProcessor : public VideoFrameProcessorBase
{
    Q_OBJECT

public:
    explicit VideoFrameProcessor(QObject *parent = nullptr);

public slots:
    void processFrame(const QVideoFrame &frame) override;

signals:
    void frameReady(const QImage &image);
};
