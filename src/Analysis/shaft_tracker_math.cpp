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

#include "shaft_tracker_math.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace pinpoint::analysis {

namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.f * kPi;

// Wrap to (-π, π].
inline float wrapPi(float a)
{
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.f)
        a += kTwoPi;
    return a - kPi;
}

inline float angAbsDiff(float a, float b) { return std::abs(wrapPi(a - b)); }

struct ThresholdInfo {
    int thr        = 0;   // adaptive supra-threshold gate for this response map
    int supraCount = 0;   // pixels above thr — ~0 means the polarity is dead
};

// Adaptive ridge threshold from the response map's noise statistics:
// median + K · 1.4826 · MAD, via two 256-bin histograms (8-bit map — exact
// and allocation-free). The response is mostly background noise, so the
// median/MAD characterise noise, not signal. The supra count falls out of
// the same histogram and lets the caller skip a polarity with no signal at
// all (a club is steel OR graphite — one map is usually empty).
ThresholdInfo madThreshold(const cv::Mat &m, float k, float floorV)
{
    int hist[256] = {0};
    for (int y = 0; y < m.rows; ++y) {
        const uchar *row = m.ptr<uchar>(y);
        for (int x = 0; x < m.cols; ++x)
            ++hist[row[x]];
    }
    const int total = m.rows * m.cols;
    const auto histMedian = [total](const int *h) {
        const int half = (total + 1) / 2;
        int cum = 0;
        for (int v = 0; v < 256; ++v) {
            cum += h[v];
            if (cum >= half)
                return v;
        }
        return 255;
    };
    const int med = histMedian(hist);
    int dev[256] = {0};
    for (int v = 0; v < 256; ++v)
        dev[std::abs(v - med)] += hist[v];
    const int mad = histMedian(dev);
    const float thrF = static_cast<float>(med) + k * 1.4826f * static_cast<float>(mad);
    ThresholdInfo out;
    out.thr = std::max(static_cast<int>(std::lround(floorV)),
                       static_cast<int>(std::lround(thrF)));
    for (int v = out.thr + 1; v < 256; ++v)
        out.supraCount += hist[v];
    return out;
}

// Per-θ column state after the run scan (one per θ bin per anchor).
struct ColumnInfo {
    float wScore = 0.f;   // clutter-masked, prior-weighted score (peak finding)
    int   start  = -1;    // supra-threshold run extent, ρ bins
    int   end    = -1;
    bool  dark   = false; // black-hat beat top-hat on this column
};

// Incremental supra-threshold run accumulator for one polarity along one ray.
// The run must begin within startLimit of the scan start (anchored — exactly
// this rejects non-radial clutter: an alignment stick crossing the ray far
// from the grip can never start a run) and may bridge internal sub-threshold
// gaps up to maxGap bins (a ray crossing the torso loses contrast mid-run but
// keeps it on both sides — the coverage term keeps such columns far above
// clutter rays).
struct RunAcc {
    int   first = -1, last = -1;
    int   gap   = 0;
    float sum   = 0.f;
    bool  dead  = false;

    inline void push(int r, int v, int thr, int startLimit, int maxGap)
    {
        if (first < 0) {
            if (v > thr) {
                first = last = r;
                sum   = static_cast<float>(v);
            } else if (r >= startLimit) {
                dead = true;
            }
            return;
        }
        if (v > thr) {
            last = r;
            sum += static_cast<float>(v);
            gap = 0;
        } else if (++gap > maxGap) {
            dead = true;
        }
    }
};

// Column score = (mean response over the run, i.e. mean supra strength ×
// coverage) × √(runLen / available) — run length rewards longer support
// sublinearly.
inline float runScore(const RunAcc &a, int nRho, int rhoMinBin)
{
    if (a.first < 0)
        return 0.f;
    const int   runLen = a.last - a.first + 1;
    const float avail  = static_cast<float>(std::max(1, nRho - rhoMinBin));
    return (a.sum / static_cast<float>(runLen)) *
           std::sqrt(static_cast<float>(runLen) / avail);
}

// Blur-mode integrator (R8): sums the supra-(lowered-threshold) response over
// ALL ρ — NOT a contiguous run starting at the grip. A fast shaft images as a
// faint, broken motion-blur fan whose per-pixel response never clears the sharp
// threshold and whose support is gappy; integrating the coherent signal at a
// lowered threshold recovers it where the anchored run scan gives up. Used only
// inside the kinematic envelope (so the lowered threshold can't raise far-field
// false positives), and scored with runScore's shape so the peak-finding,
// minScoreFrac and wedge-plateau stages downstream are unchanged.
struct BlurAcc {
    int   first = -1, last = -1, count = 0;
    float sum   = 0.f;
    inline void push(int r, int v, int loweredThr)
    {
        if (v > loweredThr) {
            if (first < 0) first = r;
            last = r;
            sum += static_cast<float>(v - loweredThr);
            ++count;
        }
    }
};
inline float blurScore(const BlurAcc &a, int nRho, int rhoMinBin)
{
    if (a.count <= 0)
        return 0.f;
    const float avail = static_cast<float>(std::max(1, nRho - rhoMinBin));
    return (a.sum / static_cast<float>(a.count)) *
           std::sqrt(static_cast<float>(a.count) / avail);
}

