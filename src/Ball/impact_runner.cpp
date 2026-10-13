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

#include "impact_runner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <variant>
#include <vector>

#include <QElapsedTimer>

#include "format_descriptor.h"
#include "swing_window.h"
#include "../Core/pp_debug.h"
#include "../Core/pp_profiler.h"

#if defined(HAVE_OPENCV)
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "../Export/frame_decode.h"
#endif

using pinpoint::analysis::ImpactSample2D;
using pinpoint::analysis::ImpactTrack2D;

#if defined(HAVE_OPENCV)
namespace {

constexpr int    kBackgroundFrames = 30;     // the clip opens ≥ 150 ms before impact (keep band)
constexpr double kBallDiameterMm   = 42.67;
constexpr double kShaftHalfWidthPx = 2.5;    // on-axis band the shaft occupies
constexpr double kHeadOffAxisPx    = 3.5;    // beyond this the pixel is head, not shaft
constexpr int    kPathBefore       = 30;     // frames before departure the path is fitted on
constexpr int    kPathAfter        = 1000;   // the whole contiguous run after departure — the tracker follows post-impact
constexpr int    kMinPathPoints    = 4;
// The head centre's offset from the hosel in the club's own frame when the
// frames show too little of it: along the shaft, and across toward the toe.
// A generic head (impactlab found +40 mm / +20 mm on the studio driver).
constexpr double kHeadPriorAlongMm  = 30.0;
constexpr double kHeadPriorAcrossMm = 35.0;

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}
double medianAbsDev(const std::vector<double> &v)
{
    if (v.empty()) return 0.0;
    const double m = median(v);
    std::vector<double> d;
    d.reserve(v.size());
    for (double x : v) d.push_back(std::abs(x - m));
    return median(d);
}
double evalQuadratic(const std::array<double, 3> &c, double t) { return (c[0] * t + c[1]) * t + c[2]; }

// Robust LOCAL fit at tq: tricube weights over ±half frames on the kept
// samples, quadratic when five or more carry weight (linear with fewer), the
// window widened at a thin end. A global quadratic cannot follow a velocity
// that changes at impact; this follows the run after it (impactlab, 2026-09-15).
double loess(const std::vector<double> &t, const std::vector<double> &y, const std::vector<char> &keep,
             double tq, double half = 6.0)
{
    const size_t n = t.size();
    std::vector<double> w(n, 0.0);
    int nz = 0;
    for (double h : { half, half * 1.5, half * 2.5, half * 4.0 }) {
        nz = 0;
        for (size_t k = 0; k < n; ++k) {
            const double a = std::abs(t[k] - tq) / h;
            w[k] = (keep[k] && a < 1.0) ? std::pow(1.0 - a * a * a, 3.0) : 0.0;
            nz += w[k] > 0;
        }
        if (nz >= 4) break;
    }
    const int deg = nz >= 5 ? 2 : (nz >= 3 ? 1 : 0);
    if (deg == 0) {   // nothing local at all: the kept mean
        double sy = 0; int c = 0;
        for (size_t k = 0; k < n; ++k) if (keep[k]) { sy += y[k]; ++c; }
        return c ? sy / c : 0.0;
    }
    const int m = deg + 1;
    cv::Mat A = cv::Mat::zeros(m, m, CV_64F), B = cv::Mat::zeros(m, 1, CV_64F);
    for (size_t k = 0; k < n; ++k) {
        if (w[k] <= 0) continue;
        const double tc = t[k] - tq;                    // centred: the value at tq is the constant term
        double basis[3] = { 1.0, tc, tc * tc };
        for (int r = 0; r < m; ++r) {
            B.at<double>(r) += w[k] * basis[r] * y[k];
            for (int q = 0; q < m; ++q) A.at<double>(r, q) += w[k] * basis[r] * basis[q];
        }
    }
    cv::Mat x;
    if (!cv::solve(A, B, x, cv::DECOMP_SVD)) return 0.0;
    return x.at<double>(0);
}

// Weighted least-squares polynomial (degree 1 or 2), iteratively reweighted
// (Huber, c = 3 MAD) over the samples flagged in `keep` — the de-jitter.
// Returns {a, b, c} for a·t² + b·t + c (a = 0 for a line).
std::array<double, 3> robustQuadratic(const std::vector<double> &t, const std::vector<double> &y,
                                      const std::vector<char> &keep, int deg = 2)
{
    const size_t n = t.size();
    const int m = deg + 1;
    std::vector<double> w(n, 1.0);
    std::array<double, 3> c{ 0, 0, 0 };
    for (int iter = 0; iter < 5; ++iter) {
        cv::Mat A = cv::Mat::zeros(m, m, CV_64F), B = cv::Mat::zeros(m, 1, CV_64F);
        for (size_t k = 0; k < n; ++k) {
            if (!keep[k]) continue;
            const double basis[3] = { 1.0, t[k], t[k] * t[k] };
            for (int r = 0; r < m; ++r) {
                B.at<double>(r) += w[k] * basis[r] * y[k];
                for (int q = 0; q < m; ++q) A.at<double>(r, q) += w[k] * basis[r] * basis[q];
            }
        }
        cv::Mat x;
        if (!cv::solve(A, B, x, cv::DECOMP_SVD)) break;
        c = { deg == 2 ? x.at<double>(2) : 0.0, x.at<double>(1), x.at<double>(0) };
        if (iter == 4) break;
        std::vector<double> res;
        for (size_t k = 0; k < n; ++k) if (keep[k]) res.push_back(y[k] - evalQuadratic(c, t[k]));
        const double s = 1.4826 * medianAbsDev(res) + 1e-6;
        for (size_t k = 0; k < n; ++k) {
            const double a = std::abs(y[k] - evalQuadratic(c, t[k])) / (3.0 * s);
            w[k] = a <= 1.0 ? 1.0 : 1.0 / a;
        }
    }
    return c;
}

