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

#if defined(HAVE_OPENCV) && defined(HAVE_MEDIAPIPE) && defined(HAVE_ONNXRUNTIME)

#include "pose_estimator_mediapipe.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>
#include "pp_debug.h"

#include <onnxruntime_cxx_api.h>
#include "ort_log.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

#ifdef WITH_COREML
#  include <coreml_provider_factory.h>
#endif

// ── MediaPipe BlazePose landmark → PoseJoint index mapping ───────────────────
// MediaPipe uses 33 landmarks; map the 17 that align with MoveNet's PoseJoint.
// MediaPipe index: LEFT and RIGHT are from the subject's perspective.
static constexpr int kMpToMoveNet[PoseResult::kNumKeypoints] = {
     0,  // Nose          ← MP 0  NOSE
     2,  // LeftEye       ← MP 2  LEFT_EYE
     5,  // RightEye      ← MP 5  RIGHT_EYE
     7,  // LeftEar       ← MP 7  LEFT_EAR
     8,  // RightEar      ← MP 8  RIGHT_EAR
    11,  // LeftShoulder  ← MP 11 LEFT_SHOULDER
    12,  // RightShoulder ← MP 12 RIGHT_SHOULDER
    13,  // LeftElbow     ← MP 13 LEFT_ELBOW
    14,  // RightElbow    ← MP 14 RIGHT_ELBOW
    15,  // LeftWrist     ← MP 15 LEFT_WRIST
    16,  // RightWrist    ← MP 16 RIGHT_WRIST
    23,  // LeftHip       ← MP 23 LEFT_HIP
    24,  // RightHip      ← MP 24 RIGHT_HIP
    25,  // LeftKnee      ← MP 25 LEFT_KNEE
    26,  // RightKnee     ← MP 26 RIGHT_KNEE
    27,  // LeftAnkle     ← MP 27 LEFT_ANKLE
    28,  // RightAnkle    ← MP 28 RIGHT_ANKLE
};

// ── OrtState ─────────────────────────────────────────────────────────────────
struct PoseEstimatorMediaPipe::OrtState {
    Ort::Env            env { ORT_LOGGING_LEVEL_WARNING, "MediaPipe", ppOrtLogCallback, nullptr };
    Ort::RunOptions     runOpts;
    Ort::AllocatorWithDefaultOptions allocator;

    // Two separate sessions (each gets its own SessionOptions for clarity).
    Ort::SessionOptions detOpts;
    Ort::SessionOptions lmOpts;
    std::unique_ptr<Ort::Session> detSession;
    std::unique_ptr<Ort::Session> lmSession;

    // ── Detector metadata (queried from model at load) ─────────────────────
    std::string detInputName;
    int64_t     detInputH  = 128;
    int64_t     detInputW  = 128;
    bool        detNchw    = true;

    std::vector<std::string> detOutNames;

    // The detector has 4 outputs split by stride level:
    //   [1,512,12] boxes + [1,384,12] boxes + [1,512,1] scores + [1,384,1] scores
    // We store (scoresOutputIdx, boxesOutputIdx, numAnchors, anchorOffset) per level.
    struct DetLevel {
        int   scoresIdx   = -1;
        int   boxesIdx    = -1;
        int   numAnchors  = 0;
        int   anchorOffset = 0;  // index into detAnchors where this level starts
        int   valPerBox   = 12;
    };
    std::vector<DetLevel>              detLevels;
    std::vector<std::array<float, 2>>  detAnchors;  // (cx, cy) each in [0,1]

    // ── Landmark metadata (queried from model at load) ─────────────────────
    std::string lmInputName;
    int64_t     lmInputH = 256;
    int64_t     lmInputW = 256;
    bool        lmNchw   = true;

    std::vector<std::string> lmOutNames;
    int lmLandmarksIdx       = -1;  // output index of landmark tensor [1, N, M]
    int lmFlagIdx            = -1;  // output index of pose presence flag [1]
    int lmNumLandmarks       = 31;  // confirmed from model inspection
    int lmValuesPerLandmark  = 4;   // (x, y, z, visibility) — no separate presence

    // One-time detection of coordinate and logit conventions on first frame.
    bool  coordsChecked     = false;
    bool  coordsNormalized  = false;
    bool  visChecked        = false;
    bool  visIsLogit        = false;

    QElapsedTimer wallTimer;
};

// ── Anchor grid generation ────────────────────────────────────────────────────
// Standard BlazePose SSD anchors: strides [8, 16], anchors-per-cell [2, 6].
// These are the (cx, cy) of each anchor in normalised [0,1] coordinates.
static std::vector<std::array<float, 2>> generateAnchors(int inputSize)
{
    static constexpr int kStrides[]       = { 8, 16 };
    static constexpr int kAnchorsPerCell[] = { 2,  6 };
    std::vector<std::array<float, 2>> anchors;
    anchors.reserve(3000);
    for (int s = 0; s < 2; ++s) {
        const int gridSz = inputSize / kStrides[s];
        for (int y = 0; y < gridSz; ++y)
            for (int x = 0; x < gridSz; ++x)
                for (int a = 0; a < kAnchorsPerCell[s]; ++a)
                    anchors.push_back({ (x + 0.5f) / gridSz, (y + 0.5f) / gridSz });
    }
    return anchors;
}

