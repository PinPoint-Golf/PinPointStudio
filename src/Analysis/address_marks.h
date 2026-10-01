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

// The body's OUTER edges at hip height on the address frame — the coach's reference lines
// (docs/design/auto_annotations_design.md §1): face-on, the outside of each hip, the cue for
// sway; down the line, the rear of the butt, the cue for moving off the line. The pose knows
// joint CENTRES; the edges come from the person mask (src/Pose/person_segmenter.h, u2netp)
// on the one frame nearest the swing's Address instant, read along the rows at hip height.
//
// Pure OpenCV, no Qt: `outerExtents` is the whole measurement and a test can press it with a
// drawn mask. The stage (AddressMarksStage, wrist_analyzer.cpp) supplies the mask, the hip
// keypoints and which side the butt is on; the serialiser (address_marks_json.h) writes the
// result as `analysis.addressMarks`; nothing reads it but the tile. Coordinates are
// NORMALISED by the frame (x by width, y by height) once measured, like every overlay block.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// The result structs below are plain and every consumer of swing_analysis.h sees them; the
// measurement needs OpenCV and is compiled only where HAVE_OPENCV is defined (the app, the
// offline tools, the test) — the document converters that compile swing_doc.cpp without
// OpenCV get the structs and nothing else.
#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#endif

namespace pinpoint::analysis::addressmarks {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// One camera's marks. `leftX/rightX` are the outer edges of the body on the hip rows (face-on:
// the two hips; DTL: the body's front and rear); `buttX` is the rear edge on the DTL view, the
// side away from the hands (`side` −1 = image-left is the rear, +1 = image-right). The hip
// JOINTS the rows were read at are kept for the record (and for a tile to show what was read).
struct ViewMarks {
    bool    found    = false;
    int64_t frameTUs = 0;          // the frame the mask was read on
    double  rowY     = kNan;       // the hip row, normalised
    double  hipLeftX = kNan, hipRightX = kNan;   // the pose's hip joints, normalised
    double  leftX    = kNan, rightX = kNan;      // the mask's outer edges on the hip rows, normalised
    double  buttX    = kNan;       // DTL only: the rear edge
    int     side     = 0;          // DTL only: which side the rear is (−1 left, +1 right)
    int     rows     = 0;          // rows that contributed to the medians
    // Which rows were read: 0 = the hip joints' row (always, since 2026-10-01 10:30). The wrists'
    // row was tried first (the arms hang outside the hips at hip height, so the edge there can
    // be an elbow, ~1 % of the frame outside the pelvis) and REJECTED: the segmenter's mask
    // drops the dark lead thigh beside the bright glove at that height, and the "edge" became
    // the glove, 5 % of the frame INSIDE the thigh (library 4 July s8). The elbow is at worst a
    // small error on the outside; the glove is a large one on the inside. 1 is kept for the
    // record's sake and never produced.
    int     rowSource = 0;
};

struct AddressMarks {
    ViewMarks faceOn, dtl;
    bool any() const { return faceOn.found || dtl.found; }
};

#ifdef HAVE_OPENCV
// The outer extents of the body on the rows [yPx − halfRows, yPx + halfRows]: on each row,
// the LEFT edge is reached by walking left from the left hip's x and the RIGHT edge by
// walking right from the right hip's x — each seed snapped to the nearest person pixel
// within `seedReachPx` (a thin mask at the waist can miss the joint by a few pixels), each
// walk going the whole `maxReachPx` and keeping the LAST person pixel it met — the outermost
// body pixel within reach, across any gap (`gapPx` > 0 limits the gap instead). Two seeds,
// not one at the midpoint, and the farthest pixel, not the first run: at the wrists' row the
// hands hang in FRONT of the lead thigh as their own mask run with a thin gap to the thigh
// behind them (07-04 s7: the "edge" landed on the glove, 12 px inside the thigh), and the
// seed's own run is the hands. The reach is what keeps a background object out, so it is
// tight (one and a half hip widths). The per-row edges are medianed per side; fewer than
// `minRows` usable rows on either side is not a measurement. `mask` is CV_32F in [0, 1]
// (the segmenter's) or CV_8U; `thr` is the person threshold in the mask's own units.
inline bool outerExtents(const cv::Mat &mask, double xlPx, double xrPx, double yPx,
                         int halfRows, double maxReachPx, double &leftPx, double &rightPx, int &rowsUsed,
                         double thr = 0.5, int gapPx = 0, int seedReachPx = 12, int minRows = 3)
{
    leftPx = rightPx = kNan;
    rowsUsed = 0;
    if (mask.empty() || !(mask.type() == CV_32F || mask.type() == CV_8U)) return false;
    const int W = mask.cols, H = mask.rows;
    auto person = [&](int x, int y) {
        if (x < 0 || x >= W || y < 0 || y >= H) return false;
        return mask.type() == CV_32F ? mask.at<float>(y, x) > float(thr)
                                     : double(mask.at<uint8_t>(y, x)) > thr;
    };
    const int xl = int(std::lround(std::min(xlPx, xrPx)));
    const int xr = int(std::lround(std::max(xlPx, xrPx)));
    const int yc = int(std::lround(yPx));
    const int reach = int(std::lround(maxReachPx));
    auto seedNear = [&](int x0, int y) {
        if (person(x0, y)) return x0;
        for (int d = 1; d <= seedReachPx; ++d) {
            if (person(x0 - d, y)) return x0 - d;
            if (person(x0 + d, y)) return x0 + d;
        }
        return -1;
    };
    auto walk = [&](int seed, int y, int dir) {
        int x = seed, last = seed, gap = 0;
        for (;;) {
            x += dir;
            if (x < 0 || x >= W || std::abs(x - seed) > reach) break;
            if (person(x, y)) { last = x; gap = 0; }
            else if (gapPx > 0 && ++gap >= gapPx) break;
        }
        return last;
    };
    // A walk that reaches the frame's own edge found no body edge — the golfer is clipped by
    // the framing (07-04 s1–3, the rig before it was moved: the back of the body is out of the
    // picture and the "butt" would be the image border). That row contributes nothing on that side.
    std::vector<double> L, R;
    for (int y = yc - halfRows; y <= yc + halfRows; ++y) {
        if (y < 0 || y >= H) continue;
        const int sl = seedNear(xl, y), sr = seedNear(xr, y);
        if (sl >= 0) { const int e = walk(sl, y, -1); if (e > 0)     L.push_back(double(e)); }
        if (sr >= 0) { const int e = walk(sr, y, +1); if (e < W - 1) R.push_back(double(e)); }
    }
    if (int(L.size()) < minRows || int(R.size()) < minRows) return false;
    auto med = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    };
    leftPx = med(L);
    rightPx = med(R);
    rowsUsed = int(std::min(L.size(), R.size()));
    return true;
}

