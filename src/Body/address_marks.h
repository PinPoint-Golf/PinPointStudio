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
// drawn mask. The stage (AddressMarksStage, analysis_pipeline.cpp) supplies the mask, the hip
// keypoints and which side the butt is on; the serialiser (address_marks_json.h) writes the
// result as `analysis.addressMarks`; nothing reads it but the tile. Coordinates are
// NORMALISED by the frame (x by width, y by height) once measured, like every overlay block.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
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
    // DTL only: TPI's spine angle line, "from the base of the tailbone to the back of the neck"
    // (auto_annotations_design.md §1b), two points on the REAR outline of the silhouette, each
    // read by walking at a right angle to the trunk axis (hip-joint midpoint → shoulder-joint
    // midpoint) toward the rear (`side`) — not along an image row: with the trunk tilted ~40°
    // forward at address a row at neck height reads the upper back, 70 px behind the neck
    // (Nick Henn, 20 Oct). The tailbone: behind the hip-joint midpoint. The back of the neck:
    // behind the point halfway from the shoulder-joint midpoint to the ears. Normalised; NaN when
    // not measured (no rear side, a trunk or ear keypoint missing, too few walks).
    double  tailboneX = kNan, tailboneY = kNan;
    double  neckX     = kNan, neckY     = kNan;
    int     backWalks = 0;         // walks behind the lower of the two counts
    // THE HEAD OUTLINE (stage version 4, 22 Oct 2026; measureHead below): the head circle the coach
    // lines draw, off the SILHOUETTE instead of the face points — at address the golfer looks down at
    // the ball, the eyes sit low and forward, and a circle on the face keypoints lands on the face, not
    // round the skull (Mark, Nick Henn 20 Oct swing 2). The crown is the mask's top over the head; the
    // width the widest row of the head's outline between the crown and the ears; the circle touches the
    // crown with that width as its diameter. `headCx` ÷ W, `headCy` and `headR` ÷ H (the coach lines'
    // radius convention: a fraction of the frame HEIGHT), `crownY` and `headRowY` (the widest row) ÷ H,
    // `headLeftX/headRightX` (that row's ends) ÷ W. Not found ⇒ `headRefused` says why (a hood or a cap
    // merging with the body, the mask at the frame's edge, no head points); the readers then fall back
    // to the face-point circle.
    bool        headFound = false;
    double      headCx = kNan, headCy = kNan, headR = kNan;
    double      crownY = kNan, headRowY = kNan;
    double      headLeftX = kNan, headRightX = kNan;
    std::string headRefused;
};

// One DOWN-THE-LINE P-frame's marks (posture_chart_design.md §2.3, §10 stage 4a; stage version 3):
// the butt line and TPI's back line read on the frame nearest each position P1…P7, so the trunk
// bend and the hip depth can be read off the silhouette where the joint points turn out of the
// picture (P3–P5). The rear side is FROZEN from address (the hands go up at the top, and "away
// from the hands" would flip). P1 is the address frame, mask and butt line, copied from the coach
// lines' `dtl` view; its back line is re-read on that mask by the P-frame rule (behind the ears,
// AddressMarks::phaseNeckFrac), so the seven back lines are one construction.
struct DtlPhaseMarks {
    int         phase    = 0;      // the Phase (swing_analysis.h), as its persisted int
    int64_t     eventTUs = 0;      // the position's instant on the ladder
    ViewMarks   v;                 // found, the frame read, rowY, buttX, side (frozen), tailbone, neck
    std::string refused;           // why there is no reading, when there is none ("" when found)
};

struct AddressMarks {
    ViewMarks faceOn, dtl;
    std::vector<DtlPhaseMarks> dtlPhases;   // P1…P7 on the DTL camera, ladder order; empty before v3
    // Where the P-frames' back line reads its upper point (a fraction of the way from the shoulder
    // midpoint to the ears; AddressMarksStage phaseBackLine, 1.0 by default). The coach line in
    // `dtl` is read at 0.5, the collar; P1's entry re-reads the same address mask at this fraction.
    double phaseNeckFrac = kNan;
    bool any() const { return faceOn.found || dtl.found; }
};

