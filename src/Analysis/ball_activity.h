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

// W3 club-corridor activity (ball.clubActivity) — the annulus + per-pixel
// temporal-median statistics, factored out of BallRunner::run so they are
// unit-testable without a video fixture (ball_activity_test.cpp). Header-only,
// OpenCV-only (no Qt), the detector-math convention (ball_temporal.h).

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/core.hpp>

namespace pinpoint::analysis {

// Club-corridor activity for one frame:
//
//   act = mean over the ANNULUS { innerRadPx <= |p − (cx,cy)| <= outerRadPx }
//         of |crop(p) − median_t ring(p)| , divided by sigma.
//
//   • crop  — the CURRENT gray ROI crop (CV_8U, single channel).
//   • ring  — the previous refFrames gray crops (each SAME size/type as crop).
//             Their PER-PIXEL temporal median is the reference: a club bob dwells
//             at its travel extremes, so a median reference beats a raw frame-diff
//             (which a symmetric bob washes out at its endpoints).
//   • (cx,cy) — locked ball centre in CROP-LOCAL px (sub-pixel ok).
//   • inner radius EXCLUDES the ball disc (so ball-lock jitter isn't read as
//     activity); the outer radius covers the resting clubhead beside the ball.
//   • sigma — the crop's robustNoise (exposure/noise normalisation), so the
//     ratio is comparable across frames of differing exposure/contrast.
//
// Returns -1 (absent — the BallSample2D::clubActivity contract) when: crop empty,
// ring empty, any ring crop mismatches crop's size/type, sigma <= 0, the radii are
// degenerate (outer <= inner), or the annulus admits no in-bounds pixel. Never
// throws / reads out of bounds — the annulus bounding box is clamped to the crop.
inline float clubCorridorActivity(const cv::Mat &crop,
                                  const std::vector<cv::Mat> &ring,
                                  double cx, double cy,
                                  double innerRadPx, double outerRadPx,
                                  double sigma)
{
    if (crop.empty() || ring.empty() || sigma <= 0.0)
        return -1.f;
    const int W = crop.cols, H = crop.rows;
    for (const cv::Mat &m : ring)
        if (m.rows != H || m.cols != W || m.type() != crop.type() || m.channels() != 1)
            return -1.f;

    const double inner2 = innerRadPx > 0.0 ? innerRadPx * innerRadPx : 0.0;
    const double outer2 = outerRadPx * outerRadPx;
    if (outer2 <= inner2) return -1.f;

    // Bounding box of the outer disc, clamped to the crop.
    const int x0 = std::max(0, int(std::floor(cx - outerRadPx)));
    const int x1 = std::min(W - 1, int(std::ceil(cx + outerRadPx)));
    const int y0 = std::max(0, int(std::floor(cy - outerRadPx)));
    const int y1 = std::min(H - 1, int(std::ceil(cy + outerRadPx)));

    double acc = 0.0;
    long   n   = 0;
    std::vector<double> col;
    col.reserve(ring.size());
    for (int y = y0; y <= y1; ++y) {
        const uchar *crow = crop.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const double dx = double(x) - cx, dy = double(y) - cy;
            const double d2 = dx * dx + dy * dy;
            if (d2 < inner2 || d2 > outer2) continue;
            col.clear();
            for (const cv::Mat &m : ring) col.push_back(double(m.ptr<uchar>(y)[x]));
            std::nth_element(col.begin(), col.begin() + col.size() / 2, col.end());
            const double med = col[col.size() / 2];   // lower-median (odd refFrames ⇒ true median)
            acc += std::abs(double(crow[x]) - med);
            ++n;
        }
    }
    if (n == 0) return -1.f;
    return float((acc / double(n)) / sigma);
}

} // namespace pinpoint::analysis
