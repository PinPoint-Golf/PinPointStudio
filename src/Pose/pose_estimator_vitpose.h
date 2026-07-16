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
#include "heatmap_decode.h"   // pinpoint::pose::DecodeMode + decode functions
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
// 1-to-1 onto PoseJoint.  ALL 133 channels — body plus the feet/face/hand
// tail — are additionally decoded into WholeBodyResult when
// setDecodeWholeBody(true) — used by the offline analyzer (PoseRunner); the
// live path leaves the flag off and decodes 17 channels exactly as before.

// COCO-WholeBody 133-keypoint decode — the full heatmap output. A sibling of
// PoseResult: the 17-slot PoseResult shape is never widened (BodyPoseAdapter
// reads it by index). kp[0..16] carry the SAME values as the emitted
// PoseResult (copied from the one shared body decode — bit-identical).
struct WholeBodyResult {
    static constexpr int kJoints = 133;   // COCO-WholeBody: 0-16 body, 17-22 feet, 23-90 face, 91-111 L hand, 112-132 R hand
    std::array<QPointF, 133> kp{};        // normalized x/y, PoseResult convention
    std::array<float, 133>   score{};
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

    // Opt-in decode of ALL 133 COCO-WholeBody channels in the same heatmap
    // pass. Default OFF — the live 60 Hz path is behaviourally unchanged.
    void setDecodeWholeBody(bool on) { m_decodeWholeBody = on; }
    bool decodeWholeBody() const     { return m_decodeWholeBody; }

    // Sub-pixel decode mode (heatmap_decode.h). Default Argmax so the live path
    // and MotionCaptureProbe benchmarks are byte-identical to before; PoseRunner
    // selects Dark (DARK refinement) when the pose.decode.dark tunable is on.
    using DecodeMode = pinpoint::pose::DecodeMode;
    void       setDecodeMode(DecodeMode m) { m_decodeMode = m; }
    DecodeMode decodeMode() const          { return m_decodeMode; }

    // Whole-body keypoints from the most recent inference. Valid (valid ==
    // true) only after a synchronous estimatePose() call returns with
    // whole-body decode enabled and inference succeeded.
    const WholeBodyResult &lastWholeBody() const { return m_lastWholeBody; }

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

    bool            m_decodeWholeBody = false;
    WholeBodyResult m_lastWholeBody;

    DecodeMode         m_decodeMode = DecodeMode::Argmax;
    std::vector<float> m_decodeBlur;   // DARK blur scratch (heatmap-sized); reused per channel

    // Decode one heatmap channel per the active mode (Argmax | Dark).
    void decodeChannel(const float *hm, float &nx, float &ny, float &score);

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