// THE BACK LINE'S LEAN from the vertical, in degrees, + over the ball — spineForwardBend's sign
// (dtl_posture.h: atan2(toward·dx, −dy) on the hip → shoulder chord), read on tailbone → back of
// the neck instead. `toward` is +1 when the ball is image-right, −1 image-left; W/H the frame
// (the points are normalised). NaN when either point is missing or they coincide.
inline double backLineBendDeg(const ViewMarks &v, int W, int H, double toward)
{
    if (!(toward == 1.0 || toward == -1.0) || W <= 0 || H <= 0) return kNan;
    for (double q : { v.tailboneX, v.tailboneY, v.neckX, v.neckY }) if (!std::isfinite(q)) return kNan;
    const double dx = (v.neckX - v.tailboneX) * W, dy = (v.neckY - v.tailboneY) * H;
    if (std::hypot(dx, dy) < 1.0) return kNan;
    return std::atan2(toward * dx, -dy) * 180.0 / 3.14159265358979323846;
}

// THE BUTT LINE'S TRAVEL from address in centimetres, + toward the ball — pelvisThrust's sign.
// `cmPerPx` is the DTL ruler (dtl_posture.h buildDtlPosture, persisted as analysis.dtlRuler); no
// ruler (≤ 0) ⇒ NaN, never another unit.
inline double buttShiftCm(const ViewMarks &v, const ViewMarks &addr, int W, double toward, double cmPerPx)
{
    if (!(toward == 1.0 || toward == -1.0) || W <= 0 || !(cmPerPx > 0.0)) return kNan;
    if (!std::isfinite(v.buttX) || !std::isfinite(addr.buttX)) return kNan;
    return toward * (v.buttX - addr.buttX) * W * cmPerPx;
}

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

// measureView on a DTL P-frame past address: the rear side is GIVEN (frozen from address) rather
// than chosen away from the hands, which are over the trail shoulder at the top. side 0 ⇒ the edges
// are measured and no butt is claimed, as measureView does without hands.
inline ViewMarks measureViewFrozenSide(const cv::Mat &mask, int W, int H, int64_t frameTUs,
                                       double hipLx, double hipLy, double hipRx, double hipRy, int side)
{
    ViewMarks v = measureView(mask, W, H, frameTUs, hipLx, hipLy, hipRx, hipRy, kNan, true);
    if (v.found && side != 0) {
        v.side  = side < 0 ? -1 : +1;
        v.buttX = v.side < 0 ? v.leftX : v.rightX;
    }
    return v;
}

// The rear outline behind a point: `walks` parallel walks along the unit direction (nx, ny),
// their seeds spread ±`halfSpanPx` along the unit axis (ux, uy) around (sx, sy), each keeping
// the farthest person pixel within `reachPx` (across any gap, as outerExtents does). A walk
// whose seed has no person pixel within `seedReachPx` along ±n, or that reaches the frame's
// edge before its reach, contributes nothing — the edge is not in the picture (a clipped body
// is not measured, the butt line's rule). The per-walk distances are medianed and the edge is
// the CENTRE seed plus that distance along n; fewer than `minWalks` is not a measurement.
// `gapPx` > 0 ends a walk at the first run of that many background pixels after the body — the
// body's OWN outline, not the farthest person pixel within reach (P2–P7: the trail arm and the
// hands at the top sit behind the neck from down the line; posture_chart_design.md §10 stage 4a).
// 0 = cross any gap, the address rule.
inline bool rearEdgeAlong(const cv::Mat &mask, double sx, double sy, double ux, double uy,
                          double nx, double ny, double halfSpanPx, int walks, double reachPx,
                          double &ex, double &ey, int &used,
                          double thr = 0.5, int seedReachPx = 12, int minWalks = 3, int gapPx = 0)
{
    ex = ey = kNan;
    used = 0;
    if (mask.empty() || !(mask.type() == CV_32F || mask.type() == CV_8U) || walks < 1) return false;
    const int W = mask.cols, H = mask.rows;
    auto inside = [&](double x, double y) {
        const int ix = int(std::lround(x)), iy = int(std::lround(y));
        return ix >= 0 && ix < W && iy >= 0 && iy < H;
    };
    auto person = [&](double x, double y) {
        const int ix = int(std::lround(x)), iy = int(std::lround(y));
        if (ix < 0 || ix >= W || iy < 0 || iy >= H) return false;
        return mask.type() == CV_32F ? mask.at<float>(iy, ix) > float(thr)
                                     : double(mask.at<uint8_t>(iy, ix)) > thr;
    };
    std::vector<double> D;
    for (int i = 0; i < walks; ++i) {
        const double o = walks == 1 ? 0.0 : -halfSpanPx + 2.0 * halfSpanPx * i / (walks - 1);
        const double x0 = sx + ux * o, y0 = sy + uy * o;
        bool seeded = false;
        for (int d = 0; d <= seedReachPx && !seeded; ++d)
            seeded = person(x0 + nx * d, y0 + ny * d) || person(x0 - nx * d, y0 - ny * d);
        if (!seeded) continue;
        double last = -1.0;
        bool clipped = false;
        int gap = 0;
        for (int t = 0; t <= int(std::lround(reachPx)); ++t) {
            const double x = x0 + nx * t, y = y0 + ny * t;
            if (!inside(x, y)) { clipped = true; break; }
            if (person(x, y)) { last = double(t); gap = 0; }
            else if (gapPx > 0 && last >= 0 && ++gap >= gapPx) break;
        }
        if (clipped || last < 0) continue;
        D.push_back(last);
    }
    if (int(D.size()) < minWalks) return false;
    std::sort(D.begin(), D.end());
    const size_t n = D.size();
    const double med = n % 2 ? D[n / 2] : 0.5 * (D[n / 2 - 1] + D[n / 2]);
    ex = sx + nx * med;
    ey = sy + ny * med;
    used = int(n);
    return true;
}