// ── Local helpers ─────────────────────────────────────────────────────────────
namespace {

// Applies the same EP cascade as PoseEstimatorMoveNet::load().
// Returns the label of the chosen EP ("CoreML", "CUDA", "DirectML") or "".
QString applyEpCascade(Ort::SessionOptions &opts, const char *logPrefix)
{
    QString label;

#ifdef WITH_COREML
    if (label.isEmpty()) {
        try {
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(
                opts, COREML_FLAG_USE_NONE));
            label = QStringLiteral("CoreML");
            ppInfo() << logPrefix << "CoreML EP active";
        } catch (const Ort::Exception &e) {
            ppInfo() << logPrefix << "CoreML unavailable:" << e.what();
        }
    }
#endif

#ifdef WITH_CUDA
    if (label.isEmpty()) {
#  ifdef Q_OS_LINUX
        const bool hasNv = QFile::exists(QStringLiteral("/proc/driver/nvidia/version"));
#  elif defined(Q_OS_WIN)
        QLibrary nvcuda(QStringLiteral("nvcuda.dll"));
        const bool hasNv = nvcuda.load(); if (hasNv) nvcuda.unload();
#  else
        const bool hasNv = false;
#  endif
        if (hasNv) {
            try {
                OrtCUDAProviderOptions cuda{}; cuda.device_id = 0;
                opts.AppendExecutionProvider_CUDA(cuda);
                label = QStringLiteral("CUDA");
                ppInfo() << logPrefix << "CUDA EP active";
            } catch (const Ort::Exception &e) {
                ppInfo() << logPrefix << "CUDA unavailable:" << e.what();
            }
        }
    }
#endif

#ifdef WITH_DIRECTML
    if (label.isEmpty()) {
        try {
            opts.AppendExecutionProvider("DML");
            label = QStringLiteral("DirectML");
            ppInfo() << logPrefix << "DirectML EP active";
        } catch (const Ort::Exception &e) {
            ppInfo() << logPrefix << "DirectML unavailable:" << e.what();
        }
    }
#endif

    if (label.isEmpty())
        ppInfo() << logPrefix << "CPU-only";

    return label;
}

// Loads a BGR frame region, converts to RGB, normalises to [0,1] float32.
// Handles out-of-bounds ROIs by padding with black.
cv::Mat preprocessROI(const cv::Mat &bgr, const cv::Rect2f &roi,
                      int targetH, int targetW, bool nchw)
{
    const int fw = bgr.cols, fh = bgr.rows;
    const int x1 = static_cast<int>(std::floor(roi.x * fw));
    const int y1 = static_cast<int>(std::floor(roi.y * fh));
    const int x2 = static_cast<int>(std::ceil((roi.x + roi.width) * fw));
    const int y2 = static_cast<int>(std::ceil((roi.y + roi.height) * fh));

    cv::Rect srcRect(x1, y1, x2 - x1, y2 - y1);
    cv::Rect imgRect(0, 0, fw, fh);
    cv::Rect intersect = srcRect & imgRect;

    cv::Mat crop;
    if (intersect.width > 0 && intersect.height > 0) {
        crop = bgr(intersect);
        int top = std::max(0, intersect.y - srcRect.y);
        int bottom = std::max(0, srcRect.y + srcRect.height - (intersect.y + intersect.height));
        int left = std::max(0, intersect.x - srcRect.x);
        int right = std::max(0, srcRect.x + srcRect.width - (intersect.x + intersect.width));
        if (top > 0 || bottom > 0 || left > 0 || right > 0) {
            cv::Mat padded;
            cv::copyMakeBorder(crop, padded, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(0,0,0));
            crop = padded;
        }
    } else {
        crop = cv::Mat::zeros(std::max(1, srcRect.height), std::max(1, srcRect.width), bgr.type());
    }

    cv::Mat resized, rgb, f32;
    cv::resize(crop, resized, cv::Size(targetW, targetH));
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(f32, CV_32FC3, 1.0 / 255.0);

    if (!nchw) return f32;

    std::vector<cv::Mat> chs(3);
    cv::split(f32, chs);
    cv::Mat chw(1, 3 * targetH * targetW, CV_32F);
    float *dst = chw.ptr<float>();
    for (int c = 0; c < 3; ++c)
        std::memcpy(dst + c * targetH * targetW,
                    chs[c].ptr<float>(),
                    targetH * targetW * sizeof(float));
    return chw;
}

// Expands a normalised [0,1] ROI to be square in pixel space.
static cv::Rect2f makeSquare(const cv::Rect2f &roi, float imgAspect)
{
    float w = roi.width;
    float h = roi.height;
    float cx = roi.x + w / 2.0f;
    float cy = roi.y + h / 2.0f;

    if (w * imgAspect > h) h = w * imgAspect;
    else                   w = h / imgAspect;

    return cv::Rect2f(cx - w / 2.0f, cy - h / 2.0f, w, h);
}

} // namespace

// ── Construction / destruction ────────────────────────────────────────────────

PoseEstimatorMediaPipe::PoseEstimatorMediaPipe(QObject *parent)
    : PoseEstimatorBase(parent)
{}

PoseEstimatorMediaPipe::~PoseEstimatorMediaPipe() = default;

// ── Static helpers ────────────────────────────────────────────────────────────