// Soft von-Mises-style prior bump: 1 at dTheta = 0, decaying to floorW.
// Multiplicative weighting only — a prior must never hard-gate a measurement.
inline float priorBump(float dTheta, float sigmaRad, float floorW)
{
    const float kappa = 1.f / std::max(1e-4f, sigmaRad * sigmaRad);
    return floorW + (1.f - floorW) * std::exp(kappa * (std::cos(dTheta) - 1.f));
}

// Per-θ multiplicative weight: 0 inside the hard elbow clutter mask (the
// forearm is itself a radial ridge into the anchor — the dominant false
// positive), soft prior bumps everywhere else. Identical for every anchor in
// the perturbation grid, so it is built once.
std::vector<float> buildThetaWeights(const ShaftDetectConfig &cfg, const AnchorPrior &prior)
{
    std::vector<float> w(static_cast<size_t>(cfg.thetaBins), 1.f);
    const float binW      = kTwoPi / static_cast<float>(cfg.thetaBins);
    const float maskRad   = cfg.clutterMaskDeg * kPi / 180.f;
    const float ihSigma   = cfg.interHandSigmaDeg * kPi / 180.f;
    const float predSigma = (prior.predictedSigmaRad > 1e-4f) ? prior.predictedSigmaRad
                                                              : 5.f * kPi / 180.f;
    const int   nElbow    = std::clamp(prior.numElbowDirs, 0, 2);
    for (int t = 0; t < cfg.thetaBins; ++t) {
        const float theta = static_cast<float>(t) * binW;
        // Phase-1 plausibility sector: hard-restrict the search to within
        // ±armPlausMaxRad of the lead-forearm extension. The club cannot fold back
        // up the arm at any phase, so zeroing the implausible bins both forbids the
        // arm-as-club pick AND frees the top-K so the (often weaker) true club ridge
        // is no longer crowded out. Phase- & chirality-independent; 0 ⇒ disabled.
        if (prior.armPlausMaxRad > 0.f
            && angAbsDiff(theta, prior.armAxisRad) > prior.armPlausMaxRad) {
            w[static_cast<size_t>(t)] = 0.f;
            continue;
        }
        bool masked = false;
        for (int e = 0; e < nElbow; ++e) {
            if (angAbsDiff(theta, prior.elbowDirRad[e]) <= maskRad) {
                masked = true;
                break;
            }
        }
        if (masked) {
            w[static_cast<size_t>(t)] = 0.f;
            continue;
        }
        float v = 1.f;
        // R6 kinematic direction (φ_club_pred ± σ_β), when present, is the
        // long-baseline directional prior and supersedes the short, often-absent
        // inter-hand bump. Default (hasKinematicDir == false) keeps prior behaviour.
        if (prior.hasKinematicDir)
            v *= priorBump(wrapPi(theta - prior.kinematicDirRad),
                           prior.kinematicSigmaRad > 1e-4f ? prior.kinematicSigmaRad : ihSigma,
                           cfg.priorFloor);
        else if (prior.hasInterHandDir)
            v *= priorBump(wrapPi(theta - prior.interHandDirRad), ihSigma, cfg.priorFloor);
        if (prior.hasPredictedTheta)
            v *= priorBump(wrapPi(theta - prior.predictedThetaRad), predSigma, cfg.priorFloor);
        w[static_cast<size_t>(t)] = v;
    }
    return w;
}

