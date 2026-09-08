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

// Shaft v3.0-r1 evidence engines — faithful C++ port of tools/shaftlab/
// {stripe_fusion,stripe_annotate}.py (E2 ridge_sweep + E1 frame_band_match).
// Numerically identical to the Python within float precision; the sampler is
// nearest-neighbour integer-clamp (matching np _sample), NOT bilinear.

#include "shaft_tracker_math.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace pinpoint::analysis {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Nearest-neighbour clamped sample of `img` (CV_32F) at (fx,fy). Mirrors numpy
// `img[clip(int32(y),0,H-1), clip(int32(x),0,W-1)]` — int32 cast truncates
// toward zero, then clamp. (Coords fold negatives to 0 via the clamp, so the
// truncation direction on negatives is immaterial.)
inline float at(const cv::Mat& img, double fx, double fy)
{
    int x = int(fx);   // truncates toward zero, as numpy .astype(np.int32)
    int y = int(fy);
    if (x < 0) x = 0; else if (x >= img.cols) x = img.cols - 1;
    if (y < 0) y = 0; else if (y >= img.rows) y = img.rows - 1;
    return img.at<float>(y, x);
}

// median of exactly 4 (the bg reduction). numpy median of an even count = mean
// of the two middle order statistics.
inline float median4(float a, float b, float c, float d)
{
    if (a > b) std::swap(a, b);
    if (c > d) std::swap(c, d);
    if (a > c) std::swap(a, c);   // a = global min
    if (b > d) std::swap(b, d);   // d = global max
    return 0.5f * (b + c);        // b, c are the two middle values
}

// Lateral background at (cx,cy) across the ray direction (ux,uy): median of 4
// samples at ±9/±12 along the normal (nx,ny) = (−uy, ux) — sample at
// (cx − o·uy, cy + o·ux).
inline double lateralBg(const cv::Mat& img, double cx, double cy, double ux, double uy)
{
    const float b0 = at(img, cx - (-12.0) * uy, cy + (-12.0) * ux);
    const float b1 = at(img, cx - (-9.0)  * uy, cy + (-9.0)  * ux);
    const float b2 = at(img, cx - ( 9.0)  * uy, cy + ( 9.0)  * ux);
    const float b3 = at(img, cx - ( 12.0) * uy, cy + ( 12.0) * ux);
    return median4(b0, b1, b2, b3);
}

// E2's polarity-aware per-sample reduction, SHARED by ridgeSweep (the default
// path) and rayProfile (E4). Credits the lateral max (bright line) or min (dark
// shaft over a blown background) per the bgHi split; −12 is the max/min-of-5
// noise-bias correction so flat background cannot accrue score.
struct RaySample { double e, on, bg; bool bright; };
inline RaySample sampleRay(const cv::Mat& img, double cx, double cy, double ux, double uy,
                           const RidgeConfig& cfg)
{
    const double bg = lateralBg(img, cx, cy, ux, uy);
    double omax = -1e30, omin = 1e30;
    for (double o : {-2.0, -1.0, 0.0, 1.0, 2.0}) {
        const double v = at(img, cx - o * uy, cy + o * ux);
        omax = std::max(omax, v);
        omin = std::min(omin, v);
    }
    RaySample s;
    s.bg = bg;
    if (bg > cfg.bgHi) {
        s.bright = false; s.on = omin;
        s.e = std::clamp(bg - omin - 12.0, double(-cfg.eClipNeg), double(cfg.eClipPos));
    } else {
        s.bright = true; s.on = omax;
        s.e = std::clamp(omax - bg - 12.0, double(-cfg.eClipNeg), double(cfg.eClipPos));
    }
    return s;
}

} // namespace

