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

#ifdef HAVE_OPENCV

#include "ball_detector.h"
#include "pp_debug.h"

#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

BallDetector::BallDetector(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BallDetection>();
    qRegisterMetaType<pinpoint::ballcal::BallCalProfile>();
}

void BallDetector::setRoi(QRectF roi)
{
    m_roi = roi;
}

void BallDetector::setProfile(const pinpoint::ballcal::BallCalProfile &profile)
{
    m_profile  = profile;
    m_drifting = false;
}

void BallDetector::clearProfile()
{
    m_profile  = pinpoint::ballcal::BallCalProfile{};
    m_drifting = false;
}

void BallDetector::beginCalibCapture(int targetFrames)
{
    m_calibTarget = qMax(1, targetFrames);
    m_calibHave   = 0;
}

void BallDetector::cancelCalibCapture()
{
    m_calibTarget = 0;
    m_calibHave   = 0;
}

void BallDetector::detect(const cv::Mat &frame)
{
    if (!isEnabled()) {     // disabled by method — release the throttle, keep ball state
        emit detectionSkipped();
        return;
    }

    if (m_roi.isEmpty() || frame.empty()) {
        // No hitting area configured (or degenerate frame) — release the
        // throttle without a result. Emitting ballDetected here would feed a
        // spurious miss into the presence-smoothing window and fire a fake
        // ball-lost transition (see the early-out contract in CLAUDE.md).
        emit detectionSkipped();
        return;
    }

    QElapsedTimer t;
    t.start();

    const int fw = frame.cols;
    const int fh = frame.rows;

    // Map the normalized ROI to pixel coordinates and clamp to frame bounds.
    const int rx = qBound(0, static_cast<int>(m_roi.x()      * fw), fw - 1);
    const int ry = qBound(0, static_cast<int>(m_roi.y()      * fh), fh - 1);
    const int rw = qBound(1, static_cast<int>(m_roi.width()  * fw), fw - rx);
    const int rh = qBound(1, static_cast<int>(m_roi.height() * fh), fh - ry);

    cv::Mat roiMat = frame(cv::Rect(rx, ry, rw, rh));

    // Calibration capture (side effect — detection continues below so the
    // overlays stay live and the throttle contract is untouched).
    if (m_calibTarget > 0) {
        emit calibFrame(roiMat.clone(), ++m_calibHave, m_calibTarget);
        if (m_calibHave >= m_calibTarget) {
            m_calibTarget = 0;
            m_calibHave   = 0;
            emit calibCaptureDone();
        }
    }

    // Calibrated path when a valid profile matches the current ROI geometry;
    // a resolution change (hard invalidation, design §6) falls back to legacy
    // rather than feeding a permanent found=false stream downstream.
    if (m_profile.valid
        && m_profile.background.meanGray.cols == rw
        && m_profile.background.meanGray.rows == rh) {
        detectCalibrated(roiMat, rx, ry, fw, fh, t);
        return;
    }

    detectLegacy(roiMat, rx, ry, fw, fh, t);
}

void BallDetector::detectCalibrated(const cv::Mat &roiMat, int rx, int ry,
                                    int fw, int fh, const QElapsedTimer &t)
{
    using namespace pinpoint::ballcal;

    const Detection det = pinpoint::ballcal::detect(roiMat, m_profile.background,
                                                    m_profile.ball, m_profile.theta);

    // Illumination drift monitor — state-change edges only (§6 soft drift).
    const double severity = driftSeverity(det.gain);
    const bool drifting   = severity > tuning::kGainDriftLog;
    if (drifting != m_drifting) {
        m_drifting = drifting;
        emit environmentDrift(drifting, severity);
    }

    if (det.found) {
        const float cx = (rx + det.centerPx.x) / static_cast<float>(fw);
        const float cy = (ry + det.centerPx.y) / static_cast<float>(fh);
        const float cr =       det.radiusPx    / static_cast<float>(fw);
        emit ballDetected(BallDetection{true, cx, cy, cr, t.elapsed(), det.score});
    } else {
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f, t.elapsed(), det.score});
    }
}