// The polar transform + column scan, fused: every θ column of every anchor is
// scanned by walking the ray straight off the Cartesian response maps
// (row t ↔ angle t·2π/thetaBins from +x toward +y — cv::warpPolar's image
// convention; ρ bin r ↔ r·rhoScale px). Nearest-neighbour sampling — θ
// precision comes from the bin-domain parabola/centroid and the TLS refit,
// not from sample interpolation — out-of-ROI samples read as 0. Early exit
// once a column's runs are dead: most columns die right past the start gate,
// which (with no intermediate polar images and no per-anchor resample) is
// why this outruns 18 warpPolar/remap passes by an order of magnitude.
// Parallel over θ rows; writes are disjoint per (anchor, θ).
void scanAllAnchors(const cv::Mat *top, const cv::Mat *black, const cv::Mat *diffImage,
                    const cv::Point2f *anchors, int nAnchors,
                    const ShaftDetectConfig &cfg, int thrTop, int thrBlack, int thrDiff,
                    const std::vector<float> &thetaWeight,
                    float rhoScale, int nRho,
                    std::vector<std::vector<ColumnInfo>> &colsA,
                    bool blurMode, float centerRad, float halfRad)
{
    const int rhoMinBin = std::clamp(static_cast<int>(std::lround(cfg.rhoMinPx / rhoScale)),
                                     0, nRho - 1);
    const int startLimit = std::min(nRho - 1,
        rhoMinBin + std::max(0, static_cast<int>(std::lround(cfg.runStartGapPx / rhoScale))));
    const int maxGap = std::max(0, static_cast<int>(std::lround(cfg.runMaxGapPx / rhoScale)));
    // R8 lowered thresholds inside the blur window (integrate the faint fan).
    const int lthrTop   = std::max(1, static_cast<int>(static_cast<float>(thrTop)   * cfg.blurThreshScale));
    const int lthrBlack = std::max(1, static_cast<int>(static_cast<float>(thrBlack) * cfg.blurThreshScale));
    const int lthrDiff  = std::max(1, static_cast<int>(static_cast<float>(thrDiff)  * cfg.blurThreshScale));

    const cv::Mat &ref   = top ? *top : (black ? *black : *diffImage);
    const int      w     = ref.cols, h = ref.rows;
    const uchar   *tData = top ? top->ptr<uchar>(0) : nullptr;
    const uchar   *bData = black ? black->ptr<uchar>(0) : nullptr;
    const uchar   *dData = diffImage ? diffImage->ptr<uchar>(0) : nullptr;
    const size_t   tStep = top ? top->step : 0;
    const size_t   bStep = black ? black->step : 0;
    const size_t   dStep = diffImage ? diffImage->step : 0;
    const float    binW  = kTwoPi / static_cast<float>(cfg.thetaBins);

    cv::parallel_for_(cv::Range(0, cfg.thetaBins), [&](const cv::Range &range) {
        for (int t = range.start; t < range.end; ++t) {
            const float ct = std::cos(static_cast<float>(t) * binW) * rhoScale;
            const float st = std::sin(static_cast<float>(t) * binW) * rhoScale;
            // Blur mode (R8): score every column by integration instead of the
            // anchored run — recovers the broken, faint motion-blur fan. The
            // envelope restriction + SNR gate are applied in detectShaft (which
            // needs the out-of-envelope columns as the noise/clutter baseline).
            const bool inBlur = blurMode;
            for (int a = 0; a < nAnchors; ++a) {
                const float ax = anchors[a].x, ay = anchors[a].y;
                float score;
                bool  dark;
                int   cStart, cEnd;
                if (inBlur) {
                    BlurAcc bt, bb, bd;
                    bool hasDiff = (dData != nullptr);
                    for (int r = rhoMinBin; r < nRho; ++r) {
                        const int x = static_cast<int>(ax + static_cast<float>(r) * ct + 0.5f);
                        const int y = static_cast<int>(ay + static_cast<float>(r) * st + 0.5f);
                        const bool in = static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
                                        static_cast<unsigned>(y) < static_cast<unsigned>(h);
                        if (tData) bt.push(r, in ? tData[static_cast<size_t>(y) * tStep + x] : 0, lthrTop);
                        if (bData) bb.push(r, in ? bData[static_cast<size_t>(y) * bStep + x] : 0, lthrBlack);
                        if (hasDiff) bd.push(r, in ? dData[static_cast<size_t>(y) * dStep + x] : 0, lthrDiff);
                    }
                    const float sT = blurScore(bt, nRho, rhoMinBin);
                    const float sB = blurScore(bb, nRho, rhoMinBin);
                    float sD = 0.f;
                    // Only use diff image inside the kinematic envelope
                    const float theta = static_cast<float>(t) * binW;
                    const bool inEnvelope = (halfRad > 0.f) && (angAbsDiff(theta, centerRad) <= halfRad);
                    if (hasDiff && inEnvelope) {
                        sD = blurScore(bd, nRho, rhoMinBin);
                    }

                    if (sD > sT && sD > sB) {
                        dark   = false;
                        score  = sD;
                        cStart = bd.first;
                        cEnd   = bd.last;
                    } else {
                        dark   = sB > sT;
                        score  = dark ? sB : sT;
                        const BlurAcc &bbest = dark ? bb : bt;
                        cStart = bbest.first;
                        cEnd   = bbest.last;
                    }
                } else {
                    RunAcc rt, rb;
                    rt.dead = (tData == nullptr);
                    rb.dead = (bData == nullptr);
                    for (int r = rhoMinBin; r < nRho; ++r) {
                        const int x = static_cast<int>(ax + static_cast<float>(r) * ct + 0.5f);
                        const int y = static_cast<int>(ay + static_cast<float>(r) * st + 0.5f);
                        const bool in = static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
                                        static_cast<unsigned>(y) < static_cast<unsigned>(h);
                        if (!rt.dead)
                            rt.push(r, in ? tData[static_cast<size_t>(y) * tStep + x] : 0,
                                    thrTop, startLimit, maxGap);
                        if (!rb.dead)
                            rb.push(r, in ? bData[static_cast<size_t>(y) * bStep + x] : 0,
                                    thrBlack, startLimit, maxGap);
                        if (rt.dead && rb.dead)
                            break;
                    }
                    const float sTop   = runScore(rt, nRho, rhoMinBin);
                    const float sBlack = runScore(rb, nRho, rhoMinBin);
                    dark   = sBlack > sTop;
                    score  = dark ? sBlack : sTop;
                    const RunAcc &best = dark ? rb : rt;
                    cStart = best.first;
                    cEnd   = best.last;
                }
                if (score <= 0.f)
                    continue;
                ColumnInfo &c = colsA[static_cast<size_t>(a)][static_cast<size_t>(t)];
                c.start  = cStart;
                c.end    = cEnd;
                c.dark   = dark;
                // Blur columns carry the raw integral with only the hard elbow
                // mask (0/1) — so in- and out-of-envelope scores are comparable
                // for the SNR baseline. Sharp columns keep the full soft weighting.
                c.wScore = inBlur
                    ? (thetaWeight[static_cast<size_t>(t)] > 0.f ? score : 0.f)
                    : score * thetaWeight[static_cast<size_t>(t)];
            }
        }
    });
}

struct Peak {
    int   bin = 0;
    float w   = 0.f;
};

