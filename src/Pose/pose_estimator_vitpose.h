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
#include <QPointF>
#include <array>
#include <memory>
#include <vector>

// ViTPose-B whole-body estimator.
//
// Model: vitpose-b-wholebody.onnx (JunkyByte/easy_ViTPose)
// Input:  [1, 3, 256, 192] float32 NCHW, RGB, ImageNet-normalised
// Output: [1, 133, 64, 48] float32 heatmaps — COCO-WholeBody 133-keypoint format.
//
// We always consume the first 17 channels (COCO body joints), which map
// 1-to-1 onto PoseJoint.  The hand channels (91–132) are additionally decoded
// into WholeBodyHands when setDecodeHands(true) — used by the offline
// analyzer (PoseRunner); the live path leaves the flag off.  The remaining
// face/feet channels are still ignored.

// COCO-WholeBody hand keypoints — channels 91–111 (left), 112–132 (right).
// A sibling of PoseResult: the 17-slot PoseResult shape is never widened
// (BodyPoseAdapter reads it by index).
struct WholeBodyHands {
    static constexpr int kHandJoints = 21;
    std::array<QPointF, 21> left{},      right{};        // normalized x/y like PoseResult
    std::array<float, 21>   leftScore{}, rightScore{};
    bool valid = false;
};

class PoseEstimatorViTPose : public PoseEstimatorBase
{
    Q_OBJECT

public:
    // Two whole-body checkpoints, same ONNX I/O ([1,3,256,192] -> [1,133,64,48]),
    // decoded identically here (17 COCO body joints + opt-in hands). They differ
    // only in backbone size and where the file lives:
    //   WholeBodyB     — ships beside the executable (models/, VITPOSE_MODEL_FILE),
    //                    the Medium tier default.
    //   WholeBodyLarge — ViTPose++-L (~1.2 GB), NOT packaged; downloaded on demand
    //                    into the writable app-data dir for the High tier.
    enum class ModelVariant { WholeBodyB = 0, WholeBodyLarge = 1 };

    explicit PoseEstimatorViTPose(ModelVariant variant = ModelVariant::WholeBodyB,
                                  QObject *parent = nullptr);
    ~PoseEstimatorViTPose() override;

    static QString modelPath(ModelVariant v = ModelVariant::WholeBodyB);
    static bool    isVariantAvailable(ModelVariant v);
    static bool    isAvailable() { return isVariantAvailable(ModelVariant::WholeBodyB); }

    // ViTPose++-L on-demand download coordinates (used by MotionCaptureProbe's
    // download controller). largeModelDir() is created lazily by the downloader.
    static QString largeModelFileName();
    static QString largeModelDir();
    static QString largeModelUrl();

    ModelVariant variant() const { return m_variant; }
    bool isReady() const { return m_ready; }

    // Opt-in decode of the COCO-WholeBody hand channels in the same heatmap
    // pass. Default OFF — the live 60 Hz path is behaviourally unchanged.
    void setDecodeHands(bool on) { m_decodeHands = on; }
    bool decodeHands() const     { return m_decodeHands; }

    // Hand keypoints from the most recent inference. Valid (valid == true)
    // only after a synchronous estimatePose() call returns with hand decode
    // enabled and inference succeeded.
    const WholeBodyHands &lastHands() const { return m_lastHands; }

    // Offline pipelined path (PoseRunner only). estimatePose() split into its
    // two halves so frame decode + preprocess can run on a producer thread
    // while ORT inference runs on the consumer. preprocess() is a pure function
    // of `frame` — it touches NO member state, so it is safe to call on the
    // producer thread concurrently with inferPrepared() on the consumer.
    // inferPrepared() runs the ORT session + heatmap decode and emits
    // poseEstimated()/estimationDone() exactly as estimatePose() would; together
    // preprocess(frame,buf)+inferPrepared(buf) are behaviourally identical to
    // estimatePose(frame). The live path still calls estimatePose() unchanged.
    void preprocess(const cv::Mat &frame, std::vector<float> &inputBuf) const;
    void inferPrepared(std::vector<float> &inputBuf);

public slots:
    void load();
    void estimatePose(const cv::Mat &frame) override;
    void resetTracking() override {}   // stateless per-frame; no-op

private:
    ModelVariant m_variant = ModelVariant::WholeBodyB;
    bool m_ready = false;

    bool           m_decodeHands = false;
    WholeBodyHands m_lastHands;

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
