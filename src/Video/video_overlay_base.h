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

// Abstract base for classes that annotate video frames before display.
//
// Subclasses override overlayFrame(), draw into the frame, and emit
// frameReady() with the annotated result.  Move the subclass to a
// dedicated QThread and wire via QueuedConnection:
//
//   connect(videoInput, &VideoInputBase::videoFrameReady,
//           overlay,    &VideoOverlayBase::overlayFrame,
//           Qt::QueuedConnection);
//   connect(overlay, &VideoOverlayBase::frameReady,
//           controller, &CameraInstance::onAnnotatedFrame,
//           Qt::QueuedConnection);

class VideoOverlayBase : public QObject
{
    Q_OBJECT

public:
    explicit VideoOverlayBase(QObject *parent = nullptr);
    ~VideoOverlayBase() override = default;

public slots:
    virtual void overlayFrame(const QVideoFrame &frame) = 0;

signals:
    void frameReady(const QVideoFrame &frame);
};