// Local maxima of the weighted score → greedy NMS (wrap-around aware) →
// top-K, then quality gates (relative score, minimum visible length).
std::vector<Peak> pickPeaks(const std::vector<ColumnInfo> &cols,
                            const ShaftDetectConfig &cfg, float rhoScale)
{
    const int n = static_cast<int>(cols.size());
    std::vector<Peak> maxima;
    for (int i = 0; i < n; ++i) {
        const float w = cols[static_cast<size_t>(i)].wScore;
        if (w <= 0.f)
            continue;
        const float wp = cols[static_cast<size_t>((i + n - 1) % n)].wScore;
        const float wn = cols[static_cast<size_t>((i + 1) % n)].wScore;
        if (w > wp && w >= wn)
            maxima.push_back({i, w});
    }
    std::sort(maxima.begin(), maxima.end(),
              [](const Peak &a, const Peak &b) { return a.w > b.w; });

    const int sepBins = std::max(1, static_cast<int>(std::lround(
                            cfg.nmsSeparationDeg / 360.f * static_cast<float>(n))));
    std::vector<Peak> kept;
    for (const Peak &p : maxima) {
        bool ok = true;
        for (const Peak &k : kept) {
            const int d = std::abs(p.bin - k.bin);
            if (std::min(d, n - d) < sepBins) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        kept.push_back(p);
        if (static_cast<int>(kept.size()) >= cfg.maxCandidates)
            break;
    }
    if (kept.empty())
        return kept;

    const float minW = cfg.minScoreFrac * kept.front().w;
    // R5 length floor: drop runs shorter than the configured minimum. This is a
    // conservative noise gate, NOT a "shaft must be ≥ one arm" claim — under
    // foreshortening a real shaft can image far shorter than an arm near the top
    // and impact. The floor only ever tightens (max with minVisibleLenPx), so a
    // default config (minShaftLenPx == minVisibleLenPx) is unchanged.
    const float lenFloor = std::max(cfg.minVisibleLenPx, cfg.minShaftLenPx);
    std::vector<Peak> out;
    for (const Peak &p : kept) {
        const ColumnInfo &c = cols[static_cast<size_t>(p.bin)];
        const float visLen  = static_cast<float>(c.end - c.start + 1) * rhoScale;
        if (p.w >= minW && visLen >= lenFloor)
            out.push_back(p);
    }
    return out;
}

// Wedge plateau (B.4): expand from the peak while the weighted score stays
// above half-peak, tolerating short noise dips (a real plateau is the score
// profile of a blur fan — its bins are noisy, and an expansion that stops at
// the first sub-half bin truncates the window asymmetrically, biasing the
// centroid). A span beyond cfg.wedgeMinSpanDeg is a motion-blur fan —
// θ = intensity-weighted centroid of the plateau, σ = its half-width.
// Otherwise: parabolic sub-bin refinement, σ ≈ one θ-bin.
void refineTheta(const std::vector<ColumnInfo> &cols, int bin,
                 const ShaftDetectConfig &cfg, float &thetaRad,
                 float &sigmaRad, bool &wedge)
{
    const int   n    = static_cast<int>(cols.size());
    const float binW = kTwoPi / static_cast<float>(n);
    const float half = 0.5f * cols[static_cast<size_t>(bin)].wScore;
    const int   maxSpan = n / 4;   // safety: never grow a "plateau" past 90°
    const int   gapTol  = 4;       // bins (2° at 720) of sub-half dip bridged

    const auto at = [&](int off) {
        return cols[static_cast<size_t>(((bin + off) % n + n) % n)].wScore;
    };
    int lo = 0, hi = 0;            // signed offsets from the peak bin
    for (int o = -1, gap = 0; o > -maxSpan; --o) {
        if (at(o) >= half) {
            lo  = o;
            gap = 0;
        } else if (++gap > gapTol) {
            break;
        }
    }
    for (int o = 1, gap = 0; o < maxSpan; ++o) {
        if (at(o) >= half) {
            hi  = o;
            gap = 0;
        } else if (++gap > gapTol) {
            break;
        }
    }

    const int   spanBins = hi - lo + 1;
    const float spanDeg  = static_cast<float>(spanBins) * binW * 180.f / kPi;
    if (spanDeg > cfg.wedgeMinSpanDeg) {
        // Plateau: blur fan. θ = the window MIDPOINT — for a uniform sweep
        // the fan edges are the shaft at exposure start/end, so mid-exposure
        // is their mean. On a flat plateau this equals the intensity
        // centroid; when the ridge kernel suppresses the fan's solid interior
        // the score profile grows two edge horns and the intensity centroid
        // drags to the stronger horn (≈ −2° measured on an 8° fan) while the
        // midpoint stays put.
        const float off = 0.5f * static_cast<float>(lo + hi);
        thetaRad = (static_cast<float>(bin) + off) * binW;
        sigmaRad = 0.5f * static_cast<float>(spanBins) * binW;
        wedge    = true;
        return;
    }

    // Parabolic sub-bin refinement on the three bins around the peak.
    const float sm = cols[static_cast<size_t>(((bin - 1) % n + n) % n)].wScore;
    const float s0 = cols[static_cast<size_t>(bin)].wScore;
    const float sp = cols[static_cast<size_t>((bin + 1) % n)].wScore;
    const float d  = sm - 2.f * s0 + sp;
    float off = 0.f;
    if (std::abs(d) > 1e-6f)
        off = std::clamp(0.5f * (sm - sp) / d, -0.5f, 0.5f);
    thetaRad = (static_cast<float>(bin) + off) * binW;
    sigmaRad = binW;
    wedge    = false;
}

struct ThetaEstimate {
    float theta = 0.f, sigma = 0.f;
    bool  wedge   = false;
    int   mateBin = -1;    // twin-horn mate consumed by the merge (−1: none)
    float mateW   = 0.f;
};

// Full θ/σ estimate for one peak: plateau/parabola refinement plus, when
// allowed, the twin-horn merge (B.4). A blur fan whose *solid* proximal
// interior is wider than the ridge kernel responds along its two EDGES, not
// as one plateau — the opening retains the filled interior, and the anchored
// start gate then kills the centre columns. The signature is a same-polarity
// local max of comparable strength a few degrees away with a genuine
// sub-half-peak valley in between (a connected plateau is already read by the
// single-window path). The search runs on the raw score profile, NOT the NMS
// survivors — the mate usually sits inside the winner's NMS radius and has
// been suppressed. For a uniform sweep the edges are the shaft at exposure
// start/end, so mid-exposure is exactly their mean and the half-separation
// adds to σ.
ThetaEstimate estimateTheta(const std::vector<ColumnInfo> &cols, int bin, float w,
                            bool allowMate, const ShaftDetectConfig &cfg)
{
    ThetaEstimate e;
    refineTheta(cols, bin, cfg, e.theta, e.sigma, e.wedge);
    if (!allowMate)
        return e;

    const int  n = static_cast<int>(cols.size());
    const auto wrapBin = [n](int b) { return (b % n + n) % n; };
    const int  maxSepBins = std::max(2, static_cast<int>(std::lround(
                                cfg.wedgePairMaxSepDeg / 360.f * static_cast<float>(n))));
    const int  minSepBins = std::max(2, static_cast<int>(std::lround(
                                1.5f / 360.f * static_cast<float>(n))));
    const bool dark0 = cols[static_cast<size_t>(bin)].dark;
    for (int off = -maxSepBins; off <= maxSepBins; ++off) {
        if (std::abs(off) < minSepBins)
            continue;
        const int b = wrapBin(bin + off);
        const ColumnInfo &cb = cols[static_cast<size_t>(b)];
        const float wb = cb.wScore;
        if (wb < 0.4f * w || wb <= e.mateW || cb.dark != dark0)
            continue;
        if (wb <= cols[static_cast<size_t>(wrapBin(b - 1))].wScore ||
            wb < cols[static_cast<size_t>(wrapBin(b + 1))].wScore)
            continue;                       // not a local max
        float valley = wb;
        const int step = (off > 0) ? 1 : -1;
        for (int o = step; o != off; o += step)
            valley = std::min(valley, cols[static_cast<size_t>(wrapBin(bin + o))].wScore);
        if (valley >= 0.5f * std::min(wb, w))
            continue;                       // connected plateau, not twin horns
        e.mateW   = wb;
        e.mateBin = b;
    }
    if (e.mateBin >= 0) {
        float thetaM = 0.f, sigmaM = 0.f;
        bool  wedgeM = false;
        refineTheta(cols, e.mateBin, cfg, thetaM, sigmaM, wedgeM);
        const float dTheta = wrapPi(thetaM - e.theta);
        e.theta += 0.5f * dTheta;           // mean of the fan edges
        e.sigma  = 0.5f * std::abs(dTheta) + std::max(e.sigma, sigmaM);
        e.wedge  = true;
    }
    return e;
}

// Clubhead seed: intensity-weighted centroid of the (above-threshold)
// response in a small disc at the ridge terminus, in ROI coordinates.
cv::Point2f headSeed(const cv::Mat &resp, int thr, cv::Point2f anchor,
                     float thetaRad, float rhoEndPx, float radius)
{
    const cv::Point2f base(anchor.x + rhoEndPx * std::cos(thetaRad),
                           anchor.y + rhoEndPx * std::sin(thetaRad));
    const int r  = std::max(2, static_cast<int>(std::lround(radius)));
    const int cx = static_cast<int>(std::lround(base.x));
    const int cy = static_cast<int>(std::lround(base.y));
    float sw = 0.f, sx = 0.f, sy = 0.f;
    for (int y = std::max(0, cy - r); y <= std::min(resp.rows - 1, cy + r); ++y) {
        const uchar *row = resp.ptr<uchar>(y);
        for (int x = std::max(0, cx - r); x <= std::min(resp.cols - 1, cx + r); ++x) {
            const int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r * r)
                continue;
            const int v = row[x];
            if (v <= thr)
                continue;
            const float w = static_cast<float>(v - thr);
            sw += w;
            sx += w * static_cast<float>(x);
            sy += w * static_cast<float>(y);
        }
    }
    if (sw <= 0.f)
        return base;
    return {sx / sw, sy / sw};
}

