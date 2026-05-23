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

#if defined(HAVE_OPENCV) && defined(HAVE_VITPOSE) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_base.h"
#include <array>
#include <memory>

// ViTPose-B whole-body estimator.
//
// Model: vitpose-b-wholebody.onnx (JunkyByte/easy_ViTPose)
// Input:  [1, 3, 256, 192] float32 NCHW, RGB, ImageNet-normalised
// Output: [1, 133, 64, 48] float32 heatmaps — COCO-WholeBody 133-keypoint format.
//
// We currently consume only the first 17 channels (COCO body joints), which
// map 1-to-1 onto PoseJoint.  The remaining 116 channels cover face, hands,
// and feet keypoints and are ignored for now.  When integrating into the wider
// pipeline, consider exposing all 133 keypoints for full-body analytics.

class PoseEstimatorViTPose : public PoseEstimatorBase
{
    Q_OBJECT

public:
    explicit PoseEstimatorViTPose(QObject *parent = nullptr);
    ~PoseEstimatorViTPose() override;

    static QString modelPath();
    static bool    isAvailable();

    bool isReady() const { return m_ready; }

public slots:
    void load();
    void estimatePose(const cv::Mat &frame) override;
    void resetTracking() override {}   // stateless per-frame; no-op

private:
    bool m_ready = false;

    // 30-sample rolling windows — same pattern as PoseEstimatorMoveNet.
    static constexpr int kWindowSize = 30;
    std::array<double, kWindowSize> m_inferenceSamples{};
    std::array<double, kWindowSize> m_intervalSamples{};
    double m_inferenceSum = 0.0;
    double m_intervalSum  = 0.0;
    int    m_timingIndex  = 0;
    int    m_timingCount  = 0;
    qint64 m_lastCallNs   = -1;

    // ONNX Runtime state isolated so onnxruntime_cxx_api.h stays out of this header.
    struct OrtState;
    std::unique_ptr<OrtState> m_ort;
};

#endif // HAVE_OPENCV && HAVE_VITPOSE && HAVE_ONNXRUNTIME