QString PoseEstimatorMediaPipe::detectorModelPath()
{
    const QString file = QStringLiteral(MEDIAPIPE_DETECTOR_FILE);
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/../Resources/models/") + file;
#else
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/models/") + file;
#endif
}

QString PoseEstimatorMediaPipe::landmarkModelPath()
{
    const QString file = QStringLiteral(MEDIAPIPE_LANDMARK_FILE);
#ifdef Q_OS_MACOS
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/../Resources/models/") + file;
#else
    return QCoreApplication::applicationDirPath()
         + QStringLiteral("/models/") + file;
#endif
}

bool PoseEstimatorMediaPipe::isAvailable()
{
    return QFile::exists(detectorModelPath()) && QFile::exists(landmarkModelPath());
}

// ── load() ────────────────────────────────────────────────────────────────────

void PoseEstimatorMediaPipe::load()
{
    m_ready        = false;
    m_trackingLost = true;
    m_ort          = std::make_unique<OrtState>();

    const QString detPath = detectorModelPath();
    const QString lmPath  = landmarkModelPath();

    if (!QFile::exists(detPath)) {
        ppError() << "[MediaPipe] Detector model not found:" << detPath;
        return;
    }
    if (!QFile::exists(lmPath)) {
        ppError() << "[MediaPipe] Landmark model not found:" << lmPath;
        return;
    }

    // Both sessions get the same thread count and optimisation level.
    for (Ort::SessionOptions *opts : { &m_ort->detOpts, &m_ort->lmOpts }) {
        opts->SetIntraOpNumThreads(1);
        opts->SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    }

    // Apply EP cascade to detector; landmark uses the same EP.
    // We only report the label once (both sessions share the same hardware).
    const QString epLabel = applyEpCascade(m_ort->detOpts, "[MediaPipe/det]");
    applyEpCascade(m_ort->lmOpts, "[MediaPipe/lm]");

    // ── Load detector session ──────────────────────────────────────────────
    try {
#ifdef Q_OS_WIN
        m_ort->detSession = std::make_unique<Ort::Session>(
            m_ort->env, detPath.toStdWString().c_str(), m_ort->detOpts);
#else
        m_ort->detSession = std::make_unique<Ort::Session>(
            m_ort->env, detPath.toUtf8().constData(), m_ort->detOpts);
#endif
    } catch (const Ort::Exception &e) {
        ppWarn() << "[MediaPipe] Failed to load detector:" << e.what();
        m_ort.reset();
        return;
    }

    // Query detector input name + shape.
    m_ort->detInputName =
        m_ort->detSession->GetInputNameAllocated(0, m_ort->allocator).get();
    {
        auto info  = m_ort->detSession->GetInputTypeInfo(0);
        auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
        // Expect [1, C, H, W] (NCHW) or [1, H, W, C] (NHWC).
        if (shape.size() == 4) {
            if (shape[1] == 3) {
                // NCHW
                m_ort->detNchw   = true;
                m_ort->detInputH = shape[2];
                m_ort->detInputW = shape[3];
            } else {
                // NHWC
                m_ort->detNchw   = false;
                m_ort->detInputH = shape[1];
                m_ort->detInputW = shape[2];
            }
        }
        ppInfo() << "[MediaPipe] Detector input:" << m_ort->detInputName.c_str()
                 << (m_ort->detNchw ? "NCHW" : "NHWC")
                 << m_ort->detInputH << "×" << m_ort->detInputW;
    }

    // Query detector outputs.
    // The exported model has 4 outputs split by stride level:
    //   [1, N, 12] boxes  (last dim >= 4)
    //   [1, N, 1]  scores (last dim == 1)
    // N differs per level (512 at stride-8, 384 at stride-16 for 128×128 input).
    // We pair them by matching anchor count then build the flat anchor array.
    {
        const size_t numOut = m_ort->detSession->GetOutputCount();

        // Collect outputs classified as scores (last dim==1) or boxes (last dim>=4).
        // Store (outputIdx, numAnchors, K) so we never re-query TypeInfo later —
        // chaining GetOutputTypeInfo().GetTensorTypeAndShapeInfo() produces a
        // dangling ConstTensorTypeAndShapeInfo because the TypeInfo temporary is
        // destroyed at the semicolon.
        struct ScoreOut { int idx; int numAnchors; };
        struct BoxOut   { int idx; int numAnchors; int valPerBox; };
        std::vector<ScoreOut> scoreOuts;
        std::vector<BoxOut>   boxOuts;

        for (size_t i = 0; i < numOut; ++i) {
            m_ort->detOutNames.push_back(
                m_ort->detSession->GetOutputNameAllocated(i, m_ort->allocator).get());
            // Keep TypeInfo alive for the full expression — never chain temporaries.
            auto typeInfo   = m_ort->detSession->GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape      = tensorInfo.GetShape();
            ppInfo() << "[MediaPipe] Detector output" << i
                     << m_ort->detOutNames.back().c_str() << "shape:" << shape;

            // Expect rank 3: [batch, numAnchors, K]
            if (shape.size() == 3 && shape[0] == 1 && shape[2] >= 1) {
                const int nAnc = static_cast<int>(shape[1]);
                const int K    = static_cast<int>(shape[2]);
                if (K == 1)
                    scoreOuts.push_back({static_cast<int>(i), nAnc});
                else if (K >= 4)
                    boxOuts.push_back({static_cast<int>(i), nAnc, K});
            }
        }

        // Generate flat anchor array; the order matches anchor generation order.
        m_ort->detAnchors = generateAnchors(static_cast<int>(m_ort->detInputH));

        // Pair score and box outputs by anchor count.
        // Sort descending so the larger level (stride-8, 512 anchors) comes first,
        // matching the order generateAnchors() produces them.
        std::sort(scoreOuts.begin(), scoreOuts.end(),
                  [](const auto &a, const auto &b){ return a.numAnchors > b.numAnchors; });
        std::sort(boxOuts.begin(), boxOuts.end(),
                  [](const auto &a, const auto &b){ return a.numAnchors > b.numAnchors; });

        int anchorOffset = 0;
        const int nLevels = static_cast<int>(std::min(scoreOuts.size(), boxOuts.size()));
        for (int lv = 0; lv < nLevels; ++lv) {
            const int nAnc = scoreOuts[lv].numAnchors;
            if (nAnc != boxOuts[lv].numAnchors) {
                ppWarn() << "[MediaPipe] Anchor count mismatch at level" << lv
                         << "(scores=" << nAnc << " boxes=" << boxOuts[lv].numAnchors
                         << ") — detector disabled";
                m_ort->detLevels.clear();
                break;
            }
            OrtState::DetLevel dl;
            dl.scoresIdx    = scoreOuts[lv].idx;
            dl.boxesIdx     = boxOuts[lv].idx;
            dl.numAnchors   = nAnc;
            dl.anchorOffset = anchorOffset;
            dl.valPerBox    = boxOuts[lv].valPerBox;  // stored during scan, no re-query
            m_ort->detLevels.push_back(dl);
            anchorOffset += nAnc;
        }

        const int totalAnchors = static_cast<int>(m_ort->detAnchors.size());
        if (anchorOffset != totalAnchors) {
            ppWarn() << "[MediaPipe] Anchor count mismatch: generated" << totalAnchors
                     << "anchors but detector outputs cover" << anchorOffset
                     << "— detector will fall back to full-frame ROI";
            m_ort->detLevels.clear();
        } else if (!m_ort->detLevels.empty()) {
            ppInfo() << "[MediaPipe] Detector ready:" << nLevels << "stride levels,"
                     << totalAnchors << "total anchors";
        } else {
            ppWarn() << "[MediaPipe] Could not map detector outputs — will use full-frame ROI";
        }
    }

    // ── Load landmark session ──────────────────────────────────────────────
    try {
#ifdef Q_OS_WIN
        m_ort->lmSession = std::make_unique<Ort::Session>(
            m_ort->env, lmPath.toStdWString().c_str(), m_ort->lmOpts);
#else
        m_ort->lmSession = std::make_unique<Ort::Session>(
            m_ort->env, lmPath.toUtf8().constData(), m_ort->lmOpts);
#endif
    } catch (const Ort::Exception &e) {
        ppError() << "[MediaPipe] Failed to load landmark model:" << e.what();
        m_ort.reset();
        return;
    }

    // Query landmark input name + shape.
    m_ort->lmInputName =
        m_ort->lmSession->GetInputNameAllocated(0, m_ort->allocator).get();
    {
        auto info  = m_ort->lmSession->GetInputTypeInfo(0);
        auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() == 4) {
            if (shape[1] == 3) {
                m_ort->lmNchw   = true;
                m_ort->lmInputH = shape[2];
                m_ort->lmInputW = shape[3];
            } else {
                m_ort->lmNchw   = false;
                m_ort->lmInputH = shape[1];
                m_ort->lmInputW = shape[2];
            }
        }
        ppInfo() << "[MediaPipe] Landmark input:" << m_ort->lmInputName.c_str()
                 << (m_ort->lmNchw ? "NCHW" : "NHWC")
                 << m_ort->lmInputH << "×" << m_ort->lmInputW;
    }

    // Query landmark outputs.
    // Expected structure (from qai_hub_models 0.53.x):
    //   flag      [1]           — pose presence (already sigmoid-activated)
    //   landmarks [1, N, M]     — N ≤ 33 keypoints, M ≤ 6 values (x,y,z,vis,...)
    //   heatmap   [1, H, W]     — segmentation map; H=W=large square → ignored
    // We distinguish landmarks from heatmaps by requiring last-dim ≤ 10 (small
    // values-per-point) and second-last-dim < 100 (small keypoint count).
    {
        const size_t numOut = m_ort->lmSession->GetOutputCount();
        for (size_t i = 0; i < numOut; ++i) {
            m_ort->lmOutNames.push_back(
                m_ort->lmSession->GetOutputNameAllocated(i, m_ort->allocator).get());
            auto info  = m_ort->lmSession->GetOutputTypeInfo(i);
            auto tinfo = info.GetTensorTypeAndShapeInfo();
            auto shape = tinfo.GetShape();
            ppInfo() << "[MediaPipe] Landmark output" << i
                     << m_ort->lmOutNames.back().c_str() << "shape:" << shape;

            const int64_t elems = tinfo.GetElementCount();
            if (elems == 1) {
                m_ort->lmFlagIdx = static_cast<int>(i);
            } else if (shape.size() == 3
                       && shape[2] <= 10      // ≤10 values per keypoint
                       && shape[1] < 100) {   // ≤99 keypoints (not a heatmap)
                // [batch, numLandmarks, valuesPerLandmark]
                m_ort->lmLandmarksIdx      = static_cast<int>(i);
                m_ort->lmNumLandmarks      = static_cast<int>(shape[1]);
                m_ort->lmValuesPerLandmark = static_cast<int>(shape[2]);
            }
            // Any remaining output (e.g. heatmap [1,128,128]) is silently ignored.
        }

        if (m_ort->lmLandmarksIdx < 0) {
            ppWarn() << "[MediaPipe] Could not identify landmark output — aborting";
            m_ort.reset();
            return;
        }
        if (m_ort->lmFlagIdx < 0)
            ppWarn() << "[MediaPipe] No flag output found — tracking loss detection disabled";

        // The qai_hub_models wrapper trims the inner model's output to
        // num_valid_landmarks=25 (indices 0-24: face + shoulders + elbows +
        // wrists + hips).  Indices 25+ are untrained and produce garbage.
        // Cap here so we never access those invalid entries.
        static constexpr int kValidLandmarks = 25;
        if (m_ort->lmNumLandmarks > kValidLandmarks) {
            ppInfo() << "[MediaPipe] Capping landmarks from" << m_ort->lmNumLandmarks
                     << "to" << kValidLandmarks << "(indices ≥25 are untrained)";
            m_ort->lmNumLandmarks = kValidLandmarks;
        }

        ppInfo() << "[MediaPipe] Landmark model ready —"
                 << m_ort->lmNumLandmarks << "landmarks,"
                 << m_ort->lmValuesPerLandmark << "values each";
    }

    m_ort->wallTimer.start();
    m_lastCallNs   = -1;
    m_trackingLost = true;
    m_ready        = true;
    emit poseBackendReady(epLabel);
}