// One weighted-PCA pass: supra-threshold pixels in a ±halfBand strip along
// `lenPx` of the ray from `base` in direction `dirRad`. Outputs the principal
// direction (sign-matched to the ray) and the weighted centroid.
bool tlsPass(const cv::Mat &resp, int thr, cv::Point2f base, float dirRad,
             float lenPx, float &outDirRad, cv::Point2f &outCentroid)
{
    const float ct = std::cos(dirRad), st = std::sin(dirRad);
    const float nx = -st, ny = ct;     // ray normal
    const int   halfBand = 3;

    double sw = 0., sx = 0., sy = 0., sxx = 0., syy = 0., sxy = 0.;
    int    nPix = 0;
    const int len = static_cast<int>(std::lround(lenPx));
    for (int s = 0; s <= len; ++s) {
        const float bx = base.x + static_cast<float>(s) * ct;
        const float by = base.y + static_cast<float>(s) * st;
        for (int t = -halfBand; t <= halfBand; ++t) {
            const int x = static_cast<int>(std::lround(bx + static_cast<float>(t) * nx));
            const int y = static_cast<int>(std::lround(by + static_cast<float>(t) * ny));
            if (x < 0 || y < 0 || x >= resp.cols || y >= resp.rows)
                continue;
            const int v = resp.ptr<uchar>(y)[x];
            if (v <= thr)
                continue;
            const double w = static_cast<double>(v - thr);
            sw  += w;
            sx  += w * x;
            sy  += w * y;
            sxx += w * x * x;
            syy += w * y * y;
            sxy += w * x * y;
            ++nPix;
        }
    }
    if (nPix < 20 || sw <= 0.)
        return false;

    const double mx  = sx / sw, my = sy / sw;
    const double cxx = sxx / sw - mx * mx;
    const double cyy = syy / sw - my * my;
    const double cxy = sxy / sw - mx * my;
    if (cxx + cyy < 1e-9)
        return false;
    // Principal axis of the weighted scatter = the TLS line direction.
    float ang = 0.5f * static_cast<float>(std::atan2(2. * cxy, cxx - cyy));
    // Orientation is mod π — pick the half consistent with the candidate ray.
    if (std::cos(ang) * ct + std::sin(ang) * st < 0.f)
        ang += kPi;
    outDirRad   = wrapPi(ang);
    outCentroid = {static_cast<float>(mx), static_cast<float>(my)};
    return true;
}

