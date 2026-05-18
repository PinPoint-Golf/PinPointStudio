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

#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

BallDetector::BallDetector(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BallDetection>();
}

void BallDetector::setRoi(QRectF roi)
{
    m_roi = roi;
}

void BallDetector::detect(const cv::Mat &frame)
{
    if (m_roi.isEmpty() || frame.empty())
        return;

    const int fw = frame.cols;
    const int fh = frame.rows;

    // Map the normalized ROI to pixel coordinates and clamp to frame bounds.
    const int rx = qBound(0, static_cast<int>(m_roi.x()      * fw), fw - 1);
    const int ry = qBound(0, static_cast<int>(m_roi.y()      * fh), fh - 1);
    const int rw = qBound(1, static_cast<int>(m_roi.width()  * fw), fw - rx);
    const int rh = qBound(1, static_cast<int>(m_roi.height() * fh), fh - ry);

    cv::Mat roiMat = frame(cv::Rect(rx, ry, rw, rh));

    // White segmentation: isolate high-V, low-S pixels (the ball). Shadows are
    // not white so they are eliminated before any geometry detection runs.
    cv::Mat hsv;
    cv::cvtColor(roiMat, hsv, cv::COLOR_BGR2HSV);
    cv::Mat whiteMask;
    cv::inRange(hsv, cv::Scalar(0, 0, 170), cv::Scalar(180, 50, 255), whiteMask);

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
                     /*param2=*/  0.7,  // confidence threshold [0,1]
                     minRadiusUp,
                     maxRadiusUp);

    if (!circles.empty()) {
        const auto &c = circles[0];
        const float cx = (rx + c[0] / 2.f) / static_cast<float>(fw);
        const float cy = (ry + c[1] / 2.f) / static_cast<float>(fh);
        const float cr =      (c[2] / 2.f)  / static_cast<float>(fw);
        ppDebug() << "[BallDetector] strategy: hough" << cx << cy << "r=" << cr;
        emit ballDetected(BallDetection{true, cx, cy, cr});
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
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f});
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
    emit ballDetected(BallDetection{true, cx, cy, cr});
}

#endif // HAVE_OPENCV