// ── BlazePose geometry helpers (mirrors mediapipe_utils.py) ──────────────────

static float normalizeRadians(float angle)
{
    return angle - float(2 * M_PI) * std::floor((angle + float(M_PI)) / float(2 * M_PI));
}

// Equivalent to mediapipe_utils.rotated_rect_to_points.
// Returns [bottom-left, top-left, top-right, bottom-right] in pixels.
static std::array<cv::Point2f, 4> rotatedRectToPoints(float cx, float cy, float sz, float rot)
{
    const float b  = std::cos(rot) * 0.5f;
    const float a  = std::sin(rot) * 0.5f;
    cv::Point2f p0{ cx - a*sz - b*sz, cy + b*sz - a*sz }; // bottom-left
    cv::Point2f p1{ cx + a*sz - b*sz, cy - b*sz - a*sz }; // top-left
    cv::Point2f p2{ 2*cx - p0.x,      2*cy - p0.y      }; // top-right
    cv::Point2f p3{ 2*cx - p1.x,      2*cy - p1.y      }; // bottom-right
    return { p0, p1, p2, p3 };
}

// Equivalent to detections_to_rect + rect_transformation(scale=1.25, square_long).
// kp0 = mid-hip (anchor for center + rotation), kp1 = rotation-encode point.
// All inputs in frame PIXEL space; returns RotatedBody in frame pixel space.
static RotatedBody buildRotatedBody(float kp0_x, float kp0_y,
                                    float kp1_x, float kp1_y,
                                    float scale = 1.25f)
{
    const float dx   = kp1_x - kp0_x;
    const float dy   = kp1_y - kp0_y;
    const float dist = std::sqrt(dx*dx + dy*dy);

    RotatedBody b;
    b.cx_px    = kp0_x;
    b.cy_px    = kp0_y;
    b.rotation = normalizeRadians(float(M_PI / 2) - std::atan2(-dy, dx));
    b.size_px  = dist * 2.0f * scale;   // box_size * scale (square_long already square)
    b.rect_points = rotatedRectToPoints(b.cx_px, b.cy_px, b.size_px, b.rotation);
    b.valid    = (dist > 1e-3f);
    return b;
}