// Total-least-squares line refit over the candidate's supporting ridge pixels
// (proximal half of the run — the part that barely blurs and that shaft-lean
// needs). Iterated: the first band follows the candidate ray from the
// (possibly off-line) anchor; each subsequent band is recentred on the
// previous fit's line — a band offset laterally from a *tapered* ridge clips
// it asymmetrically and tilts the fit, and the tilt shrinks geometrically
// per recentring pass, so iterate to convergence. The effective anchor
// thereby slides onto the fitted line. Returns false (θ untouched) without
// enough support.
bool tlsRefine(const cv::Mat &resp, int thr, cv::Point2f anchor,
               float startPx, float lenPx, float &thetaRad)
{
    constexpr int   kMaxPasses = 5;
    constexpr float kConvergedRad = 0.0005f;   // ≈ 0.03°
    const float halfLen = 0.5f * lenPx;

    cv::Point2f base(anchor.x + startPx * std::cos(thetaRad),
                     anchor.y + startPx * std::sin(thetaRad));
    float dir = thetaRad;
    bool  any = false;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        float       fitDir = 0.f;
        cv::Point2f cen;
        if (!tlsPass(resp, thr, base, dir, halfLen, fitDir, cen))
            break;
        const float delta = std::abs(wrapPi(fitDir - dir));
        // Recentre: project the band base onto the fitted line. The lateral
        // shift of the base is the convergence metric — the fitted direction
        // can already agree while the band still rides beside the ridge.
        const float vx = std::cos(fitDir), vy = std::sin(fitDir);
        const float s  = (base.x - cen.x) * vx + (base.y - cen.y) * vy;
        const cv::Point2f next(cen.x + s * vx, cen.y + s * vy);
        const float shift = std::hypot(next.x - base.x, next.y - base.y);
        base = next;
        dir  = fitDir;
        any  = true;
        if (delta < kConvergedRad && shift < 0.25f)
            break;
    }
    if (any)
        thetaRad = dir;
    return any;
}

// R8 SNR gate: restrict the integrated blur scores to the kinematic envelope and
// zero the in-envelope winner unless it beats the out-of-envelope noise/clutter
// peak by snrMargin — the "never fabricate" backstop for the lowered-threshold
// integrator (a blank/noisy frame scores the same in and out of the envelope, so
// nothing survives). Operates on one anchor's column scores in place.
void gateBlurEnvelope(std::vector<ColumnInfo> &cols, int thetaBins,
                      float centerRad, float halfRad, float snrMargin)
{
    const float binW = kTwoPi / static_cast<float>(thetaBins);
    // Baseline = a high percentile of the out-of-envelope scores (the typical
    // strong-noise / clutter level), NOT the max — a single noise outlier must
    // not set the bar and kill a genuine faint fan.
    std::vector<float> outScores;
    outScores.reserve(static_cast<size_t>(thetaBins));
    for (int t = 0; t < thetaBins; ++t)
        if (angAbsDiff(static_cast<float>(t) * binW, centerRad) > halfRad)
            outScores.push_back(cols[static_cast<size_t>(t)].wScore);
    float baseline = 0.f;
    if (!outScores.empty()) {
        const size_t k = std::min(outScores.size() - 1,
                                  static_cast<size_t>(0.97f * static_cast<float>(outScores.size())));
        std::nth_element(outScores.begin(), outScores.begin() + k, outScores.end());
        baseline = outScores[k];
    }
    const float floorW = snrMargin * baseline;
    for (int t = 0; t < thetaBins; ++t) {
        ColumnInfo &c = cols[static_cast<size_t>(t)];
        if (angAbsDiff(static_cast<float>(t) * binW, centerRad) > halfRad || c.wScore <= floorW)
            c.wScore = 0.f;
    }
}

} // namespace