RidgeResult ridgeSweep(const cv::Mat& img, double gx, double gy,
                       const std::vector<float>& thetasRad,
                       const RidgeConfig& cfg, bool brightOnly)
{
    CV_Assert(img.type() == CV_32F);
    const int W = img.cols, H = img.rows;

    std::vector<double> R;                     // R = arange(rLo, rHi, rStep)
    for (double r = cfg.rLo; r < cfg.rHi; r += cfg.rStep) R.push_back(r);
    const int nR = int(R.size());
    const int j0 = int(cfg.minLenPx / cfg.rStep);   // int(90/2) = 45

    std::vector<double> invNorm(nR);           // 1/sqrt(j+8)
    for (int j = 0; j < nR; ++j) invNorm[j] = 1.0 / std::sqrt(double(j) + 8.0);

    const int NS = int(thetasRad.size());
    RidgeResult out;
    out.score.assign(NS, 0.f);
    out.rEnd.assign(NS, 0.f);
    out.support.assign(NS, 0.f);
    if (nR <= j0) return out;

    for (int i = 0; i < NS; ++i) {
        const double ux = std::cos(double(thetasRad[i]));
        const double uy = std::sin(double(thetasRad[i]));

        double cum = 0.0;
        int    posCount = 0;
        double bestNorm = -std::numeric_limits<double>::infinity();
        int    bestJ = j0, bestPos = 0;

        for (int j = 0; j < nR; ++j) {
            const double cx = gx + ux * R[j];
            const double cy = gy + uy * R[j];
            const bool inb = (cx >= 0.0 && cx < W && cy >= 0.0 && cy < H);

            double e = 0.0;
            if (inb) {
                if (brightOnly) {
                    const double bg = lateralBg(img, cx, cy, ux, uy);
                    const double on = (double(at(img, cx - (-1.0) * uy, cy + (-1.0) * ux))
                                     + double(at(img, cx,               cy))
                                     + double(at(img, cx - ( 1.0) * uy, cy + ( 1.0) * ux))) / 3.0;
                    e = std::clamp(on - bg, double(-cfg.eClipNeg), double(cfg.eClipPos));
                } else {
                    e = sampleRay(img, cx, cy, ux, uy, cfg).e;
                }
            }

            cum += e;
            if (e > 8.0) ++posCount;
            if (j >= j0) {
                const double normJ = cum * invNorm[j];
                if (normJ > bestNorm) { bestNorm = normJ; bestJ = j; bestPos = posCount; }
            }
        }
        out.score[i]   = float(bestNorm);
        out.rEnd[i]    = float(R[bestJ]);
        out.support[i] = float(double(bestPos) / (double(bestJ) + 1.0));
    }
    return out;
}

// ── E1: discrete retro-band match ────────────────────────────────────────────
namespace {

struct Blob { double x, y; double area; };

// stripe_annotate.detect_blobs
std::vector<Blob> detectBlobs(const cv::Mat& gray, double gx, double gy, double rmax,
                              const BandMatchConfig& cfg)
{
    cv::Mat bw;
    cv::threshold(gray, bw, cfg.satT, 255, cv::THRESH_BINARY);
    cv::Mat labels, stats, cent;
    const int n = cv::connectedComponentsWithStats(bw, labels, stats, cent, 8);
    std::vector<Blob> out;
    for (int i = 1; i < n; ++i) {
        const double a = stats.at<int>(i, cv::CC_STAT_AREA);
        if (a < cfg.areaMin || a > cfg.areaMax) continue;
        const double cx = cent.at<double>(i, 0), cy = cent.at<double>(i, 1);
        if (std::hypot(cx - gx, cy - gy) > rmax) continue;
        out.push_back({cx, cy, a});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Blob& p, const Blob& q) { return p.area > q.area; });
    if (int(out.size()) > cfg.maxBlobs) out.resize(cfg.maxBlobs);
    return out;
}

// stripe_annotate.gap_dark_ok → NOPAIR(0) / BRIGHT(1) / DARK(2)
int gapDark(const cv::Mat& gray,
            const std::vector<std::array<double, 3>>& mptsR /*(x,y,r_mm) sorted by r*/,
            const BandMatchConfig& cfg)
{
    const int W = gray.cols, H = gray.rows;
    bool foundPair = false;
    for (size_t k = 0; k + 1 < mptsR.size(); ++k) {
        const double x1 = mptsR[k][0], y1 = mptsR[k][1], r1 = mptsR[k][2];
        const double x2 = mptsR[k + 1][0], y2 = mptsR[k + 1][1], r2 = mptsR[k + 1][2];
        if (r2 - r1 > cfg.gapMmMax) continue;
        foundPair = true;
        double mx = -1e30;
        bool any = false;
        for (double t : {0.4, 0.5, 0.6}) {
            const int xi = int(x1 + t * (x2 - x1));
            const int yi = int(y1 + t * (y2 - y1));
            if (xi >= 0 && xi < W && yi >= 0 && yi < H) {
                any = true;
                mx = std::max(mx, double(gray.at<uchar>(yi, xi)));
            }
        }
        if (any && mx <= cfg.gapDark) return 2;   // dark
    }
    return foundPair ? 1 : 0;                       // bright / nopair
}