// ── detectPerson() ────────────────────────────────────────────────────────────
// Runs the SSD detector on a square-padded frame.  Decodes keypoints kp[0]
// (mid-hip) and kp[1] (rotation-encode point) from the best anchor's 12-value
// box, computes the rotated ROI via buildRotatedBody, and returns it.
// Returns an invalid RotatedBody when no detection exceeds the threshold.

RotatedBody PoseEstimatorMediaPipe::detectPerson(const cv::Mat &frame)
{
    RotatedBody invalid; // .valid == false

    if (m_ort->detLevels.empty() || m_ort->detAnchors.empty())
        return invalid;

    const int H = static_cast<int>(m_ort->detInputH);
    const int W = static_cast<int>(m_ort->detInputW);

    // Feed a square-padded copy of the full frame to the detector.
    const float aspect   = static_cast<float>(frame.cols) / frame.rows;
    const cv::Rect2f detRoi = makeSquare(cv::Rect2f(0,0,1,1), aspect);
    cv::Mat input = preprocessROI(frame, detRoi, H, W, m_ort->detNchw);

    std::vector<int64_t> shape = m_ort->detNchw
        ? std::vector<int64_t>{1, 3, H, W}
        : std::vector<int64_t>{1, H, W, 3};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(
        mem, input.ptr<float>(), static_cast<size_t>(input.total() * input.channels()),
        shape.data(), shape.size());

    std::vector<const char*> inNames  = { m_ort->detInputName.c_str() };
    std::vector<const char*> outNames;
    for (const auto &n : m_ort->detOutNames) outNames.push_back(n.c_str());

    Ort::Value inputs[] = { std::move(tensor) };
    auto outputs = m_ort->detSession->Run(
        m_ort->runOpts, inNames.data(), inputs, 1,
        outNames.data(), outNames.size());

    // Scan all stride levels for the highest-confidence anchor.
    static constexpr float kScoreThresh = 0.10f;
    int   bestLevel    = -1;
    int   bestLocalIdx = -1;
    float bestScore    = kScoreThresh;

    for (int lv = 0; lv < static_cast<int>(m_ort->detLevels.size()); ++lv) {
        const auto  &dl     = m_ort->detLevels[lv];
        const float *scores = outputs[dl.scoresIdx].GetTensorData<float>();
        for (int i = 0; i < dl.numAnchors; ++i) {
            const float s = 1.0f / (1.0f + std::exp(-scores[i]));
            if (s > bestScore) { bestScore = s; bestLevel = lv; bestLocalIdx = i; }
        }
    }

    if (bestLevel < 0) {
        ppInfo() << "[MediaPipe] Detector: no person above threshold";
        return invalid;
    }

    const auto  &dl      = m_ort->detLevels[bestLevel];
    const float *boxes   = outputs[dl.boxesIdx].GetTensorData<float>();
    const float *b       = boxes + bestLocalIdx * dl.valPerBox;
    const int    flatIdx = dl.anchorOffset + bestLocalIdx;
    const auto  &anc     = m_ort->detAnchors[flatIdx];

    // Decode all 12 values using the reference formula:
    //   decoded[j] = raw[j] / scale + anchor_cx  (for x-values, even j)
    //   decoded[j] = raw[j] / scale + anchor_cy  (for y-values, odd j)
    // The 12 values are 6 pairs: (cx,cy), (w,h), (kp0_x,kp0_y), (kp1_x,kp1_y), ...
    auto decodeX = [&](int j) { return b[j] / static_cast<float>(W) + anc[0]; };
    auto decodeY = [&](int j) { return b[j] / static_cast<float>(H) + anc[1]; };

    // kp[0] = mid hip (center of the rotated ROI)
    // kp[1] = rotation-encode point (distance + direction from mid hip)
    // Both are decoded in [0,1] of the square detector input (detRoi space).
    const float kp0_xsq = decodeX(4);
    const float kp0_ysq = decodeY(5);
    const float kp1_xsq = decodeX(6);
    const float kp1_ysq = decodeY(7);

    // Map from square-detector [0,1] → original frame pixels.
    auto toPixX = [&](float xsq) { return (detRoi.x + xsq * detRoi.width)  * frame.cols; };
    auto toPixY = [&](float ysq) { return (detRoi.y + ysq * detRoi.height) * frame.rows; };

    const float kp0_x = toPixX(kp0_xsq);
    const float kp0_y = toPixY(kp0_ysq);
    const float kp1_x = toPixX(kp1_xsq);
    const float kp1_y = toPixY(kp1_ysq);

    return buildRotatedBody(kp0_x, kp0_y, kp1_x, kp1_y);
}