std::vector<ShaftCandidate> detectShaft(const cv::Mat &luma,
                                        const ShaftDetectConfig &cfg,
                                        const AnchorPrior &prior,
                                        const cv::Mat &diffImage)
{
    std::vector<ShaftCandidate> out;
    if (luma.empty() || luma.type() != CV_8UC1 || cfg.thetaBins < 16 ||
        cfg.maxRadiusPx <= cfg.rhoMinPx)
        return out;

    // 1. ROI: square bounding the search disc, clamped to the image.
    const int R = static_cast<int>(std::ceil(cfg.maxRadiusPx));
    const cv::Rect roiRect =
        cv::Rect(static_cast<int>(std::floor(prior.gripPx.x)) - R,
                 static_cast<int>(std::floor(prior.gripPx.y)) - R,
                 2 * R + 1, 2 * R + 1) &
        cv::Rect(0, 0, luma.cols, luma.rows);
    if (roiRect.width < 8 || roiRect.height < 8)
        return out;
    const cv::Mat roi = luma(roiRect);
    const cv::Point2f offset(static_cast<float>(roiRect.x), static_cast<float>(roiRect.y));
    cv::Point2f anchor0 = prior.gripPx - offset;
    anchor0.x = std::clamp(anchor0.x, 0.f, static_cast<float>(roiRect.width - 1));
    anchor0.y = std::clamp(anchor0.y, 0.f, static_cast<float>(roiRect.height - 1));

    // 2. Thin-structure ridge responses, both polarities, with adaptive
    //    per-map thresholds from the ROI's noise MAD. RECT kernel: for ridges
    //    thinner than the kernel the opening result is the same as an ellipse
    //    of equal width, but rect morphology is separable — ~6× faster here.
    const int ksz = std::max(3, cfg.ridgeKernelPx);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksz, ksz));
    cv::Mat top, black;
    cv::morphologyEx(roi, top, cv::MORPH_TOPHAT, kernel);
    cv::morphologyEx(roi, black, cv::MORPH_BLACKHAT, kernel);
    const ThresholdInfo tiTop   = madThreshold(top, cfg.noiseSigmaK, cfg.thresholdFloor);
    const ThresholdInfo tiBlack = madThreshold(black, cfg.noiseSigmaK, cfg.thresholdFloor);
    const int thrTop   = tiTop.thr;
    const int thrBlack = tiBlack.thr;
    // A polarity with fewer supra-threshold pixels than could ever support a
    // minimum-length run has no signal — skip its resample/scan entirely
    // (a club is steel OR graphite; one map is usually dead).
    const int  minSupra = std::max(8, static_cast<int>(cfg.minVisibleLenPx) / 2);
    const cv::Mat *topP   = (tiTop.supraCount   >= minSupra) ? &top   : nullptr;
    const cv::Mat *blackP = (tiBlack.supraCount >= minSupra) ? &black : nullptr;

    cv::Mat diffRoi;
    const cv::Mat *diffP = nullptr;
    int thrDiff = 8;
    if (!diffImage.empty() && diffImage.size() == luma.size()) {
        diffRoi = diffImage(roiRect);
        const ThresholdInfo tiDiff = madThreshold(diffRoi, cfg.noiseSigmaK, cfg.thresholdFloor);
        if (tiDiff.supraCount >= minSupra) {
            diffP = &diffRoi;
            thrDiff = tiDiff.thr;
        }
    }

    if (!topP && !blackP && !diffP)
        return out;

    const int   nRho     = std::max(8, static_cast<int>(std::lround(cfg.maxRadiusPx)));
    const float rhoScale = cfg.maxRadiusPx / static_cast<float>(nRho);   // ≈ 1 px/bin

    // 3.–5. Polar column scan about the anchor and a 3×3 grid of
    // perturbations, fused with the per-column run scoring (scanAllAnchors).
    const std::vector<float> thetaWeight = buildThetaWeights(cfg, prior);

    const int d = static_cast<int>(std::lround(cfg.anchorPerturbPx));
    const int nAnchors = (d > 0) ? 9 : 1;
    static const int kGrid[9][2] = {{0, 0},  {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                                    {1, 0},  {-1, 1},  {0, 1},  {1, 1}};
    cv::Point2f anchors[9];
    for (int a = 0; a < nAnchors; ++a)
        anchors[a] = {anchor0.x + static_cast<float>(kGrid[a][0] * d),
                      anchor0.y + static_cast<float>(kGrid[a][1] * d)};
    std::vector<std::vector<ColumnInfo>> colsA(
        static_cast<size_t>(nAnchors),
        std::vector<ColumnInfo>(static_cast<size_t>(cfg.thetaBins)));
    // R8 blur window: integrate inside the kinematic envelope (needs a prediction
    // for the centre; half-width = the R6 envelope, floored to fit the fan).
    const bool  blurOn   = cfg.blurMode && prior.kinematicSigmaRad > 1e-4f;
    const float blurHalf = blurOn
        ? std::max(cfg.envelopeKSigma * prior.kinematicSigmaRad,
                   cfg.predFanHalfRad + 5.f * kPi / 180.f)
        : 0.f;
    const bool blurActive = blurOn && blurHalf > 0.f;
    scanAllAnchors(topP, blackP, diffP, anchors, nAnchors, cfg, thrTop, thrBlack, thrDiff,
                   thetaWeight, rhoScale, nRho, colsA, blurActive,
                   prior.kinematicDirRad, blurHalf);

    // 6. NMS top-K per anchor; keep the best-scoring detection set.
    std::vector<Peak> bestPeaks;
    int               bestA = 0;
    float             bestW = -1.f;
    for (int a = 0; a < nAnchors; ++a) {
        if (blurActive)
            gateBlurEnvelope(colsA[static_cast<size_t>(a)], cfg.thetaBins,
                             prior.kinematicDirRad, blurHalf, cfg.blurSnrMargin);
        std::vector<Peak> peaks = pickPeaks(colsA[static_cast<size_t>(a)], cfg, rhoScale);
        const float w = peaks.empty() ? 0.f : peaks.front().w;
        if (w > bestW) {            // strict '>' — ties prefer the centre anchor
            bestW = w;
            bestA = a;
            bestPeaks = std::move(peaks);
        }
    }
    if (bestPeaks.empty())
        return out;
    const std::vector<ColumnInfo> &bestCols   = colsA[static_cast<size_t>(bestA)];
    const cv::Point2f              bestAnchor = anchors[bestA];

    // 7.–8. Finalise candidates: wedge plateau / twin-horn merge / sub-bin θ
    // (estimateTheta), head-blob seed.
    out.reserve(bestPeaks.size());
    int consumedMateBin = -1;
    for (size_t i = 0; i < bestPeaks.size(); ++i) {
        const Peak &p = bestPeaks[i];
        if (i > 0 && p.bin == consumedMateBin)
            continue;                       // consumed by the winner's wedge merge
        const ColumnInfo &c = bestCols[static_cast<size_t>(p.bin)];
        ShaftCandidate cand;
        cand.score        = p.w;
        cand.visibleLenPx = static_cast<float>(c.end - c.start + 1) * rhoScale;
        cand.darkPolarity = c.dark;
        if (i == 0) {
            ThetaEstimate est = estimateTheta(bestCols, p.bin, p.w, /*allowMate=*/true, cfg);
            consumedMateBin = est.mateBin;
            cand.score += est.mateW;        // combined support of both fan edges
            if (est.wedge && bestA != 0) {
                // A perturbed anchor wins on ridge support, but it skews the
                // angular geometry of a blur fan about the grip — and unlike
                // line candidates a wedge has no TLS correction stage. The
                // pose grip is the unbiased angular origin: re-estimate θ/σ
                // from the centre anchor's profile when it has support
                // (measured: −2.2° systematic from a 3 px winning offset on
                // an 8° fan vs ±0.5° about the true grip).
                const std::vector<ColumnInfo> &cols0 = colsA[0];
                const int n = cfg.thetaBins;
                const int maxSepBins = std::max(2, static_cast<int>(std::lround(
                    cfg.wedgePairMaxSepDeg / 360.f * static_cast<float>(n))));
                int   b0 = -1;
                float w0 = 0.f;
                for (int off = -maxSepBins; off <= maxSepBins; ++off) {
                    const int b = ((p.bin + off) % n + n) % n;
                    const float wb = cols0[static_cast<size_t>(b)].wScore;
                    if (wb > w0) {
                        w0 = wb;
                        b0 = b;
                    }
                }
                if (b0 >= 0 && w0 > 0.f) {
                    const ThetaEstimate e0 = estimateTheta(cols0, b0, w0, true, cfg);
                    est.theta = e0.theta;
                    est.sigma = e0.sigma;
                    est.wedge = est.wedge || e0.wedge;
                }
            }
            cand.thetaRad      = est.theta;
            cand.sigmaThetaRad = est.sigma;
            cand.wedge         = est.wedge;
        } else {
            refineTheta(bestCols, p.bin, cfg, cand.thetaRad, cand.sigmaThetaRad, cand.wedge);
        }
        cand.thetaRad = wrapPi(cand.thetaRad);
        const cv::Mat &resp = c.dark ? black : top;
        const int      thr  = c.dark ? thrBlack : thrTop;
        const float rhoEnd  = (static_cast<float>(c.end) + 0.5f) * rhoScale;
        const cv::Point2f h = headSeed(resp, thr, bestAnchor, cand.thetaRad, rhoEnd,
                                       cfg.headSeedRadiusPx);
        cand.headPx = h + offset;
        out.push_back(cand);
    }

    // 9. TLS line refit of the winning candidate over its proximal ridge
    //    pixels — the effective anchor slides onto the fitted line. Skipped
    //    for wedge winners: a blur fan is not a line, and its θ is by
    //    definition the plateau centroid (B.4), not a pixel fit.
    if (!out.front().wedge) {
        ShaftCandidate &wnr = out.front();
        const ColumnInfo &c = bestCols[static_cast<size_t>(bestPeaks.front().bin)];
        const cv::Mat &resp = c.dark ? black : top;
        const int      thr  = c.dark ? thrBlack : thrTop;
        float theta = wnr.thetaRad;
        if (tlsRefine(resp, thr, bestAnchor, static_cast<float>(c.start) * rhoScale,
                      wnr.visibleLenPx, theta))
            wnr.thetaRad = theta;
    }
    return out;
}

} // namespace pinpoint::analysis