// Least-squares fit t = slope·r + intercept over paired (r, t) — numpy lstsq on
// A = [[r,1]].
void lstsqLine(const std::vector<double>& r, const std::vector<double>& t,
               double& slope, double& intercept)
{
    const double m = double(r.size());
    double sr = 0, st = 0, srr = 0, srt = 0;
    for (size_t i = 0; i < r.size(); ++i) { sr += r[i]; st += t[i]; srr += r[i] * r[i]; srt += r[i] * t[i]; }
    const double det = m * srr - sr * sr;
    if (std::abs(det) < 1e-12) { slope = 0; intercept = st / m; return; }
    slope     = (m * srt - sr * st) / det;
    intercept = (srr * st - sr * srt) / det;
}

struct MatchResult {
    int n = 0; double rms = 0, s = 0, r0 = 0;
    std::vector<std::pair<int, int>> pairs;   // (blob index into tproj, band index)
};

// stripe_annotate.match_pattern (flip_rms omitted — its result is unused by
// frame_band_match). Best by (nPairs, -rms) lexicographic.
MatchResult matchPattern(const std::vector<double>& tproj, const std::vector<double>& bands,
                         const BandMatchConfig& cfg)
{
    const int nb = int(tproj.size());
    const int nBands = int(bands.size());
    bool haveBest = false;
    std::pair<int, double> bestKey{0, 0.0};
    MatchResult best;

    for (int a0 = 0; a0 < nb; ++a0)
        for (int b0 = a0 + 1; b0 < nb; ++b0) {
            double ta = tproj[a0], tb = tproj[b0];
            if (tb <= ta) std::swap(ta, tb);
            for (int i = 0; i < nBands; ++i)
                for (int l = i + 1; l < nBands; ++l) {
                    const double s = (tb - ta) / (bands[l] - bands[i]);
                    if (s < cfg.sMin || s > cfg.sMax) continue;
                    const double r0 = bands[i] - ta / s;
                    if (r0 < cfg.r0Min || r0 > cfg.r0Max) continue;
                    const double tol = std::max(3.0, 0.2 * 46.0 * s);
                    std::vector<char> used(nb, 0);
                    std::vector<std::pair<int, int>> pairs;
                    std::vector<double> errs;
                    for (int k = 0; k < nBands; ++k) {
                        const double tp = s * (bands[k] - r0);
                        int jbest = 0; double dbest = 1e30;
                        for (int j = 0; j < nb; ++j) {
                            const double dd = std::abs(tproj[j] - tp);
                            if (dd < dbest) { dbest = dd; jbest = j; }
                        }
                        if (used[jbest] || dbest > tol) continue;
                        used[jbest] = 1;
                        pairs.emplace_back(jbest, k);
                        errs.push_back(tproj[jbest] - tp);
                    }
                    if (int(pairs.size()) < 2) continue;
                    double ss = 0; for (double e : errs) ss += e * e;
                    const double rms = std::sqrt(ss / errs.size());
                    const std::pair<int, double> key{int(pairs.size()), -rms};
                    if (!haveBest || key > bestKey) {
                        std::vector<int> order(pairs.size());
                        std::iota(order.begin(), order.end(), 0);
                        std::stable_sort(order.begin(), order.end(), [&](int p, int q) {
                            return bands[pairs[p].second] < bands[pairs[q].second];
                        });
                        std::vector<double> tj, rk;
                        for (int o : order) { tj.push_back(tproj[pairs[o].first]); rk.push_back(bands[pairs[o].second]); }
                        bool ordered = true;
                        for (size_t z = 1; z < tj.size(); ++z) if (tj[z] - tj[z - 1] <= 0) { ordered = false; break; }
                        if (!ordered) continue;
                        double s2, intercept;
                        lstsqLine(rk, tj, s2, intercept);
                        if (s2 < cfg.sMin || s2 > cfg.sMax) continue;
                        const double r02 = -intercept / s2;
                        double ss2 = 0; for (size_t z = 0; z < tj.size(); ++z) { const double e = tj[z] - s2 * (rk[z] - r02); ss2 += e * e; }
                        const double rms2 = std::sqrt(ss2 / tj.size());
                        haveBest = true; bestKey = key;
                        best.n = int(pairs.size()); best.rms = rms2; best.s = s2; best.r0 = r02; best.pairs = pairs;
                    }
                }
        }
    return best;
}

} // namespace

