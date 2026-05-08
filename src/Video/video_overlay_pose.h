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

private:
    void drawSkeleton(QImage &img, const PoseResult &pose) const;

    PoseResult m_pose;
    bool       m_havePose = false;
};

#endif // HAVE_OPENCV
