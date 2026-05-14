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

#if defined(HAVE_OPENCV) && defined(HAVE_MEDIAPIPE) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_base.h"
#include <array>
#include <memory>
#include <opencv2/core.hpp>

// Two-model BlazePose pipeline following the geaxgx/depthai_blazepose reference.
//
// Pipeline (mirrors mediapipe_utils.py exactly):
//   1. detectPerson()  — runs the SSD detector on a square-padded frame, decodes
//      keypoints kp[0] (mid-hip) and kp[1] (rotation-encode point) from the
//      12-value box output, computes a ROTATED square ROI via detections_to_rect /
//      rect_transformation (scale=1.25, square_long=true).
//   2. runLandmarks()  — extracts the ROTATED crop with cv::warpAffine, runs the
//      landmark model, then maps landmarks back to full-frame pixels using a second
//      cv::getAffineTransform (from normalised [0,1] landmark space to the 4 rect
//      corners).  This is the same inverse-affine trick as lm_postprocess() in the
//      reference.
//
// Without the rotated crop the landmark model sees a tilted person and produces
// garbage, which is why the axis-aligned-crop approach failed.

// Rotated bounding rectangle computed from the detector keypoints.
// All pixel fields are in the original (unpadded) frame pixel space.
struct RotatedBody {
    float cx_px    = 0.f; // centre x in frame pixels
    float cy_px    = 0.f; // centre y in frame pixels
    float size_px  = 0.f; // side length of the square in frame pixels
    float rotation = 0.f; // rotation angle in radians (CCW from +x axis)
    // 4 corners of the rotated square, in frame pixels, going:
    //   [0]=bottom-left, [1]=top-left, [2]=top-right, [3]=bottom-right
    // Matches the rect_points convention in mediapipe_utils.rotated_rect_to_points.
    std::array<cv::Point2f, 4> rect_points{};
    bool valid = false;
};

class PoseEstimatorMediaPipe : public PoseEstimatorBase
{
    Q_OBJECT

public:
    explicit PoseEstimatorMediaPipe(QObject *parent = nullptr);
    ~PoseEstimatorMediaPipe() override;

    static QString detectorModelPath();
    static QString landmarkModelPath();
    static bool    isAvailable();

public slots:
    void load();
    void estimatePose(const cv::Mat &frame) override;
    void resetTracking() override { m_trackingLost = true; m_body = {}; }

private:
    bool        m_ready        = false;
    bool        m_trackingLost = true;
    RotatedBody m_body;   // rotated ROI carried between frames for tracking

    // 30-sample rolling windows — same pattern as PoseEstimatorMoveNet.
    static constexpr int kWindowSize = 30;
    std::array<double, kWindowSize> m_inferenceSamples{};
    std::array<double, kWindowSize> m_intervalSamples{};
    double m_inferenceSum = 0.0;
    double m_intervalSum  = 0.0;
    int    m_timingIndex  = 0;
    int    m_timingCount  = 0;
    qint64 m_lastCallNs   = -1;

    // All ORT state in a pimpl so onnxruntime headers stay out of this header.
    struct OrtState;
    std::unique_ptr<OrtState> m_ort;

    // Runs the SSD detector and returns a RotatedBody with the rotated ROI.
    // Returns an invalid RotatedBody on failure.
    RotatedBody detectPerson(const cv::Mat &frame);

    // Extracts the rotated crop via warpAffine, runs the landmark model, maps
    // landmarks back to full-frame pixel space using the inverse affine, and
    // updates m_body for the next frame.
    PoseResult runLandmarks(const cv::Mat &frame, const RotatedBody &body);
};

#endif // HAVE_OPENCV && HAVE_MEDIAPIPE && HAVE_ONNXRUNTIME