struct Blob {
    int    label = 0;
    int    area  = 0;
    cv::Rect box;
    cv::Point2d centroid;
    double roundness = 0.0;   // area / (π (max(w,h)/2)²): 1 = a disc
    double aspect    = 1.0;   // w / h
};

// The bright round blob in a still frame — the resting ball. Returns false
// when there is none (the ball was never in the frame, or already gone).
bool findRestingBall(const cv::Mat &bg8, cv::Point2d &centre, double &radius)
{
    // The ball is the brightest LARGE thing on the mat: threshold at the
    // 99.5th percentile of the background (a 640×240 ball is ~0.5% of the
    // frame), never below 30 so a clip with nothing bright yields nothing.
    // Not the maximum — a lit edge at the frame border (2026-09-15 clips) is
    // brighter than the ball and would put the threshold above it.
    int thr = 30;
    {
        int hist[256] = {};
        for (int y = 0; y < bg8.rows; ++y) {
            const uchar *row = bg8.ptr<uchar>(y);
            for (int x = 0; x < bg8.cols; ++x) ++hist[row[x]];
        }
        const int target = int(0.995 * bg8.rows * bg8.cols);
        int acc = 0, p995 = 255;
        for (int v = 0; v < 256; ++v) { acc += hist[v]; if (acc >= target) { p995 = v; break; } }
        thr = std::max(30, int(p995 * 0.8));
    }
    cv::Mat m;
    cv::threshold(bg8, m, thr, 255, cv::THRESH_BINARY);
    cv::Mat lab, st, cen;
    const int n = cv::connectedComponentsWithStats(m, lab, st, cen, 8, CV_32S);
    bool found = false;
    int bestArea = 0;
    for (int i = 1; i < n; ++i) {
        const int a = st.at<int>(i, cv::CC_STAT_AREA);
        const int w = st.at<int>(i, cv::CC_STAT_WIDTH);
        const int h = st.at<int>(i, cv::CC_STAT_HEIGHT);
        if (a < 40 || w < 8 || h < 8) continue;
        // The lit part of a ball is its top cap: the blob's WIDTH is the
        // diameter, its height may be less (the underside in shadow, or a
        // tee's reflection pulling the box down — 2026-09-15 swing 4), and
        // its top edge is the best-lit edge, so the centre is one radius
        // below the top rather than at the blob's centroid.
        const double r = w / 2.0;
        const double round = a / (M_PI * r * r);
        const double aspect = double(w) / h;
        if (round < 0.5 || round > 1.3 || aspect < 0.7 || aspect > 1.8) continue;
        if (a > bestArea) {
            bestArea = a;
            centre = { cen.at<double>(i, 0), double(st.at<int>(i, cv::CC_STAT_TOP)) + r };
            radius = r;
            found = true;
        }
    }
    return found;
}

// Principal axis of a pixel set (unit vector, oriented downward: the club
// arrives from above, so +y along the axis points at the head end).
void principalAxis(const std::vector<cv::Point> &pts, cv::Point2d &mean, cv::Point2d &axis)
{
    double sx = 0, sy = 0;
    for (const auto &p : pts) { sx += p.x; sy += p.y; }
    mean = { sx / pts.size(), sy / pts.size() };
    double cxx = 0, cxy = 0, cyy = 0;
    for (const auto &p : pts) {
        const double dx = p.x - mean.x, dy = p.y - mean.y;
        cxx += dx * dx; cxy += dx * dy; cyy += dy * dy;
    }
    // Largest eigenvector of the 2×2 covariance.
    const double tr = cxx + cyy, det = cxx * cyy - cxy * cxy;
    const double l1 = tr / 2 + std::sqrt(std::max(0.0, tr * tr / 4 - det));
    cv::Point2d v = (std::abs(cxy) > 1e-9) ? cv::Point2d(l1 - cyy, cxy)
                  : (cxx >= cyy ? cv::Point2d(1, 0) : cv::Point2d(0, 1));
    const double len = std::hypot(v.x, v.y);
    v = (len > 1e-9) ? v * (1.0 / len) : cv::Point2d(0, 1);
    if (v.y < 0) v = -v;
    axis = v;
}

} // namespace

