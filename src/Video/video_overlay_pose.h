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

#include "video_overlay_base.h"
#include "pose_estimator_base.h"

// Overlay that draws the MoveNet 17-joint skeleton onto each video frame.
//
// Wire pose results in and video frames in; annotated frames come out:
//
//   connect(poseEstimator, &PoseEstimatorBase::poseEstimated,
//           overlay,       &VideoOverlayPose::updatePose,
//           Qt::QueuedConnection);
//   connect(videoInput, &VideoInputBase::videoFrameReady,
//           overlay,    &VideoOverlayBase::overlayFrame,
//           Qt::QueuedConnection);
//
// updatePose() and overlayFrame() are both invoked on the overlay's own
// thread via QueuedConnection, so no locking is needed.

class VideoOverlayPose : public VideoOverlayBase
{
    Q_OBJECT

public:
    explicit VideoOverlayPose(QObject *parent = nullptr);

public slots:
    void overlayFrame(const QVideoFrame &frame) override;
    void updatePose(const PoseResult &result);
    void clearPose();

private:
    void drawSkeleton(QImage &img, const PoseResult &pose) const;

    PoseResult m_pose;
    bool       m_havePose = false;
};

#endif // HAVE_OPENCV