// TPI's spine angle line on the DTL address frame: fills `v.tailbone*` / `v.neck*` (normalised)
// when `v.side` names the rear. Pixel inputs: the hip-joint and shoulder-joint midpoints and the
// ears' midpoint (any NaN ⇒ nothing). The trunk axis u = hip → shoulder (length L); the rear
// direction n ⊥ u on the `side` half. Tailbone: behind the hip-joint midpoint (`tailboneFrac` 0
// of the way up the axis); back of the neck: behind the point `neckFrac` 0.5 of the way from the
// shoulder midpoint to the ears. Each read with 11 walks over ±`spanFrac` 0.05 L, reach `reachPx`.
// Chosen on Nick Henn's 20 Oct swing 1 address frame (auto_annotations_design.md §1b): behind
// the hip joints is the top of the seat at the waistband; halfway to the ears is the collar —
// the base of the neck — where 0.75 and 1.0 climb onto the back of the head's hairline.
inline void measureBackLine(const cv::Mat &mask, int W, int H, ViewMarks &v,
                            double hipX, double hipY, double shX, double shY, double earX, double earY,
                            double reachPx, double tailboneFrac = 0.0, double neckFrac = 0.5,
                            double spanFrac = 0.05, int walks = 11, int gapPx = 0)
{
    v.tailboneX = v.tailboneY = v.neckX = v.neckY = kNan;
    v.backWalks = 0;
    if (mask.empty() || W <= 0 || H <= 0 || v.side == 0) return;
    for (double q : { hipX, hipY, shX, shY, earX, earY }) if (!std::isfinite(q)) return;
    const double ax = shX - hipX, ay = shY - hipY, L = std::hypot(ax, ay);
    if (!(L > 1.0)) return;
    const double ux = ax / L, uy = ay / L;
    double nx = -uy, ny = ux;                            // ⊥ u, then onto the rear half
    if ((nx < 0) != (v.side < 0)) { nx = -nx; ny = -ny; }
    double tx, ty, kx, ky; int tw, kw;
    if (!rearEdgeAlong(mask, hipX + tailboneFrac * ax, hipY + tailboneFrac * ay, ux, uy, nx, ny,
                       spanFrac * L, walks, reachPx, tx, ty, tw, 0.5, 12, 3, gapPx)) return;
    if (!rearEdgeAlong(mask, shX + neckFrac * (earX - shX), shY + neckFrac * (earY - shY), ux, uy, nx, ny,
                       spanFrac * L, walks, reachPx, kx, ky, kw, 0.5, 12, 3, gapPx)) return;
    v.tailboneX = tx / W; v.tailboneY = ty / H;
    v.neckX     = kx / W; v.neckY     = ky / H;
    v.backWalks = std::min(tw, kw);
}

