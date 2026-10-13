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

// THE BALL ANCHORS IMPACT (2026-09-29). At contact the clubhead IS at the ball, so the shaft at
// impact is the line from the hands to the address ball — both ends sharp: the ball has sat still
// for a second, the hands are tracked every frame, and neither is the blurred shaft the tracker has
// to read at 2°/ms. Against 32 hand-marked P7s (whose marks put the head ON the ball) the line from
// the hands at the pipeline's P7 to the marked ball reads −0.8° (sd 2.1°) — the marks themselves
// scatter 1.9° — where the tracker's own impact lean read +12.5° (sd ~10°).
//
// The ball's position is the hard part. The ball runner's background subtraction needs an empty-mat
// baseline; recordings without one seed it from the first frames, where the ball is ALREADY sitting,
// and the detector then locks onto whatever else changes (07-03, 07-08, 06-11, 09-09 in the corpus:
// 60–200 px off). So the address ball is found here by what makes it unique: it is the one thing on
// the mat that is STILL through the backswing and GONE after impact. A median of backswing frames
// (club in the air, ball sitting) minus a median of frames well after impact (ball gone) leaves the
// ball; confined to the mat below the toes, beside the P1 clubhead, at ball size. On the corpus:
// 29/32 within 10 px of the marked ball, median 1.9 px (prototype, 2026-09-29).
//
// Pure: OpenCV Mats in, a point out. The caller decodes the frames and supplies the geometry.

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

struct ImpactAnchorConfig {
    bool    enabled          = true;    // impactAnchor.enabled — false ⇒ nothing anchored, every metric as before
    int     framesPerMedian  = 5;       // impactAnchor.framesPerMedian
    int64_t afterDelayUs     = 150000;  // impactAnchor.afterDelayUs — the "gone" frames start this long after impact
    int64_t afterSpanUs      = 260000;  // impactAnchor.afterSpanUs — …and run this long
    double  searchHalfWidthPx = 150.0;  // impactAnchor.searchHalfWidthPx — |x − P1 head x| ≤ this (at 1280 px)
    double  belowToesPx      = 10.0;    // impactAnchor.belowToesPx — the ball sits below the lowest toe/heel point
    double  aboveHeadPx      = 60.0;    // impactAnchor.aboveHeadPx — …and not far above the P1 clubhead
    double  minRadiusPx      = 5.0;     // impactAnchor.minRadiusPx — ball radius range (at 1280 px wide)
    double  maxRadiusPx      = 9.0;     // impactAnchor.maxRadiusPx
    double  beforeWeight     = 0.5;     // the ball is a disc in the backswing frames…
    double  afterWeight      = 1.0;     // …and the mat is flat once it has gone
    double  minScore         = 15.0;    // impactAnchor.minScore — every genuine corpus find scored ≥ 32
};

struct AddressBall {
    bool    ok       = false;
    QPointF px;                         // ball centre, image px
    double  radiusPx = 0.0;
    double  score    = 0.0;
};

namespace impact_anchor_detail {
// Centred box mean of radius r (a (2r+1)² box), edges replicated.
inline cv::Mat boxMean(const cv::Mat& f32, int r)
{
    cv::Mat out;
    cv::boxFilter(f32, out, CV_32F, cv::Size(2 * r + 1, 2 * r + 1), cv::Point(-1, -1), true,
                  cv::BORDER_REPLICATE);
    return out;
}
// A bright disc of radius r against its surround: mean inside minus mean of the 2r box.
inline cv::Mat discResponse(const cv::Mat& f32, int r)
{
    return boxMean(f32, r) - boxMean(f32, 2 * r);
}
} // namespace impact_anchor_detail

// Pixel-wise median of same-size 8-bit grey frames, as CV_32F. Empty input ⇒ empty Mat.
inline cv::Mat medianFrame(const std::vector<cv::Mat>& frames)
{
    std::vector<cv::Mat> ok;
    for (const cv::Mat& f : frames)
        if (!f.empty() && (ok.empty() || f.size() == ok.front().size())) ok.push_back(f);
    if (ok.empty()) return {};
    const int n = int(ok.size()), rows = ok.front().rows, cols = ok.front().cols;
    cv::Mat out(rows, cols, CV_32F);
    std::vector<uchar> v(size_t(n), 0);
    for (int y = 0; y < rows; ++y) {
        float* o = out.ptr<float>(y);
        for (int x = 0; x < cols; ++x) {
            for (int k = 0; k < n; ++k) v[size_t(k)] = ok[size_t(k)].at<uchar>(y, x);
            std::nth_element(v.begin(), v.begin() + n / 2, v.end());
            o[x] = float(v[size_t(n / 2)]);
        }
    }
    return out;
}

// The address ball from a BEFORE median (backswing: club up, ball still there) and an AFTER median
// (well past impact: ball gone), both CV_32F grey of one size. p1HeadPx is the tracker's clubhead at
// address; toeLineY the lowest foot keypoint (image y grows downward), ≤ 0 ⇒ unknown. Scales its
// pixel figures by width/1280.
inline AddressBall findAddressBallByDeparture(const cv::Mat& before, const cv::Mat& after,
                                              QPointF p1HeadPx, double toeLineY,
                                              const ImpactAnchorConfig& cfg = {})
{
    using namespace impact_anchor_detail;
    AddressBall res;
    if (!cfg.enabled || before.empty() || after.empty() || before.size() != after.size()
        || before.type() != CV_32F || after.type() != CV_32F) return res;
    const int H = before.rows, W = before.cols;
    const double s = double(W) / 1280.0;
    const cv::Mat diff = before - after;

    const double yMin = std::max(toeLineY > 0.0 ? toeLineY + cfg.belowToesPx * s : 0.0,
                                 p1HeadPx.y() - cfg.aboveHeadPx * s);
    const int x0 = std::max(0, int(std::floor(p1HeadPx.x() - cfg.searchHalfWidthPx * s)));
    const int x1 = std::min(W - 1, int(std::ceil(p1HeadPx.x() + cfg.searchHalfWidthPx * s)));
    const int y0 = std::max(0, int(std::ceil(yMin)));
    if (x1 <= x0 || y0 >= H) return res;

    const int rLo = std::max(2, int(std::lround(cfg.minRadiusPx * s)));
    const int rHi = std::max(rLo, int(std::lround(cfg.maxRadiusPx * s)));
    double best = -std::numeric_limits<double>::infinity();
    for (int r = rLo; r <= rHi; ++r) {
        const cv::Mat score = discResponse(diff, r) + cfg.beforeWeight * discResponse(before, r)
                            - cfg.afterWeight * cv::abs(discResponse(after, r));
        for (int y = y0; y < H; ++y) {
            const float* row = score.ptr<float>(y);
            for (int x = x0; x <= x1; ++x) {
                if (row[x] > best) {
                    best = row[x];
                    res.px = QPointF(x, y);
                    res.radiusPx = r;
                }
            }
        }
    }
    res.score = best;
    res.ok = std::isfinite(best) && best >= cfg.minScore;
    return res;
}

// The shaft at contact: the hands at impact to the ball. Returns the grip→head angle in the image
// atan2 convention (radians, like ShaftSample2D::thetaRad) and the length in px.
inline void ballLine(QPointF gripPx, QPointF ballPx, double& thetaRad, double& lenPx)
{
    thetaRad = std::atan2(ballPx.y() - gripPx.y(), ballPx.x() - gripPx.x());
    lenPx    = std::hypot(ballPx.x() - gripPx.x(), ballPx.y() - gripPx.y());
}

} // namespace pinpoint::analysis
