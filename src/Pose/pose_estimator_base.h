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

#include <QObject>
#include <opencv2/core.hpp>
#include <array>

// MoveNet output keypoint order (17 joints).
enum class PoseJoint {
    Nose          = 0,
    LeftEye       = 1,
    RightEye      = 2,
    LeftEar       = 3,
    RightEar      = 4,
    LeftShoulder  = 5,
    RightShoulder = 6,
    LeftElbow     = 7,
    RightElbow    = 8,
    LeftWrist     = 9,
    RightWrist    = 10,
    LeftHip       = 11,
    RightHip      = 12,
    LeftKnee      = 13,
    RightKnee     = 14,
    LeftAnkle     = 15,
    RightAnkle    = 16,
};

struct PoseKeypoint {
    float y;     // row position, normalised [0, 1]
    float x;     // col position, normalised [0, 1]
    float score; // confidence [0, 1]
};

struct PoseResult {
    static constexpr int kNumKeypoints = 17;
    std::array<PoseKeypoint, kNumKeypoints> keypoints{};
    float  confidence = 0.f; // mean keypoint score
    qint64 timestamp  = 0;   // milliseconds since epoch
};
Q_DECLARE_METATYPE(PoseResult)

// Abstract base for pose estimation classes.
//
// Wire to the preprocessor output and move to a dedicated QThread:
//
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           estimator,    &PoseEstimatorBase::estimatePose,
//           Qt::QueuedConnection);

class PoseEstimatorBase : public QObject
{
    Q_OBJECT

public:
    explicit PoseEstimatorBase(QObject *parent = nullptr);
    ~PoseEstimatorBase() override = default;

public slots:
    // Receives a BGR CV_8UC3 frame from VideoPreprocessorOpenCV.
    virtual void estimatePose(const cv::Mat &frame) = 0;

    // Resets any inter-frame tracking state (e.g. BlazePose ROI window).
    // Call before single-frame annotation to prevent a stale ROI from a
    // previous call influencing the next one.
    virtual void resetTracking() {}

signals:
    void poseEstimated(const PoseResult &result);

    // Emitted when a single estimatePose() call finishes (success or error).
    // FrameThrottle connects this to clearBusy() to release the next frame.
    void estimationDone();

    // Emitted once after load() completes. Empty string = CPU only;
    // otherwise "CoreML", "CUDA", or "DirectML".
    void poseBackendReady(const QString &label);

    // Rolling average inference time and throughput FPS, emitted each frame
    // once the 30-sample measurement window is warm.
    void poseStatsUpdated(double avgMs, double fps);
};

#endif // HAVE_OPENCV