void BallDetector::detectLegacy(const cv::Mat &roiMat, int rx, int ry,
                                int fw, int fh, const QElapsedTimer &t)
{
    const int rw = roiMat.cols;
    const int rh = roiMat.rows;

    // White segmentation: isolate high-V, low-S pixels (the ball). Shadows are
    // not white so they are eliminated before any geometry detection runs.
    cv::Mat hsv;
    cv::cvtColor(roiMat, hsv, cv::COLOR_BGR2HSV);
    cv::Mat whiteMask;
    cv::inRange(hsv, cv::Scalar(0, 0, 170), cv::Scalar(180, m_whiteSatCeil, 255), whiteMask);

    // Morphological close fills dimple-texture gaps in the white mask.
    const cv::Mat closeKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(whiteMask, whiteMask, cv::MORPH_CLOSE, closeKernel);

    // Radius bounds relative to the ROI so the detector adapts to different
    // camera distances.
    const int roiMin    = std::min(rw, rh);
    const int minRadius = std::max(5, roiMin / 15);
    const int maxRadius = roiMin / 2;
    const int minDist   = std::max(10, rh / 8);

    // Upsample 2× so small distant balls generate more accumulator votes per arc.
    cv::Mat upsampled;
    cv::resize(whiteMask, upsampled, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    const int minRadiusUp = minRadius * 2;
    const int maxRadiusUp = maxRadius * 2;
    const int minDistUp   = minDist   * 2;

    // Strategy 1: HOUGH_GRADIENT_ALT in confidence mode on the white mask.
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(upsampled, circles, cv::HOUGH_GRADIENT_ALT,
                     /*dp=*/      1,
                     /*minDist=*/ minDistUp,
                     /*param1=*/  300,  // Canny high threshold
                     /*param2=*/  m_houghConf,
                     minRadiusUp,
                     maxRadiusUp);

    if (!circles.empty()) {
        const auto &c = circles[0];
        const float cx = (rx + c[0] / 2.f) / static_cast<float>(fw);
        const float cy = (ry + c[1] / 2.f) / static_cast<float>(fh);
        const float cr =      (c[2] / 2.f)  / static_cast<float>(fw);
        ppDebug() << "[BallDetector] strategy: hough" << cx << cy << "r=" << cr;
        emit ballDetected(BallDetection{true, cx, cy, cr, t.elapsed()});
        return;
    }

    // Strategy 2: SimpleBlobDetector fallback handles cases where the ball and
    // shadow merge into a non-circular but still convex blob.
    cv::SimpleBlobDetector::Params blobParams;
    blobParams.filterByColor       = true;
    blobParams.blobColor           = 255;
    blobParams.filterByCircularity = true;
    blobParams.minCircularity      = 0.55f;
    blobParams.filterByArea        = true;
    blobParams.minArea             = static_cast<float>(CV_PI * minRadiusUp * minRadiusUp);
    blobParams.maxArea             = static_cast<float>(CV_PI * maxRadiusUp * maxRadiusUp);
    blobParams.filterByConvexity   = true;
    blobParams.minConvexity        = 0.70f;
    blobParams.filterByInertia     = true;
    blobParams.minInertiaRatio     = 0.45f;

    std::vector<cv::KeyPoint> keypoints;
    cv::SimpleBlobDetector::create(blobParams)->detect(upsampled, keypoints);

    if (keypoints.empty()) {
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f, t.elapsed()});
        return;
    }

    // Take the largest blob.
    const auto &best = *std::max_element(keypoints.begin(), keypoints.end(),
        [](const cv::KeyPoint &a, const cv::KeyPoint &b) { return a.size < b.size; });

    // best.pt is in upsampled space; /2 converts back. best.size is diameter.
    const float cx = (rx + best.pt.x / 2.f) / static_cast<float>(fw);
    const float cy = (ry + best.pt.y / 2.f) / static_cast<float>(fh);
    const float cr =      (best.size  / 4.f) / static_cast<float>(fw);

    ppDebug() << "[BallDetector] strategy: blob" << cx << cy << "r=" << cr;
    emit ballDetected(BallDetection{true, cx, cy, cr, t.elapsed()});
}

void BallDetector::setParams(double houghConf, int whiteSatCeil)
{
    m_houghConf    = qBound(0.3, houghConf,    1.0);
    m_whiteSatCeil = qBound(20,  whiteSatCeil, 120);
}

#endif // HAVE_OPENCV