// ── runLandmarks() ────────────────────────────────────────────────────────────
// Extracts a ROTATED crop via cv::warpAffine, runs the landmark model, then
// maps landmark coordinates back to the original frame using the inverse affine.
// This mirrors lm_postprocess() in BlazeposeDepthai.py exactly.

PoseResult PoseEstimatorMediaPipe::runLandmarks(const cv::Mat &frame,
                                                const RotatedBody &body)
{
    const int H  = static_cast<int>(m_ort->lmInputH);
    const int W  = static_cast<int>(m_ort->lmInputW);
    const int fw = frame.cols, fh = frame.rows;

    // Debug: save the rotated crop once so it can be visually verified.
    // Writes to ~/mediapipe_crop_debug.jpg — remove once confirmed correct.
    static bool sCropSaved = false;

    // ── Build affine: rect corners → (0,0),(W,0),(W,H) ───────────────────
    // rect_points[1]=top-left, [2]=top-right, [3]=bottom-right
    cv::Point2f srcPts[3] = { body.rect_points[1], body.rect_points[2], body.rect_points[3] };
    cv::Point2f dstPts[3] = { {0.f,0.f}, {float(W),0.f}, {float(W),float(H)} };
    cv::Mat affineExtract  = cv::getAffineTransform(srcPts, dstPts);

    // Extract the rotated crop (BGR, matches what landmark model expects after RGB flip).
    cv::Mat rotatedCrop;
    cv::warpAffine(frame, rotatedCrop, affineExtract, cv::Size(W, H), cv::INTER_LINEAR,
                   cv::BORDER_CONSTANT, cv::Scalar(0,0,0));

    if (!sCropSaved) {
        sCropSaved = true;
        const QString path = QDir::homePath() + QStringLiteral("/mediapipe_crop_debug.jpg");
        cv::imwrite(path.toStdString(), rotatedCrop);
        ppWarn() << "[MediaPipe] Rotated crop saved to" << path
                 << " — center(" << body.cx_px << body.cy_px << ")"
                 << " size=" << body.size_px << "px"
                 << " rot=" << (body.rotation * 180.f / float(M_PI)) << "deg";
    }

    // Preprocess: BGR→RGB, normalise [0,1], CHW if needed.
    cv::Mat rgb, f32;
    cv::cvtColor(rotatedCrop, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(f32, CV_32FC3, 1.0 / 255.0);

    cv::Mat input;
    if (m_ort->lmNchw) {
        std::vector<cv::Mat> chs(3);
        cv::split(f32, chs);
        input = cv::Mat(1, 3 * H * W, CV_32F);
        float *dst = input.ptr<float>();
        for (int c = 0; c < 3; ++c)
            std::memcpy(dst + c*H*W, chs[c].ptr<float>(), H*W*sizeof(float));
    } else {
        input = f32;
    }

    std::vector<int64_t> shape = m_ort->lmNchw
        ? std::vector<int64_t>{1, 3, H, W}
        : std::vector<int64_t>{1, H, W, 3};

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(
        mem, input.ptr<float>(), static_cast<size_t>(input.total() * input.channels()),
        shape.data(), shape.size());

    std::vector<const char*> inNames  = { m_ort->lmInputName.c_str() };
    std::vector<const char*> outNames;
    for (const auto &n : m_ort->lmOutNames) outNames.push_back(n.c_str());

    Ort::Value lmInputs[] = { std::move(tensor) };
    auto outputs = m_ort->lmSession->Run(
        m_ort->runOpts, inNames.data(), lmInputs, 1,
        outNames.data(), outNames.size());

    // ── Presence flag ──────────────────────────────────────────────────────
    float flag = 1.0f;
    if (m_ort->lmFlagIdx >= 0) {
        float raw = outputs[m_ort->lmFlagIdx].GetTensorData<float>()[0];
        flag = (raw > 1.0f || raw < -0.1f) ? 1.0f/(1.0f+std::exp(-raw)) : raw;
    }

    if (flag < 0.02f) {
        m_trackingLost = true;
        PoseResult r; r.timestamp = QDateTime::currentMSecsSinceEpoch(); return r;
    }

    // ── Build inverse affine: normalised [0,1] landmark space → frame pixels ─
    // Mirrors lm_postprocess():
    //   src = [(0,0),(1,0),(1,1)]
    //   dst = rect_points[1:] (top-left, top-right, bottom-right) in pixels
    cv::Point2f srcNorm[3] = { {0.f,0.f}, {1.f,0.f}, {1.f,1.f} };
    cv::Point2f dstRect[3] = { body.rect_points[1], body.rect_points[2], body.rect_points[3] };
    cv::Mat affineBack = cv::getAffineTransform(srcNorm, dstRect);

    // ── Parse landmarks ────────────────────────────────────────────────────
    const float *ld = outputs[m_ort->lmLandmarksIdx].GetTensorData<float>();
    const int    N  = m_ort->lmNumLandmarks;
    const int    M  = m_ort->lmValuesPerLandmark;

    // Coordinate scale: the 'div' ONNX op already divides by lm_input_length (256),
    // so landmark x,y are in [0,1] relative to the rotated crop.
    // Visibility logits need sigmoid regardless of magnitude.
    struct MPt { float x, y, conf; };
    std::array<MPt, 39> pts{};

    // Gather landmark points in normalised crop space for batch transform.
    std::vector<cv::Point2f> normPts(N);
    for (int i = 0; i < N && i < 39; ++i)
        normPts[i] = { ld[i*M+0], ld[i*M+1] };

    // Batch transform: all norm landmarks → frame pixel space.
    std::vector<cv::Point2f> framePts(N);
    cv::transform(normPts, framePts, affineBack);

    for (int i = 0; i < N && i < 39; ++i) {
        float vis  = 1.0f / (1.0f + std::exp(-ld[i*M+3]));
        float pres = (M >= 5) ? 1.0f/(1.0f+std::exp(-ld[i*M+4])) : vis;
        pts[i] = { framePts[i].x / fw, framePts[i].y / fh, std::min(vis, pres) };
    }

    // ── Update tracking ROI from body landmarks ────────────────────────────
    // The reference uses special auxiliary landmarks 33/34 that mimic the
    // detector's kp0/kp1 (mid-hip + full-body rotation-encode point).  Our
    // model only has landmarks 0-24, so we can't replicate that directly.
    //
    // Strategy: keep mid-hip as the centre and compute rotation from the
    // hip→shoulder direction, but derive the SQUARE SIZE from the bounding
    // box of ALL visible landmarks so the crop always covers the full body.
    // Using hip→shoulder distance as the size (as before) made the box
    // shrink to torso-only on every tracking frame, causing the spiral.
    {
        const bool hipOk = (23 < N && pts[23].conf > 0.3f && 24 < N && pts[24].conf > 0.3f);
        const bool shlOk = (11 < N && pts[11].conf > 0.3f && 12 < N && pts[12].conf > 0.3f);

        if (hipOk) {
            const float hx = (pts[23].x + pts[24].x) * 0.5f * fw;
            const float hy = (pts[23].y + pts[24].y) * 0.5f * fh;

            // Rotation from hip→shoulder (or keep previous rotation if no shoulders).
            float rot = m_body.rotation;
            if (shlOk) {
                const float sx = (pts[11].x + pts[12].x) * 0.5f * fw;
                const float sy = (pts[11].y + pts[12].y) * 0.5f * fh;
                rot = normalizeRadians(float(M_PI/2) - std::atan2(-(sy-hy), sx-hx));
            }

            // Estimate full-body size from nose→mid-hip distance.
            // The detector places kp1 (rotation-encode point) roughly at head
            // level — the same height as the nose.  Hips are approximately the
            // midpoint of the body, so:
            //   full_body_height ≈ 2 × dist(nose, mid_hip)
            // This matches the detector's  size = 2 × dist(kp0, kp1) × 1.25.
            // Using maxDist (from any visible landmark) gives a smaller box
            // because the farthest landmark is the nose — slightly BELOW kp1's
            // typical position — causing the skeleton to appear uniformly smaller.
            float halfBodyDist = 0.f;
            const bool noseOk = (pts[0].conf > 0.3f);
            if (noseOk) {
                const float dx = pts[0].x * fw - hx;
                const float dy = pts[0].y * fh - hy;
                halfBodyDist = std::sqrt(dx*dx + dy*dy);
            } else {
                // Fallback: use the farthest visible landmark.
                for (int i = 1; i < N && i < 39; ++i) {
                    if (pts[i].conf > 0.3f) {
                        const float dx = pts[i].x * fw - hx;
                        const float dy = pts[i].y * fh - hy;
                        halfBodyDist = std::max(halfBodyDist, std::sqrt(dx*dx + dy*dy));
                    }
                }
            }

            if (halfBodyDist > 10.f) {
                m_body.cx_px     = hx;
                m_body.cy_px     = hy;
                m_body.rotation  = rot;
                m_body.size_px   = halfBodyDist * 2.0f * 1.25f; // full body × scale
                m_body.rect_points = rotatedRectToPoints(hx, hy, m_body.size_px, rot);
                m_body.valid     = true;
            } else {
                m_trackingLost = true;
            }
        } else {
            m_trackingLost = true;
        }
    }

    // ── Map to 17-keypoint PoseResult ─────────────────────────────────────
    PoseResult result;
    result.timestamp = QDateTime::currentMSecsSinceEpoch();
    for (int k = 0; k < PoseResult::kNumKeypoints; ++k) {
        const int mpIdx = kMpToMoveNet[k];
        if (mpIdx < N) {
            result.keypoints[k].x     = pts[mpIdx].x;
            result.keypoints[k].y     = pts[mpIdx].y;
            result.keypoints[k].score = pts[mpIdx].conf;
        }
    }

    float scoreSum = 0.f;
    for (const auto &kp : result.keypoints) scoreSum += kp.score;
    result.confidence = scoreSum / PoseResult::kNumKeypoints;
    if (result.confidence < 0.25f) m_trackingLost = true;

    return result;
}

// ── estimatePose() ────────────────────────────────────────────────────────────

void PoseEstimatorMediaPipe::estimatePose(const cv::Mat &frame)
{
    if (!m_ready || frame.empty()) {
        emit estimationDone();
        return;
    }

    const qint64 nowNs = m_ort->wallTimer.nsecsElapsed();

    QElapsedTimer inferTimer;
    inferTimer.start();

    try {
        if (m_trackingLost) {
            m_body = detectPerson(frame);
            m_trackingLost = false;
            if (!m_body.valid) {
                // Detector found nothing — skip landmark model this frame.
                m_trackingLost = true;
                emit estimationDone();
                return;
            }
        }

        const PoseResult result = runLandmarks(frame, m_body);
        emit poseEstimated(result);

    } catch (const Ort::Exception &e) {
        ppError() << "[MediaPipe] Inference error:" << e.what();
        m_trackingLost = true;
        emit estimationDone();
        return;
    }

    // ── Rolling timing stats (same pattern as MoveNet) ─────────────────────
    const double inferMs    = inferTimer.nsecsElapsed() / 1e6;
    const double intervalMs = (m_lastCallNs >= 0)
                              ? (nowNs - m_lastCallNs) / 1e6
                              : inferMs;
    m_lastCallNs = nowNs;

    m_inferenceSum -= m_inferenceSamples[m_timingIndex];
    m_intervalSum  -= m_intervalSamples[m_timingIndex];
    m_inferenceSamples[m_timingIndex] = inferMs;
    m_intervalSamples[m_timingIndex]  = intervalMs;
    m_inferenceSum += inferMs;
    m_intervalSum  += intervalMs;
    m_timingIndex   = (m_timingIndex + 1) % kWindowSize;
    if (m_timingCount < kWindowSize) ++m_timingCount;

    if (m_timingCount == kWindowSize) {
        const double avgMs   = m_inferenceSum / kWindowSize;
        const double avgIval = m_intervalSum  / kWindowSize;
        const double fps     = (avgIval > 0.0) ? 1000.0 / avgIval : 0.0;
        emit poseStatsUpdated(avgMs, fps);
    }

    emit estimationDone();
}

#endif // HAVE_OPENCV && HAVE_MEDIAPIPE && HAVE_ONNXRUNTIME