BandMatch frameBandMatch(const cv::Mat& gray, double gx, double gy, double rmax,
                         const std::vector<double>& bandsMm, const BandMatchConfig& cfg)
{
    BandMatch none;
    if (bandsMm.size() < 2) return none;             // untaped / too few bands
    CV_Assert(gray.type() == CV_8UC1);

    const std::vector<Blob> blobs = detectBlobs(gray, gx, gy, rmax, cfg);
    if (blobs.size() < 2) return none;

    bool haveRes = false;
    std::pair<int, double> resKey{0, 0.0};
    double resS = 0, resR0 = 0, resUx = 0, resUy = 0, resRms = 0; int resN = 0;
    std::vector<std::pair<double, double>> resMpts;

    std::vector<double> tried;
    const int nb = int(blobs.size());
    for (int a = 0; a < nb; ++a)
        for (int b = a + 1; b < nb; ++b) {
            const double dx = blobs[b].x - blobs[a].x, dy = blobs[b].y - blobs[a].y;
            const double nd = std::hypot(dx, dy);
            if (nd < 8.0) continue;
            const double ux = dx / nd, uy = dy / nd;
            double ang = std::fmod(std::atan2(uy, ux), kPi);
            if (ang < 0) ang += kPi;
            bool dup = false;
            for (double tt : tried)
                if (std::abs(std::fmod(ang - tt + kPi / 2, kPi) - kPi / 2) < 0.03) { dup = true; break; }
            if (dup) continue;
            tried.push_back(ang);

            std::vector<int> idx;
            for (int p = 0; p < nb; ++p) {
                const double rx = blobs[p].x - blobs[a].x, ry = blobs[p].y - blobs[a].y;
                if (std::abs(rx * uy - ry * ux) < cfg.latTol) idx.push_back(p);
            }
            if (idx.size() < 2) continue;

            const double grx = gx - blobs[a].x, gry = gy - blobs[a].y;
            if (std::abs(grx * uy - gry * ux) > cfg.gripGate) continue;
            const double t0 = grx * ux + gry * uy;

            for (double sgn : {1.0, -1.0}) {
                std::vector<double> tp;
                tp.reserve(idx.size());
                for (int p : idx) {
                    const double rel = (blobs[p].x - blobs[a].x) * ux + (blobs[p].y - blobs[a].y) * uy;
                    tp.push_back(sgn * (rel - t0));
                }
                const MatchResult mr = matchPattern(tp, bandsMm, cfg);
                const double gate = (mr.n == 4) ? cfg.rms4 : cfg.rms5;
                if (mr.n < 4 || mr.rms > gate) continue;

                std::vector<std::array<double, 3>> mpr;
                for (auto& pr : mr.pairs)
                    mpr.push_back({blobs[idx[pr.first]].x, blobs[idx[pr.first]].y, bandsMm[pr.second]});
                std::stable_sort(mpr.begin(), mpr.end(),
                                 [](const std::array<double, 3>& p, const std::array<double, 3>& q) { return p[2] < q[2]; });
                const int gd = gapDark(gray, mpr, cfg);
                if (gd == 1 || (mr.n == 4 && gd != 2)) continue;

                const std::pair<int, double> key{mr.n, -mr.rms};
                if (!haveRes || key > resKey) {
                    haveRes = true; resKey = key;
                    resN = mr.n; resRms = mr.rms; resS = mr.s; resR0 = mr.r0;
                    resUx = sgn * ux; resUy = sgn * uy;
                    resMpts.clear();
                    for (auto& pr : mr.pairs) resMpts.emplace_back(blobs[idx[pr.first]].x, blobs[idx[pr.first]].y);
                }
            }
        }

    if (!haveRes) return none;
    BandMatch out;
    out.ok = true; out.n = resN; out.rms = float(resRms); out.s = float(resS); out.r0 = float(resR0);
    double th = std::fmod(std::atan2(resUy, resUx) * 180.0 / kPi, 360.0);
    if (th < 0) th += 360.0;
    out.thetaDeg = float(th);
    double mx = 0, my = 0;
    for (auto& p : resMpts) { mx += p.first; my += p.second; }
    out.mbx = float(mx / resMpts.size());
    out.mby = float(my / resMpts.size());
    return out;
}