// Measure one view. `hipL/hipR` the hip joints in px, `handsX` the mid-hands x in px (DTL:
// the rear is the side AWAY from the hands; NaN ⇒ no butt mark), `W/H` the frame. The hip
// rows are ±`bandFrac` of the frame height; the walk is capped at `reachHipWidths` times the
// hip joints' separation (or `reachMinFrac` of the width when the joints are close, as they
// are down the line).
// `wristY` / `kneeY` are accepted for the record and NOT used to choose the rows any more (see
// ViewMarks::rowSource): the hip joints' row is read on both cameras.
inline ViewMarks measureView(const cv::Mat &mask, int W, int H, int64_t frameTUs,
                             double hipLx, double hipLy, double hipRx, double hipRy,
                             double handsX, bool dtl,
                             double wristY = kNan, double kneeY = kNan,
                             double bandFrac = 0.015, double reachHipWidths = 1.5, double reachMinFrac = 0.20)
{
    ViewMarks v;
    v.frameTUs = frameTUs;
    if (mask.empty() || W <= 0 || H <= 0) return v;
    if (!(std::isfinite(hipLx) && std::isfinite(hipRx) && std::isfinite(hipLy) && std::isfinite(hipRy))) return v;
    (void)wristY; (void)kneeY;
    const double yPx = 0.5 * (hipLy + hipRy);
    const double reach = std::max(reachHipWidths * std::fabs(hipRx - hipLx), reachMinFrac * W);
    double l, r; int rows;
    if (!outerExtents(mask, hipLx, hipRx, yPx, std::max(1, int(std::lround(bandFrac * H))), reach, l, r, rows)) return v;
    v.found     = true;
    v.rows      = rows;
    v.rowY      = yPx / H;
    v.hipLeftX  = std::min(hipLx, hipRx) / W;
    v.hipRightX = std::max(hipLx, hipRx) / W;
    v.leftX     = l / W;
    v.rightX    = r / W;
    if (dtl && std::isfinite(handsX)) {
        const double mid = 0.5 * (hipLx + hipRx);
        v.side  = handsX >= mid ? -1 : +1;          // the rear is away from the hands
        v.buttX = (v.side < 0 ? l : r) / W;
    }
    return v;
}

#endif // HAVE_OPENCV

} // namespace pinpoint::analysis::addressmarks