// THE HEAD OUTLINE on the address mask (ViewMarks::head*; stage version 4). Inputs in px: the five
// COCO head points (nose, eyes, ears: `hx/hy`, `hok` whether each is confident) and the shoulders' top
// row `shoulderTopY` (the higher of the seen shoulder joints; NaN when neither is seen).
//   · SCALE S: the head's width as the face points give it — the ear span, else the eye span × 1.8
//     (head_track's ear/eye ratio), else 1.3 × nose→ear; the largest that is available. No two head
//     points ⇒ refused.
//   · THE HEAD'S BLOB: the person pixels 4-connected to the seed (the head points' mean, snapped to the
//     nearest person pixel within 0.4 S), inside a search box ±`spanS` S beside the head points and from
//     the frame's top down to the shoulders (or 1.5 S below the head points).
//   · THE CROWN: that blob's top-most row. Refused when the blob reaches the frame's top edge (the crown
//     is out of the picture), when it reaches the box's top above the frame (the walk left the head),
//     when it sits more than `crownAboveS` S above the highest head point (a hood or a cap stands proud
//     of the skull, or the background merges), or when it is not above the head points at all.
//   · THE WIDTH: the widest row of the blob between the crown and the ears' row (the eyes', the head
//     points' mean), the walk down stopping at the first row whose outline steps out by more than
//     `stepS` S on a side (something joining the head) once it is half a head wide; refused when that row runs into the search
//     box's side (the head merges with the shoulders, a collar or the background) or is outside
//     [`minWidthS`, `maxWidthS`] S.
//   · THE CIRCLE: diameter = that width, centred on that row's middle, its top at the crown.
// Fills v.head* (normalised) on success, else v.headRefused. A circle, not an ellipse: the crown and the
// width are what a coach's circle shows, and the chin is not in question.
inline void measureHead(const cv::Mat &mask, int W, int H, ViewMarks &v,
                        const double hx[5], const double hy[5], const bool hok[5], double shoulderTopY,
                        double thr = 0.5, double spanS = 0.9, double crownAboveS = 1.35,
                        double minWidthS = 0.75, double maxWidthS = 1.7, double stepS = 0.12)
{
    v.headFound = false;
    v.headCx = v.headCy = v.headR = v.crownY = v.headRowY = v.headLeftX = v.headRightX = kNan;
    v.headRefused.clear();
    if (mask.empty() || !(mask.type() == CV_32F || mask.type() == CV_8U) || W <= 0 || H <= 0
        || mask.cols != W || mask.rows != H) { v.headRefused = "no mask"; return; }
    auto person = [&](int x, int y) {
        if (x < 0 || x >= W || y < 0 || y >= H) return false;
        return mask.type() == CV_32F ? mask.at<float>(y, x) > float(thr) : double(mask.at<uint8_t>(y, x)) > thr;
    };
    int n = 0;
    double sx = 0, sy = 0, minX = 1e18, maxX = -1e18, minY = 1e18;
    for (int j = 0; j < 5; ++j) {
        if (!hok[j] || !std::isfinite(hx[j]) || !std::isfinite(hy[j])) continue;
        sx += hx[j]; sy += hy[j]; ++n;
        minX = std::min(minX, hx[j]); maxX = std::max(maxX, hx[j]); minY = std::min(minY, hy[j]);
    }
    if (n < 2) { v.headRefused = "fewer than two head points seen"; return; }
    sx /= n; sy /= n;
    auto span = [&](int a, int b) { return (hok[a] && hok[b]) ? std::hypot(hx[a] - hx[b], hy[a] - hy[b]) : 0.0; };
    double S = std::max(span(3, 4), 1.8 * span(1, 2));
    S = std::max(S, 1.3 * std::max(span(0, 3), span(0, 4)));
    if (!(S >= 4.0)) { v.headRefused = "the head points give no size"; return; }
    // the ears' row (else the eyes', else the points' mean): the bottom of the width search
    double earY = kNan;
    if (hok[3] && hok[4]) earY = 0.5 * (hy[3] + hy[4]);
    else if (hok[3] || hok[4]) earY = hok[3] ? hy[3] : hy[4];
    else if (hok[1] && hok[2]) earY = 0.5 * (hy[1] + hy[2]);
    else earY = sy;
    // the search box
    const int x0 = std::max(0, int(std::floor(minX - spanS * S)));
    const int x1 = std::min(W - 1, int(std::ceil(maxX + spanS * S)));
    const double floorY = std::isfinite(shoulderTopY) && shoulderTopY > sy ? shoulderTopY : sy + 1.5 * S;
    const int yTop = std::max(0, int(std::floor(minY - 2.5 * S)));
    const int yBot = std::min(H - 1, int(std::ceil(floorY)));
    if (x1 - x0 < 4 || yBot - yTop < 4) { v.headRefused = "the head is at the frame's edge"; return; }
    // the seed, snapped to the nearest person pixel
    int seedX = -1, seedY = -1;
    {
        const int cx = int(std::lround(sx)), cy = int(std::lround(sy)), reach = int(std::lround(0.4 * S));
        double best = 1e18;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx) {
                const double d = double(dx) * dx + double(dy) * dy;
                if (d < best && d <= double(reach) * reach && person(cx + dx, cy + dy)) { best = d; seedX = cx + dx; seedY = cy + dy; }
            }
    }
    if (seedX < 0 || seedY < yTop || seedY > yBot) { v.headRefused = "no person mask at the head"; return; }
    // the blob: 4-connected person pixels inside the box
    const int bw = x1 - x0 + 1, bh = yBot - yTop + 1;
    std::vector<uint8_t> in(size_t(bw) * size_t(bh), 0);
    std::vector<int> stack{ (seedY - yTop) * bw + (seedX - x0) };
    in[size_t(stack.back())] = 1;
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back();
        const int bx = i % bw, by = i / bw;
        const int nb[4][2] = { { bx - 1, by }, { bx + 1, by }, { bx, by - 1 }, { bx, by + 1 } };
        for (const auto &q : nb) {
            if (q[0] < 0 || q[0] >= bw || q[1] < 0 || q[1] >= bh) continue;
            const int k = q[1] * bw + q[0];
            if (in[size_t(k)] || !person(x0 + q[0], yTop + q[1])) continue;
            in[size_t(k)] = 1;
            stack.push_back(k);
        }
    }
    // the crown
    int crown = -1;
    for (int by = 0; by < bh && crown < 0; ++by)
        for (int bx = 0; bx < bw; ++bx) if (in[size_t(by) * bw + bx]) { crown = by; break; }
    if (crown < 0) { v.headRefused = "no person mask at the head"; return; }
    const double crownPx = yTop + crown;
    if (crown == 0) {
        v.headRefused = yTop == 0 ? "the mask runs to the top of the frame: the crown is out of the picture"
                                  : "the outline above the head runs on past the search: not a head (a hood, a cap or the background)";
        return;
    }
    if (minY - crownPx > crownAboveS * S) {
        v.headRefused = "the outline stands too far above the face for a head (a hood, a cap or the background merges)";
        return;
    }
    if (crownPx >= minY) { v.headRefused = "the mask does not cover the top of the head"; return; }
    // the width: the widest row between the crown and the ears
    // Walking down from the crown the skull widens a few pixels a row; a row whose outline steps OUT by
    // more than `stepS` S on either side is something else joining it — down the line the upper back
    // behind the head, a hood's brim, the shoulders — and the search stops above it (Nick Henn, Mark,
    // Jess Ratcliffe, the 20 Oct / 7 Oct swing 2 address masks: the back of the head meets the upper back
    // in a step of 0.2–0.8 S in one row, the skull itself widens by under 0.1 S a row).
    const double stepPx = stepS * S;
    const int rowEnd = std::min(bh - 1, int(std::lround(earY)) - yTop);
    int bestRow = -1, bestL = 0, bestR = -1, prevL = -1, prevR = -1;
    bool stepped = false;
    for (int by = crown; by <= rowEnd; ++by) {
        int l = -1, r = -1;
        for (int bx = 0; bx < bw; ++bx) if (in[size_t(by) * bw + bx]) { if (l < 0) l = bx; r = bx; }
        if (l < 0) continue;
        // (not near the crown, where a round head itself widens fast: only once the outline is half a head wide)
        if (prevL >= 0 && prevR - prevL + 1 >= 0.5 * S && (prevL - l > stepPx || r - prevR > stepPx)) { stepped = true; break; }
        prevL = l; prevR = r;
        if (r - l > bestR - bestL) { bestRow = by; bestL = l; bestR = r; }
    }
    if (bestRow < 0) { v.headRefused = "no head outline between the crown and the ears"; return; }
    if ((bestL == 0 && x0 > 0) || (bestR == bw - 1 && x1 < W - 1)) {
        v.headRefused = "the head's outline runs into the shoulders or the background beside it";
        return;
    }
    if (bestL == 0 || bestR == bw - 1) { v.headRefused = "the head is at the frame's edge"; return; }
    const double width = double(bestR - bestL + 1);
    if (width > maxWidthS * S) { v.headRefused = "the outline is wider than a head (a hood or a collar merges)"; return; }
    if (width < minWidthS * S) {
        v.headRefused = stepped ? "the outline steps out before it is a head's width (a hood, a cap's brim or the shoulders join it)"
                                : "the outline is narrower than a head (the mask lost part of it)";
        return;
    }
    const double r = 0.5 * width;
    const double cx = x0 + 0.5 * (bestL + bestR);
    v.headFound  = true;
    v.headCx     = cx / W;
    v.headCy     = (crownPx + r) / H;
    v.headR      = r / H;
    v.crownY     = crownPx / H;
    v.headRowY   = double(yTop + bestRow) / H;
    v.headLeftX  = double(x0 + bestL) / W;
    v.headRightX = double(x0 + bestR) / W;
}

#endif // HAVE_OPENCV

} // namespace pinpoint::analysis::addressmarks
