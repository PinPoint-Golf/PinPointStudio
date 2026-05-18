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

    cv::Mat gray;
    cv::cvtColor(roiMat, gray, cv::COLOR_BGR2GRAY);

    // Gaussian blur reduces noise and suppresses the ball's dimple texture,
    // making the overall circular outline more prominent to the Hough transform.
    cv::GaussianBlur(gray, gray, cv::Size(9, 9), 2.0);

    // Radius bounds relative to the ROI so the detector adapts to different
    // camera distances.
    const int roiMin    = std::min(rw, rh);
    const int minRadius = std::max(5, roiMin / 15);
    const int maxRadius = roiMin / 2;
    const int minDist   = std::max(10, rh / 8);

    // Low param1 (Canny threshold) and low param2 (accumulator threshold) let
    // HOUGH_GRADIENT accumulate votes from partial arcs — each edge pixel votes
    // along its gradient direction so even a quarter-circle of visible rim is
    // enough to locate the centre.
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT,
                     /*dp=*/      1,
                     /*minDist=*/ minDist,
                     /*param1=*/  30,   // Canny high threshold — lenient edge detection
                     /*param2=*/  30,   // accumulator threshold — accepts partial arcs
                     minRadius,
                     maxRadius);

    if (circles.empty()) {
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f});
        return;
    }

    // HOUGH_GRADIENT returns circles in decreasing accumulator order — take the first.
    const auto &c = circles[0];
    const float cx = (rx + c[0]) / static_cast<float>(fw);
    const float cy = (ry + c[1]) / static_cast<float>(fh);
    const float cr =       c[2]  / static_cast<float>(fw);

    ppDebug() << "[BallDetector] found at" << cx << cy << "r=" << cr;
    emit ballDetected(BallDetection{true, cx, cy, cr});
}

#endif // HAVE_OPENCV