// ── E4: steel-segment lock ───────────────────────────────────────────────────

RayProfile rayProfile(const cv::Mat& img, double gx, double gy, double thetaRad,
                      double rLo, double rHi, double rStep, const RidgeConfig& cfg)
{
    CV_Assert(img.type() == CV_32F);
    const int W = img.cols, H = img.rows;
    const double ux = std::cos(thetaRad), uy = std::sin(thetaRad);
    RayProfile P;
    for (double r = rLo; r < rHi; r += rStep) {
        const double cx = gx + ux * r, cy = gy + uy * r;
        const bool inb = (cx >= 0.0 && cx < W && cy >= 0.0 && cy < H);
        P.r.push_back(float(r));
        if (!inb) {   // off-frame: zero evidence, as ridgeSweep
            P.e.push_back(0.f); P.on.push_back(0.f); P.bg.push_back(0.f);
            P.wide.push_back(0.f); P.bright.push_back(1);
            continue;
        }
        const RaySample smp = sampleRay(img, cx, cy, ux, uy, cfg);
        const double w = (double(at(img, cx - (-7.0) * uy, cy + (-7.0) * ux))
                        + double(at(img, cx - (-5.0) * uy, cy + (-5.0) * ux))
                        + double(at(img, cx - ( 5.0) * uy, cy + ( 5.0) * ux))
                        + double(at(img, cx - ( 7.0) * uy, cy + ( 7.0) * ux))) / 4.0;
        P.e.push_back(float(smp.e)); P.on.push_back(float(smp.on)); P.bg.push_back(float(smp.bg));
        P.wide.push_back(float(w)); P.bright.push_back(smp.bright ? 1 : 0);
    }
    return P;
}

namespace {

// Mean of e over [a, b] (inclusive, clamped to the profile).
double meanE(const RayProfile& P, int a, int b)
{
    const int n = int(P.e.size());
    a = std::max(a, 0); b = std::min(b, n - 1);
    if (b < a) return 0.0;
    double acc = 0.0;
    for (int i = a; i <= b; ++i) acc += P.e[size_t(i)];
    return acc / double(b - a + 1);
}

} // namespace