ImpactTrack2D ImpactRunner::run(const pinpoint::SwingWindow &window,
                                pinpoint::SourceId impactSource,
                                int64_t impactHintUs)
{
    ImpactTrack2D track;
    track.camera = impactSource;
    if (impactSource == pinpoint::kInvalidSourceId)
        return track;

    const auto entries = window.entriesFor(impactSource);
    if (entries.size() < size_t(kBackgroundFrames + 4)) {
        ppWarn() << "[ImpactRunner] only" << entries.size() << "frames for source" << impactSource
                 << "— no track";
        return track;
    }
    const pinpoint::FormatDescriptor &fd = window.formatOf(impactSource);
    const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
    if (!cfmt) {
        ppWarn() << "[ImpactRunner] source" << impactSource << "is not a camera — no track";
        return track;
    }
    if (!pinpoint::demosaicPlanFor(cfmt->pixel_format).supported) {
        ppWarn() << "[ImpactRunner] unsupported pixel format for source" << impactSource;
        return track;
    }

    QElapsedTimer wall;
    wall.start();
    PP_PROFILE_SCOPE("Analysis.ImpactRunner.run");

    const int W = int(cfmt->width), H = int(cfmt->height);
    track.width = W; track.height = H;

    // ── Decode every frame to 8-bit grey once (≈180 × 150 KB — trivial). A
    //    real demosaic, not the luma fast path: that wraps a Bayer mosaic as
    //    is, and its checkerboard halves every blob's area, which fails the
    //    resting ball's roundness test (seen on the first run, 2026-09-15).
    std::vector<cv::Mat> frames;
    frames.reserve(entries.size());
    std::vector<int64_t> tUs;
    tUs.reserve(entries.size());
    {
        cv::Mat bgr, grey;
        for (const auto &e : entries) {
            const pinpoint::SourceRing::ReadHandle handle = window.payloadOf(e);
            if (!pinpoint::decodeToBgr(*cfmt, handle.data, handle.bytes, bgr))
                continue;
            if (bgr.cols != W || bgr.rows != H)
                continue;
            cv::cvtColor(bgr, grey, cv::COLOR_BGR2GRAY);
            frames.push_back(grey.clone());    // bgr may alias the ring
            tUs.push_back(e.timestamp_us);
        }
    }
    if (frames.size() < size_t(kBackgroundFrames + 4)) {
        ppWarn() << "[ImpactRunner] only" << frames.size() << "decodable frames — no track";
        return track;
    }
    const int N = int(frames.size());

    // ── Background: per-pixel median and spread over the opening frames. ──
    cv::Mat bg(H, W, CV_32F), spread(H, W, CV_32F);
    {
        std::vector<uchar> col(kBackgroundFrames);
        for (int y = 0; y < H; ++y) {
            float *bgRow = bg.ptr<float>(y);
            float *spRow = spread.ptr<float>(y);
            for (int x = 0; x < W; ++x) {
                for (int k = 0; k < kBackgroundFrames; ++k) col[k] = frames[k].at<uchar>(y, x);
                std::nth_element(col.begin(), col.begin() + kBackgroundFrames / 2, col.end());
                const uchar med = col[kBackgroundFrames / 2];
                // Median absolute deviation — robust to the club entering late
                // in the background frames on a fast swing.
                for (int k = 0; k < kBackgroundFrames; ++k)
                    col[k] = uchar(std::abs(int(frames[k].at<uchar>(y, x)) - int(med)));
                std::nth_element(col.begin(), col.begin() + kBackgroundFrames / 2, col.end());
                bgRow[x] = float(med);
                spRow[x] = float(col[kBackgroundFrames / 2]);
            }
        }
    }
    cv::Mat bg8;
    bg.convertTo(bg8, CV_8U);
    double noise;
    {
        // Robust frame noise: the median spread over the frame.
        std::vector<float> v(spread.begin<float>(), spread.end<float>());
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        noise = std::max(1.0, double(v[v.size() / 2]));
    }
    // Two thresholds (hysteresis): STRONG seeds the foreground, WEAK grows it
    // — the shaft and the ball are strong, the head's glints are weak and
    // would be lost to a single threshold. The head BODY is at mat level and
    // in neither (measured 2026-09-15): that needs light, not a threshold.
    const double thr     = std::max(10.0, 6.0 * noise);
    const double thrWeak = std::max(4.0,  2.5 * noise);
    // Never-foreground mask: anything that flickers over the opening frames,
    // plus anything WEAKLY foreground on more than a third of ALL frames — a
    // lit edge at the frame border whose brightness drifts over the clip. The
    // club and the ball pass any pixel on a handful of frames.
    cv::Mat never;
    cv::compare(spread, float(std::max(thr, 4.0 * noise)), never, cv::CMP_GT);
    {
        cv::Mat count = cv::Mat::zeros(H, W, CV_32F), f32, above;
        for (int i = 0; i < N; ++i) {
            frames[i].convertTo(f32, CV_32F);
            cv::compare(f32 - bg, float(thrWeak), above, cv::CMP_GT);
            cv::add(count, 1.0f, count, above);
        }
        cv::Mat persist;
        cv::compare(count, float(0.33 * N), persist, cv::CMP_GT);
        cv::dilate(persist, persist, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));
        cv::bitwise_or(never, persist, never);
    }
    cv::dilate(never, never, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));

    // ── The resting ball, and the frame scale. ──
    cv::Point2d restC;
    double restR = 0.0;
    const bool haveRest = findRestingBall(bg8, restC, restR);
    if (haveRest) {
        track.ballRest  = QPointF(restC.x / W, restC.y / H);
        track.ballRestR = float(restR / W);
        track.mmPerPx   = kBallDiameterMm / (2.0 * restR);
    }

    // ── Per frame: foreground → ball first, then the club. ──
    const cv::Mat kOpen  = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    const cv::Mat kClose = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));
    cv::Mat f32, diff, fw, fs, region, lab0, st0, cen0, lab, st, cen;
    bool ballGone = false;
    int departIdx = -1;
    cv::Point2d lastBall = restC, lastVel(0, 0);
    bool haveLastBall = haveRest;
    int coast = 0;
    cv::Point2d lastHead;
    bool haveLastHead = false;
    int headMiss = 0;
    struct HeadPt { int i; double hx, hy, sx, sy; bool seen, edge; };
    std::vector<HeadPt> heads;

    track.frames.reserve(N);
    for (int i = 0; i < N; ++i) {
        ImpactSample2D s;
        s.t_us = tUs[i];
        frames[i].convertTo(f32, CV_32F);
        diff = f32 - bg;

        // Departure: the resting ball's spot has gone dark.
        if (haveRest && !ballGone) {
            const int bx = int(std::lround(restC.x)), by = int(std::lround(restC.y));
            if (bx >= 0 && bx < W && by >= 0 && by < H && diff.at<float>(by, bx) < -float(thr)) {
                ballGone = true;
                departIdx = i;
                track.ballLeaveTUs = tUs[i];
            }
        }
        if (haveRest && !ballGone) {
            s.ballFound = true;
            s.ball  = QPointF(restC.x / W, restC.y / H);
            s.ballR = float(restR / W);
        }

        cv::compare(diff, float(thrWeak), fw, cv::CMP_GT);
        cv::compare(diff, float(thr),     fs, cv::CMP_GT);
        fw.setTo(0, never);
        fs.setTo(0, never);
        if (haveRest && ballGone) {   // the empty spot goes dark and its rim flickers
            cv::circle(fw, cv::Point(int(restC.x), int(restC.y)), int(restR * 1.3), 0, -1);
            cv::circle(fs, cv::Point(int(restC.x), int(restC.y)), int(restR * 1.3), 0, -1);
        }
        // Seeds cleaned of speckle; the weak components those seeds sit in
        // (grown, never opened — a 3 px shaft does not survive an opening);
        // then closed so the club's pieces become ONE component.
        cv::morphologyEx(fs, fs, cv::MORPH_OPEN, kOpen);
        const int n0 = cv::connectedComponentsWithStats(fw, lab0, st0, cen0, 8, CV_32S);
        std::vector<int> seeded(size_t(n0), 0);
        for (int y = 0; y < H; ++y) {
            const int   *lr = lab0.ptr<int>(y);
            const uchar *sr = fs.ptr<uchar>(y);
            for (int x = 0; x < W; ++x) if (sr[x]) ++seeded[size_t(lr[x])];
        }
        region = cv::Mat::zeros(H, W, CV_8U);
        for (int y = 0; y < H; ++y) {
            const int *lr = lab0.ptr<int>(y);
            uchar *rr = region.ptr<uchar>(y);
            for (int x = 0; x < W; ++x) if (lr[x] > 0 && seeded[size_t(lr[x])] >= 6) rr[x] = 255;
        }
        cv::morphologyEx(region, region, cv::MORPH_CLOSE, kClose);
        const int n = cv::connectedComponentsWithStats(region, lab, st, cen, 8, CV_32S);
        std::vector<Blob> blobs;
        for (int j = 1; j < n; ++j) {
            Blob b;
            b.label = j;
            b.area  = st.at<int>(j, cv::CC_STAT_AREA);
            if (b.area < 20) continue;
            b.box = cv::Rect(st.at<int>(j, cv::CC_STAT_LEFT), st.at<int>(j, cv::CC_STAT_TOP),
                             st.at<int>(j, cv::CC_STAT_WIDTH), st.at<int>(j, cv::CC_STAT_HEIGHT));
            int strongPx = 0;
            for (int y = b.box.y; y < b.box.y + b.box.height; ++y) {
                const int *lr = lab.ptr<int>(y);
                const uchar *sr = fs.ptr<uchar>(y);
                for (int x = b.box.x; x < b.box.x + b.box.width; ++x) if (lr[x] == j && sr[x]) ++strongPx;
            }
            if (strongPx < 6) continue;
            b.centroid = { cen.at<double>(j, 0), cen.at<double>(j, 1) };
            const double r = std::max(b.box.width, b.box.height) / 2.0;
            b.roundness = b.area / (M_PI * r * r);
            b.aspect    = double(b.box.width) / b.box.height;
            blobs.push_back(b);
        }
        auto pixelsOf = [&](int label, const cv::Rect &box, std::vector<cv::Point> &out) {
            out.clear();
            for (int y = box.y; y < box.y + box.height; ++y) {
                const int *lr = lab.ptr<int>(y);
                for (int x = box.x; x < box.x + box.width; ++x) if (lr[x] == label) out.emplace_back(x, y);
            }
        };

        // The ball after departure: the round, ball-sized blob nearest where
        // it is heading; when none is round (it touches the club at impact)
        // the predicted disc mostly foreground is the ball.
        int ballLabel = 0;
        if (haveRest && ballGone) {
            const bool havePred = haveLastBall;
            const cv::Point2d pred = lastBall + lastVel;
            const double gate = 4.0 * restR + 40.0 + 30.0 * coast;
            double bestD = 1e18;
            const Blob *best = nullptr;
            for (const Blob &b : blobs) {
                const double r = std::max(b.box.width, b.box.height) / 2.0;
                if (b.roundness < 0.5 || b.roundness > 1.3) continue;
                if (b.aspect < 0.65 || b.aspect > 1.5) continue;
                if (r < 0.6 * restR || r > 1.6 * restR) continue;
                const double d = havePred ? cv::norm(b.centroid - pred) : 0.0;
                if (havePred && d > gate) continue;
                if (d < bestD) { bestD = d; best = &b; }
            }
            if (best) {
                ballLabel = best->label;
                const double r = std::max(best->box.width, best->box.height) / 2.0;
                s.ballFound = true;
                s.ball  = QPointF(best->centroid.x / W, best->centroid.y / H);
                s.ballR = float(r / W);
                if (haveLastBall) lastVel = best->centroid - lastBall;
                lastBall = best->centroid; haveLastBall = true; coast = 0;
            } else if (havePred) {
                cv::Mat disc = cv::Mat::zeros(H, W, CV_8U);
                cv::circle(disc, cv::Point(int(pred.x), int(pred.y)), int(restR * 1.25), 255, -1);
                cv::Mat inside;
                cv::bitwise_and(region, disc, inside);
                const int insidePx = cv::countNonZero(inside);
                if (insidePx > 0.45 * M_PI * restR * restR) {
                    const cv::Moments m = cv::moments(inside, true);
                    const cv::Point2d c(m.m10 / m.m00, m.m01 / m.m00);
                    s.ballFound = true;
                    s.ball  = QPointF(c.x / W, c.y / H);
                    s.ballR = float(restR / W);
                    lastVel = c - lastBall; lastBall = c; coast = 0;
                } else if (++coast > 8) {
                    haveLastBall = false;
                }
            }
        }
        const bool haveBallHere = s.ballFound;
        const cv::Point2d ballC(s.ball.x() * W, s.ball.y() * H);
        const double ballR = s.ballR * W;

        // The club: not the ball, big enough, not lying near-horizontal (the
        // grip is out of the top of the strip, so a club's axis comes from
        // above — a lit border edge lies flat), continuous with the last one.
        const Blob *club = nullptr;
        std::vector<cv::Point> pts;
        {
            double bestScore = -1;
            for (const Blob &b : blobs) {
                if (b.label == ballLabel || b.area < 40) continue;
                if (std::max(b.box.width, b.box.height) < 25) continue;
                pixelsOf(b.label, b.box, pts);
                if (pts.size() >= 5) {
                    cv::Point2d m, ax;
                    principalAxis(pts, m, ax);
                    if (std::abs(ax.y) < 0.5) continue;
                }
                double score = b.area;
                if (haveLastHead) {
                    const double d = cv::norm(b.centroid - lastHead);
                    if (d > 160.0 + 40.0 * headMiss) continue;
                    score = b.area * (1.0 + 200.0 / (d + 20.0));
                }
                if (score > bestScore) { bestScore = score; club = &b; }
            }
        }
        if (club) {
            pixelsOf(club->label, club->box, pts);
            // The ball is never part of the club: drop its disc (they touch
            // at impact, and the merged blob's centroid is the ball's).
            if (haveBallHere) {
                std::vector<cv::Point> kept;
                kept.reserve(pts.size());
                const double rr = ballR * 1.15 + 2.0;
                for (const auto &p : pts)
                    if (cv::norm(cv::Point2d(p.x, p.y) - ballC) > rr) kept.push_back(p);
                pts.swap(kept);
            }
            if (pts.size() < 20) club = nullptr;
        }
        if (club) {
            cv::Point2d mean, axis;
            principalAxis(pts, mean, axis);
            const cv::Point2d perp(-axis.y, axis.x);
            double pMin = 1e9, pMax = -1e9;
            std::vector<cv::Point> headPts;
            for (const auto &p : pts) {
                const cv::Point2d d(p.x - mean.x, p.y - mean.y);
                const double along = d.dot(axis), off = std::abs(d.dot(perp));
                if (off <= kShaftHalfWidthPx) { pMin = std::min(pMin, along); pMax = std::max(pMax, along); }
                if (off > kHeadOffAxisPx) headPts.push_back(p);
            }
            if (pMin > pMax) { pMin = 0; pMax = 0; }
            const cv::Point2d a = mean + axis * pMin, hosel = mean + axis * pMax;
            // What the light shows of the head: the club's off-shaft pixels plus
            // any small blob (a sole / crown glint) in a head-sized window
            // beyond the hosel — 0..70 px along the shaft, ±45 px across.
            std::vector<cv::Point> satPts;
            for (const Blob &b : blobs) {
                if (b.label == club->label || b.label == ballLabel) continue;
                if (haveBallHere && cv::norm(b.centroid - ballC) < ballR * 1.3) continue;
                const cv::Point2d v = b.centroid - hosel;
                const double alongV = v.dot(axis), across = std::abs(v.dot(perp));
                if (alongV >= -5.0 && alongV <= 70.0 && across <= 45.0) {
                    pixelsOf(b.label, b.box, satPts);
                    headPts.insert(headPts.end(), satPts.begin(), satPts.end());
                    pts.insert(pts.end(), satPts.begin(), satPts.end());
                }
            }
            s.clubFound = true;
            s.shaftA = QPointF(a.x / W, a.y / H);
            s.shaftB = QPointF(hosel.x / W, hosel.y / H);
            cv::Point2d head = hosel;
            if (headPts.size() >= 10) {
                double hx = 0, hy = 0;
                for (const auto &p : headPts) { hx += p.x; hy += p.y; }
                head = { hx / headPts.size(), hy / headPts.size() };
                s.headSeen = true;
            }
            s.head = QPointF(head.x / W, head.y / H);
            s.atEdge = club->box.x <= 0
                    || club->box.x + club->box.width >= W || club->box.y + club->box.height >= H;
            std::vector<cv::Point> hull;
            cv::convexHull(pts, hull);
            s.outline.reserve(hull.size());
            for (const auto &p : hull) s.outline.emplace_back(double(p.x) / W, double(p.y) / H);
            lastHead = head; haveLastHead = true; headMiss = 0;
            heads.push_back({ i, head.x, head.y, hosel.x, hosel.y, s.headSeen, s.atEdge });
        } else if (haveLastHead && ++headMiss > 3) {
            haveLastHead = false;
        }
        track.frames.push_back(s);
    }

    // ── Keep one contiguous club track around departure (gaps ≤ 2 frames):
    //    a lone detection elsewhere in the clip is noise, whatever it was. ──
    const int centre = departIdx >= 0 ? departIdx
                     : (impactHintUs > 0
                        ? int(std::min_element(tUs.begin(), tUs.end(), [&](int64_t p, int64_t q) {
                              return std::llabs(p - impactHintUs) < std::llabs(q - impactHintUs); }) - tUs.begin())
                        : N / 2);
    {
        std::vector<char> hasClub(size_t(N), 0);
        for (const HeadPt &h : heads) hasClub[size_t(h.i)] = 1;
        int startIdx = -1, bestD = 1 << 30;
        for (int i = 0; i < N; ++i)
            if (hasClub[size_t(i)] && std::abs(i - centre) < bestD) { bestD = std::abs(i - centre); startIdx = i; }
        std::vector<char> inRun(size_t(N), 0);
        if (startIdx >= 0) {
            inRun[size_t(startIdx)] = 1;
            for (int step : { 1, -1 }) {
                int k = startIdx, gap = 0;
                while (true) {
                    k += step;
                    if (k < 0 || k >= N) break;
                    if (hasClub[size_t(k)]) { inRun[size_t(k)] = 1; gap = 0; }
                    else if (++gap > 2) break;
                }
            }
        }
        for (int i = 0; i < N; ++i) {
            if (track.frames[size_t(i)].clubFound && !inRun[size_t(i)]) {
                ImpactSample2D &s = track.frames[size_t(i)];
                s.clubFound = false; s.headSeen = false; s.atEdge = false; s.outline.clear();
            }
        }
        heads.erase(std::remove_if(heads.begin(), heads.end(),
                                   [&](const HeadPt &h) { return !inRun[size_t(h.i)]; }), heads.end());
    }

    // ── The synthesised path (impact_camera_design.md §7; proven in
    //    tools/impactlab first). Robust quadratics in time for the hosel and
    //    the shaft angle over the run, broken hosels dropped; the head as a
    //    rigid offset from the hosel in the club's frame, self-calibrated by
    //    a feedback loop that gathers weak foreground inside the predicted
    //    head disc on every frame; then one head per run frame. ──
    {
        struct Row { int i; double t, hx, hy, th; bool edge; };
        std::vector<Row> rows;
        for (const HeadPt &h : heads) {
            if (h.i < centre - kPathBefore || h.i > centre + kPathAfter) continue;
            const ImpactSample2D &sm = track.frames[size_t(h.i)];
            const double ax = (sm.shaftB.x() - sm.shaftA.x()) * W, ay = (sm.shaftB.y() - sm.shaftA.y()) * H;
            const double period = N > 1 ? double(tUs[N - 1] - tUs[0]) / (N - 1) : 1.0;
            rows.push_back({ h.i, double(tUs[size_t(h.i)] - tUs[size_t(centre)]) / period,
                             h.sx, h.sy, std::atan2(ay, ax), h.edge });
        }
        std::vector<const Row *> fit;
        for (const Row &r : rows) if (!r.edge) fit.push_back(&r);
        if (int(fit.size()) >= kMinPathPoints) {
            std::vector<double> t, hx, hy, th;
            for (const Row *r : fit) { t.push_back(r->t); hx.push_back(r->hx); hy.push_back(r->hy); th.push_back(r->th); }
            std::vector<char> keep(fit.size(), 1);
            std::array<double, 3> cx{}, cy{}, cth{};
            for (int round = 0; round < 2; ++round) {
                cx = robustQuadratic(t, hx, keep); cy = robustQuadratic(t, hy, keep);
                std::vector<double> res(fit.size()), kept;
                for (size_t k = 0; k < fit.size(); ++k) {
                    res[k] = std::hypot(hx[k] - evalQuadratic(cx, t[k]), hy[k] - evalQuadratic(cy, t[k]));
                    if (keep[k]) kept.push_back(res[k]);
                }
                const double mad = 1.4826 * medianAbsDev(kept) + 1e-6;
                std::vector<char> nk(fit.size());
                int n = 0;
                for (size_t k = 0; k < fit.size(); ++k) { nk[k] = res[k] <= std::max(8.0, 3.0 * mad); n += nk[k]; }
                if (n < kMinPathPoints || nk == keep) break;
                keep = nk;
            }
            // The path is an ARC in space, whatever the timing does: the head
            // slows at impact but the shape it sweeps stays one smooth curve.
            // The hosel's y as a robust quadratic in x over the whole run, the
            // shaft angle as a robust line in x, and TIME only says where along
            // the arc the club is on each frame (x(t), a local fit). One more
            // outlier pass against the arc, then the residual it leaves.
            std::array<double, 3> arcY = robustQuadratic(hx, hy, keep, 2), arcTh = robustQuadratic(hx, th, keep, 1);
            {
                std::vector<double> res(fit.size()), kept;
                for (size_t k = 0; k < fit.size(); ++k) {
                    res[k] = std::abs(hy[k] - evalQuadratic(arcY, hx[k]));
                    if (keep[k]) kept.push_back(res[k]);
                }
                const double mad = 1.4826 * medianAbsDev(kept) + 1e-6;
                std::vector<char> nk(fit.size());
                int n = 0;
                for (size_t k = 0; k < fit.size(); ++k) { nk[k] = res[k] <= std::max(6.0, 3.0 * mad); n += nk[k]; }
                if (n >= kMinPathPoints) { keep = nk; arcY = robustQuadratic(hx, hy, keep, 2); arcTh = robustQuadratic(hx, th, keep, 1); }
            }
            auto smoothX  = [&](double tq) { return loess(t, hx, keep, tq); };
            auto smoothY  = [&](double tq) { return evalQuadratic(arcY, smoothX(tq)); };
            auto smoothTh = [&](double tq) { return evalQuadratic(arcTh, smoothX(tq)); };
            double ss = 0; int nk = 0;
            for (size_t k = 0; k < fit.size(); ++k) if (keep[k]) {
                const double r = hy[k] - evalQuadratic(arcY, hx[k]);
                ss += r * r; ++nk;
            }
            track.arcA = arcY[0] * double(W) * W / H; track.arcB = arcY[1] * double(W) / H; track.arcC = arcY[2] / H;
            track.thetaSlopePerX = arcTh[1] * W; track.thetaAtX0 = arcTh[2];
            track.hoselFitFrames     = nk;
            track.hoselDropped       = int(fit.size()) - nk;
            track.hoselResidualRmsPx = nk ? std::sqrt(ss / nk) : 0.0;

            // The head offset: a generic prior in mm (the capture's club label
            // is unreliable), then the feedback loop on the weak foreground.
            const double mmpx  = track.mmPerPx > 0 ? track.mmPerPx : 1.1;
            const double ballPx = kBallDiameterMm / mmpx;
            const double halfW  = 0.7 * ballPx;
            double dAlong = kHeadPriorAlongMm / mmpx, dAcross = kHeadPriorAcrossMm / mmpx;
            bool assumed = true; int evidence = 0;
            cv::Mat disc, cand, f32w, diffw;
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<double> ea, ec;
                for (const Row &r : rows) {
                    if (r.edge) continue;
                    const double px = smoothX(r.t), py = smoothY(r.t), ang = smoothTh(r.t);
                    const cv::Point2d u(std::cos(ang), std::sin(ang)), v(-u.y, u.x);
                    const cv::Point2d pred = cv::Point2d(px, py) + u * dAlong + v * dAcross;
                    disc = cv::Mat::zeros(H, W, CV_8U);
                    cv::circle(disc, cv::Point(int(pred.x), int(pred.y)), int(halfW), 255, -1);
                    const ImpactSample2D &sm = track.frames[size_t(r.i)];
                    if (sm.ballFound)
                        cv::circle(disc, cv::Point(int(sm.ball.x() * W), int(sm.ball.y() * H)),
                                   int(sm.ballR * W * 1.15) + 2, 0, -1);
                    cv::circle(disc, cv::Point(int(px), int(py)), 12, 0, -1);   // the neck is the hosel
                    frames[size_t(r.i)].convertTo(f32w, CV_32F);
                    diffw = f32w - bg;
                    cv::compare(diffw, float(thrWeak), cand, cv::CMP_GT);
                    cand.setTo(0, never);
                    cv::bitwise_and(cand, disc, cand);
                    const int n = cv::countNonZero(cand);
                    if (n < 15) continue;
                    const cv::Moments m = cv::moments(cand, true);
                    const cv::Point2d e(m.m10 / m.m00 - px, m.m01 / m.m00 - py);
                    ea.push_back(e.dot(u)); ec.push_back(e.dot(v));
                }
                if (int(ea.size()) >= 3) {
                    dAlong = median(ea); dAcross = median(ec);
                    assumed = false; evidence = int(ea.size());
                }
            }
            track.headAlongPx = dAlong; track.headAcrossPx = dAcross;
            track.headOffsetAssumed = assumed; track.headEvidenceFrames = evidence;
            track.pathHalfWidth = halfW / W;
            for (const Row &r : rows) {
                const double px = smoothX(r.t), py = smoothY(r.t), ang = smoothTh(r.t);
                const cv::Point2d u(std::cos(ang), std::sin(ang)), v(-u.y, u.x);
                const cv::Point2d head = cv::Point2d(px, py) + u * dAlong + v * dAcross;
                pinpoint::analysis::ImpactPathPoint p;
                p.t_us = tUs[size_t(r.i)];
                p.head = QPointF(head.x / W, head.y / H);
                p.hosel = QPointF(px / W, py / H);
                p.thetaRad = ang;
                track.path.push_back(p);
            }
            // The ribbon: the head along the arc, sampled densely in x over the
            // run's span — a curve by construction, not a chain of frames.
            if (!track.path.empty()) {
                double x0 = 1e9, x1 = -1e9;
                for (const Row &r : rows) { const double x = smoothX(r.t); x0 = std::min(x0, x); x1 = std::max(x1, x); }
                for (int k = 0; k < 64; ++k) {
                    const double x = x0 + (x1 - x0) * k / 63.0;
                    const double y = evalQuadratic(arcY, x), ang = evalQuadratic(arcTh, x);
                    const cv::Point2d u(std::cos(ang), std::sin(ang)), v(-u.y, u.x);
                    const cv::Point2d head = cv::Point2d(x, y) + u * dAlong + v * dAcross;
                    track.ribbon.emplace_back(head.x / W, head.y / H);
                }
            }
            track.pathValid = !track.path.empty();
        }
    }

    track.valid = !track.frames.empty();
    int clubFrames = 0, ballFrames = 0;
    for (const auto &s : track.frames) { clubFrames += s.clubFound; ballFrames += s.ballFound; }
    ppInfo() << "[ImpactRunner] source" << impactSource << ":" << N << "frames," << clubFrames
             << "with a club," << ballFrames << "with the ball; resting ball"
             << (haveRest ? QString("r=%1px (%2 mm/px)").arg(restR, 0, 'f', 1).arg(track.mmPerPx, 0, 'f', 2)
                          : QStringLiteral("none"))
             << "; departure frame" << departIdx << "; path"
             << (track.pathValid ? QString("%1 frames, hosel rms %2 px (%3 dropped), head offset %4/%5 px%6 from %7 frames")
                                       .arg(track.path.size()).arg(track.hoselResidualRmsPx, 0, 'f', 1)
                                       .arg(track.hoselDropped).arg(track.headAlongPx, 0, 'f', 0)
                                       .arg(track.headAcrossPx, 0, 'f', 0)
                                       .arg(track.headOffsetAssumed ? QStringLiteral(" (assumed)") : QString())
                                       .arg(track.headEvidenceFrames)
                                 : QStringLiteral("none"))
             << "; thr" << thr << "in" << wall.elapsed() << "ms";
    return track;
}

#else   // !HAVE_OPENCV

ImpactTrack2D ImpactRunner::run(const pinpoint::SwingWindow &window,
                                pinpoint::SourceId impactSource,
                                int64_t impactHintUs)
{
    Q_UNUSED(window)
    Q_UNUSED(impactHintUs)
    ppWarn() << "[ImpactRunner] built without OpenCV — no track";
    ImpactTrack2D track;
    track.camera = impactSource;
    return track;
}

#endif