SegmentLock segmentLock(const cv::Mat& img, double gx, double gy, double thetaRad,
                        double rmax, const SegmentGeom& geom, const SegmentConfig& cfg,
                        const RidgeConfig& ridge, double sPrior, double lenPriorPx)
{
    SegmentLock out;
    double thDeg = std::fmod(thetaRad * 180.0 / kPi, 360.0);
    if (thDeg < 0) thDeg += 360.0;
    out.thetaDeg = float(thDeg);
    if (!geom.valid()) return out;

    const RayProfile P = rayProfile(img, gx, gy, thetaRad, cfg.rLo, rmax, 1.0, ridge);
    const int n = int(P.e.size());
    if (n < cfg.minLenPx + 8) return out;

    // ── sample classes ──────────────────────────────────────────────────────
    // WIDE: the lateral ±5/±7 samples are lit like the ridge — a blob (hands,
    // glove, head, forearm), not a line. Polarity follows the regime.
    std::vector<uint8_t> wide(size_t(n), 0), brt(size_t(n), 0), drk(size_t(n), 0);
    for (int i = 0; i < n; ++i) {
        const double contrast = P.bright[size_t(i)] ? double(P.wide[size_t(i)]) - double(P.bg[size_t(i)])
                                                    : double(P.bg[size_t(i)]) - double(P.wide[size_t(i)]);
        wide[size_t(i)] = contrast >= cfg.wideMin ? 1 : 0;
        brt[size_t(i)]  = (P.e[size_t(i)] >= cfg.eOn && !wide[size_t(i)]) ? 1 : 0;
        drk[size_t(i)]  = (P.e[size_t(i)] <= cfg.eOff) ? 1 : 0;
    }

    // ── the steel run: longest BRIGHT stretch bridging ≤ maxHolePx non-bright
    //    samples, never bridging a WIDE stretch longer than the hole ──────────
    int bestA = -1, bestB = -1;
    {
        int a = -1, last = -1;
        for (int i = 0; i < n; ++i) {
            if (brt[size_t(i)]) {
                if (a < 0) a = i;
                last = i;
            } else if (a >= 0 && i - last > cfg.maxHolePx) {
                if (bestA < 0 || last - a > bestB - bestA) { bestA = a; bestB = last; }
                a = -1;
            }
        }
        if (a >= 0 && (bestA < 0 || last - a > bestB - bestA)) { bestA = a; bestB = last; }
    }
    if (bestA < 0 || bestB - bestA + 1 < cfg.minLenPx) return out;
    const int a = bestA, b = bestB;

    // support: fraction of e > eOff over the run
    {
        int pos = 0;
        for (int i = a; i <= b; ++i) if (!drk[size_t(i)]) ++pos;
        out.support = float(double(pos) / double(b - a + 1));
    }
    if (out.support < cfg.supportMin) return out;

    // ── distal landmark: what follows the run within lookAheadPx ────────────
    if (b >= n - 3) return out;                     // run leaves the frame / search radius: no terminus
    {
        int dark = 0;
        for (int i = b + 1; i <= std::min(n - 1, b + cfg.minDarkPx + 2); ++i) if (drk[size_t(i)]) ++dark;
        int firstWide = -1;
        for (int i = b + 1; i <= std::min(n - 1, b + cfg.lookAheadPx); ++i)
            if (wide[size_t(i)]) { firstWide = i; break; }
        // Order matters: a wide blob's interior is evidence-free (on ≈ bg ⇒ e ≈ −12)
        // and so reads as "dark" — a head that begins right at the run's end is
        // the hosel case, not a ferrule gap followed by a head.
        if (firstWide > 0 && firstWide - b <= 3)          { out.distal = 2; out.sigmaMm = cfg.hoselTolMm; }
        else if (dark >= cfg.minDarkPx && firstWide > 0)  { out.distal = 1; out.sigmaMm = cfg.ferruleTolMm; }
        else if (dark >= cfg.minDarkPx)                   { out.distal = 3; out.sigmaMm = cfg.hoselTolMm; }
        else return out;                                  // dim tail: the run petered out, no landmark
        if (out.distal != 2 && meanE(P, b - 3, b) - meanE(P, b + 1, b + 4) < cfg.edgeMin) return out;
    }
    // A ferrule of 3–4 px is shorter than the hole the run bridges, so a run
    // that reached the wide head may have swallowed it: look back over the last
    // lookAheadPx for ≥ 2 consecutive dark samples and, if found, end the steel
    // there — the ferrule is then resolved after all.
    int bSteel = b;
    if (out.distal == 2) {
        for (int i = b; i >= std::max(a + cfg.minLenPx, b - cfg.lookAheadPx); --i)
            if (drk[size_t(i)] && drk[size_t(i - 1)]) {
                int j = i - 1;
                while (j > a && drk[size_t(j - 1)]) --j;
                bSteel = j - 1; out.distal = 1; out.sigmaMm = cfg.ferruleTolMm;
                break;
            }
    }
    out.rF = P.r[size_t(bSteel)];
    // the millimetre the terminus refers to depends on what ended the run
    const double mF = (out.distal == 2) ? geom.hoselMm + cfg.hoselLenMm      // ran into the wide head
                                        : geom.hoselMm - cfg.ferruleMm;      // steel ends at the ferrule

    // ── proximal landmark: a dark gap (visible grip) right before the run ───
    // The hands' bloom is evidence-free too (on = bg = 255 ⇒ e = −12), so a dark
    // gap is only a GRIP when its on-ridge level sits below the steel's by the
    // edge step (bright regime). Over a blown background the grip is itself a
    // dark line and the onset is unresolved by construction.
    bool onsetOk = false;
    if (P.r[size_t(a)] <= cfg.proxFrac * rmax && a >= cfg.minDarkPx + 2 && P.bright[size_t(a)]) {
        int dark = 0;
        for (int i = a - cfg.minDarkPx - 2; i <= a - 1; ++i) if (drk[size_t(i)]) ++dark;
        double onIn = 0.0, onOut = 0.0;
        for (int i = a; i <= a + 3; ++i) onIn += P.on[size_t(i)];
        for (int i = a - 4; i <= a - 1; ++i) onOut += P.on[size_t(i)];
        onsetOk = dark >= cfg.minDarkPx && (onIn - onOut) / 4.0 >= cfg.edgeMin;
    }

    // ── fit ─────────────────────────────────────────────────────────────────
    double s = 0.0, r0 = 0.0;
    if (onsetOk) {
        out.rG = P.r[size_t(a)];
        s  = (double(out.rF) - double(out.rG)) / (mF - geom.gripEndMm);
        if (s <= 0.0) return out;
        r0 = geom.gripEndMm - double(out.rG) / s;
        out.n = 2; out.mode = SegmentMode::Full;
        // bands, when present, join as extra landmarks — assigned by the two-end
        // fit's prediction, then a joint least-squares refit (E1's tolerance).
        if (!geom.bandsMm.empty()) {
            std::vector<double> centres;   // plateau centres (px) on the run
            {
                int ps = -1;
                for (int i = a; i <= bSteel + 1; ++i) {
                    const bool sat = i <= bSteel && P.bright[size_t(i)] && P.on[size_t(i)] >= cfg.bandSat;
                    if (sat) { if (ps < 0) ps = i; }
                    else if (ps >= 0) { if (i - ps >= 3) centres.push_back(0.5 * (P.r[size_t(ps)] + P.r[size_t(i - 1)])); ps = -1; }
                }
            }
            // pass 1 assigns at a loose tolerance (the two-end scale carries the
            // distal landmark's ±σ) and refits; pass 2 re-assigns at E1's tight
            // tolerance from the refit and decides.
            const double tolLoose = std::max(8.0, 0.08 * (double(out.rF) - double(out.rG)));
            const double tolTight = std::max(3.0, 0.2 * 46.0 * s);
            double sFit = s, r0Fit = r0;
            for (int pass = 0; pass < 2; ++pass) {
                const double tol = pass == 0 ? tolLoose : tolTight;
                std::vector<double> rr{double(out.rG), double(out.rF)}, mm{geom.gripEndMm, mF};
                std::vector<char> used(centres.size(), 0);
                for (double m : geom.bandsMm) {
                    const double rp = sFit * (m - r0Fit);
                    int jb = -1; double db = 1e30;
                    for (size_t j = 0; j < centres.size(); ++j) {
                        const double d = std::abs(centres[j] - rp);
                        if (!used[j] && d < db) { db = d; jb = int(j); }
                    }
                    if (jb >= 0 && db <= tol) { used[size_t(jb)] = 1; rr.push_back(centres[size_t(jb)]); mm.push_back(m); }
                }
                if (rr.size() <= 2) break;
                double s2, intercept;
                lstsqLine(mm, rr, s2, intercept);
                if (s2 <= 0.0) break;
                const double r02 = -intercept / s2;
                double ss = 0.0;
                for (size_t z = 0; z < rr.size(); ++z) { const double e = rr[z] - s2 * (mm[z] - r02); ss += e * e; }
                const double rms = std::sqrt(ss / double(rr.size()));
                if (pass == 0) { sFit = s2; r0Fit = r02; continue; }
                if (rms <= cfg.rmsMax) { s = s2; r0 = r02; out.n = int(rr.size()); out.rms = float(rms); }
            }
        }
        if (sPrior > 0.0 && std::abs(s - sPrior) > cfg.sTol * sPrior) return out;
    } else if (sPrior > 0.0) {
        s  = sPrior;
        r0 = mF - double(out.rF) / s;
        out.n = 1; out.mode = SegmentMode::Terminus;
    } else {
        return out;
    }

    // ── gates ───────────────────────────────────────────────────────────────
    if (s < cfg.sMin || s > cfg.sMax) { out.mode = SegmentMode::None; return out; }
    if (r0 <= cfg.r0Min || r0 > cfg.r0Max) { out.mode = SegmentMode::None; return out; }
    if (lenPriorPx > 0.0) {
        const double L = s * (geom.clubLenMm - r0);
        if (std::abs(L - lenPriorPx) > cfg.lenTol * lenPriorPx) { out.mode = SegmentMode::None; return out; }
    }
    out.s = float(s); out.r0 = float(r0); out.ok = true;
    return out;
}

} // namespace pinpoint::analysis
