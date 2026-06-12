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

// Environment-calibrated ball detection core (docs/design/ball_detection_calibration.md §3).
//
// Header-only, OpenCV-only (no Qt) so the standalone test suite in
// src/Pose/tests builds it directly. Everything here is a pure function over
// cv::Mat — threading, signals, capture orchestration and persistence live in
// BallDetector (B2) and BallCalibrationController (B3).
//
// Replaces absolute thresholds (the V>=170 white mask that fails in dim
// studios) with two models learned in-situ:
//   BackgroundModel — per-pixel mean + luma noise sigma of the EMPTY hitting
//                     area, plus the calibration-time illumination reference.
//   BallModel       — the actual ball's measured radius, colour distribution
//                     and template under the actual lighting.
// The decision threshold theta is DERIVED from the measured score separation
// between ball-present and empty captures (deriveThreshold) — never guessed.
//
// Calibration-side note: score the empty frames used for deriveThreshold from
// a DIFFERENT capture block than the one that fitted the BackgroundModel —
// scoring the fitting frames yields diff≈0 and an optimistically low maxEmpty.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace pinpoint::ballcal {

// ── Tuning constants (fixed by design — only theta is learned) ─────────────
namespace tuning {
// Multi-cue score weights (§3: "score weights are fixed, only theta is derived").
// Contrast is a first-class cue: NCC is contrast-invariant by construction and
// diff-support is self-fulfilling (the blob came from the mask), so without it
// a faint background-texture smudge that is ball-sized and roundish scores
// dangerously high.
// Contrast outranks everything (studio physics: a light blob on the dark
// hitting area can only be the ball; apparent size and shape are fragile
// under lighting changes and attached shadows).
inline constexpr float kWeightContrast   = 0.35f;
inline constexpr float kWeightAppearance = 0.25f;
inline constexpr float kWeightSize       = 0.20f;
inline constexpr float kWeightShape      = 0.10f;
inline constexpr float kWeightDiff       = 0.10f;

inline constexpr double kSigmaFloor      = 2.0;   // luma levels — background can't be "too quiet"
inline constexpr double kDiffSigmaK      = 4.0;   // diff mask threshold, sigma-multiples
inline constexpr double kDiffSigmaRelax  = 2.5;   // retry threshold when 4-sigma finds nothing
// CALIBRATION assumes contrast, never size: the ROI may be tight or wide, so
// the ball may be a handful of pixels or a fifth of the area — the dominant
// CONTRASTING blob is the ball (the user placed it; the background is the
// learned empty area). The floors below only discard sub-morphology noise
// and near-whole-ROI changes (global lighting).
// DETECTION assumes size: all golf balls are 42.67mm, so once calibration
// has measured the pixel radius it is KNOWN — runtime hard-gates candidates
// to [kSizeWindowLo, kSizeWindowHi] x learned radius (a beach-ball-sized
// blob can never be 'the ball' after calibration).
inline constexpr double kMinCandRadius   = 2.5;   // sub-noise floor only
inline constexpr double kMaxCandRadiusRel = 0.45; // vs min ROI side — a whole-ROI change isn't a ball
inline constexpr double kSizeWindowLo    = 0.5;   // runtime size gate vs learned radius
inline constexpr double kSizeWindowHi    = 2.0;
// Contrast is KNOWN after calibration too (the learned ball's mean |ball-bg|):
// runtime candidates below this fraction of it can't be the ball — a faint
// smudge where the ball once sat, background texture, a shadow.
inline constexpr double kContrastWindowLo = 0.4;
// Calibration-side absolute floor: isolation considers HIGH-contrast blobs
// only (the white-ball-on-dark-background assumption) — background texture
// and noise patches are never picked as "the ball".
inline constexpr double kMinCalibContrast = 12.0; // mean luma diff over the blob circle
inline constexpr double kMinMargin       = 0.15;  // robustness floor for PASS
inline constexpr double kThetaBias       = 0.40;  // theta sits 40% into the gap from the empty side
inline constexpr double kMinBallScore    = 0.40;  // sanity: a real ball must score at least this
inline constexpr double kMinRadiusPx     = 2.0;   // runtime candidate floor (noise only)
inline constexpr double kRadiusSigmaRel  = 0.15;  // radius tolerance floor, relative to learned r
inline constexpr double kGainDriftLog    = 0.3001;// |ln(gain)| beyond this = soft drift (≈±35%)
}  // namespace tuning

// ── Models ──────────────────────────────────────────────────────────────────

// Per-pixel statistics of the EMPTY hitting area at calibration illumination.
// accumulate() every empty capture frame, then finalize() once.
struct BackgroundModel {
    cv::Mat meanGray;          // CV_32FC1, ROI resolution
    cv::Mat meanBgr;           // CV_32FC3, ROI resolution
    cv::Mat sigma;             // CV_32FC1, per-pixel luma stddev (>= kSigmaFloor)
    double  calibMedianLuma = 0.0;

    bool valid() const { return !meanGray.empty() && !sigma.empty() && calibMedianLuma > 0.0; }

    // -- accumulation state (calibration only) --
    cv::Mat sum, sumSq, sumBgr;          // CV_64F accumulators
    int     frames = 0;
    std::vector<double> frameMedians;

    void accumulate(const cv::Mat &roiBgr);
    bool finalize();                     // false if too few frames (< 4)
    void resetAccumulation();
};

// The learned appearance of THIS ball under THIS lighting.
struct BallModel {
    bool    valid = false;
    std::string diag;                          // why the fit failed (calibration UI)
    double  radiusPx = 0.0, radiusSigma = 0.0;
    cv::Point2f calibCenter;             // where the ball sat during calibration (px, ROI space)
    cv::Vec3f   colourMean;              // gain-normalised BGR
    cv::Matx33f colourCovInv;
    cv::Mat     template8u;              // gain-normalised gray patch for NCC
    double      minContrast = 0.0;       // mean |ball - background| luma seen at calibration
};

// One detection candidate's per-cue breakdown — preserved for diagnostics
// (the calibration UI's "where did the worst empty score come from").
struct CandidateScore {
    float size = 0.f, shape = 0.f, appearance = 0.f, diffSupport = 0.f, contrast = 0.f;
    float total() const {
        return tuning::kWeightSize * size + tuning::kWeightShape * shape
             + tuning::kWeightAppearance * appearance + tuning::kWeightDiff * diffSupport
             + tuning::kWeightContrast * contrast;
    }
};

struct Detection {
    bool        found = false;
    cv::Point2f centerPx;                // ROI-local pixels
    float       radiusPx = 0.f;
    float       score = 0.f;             // best candidate's total, 0 when no candidates
    CandidateScore cues;                 // best candidate's breakdown
    double      gain = 1.0;              // illumination gain applied this frame
};

// Everything the runtime detector needs, plus the provenance the profile
// persists. Qt-free; (de)serialisation lives with the controller (B3).
struct BallCalProfile {
    int             version = 1;
    bool            valid = false;
    BackgroundModel background;
    BallModel       ball;
    double          theta = 0.0, margin = 0.0;
    double          roiX = 0, roiY = 0, roiW = 0, roiH = 0;   // normalised hitting area
    int             roiPxW = 0, roiPxH = 0;                   // pixel dims at calibration
    std::int64_t    calibratedAtMs = 0;                       // epoch ms (caller stamps)
};

struct ThresholdResult {
    double theta = 0.0, margin = 0.0;
    double minBall = 0.0, maxEmpty = 0.0;   // robust endpoints (for diagnostics)
    bool   pass = false;
};

// ── Internal helpers ────────────────────────────────────────────────────────
namespace detail {

// Median of a CV_32FC1 Mat, stride-subsampled for speed (exact enough for gain).
inline double medianOf(const cv::Mat &m, int stride = 2)
{
    std::vector<float> v;
    v.reserve(static_cast<size_t>((m.rows / stride + 1) * (m.cols / stride + 1)));
    for (int y = 0; y < m.rows; y += stride) {
        const float *row = m.ptr<float>(y);
        for (int x = 0; x < m.cols; x += stride)
            v.push_back(row[x]);
    }
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return static_cast<double>(v[mid]);
}

inline cv::Mat toGray32f(const cv::Mat &bgr8)
{
    cv::Mat gray8, gray32;
    cv::cvtColor(bgr8, gray8, cv::COLOR_BGR2GRAY);
    gray8.convertTo(gray32, CV_32F);
    return gray32;
}

// Illumination gain vs the calibration reference. One refinement pass: the
// first estimate uses the whole-ROI median; the second restricts to pixels
// that look like background under the first gain (robust to the player's
// shadow / the ball itself shifting the global median).
inline double computeGain(const cv::Mat &gray32, const BackgroundModel &bg)
{
    const double med0 = medianOf(gray32);
    if (med0 <= 1e-3 || bg.calibMedianLuma <= 1e-3) return 1.0;
    double g = bg.calibMedianLuma / med0;

    // Refinement over background-classified pixels (stride-sampled).
    std::vector<float> bgPix;
    bgPix.reserve(2048);
    for (int y = 0; y < gray32.rows; y += 3) {
        const float *row  = gray32.ptr<float>(y);
        const float *mrow = bg.meanGray.ptr<float>(y);
        const float *srow = bg.sigma.ptr<float>(y);
        for (int x = 0; x < gray32.cols; x += 3) {
            const double diff = std::abs(row[x] * g - mrow[x]);
            if (diff < tuning::kDiffSigmaK * srow[x])
                bgPix.push_back(row[x]);
        }
    }
    if (bgPix.size() >= 64) {
        const size_t mid = bgPix.size() / 2;
        std::nth_element(bgPix.begin(), bgPix.begin() + mid, bgPix.end());
        const double med1 = static_cast<double>(bgPix[mid]);
        if (med1 > 1e-3)
            g = bg.calibMedianLuma / med1;
    }
    return std::clamp(g, 0.25, 4.0);
}

// Binary mask of pixels deviating from the background by > k*sigma (after gain).
inline cv::Mat diffMask(const cv::Mat &gray32Gained, const BackgroundModel &bg,
                        double k = tuning::kDiffSigmaK)
{
    cv::Mat diff;
    cv::absdiff(gray32Gained, bg.meanGray, diff);
    cv::Mat thresh = bg.sigma * k;
    cv::Mat mask = diff > thresh;                      // CV_8U 0/255

    // Open removes specks, close fills the ball's dimple texture.
    const cv::Mat k3 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    const cv::Mat k5 = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,  k3);
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, k5);
    return mask;
}

// NCC of the gain-normalised gray patch centred on `center` vs the template.
// Returns [0,1] (NCC remapped), 0.5-neutral when the template is unusable.
inline float appearanceScore(const cv::Mat &gray32Gained, cv::Point2f center,
                             const BallModel &ball)
{
    if (ball.template8u.empty()) return 0.5f;
    const int tw = ball.template8u.cols, th = ball.template8u.rows;

    cv::Mat gray8;
    gray32Gained.convertTo(gray8, CV_8U);

    // Replicate-pad so a candidate near the ROI edge still yields a full patch.
    const int pad = std::max(tw, th);
    cv::Mat padded;
    cv::copyMakeBorder(gray8, padded, pad, pad, pad, pad, cv::BORDER_REPLICATE);
    const int x0 = static_cast<int>(std::lround(center.x)) + pad - tw / 2;
    const int y0 = static_cast<int>(std::lround(center.y)) + pad - th / 2;
    if (x0 < 0 || y0 < 0 || x0 + tw > padded.cols || y0 + th > padded.rows) return 0.f;
    cv::Mat patch = padded(cv::Rect(x0, y0, tw, th));

    cv::Mat result;
    cv::matchTemplate(patch, ball.template8u, result, cv::TM_CCOEFF_NORMED);
    const float ncc = result.at<float>(0, 0);
    return std::clamp((ncc + 1.f) * 0.5f, 0.f, 1.f);
}

// Fraction of pixels inside the candidate circle that the diff mask supports.
inline float diffSupportScore(const cv::Mat &mask, cv::Point2f center, float radius)
{
    int inside = 0, set = 0;
    const int x0 = std::max(0, static_cast<int>(center.x - radius));
    const int x1 = std::min(mask.cols - 1, static_cast<int>(center.x + radius));
    const int y0 = std::max(0, static_cast<int>(center.y - radius));
    const int y1 = std::min(mask.rows - 1, static_cast<int>(center.y + radius));
    const float r2 = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        const uchar *row = mask.ptr<uchar>(y);
        for (int x = x0; x <= x1; ++x) {
            const float dx = x - center.x, dy = y - center.y;
            if (dx * dx + dy * dy <= r2) {
                ++inside;
                if (row[x]) ++set;
            }
        }
    }
    return inside > 0 ? static_cast<float>(set) / static_cast<float>(inside) : 0.f;
}

struct Candidate {
    cv::Point2f center;
    float       radius = 0.f;
    float       circularity = 0.f, convexity = 0.f;
};

// Contour-based candidate generation from the diff mask.
inline std::vector<Candidate> contourCandidates(const cv::Mat &mask)
{
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<Candidate> out;
    for (const auto &c : contours) {
        const double area = cv::contourArea(c);
        if (area < tuning::kMinRadiusPx * tuning::kMinRadiusPx * CV_PI * 0.5) continue;

        Candidate cand;
        cv::minEnclosingCircle(c, cand.center, cand.radius);
        if (cand.radius < tuning::kMinRadiusPx) continue;

        const double perim = cv::arcLength(c, true);
        cand.circularity = perim > 1e-3
            ? static_cast<float>(std::clamp(4.0 * CV_PI * area / (perim * perim), 0.0, 1.0))
            : 0.f;

        std::vector<cv::Point> hull;
        cv::convexHull(c, hull);
        const double hullArea = cv::contourArea(hull);
        cand.convexity = hullArea > 1e-3
            ? static_cast<float>(std::clamp(area / hullArea, 0.0, 1.0)) : 0.f;
        out.push_back(cand);
    }
    return out;
}

// Mean |frame - background| within a candidate's inscribed circle — how
// strongly this blob CONTRASTS with the learned empty area.
inline double blobContrast(const cv::Mat &diff32, cv::Point2f center, float radius)
{
    double sum = 0.0; int n = 0;
    const int x0 = std::max(0, static_cast<int>(center.x - radius));
    const int x1 = std::min(diff32.cols - 1, static_cast<int>(center.x + radius));
    const int y0 = std::max(0, static_cast<int>(center.y - radius));
    const int y1 = std::min(diff32.rows - 1, static_cast<int>(center.y + radius));
    const float r2 = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        const float *row = diff32.ptr<float>(y);
        for (int x = x0; x <= x1; ++x) {
            const float dx = x - center.x, dy = y - center.y;
            if (dx * dx + dy * dy <= r2) { sum += row[x]; ++n; }
        }
    }
    return n > 0 ? sum / n : 0.0;
}

// Calibration-time isolation is BLOB detection, not circle detection: the
// background is dark and the user just placed a white ball, so the dominant
// CONTRASTING blob is the ball. Ranking = contrast^2 * radius — contrast
// density dominates (a large but faint region like a shadow or mat shift
// loses to a compact bright blob), size breaks ties. Shape plays NO part:
// at small radii circularity is discretisation noise, and a contact shadow
// merged into the blob distorts it anyway. Cross-frame consensus and the
// validation rounds are the safety net. Only physical implausibility
// rejects: sub-noise specks and near-whole-ROI changes.
inline int pickDominantBlob(const std::vector<Candidate> &cands, const cv::Mat &diff32,
                            double maxRadius, int *sizeRejects = nullptr,
                            int *contrastRejects = nullptr)
{
    int    bestIdx = -1;
    double bestScore = 0.0;
    for (size_t i = 0; i < cands.size(); ++i) {
        const Candidate &c = cands[i];
        if (c.radius < tuning::kMinCandRadius || c.radius > maxRadius) {
            if (sizeRejects) ++*sizeRejects;
            continue;
        }
        const double contrast = blobContrast(diff32, c.center, c.radius);
        if (contrast < tuning::kMinCalibContrast) {
            if (contrastRejects) ++*contrastRejects;
            continue;                       // high-contrast blobs ONLY
        }
        const double score = contrast * contrast * c.radius;
        if (score > bestScore) { bestScore = score; bestIdx = static_cast<int>(i); }
    }
    return bestIdx;
}

}  // namespace detail

// ── BackgroundModel implementation ─────────────────────────────────────────

inline void BackgroundModel::accumulate(const cv::Mat &roiBgr)
{
    cv::Mat gray32 = detail::toGray32f(roiBgr);
    cv::Mat bgr32;
    roiBgr.convertTo(bgr32, CV_32FC3);

    if (sum.empty()) {
        sum    = cv::Mat::zeros(gray32.size(), CV_64F);
        sumSq  = cv::Mat::zeros(gray32.size(), CV_64F);
        sumBgr = cv::Mat::zeros(gray32.size(), CV_64FC3);
        frames = 0;
        frameMedians.clear();
    }
    if (gray32.size() != sum.size()) return;       // frame-size change mid-capture: drop

    cv::Mat g64, b64;
    gray32.convertTo(g64, CV_64F);
    bgr32.convertTo(b64, CV_64FC3);
    sum    += g64;
    sumSq  += g64.mul(g64);
    sumBgr += b64;
    ++frames;
    frameMedians.push_back(detail::medianOf(gray32));
}

inline bool BackgroundModel::finalize()
{
    if (frames < 4 || sum.empty()) return false;

    cv::Mat mean64 = sum / frames;
    cv::Mat var64  = sumSq / frames - mean64.mul(mean64);
    cv::max(var64, 0.0, var64);

    mean64.convertTo(meanGray, CV_32F);
    cv::Mat meanBgr64 = sumBgr / frames;
    meanBgr64.convertTo(meanBgr, CV_32FC3);
    cv::Mat sd64;
    cv::sqrt(var64, sd64);
    cv::max(sd64, tuning::kSigmaFloor, sd64);
    sd64.convertTo(sigma, CV_32F);

    std::vector<double> meds = frameMedians;
    std::nth_element(meds.begin(), meds.begin() + meds.size() / 2, meds.end());
    calibMedianLuma = meds[meds.size() / 2];

    resetAccumulation();
    return calibMedianLuma > 0.0;
}

inline void BackgroundModel::resetAccumulation()
{
    sum.release(); sumSq.release(); sumBgr.release();
    frames = 0;
    frameMedians.clear();
}

// ── Detection ───────────────────────────────────────────────────────────────

// Score a single candidate against the learned models.
inline CandidateScore scoreCandidate(const detail::Candidate &cand,
                                     const cv::Mat &gray32Gained,
                                     const cv::Mat &mask,
                                     const cv::Mat &diff32,
                                     const BallModel &ball)
{
    CandidateScore s;
    const double sigmaR = std::max(ball.radiusSigma, tuning::kRadiusSigmaRel * ball.radiusPx);
    const double dr     = (cand.radius - ball.radiusPx) / std::max(sigmaR, 1e-6);
    s.size        = static_cast<float>(std::exp(-0.5 * dr * dr));
    s.shape       = cand.circularity * cand.convexity;
    s.appearance  = detail::appearanceScore(gray32Gained, cand.center, ball);
    s.diffSupport = detail::diffSupportScore(mask, cand.center, cand.radius);
    // Contrast vs the calibrated ball's (NCC is contrast-invariant, so this
    // is the cue that separates the real ball from a faint look-alike).
    const double c = detail::blobContrast(diff32, cand.center, cand.radius);
    s.contrast    = static_cast<float>(std::clamp(c / std::max(ball.minContrast, 1.0), 0.0, 1.0));
    return s;
}

// The runtime entry point — also used verbatim by calibration to score its
// captures (pass theta = the current candidate threshold; `found` reflects it,
// `score` is always the best candidate's total).
inline Detection detect(const cv::Mat &roiBgr, const BackgroundModel &bg,
                        const BallModel &ball, double theta)
{
    Detection det;
    if (!bg.valid() || !ball.valid || roiBgr.empty()) return det;

    cv::Mat gray32 = detail::toGray32f(roiBgr);
    if (gray32.size() != bg.meanGray.size()) return det;   // profile/ROI mismatch

    det.gain = detail::computeGain(gray32, bg);
    cv::Mat gained = gray32 * det.gain;
    cv::Mat mask   = detail::diffMask(gained, bg);

    auto candidates = detail::contourCandidates(mask);
    if (candidates.empty()) {
        // Nothing above 4-sigma — a ball barely above the noise floor (very
        // dim studios) still shows at the relaxed threshold. Only taken when
        // the strict pass is empty, so a quiet scene costs one extra pass and
        // theta (derived through this same ladder) stays consistent.
        mask       = detail::diffMask(gained, bg, tuning::kDiffSigmaRelax);
        candidates = detail::contourCandidates(mask);
    }

    // Post-calibration the ball's pixel size AND contrast are KNOWN (all golf
    // balls are 42.67mm; calibration measured this setup's radius and the
    // ball's |ball-background| contrast) — hard-gate candidates to both
    // physical windows. A beach-ball-sized blob can never be 'the ball', and
    // neither can a faint smudge however ball-shaped it is.
    const double rLo = tuning::kSizeWindowLo * ball.radiusPx;
    const double rHi = tuning::kSizeWindowHi * ball.radiusPx;
    const double cLo = tuning::kContrastWindowLo * ball.minContrast;
    cv::Mat diff32;
    cv::absdiff(gained, bg.meanGray, diff32);

    float bestTotal = 0.f;
    for (const auto &cand : candidates) {
        if (cand.radius < rLo || cand.radius > rHi) continue;
        if (detail::blobContrast(diff32, cand.center, cand.radius) < cLo) continue;
        const CandidateScore s = scoreCandidate(cand, gained, mask, diff32, ball);
        const float total = s.total();
        if (total > bestTotal) {
            bestTotal     = total;
            det.centerPx  = cand.center;
            det.radiusPx  = cand.radius;
            det.score     = total;
            det.cues      = s;
        }
    }
    det.found = bestTotal > 0.f && bestTotal >= static_cast<float>(theta);
    return det;
}

// ── Calibration-side fitting ────────────────────────────────────────────────

// Learn the ball's appearance from ball-present captures. The ball is
// segmented by background difference (no chicken-and-egg with the detector
// being calibrated). Isolation tries hard (the user is standing there — fail
// honestly, not lazily):
//   1. per frame, EVERY diff blob is considered and the most BALL-LIKE one
//      wins (shape-ranked — a bigger shadow or mat-shift blob can't shadow
//      the ball, and a tee stalk's circularity hit is tolerated);
//   2. a frame with nothing above 4-sigma retries at 2.5-sigma (dim studios);
//   3. per-frame picks must agree ACROSS frames — a positional consensus
//      around the median centre drops stragglers (hand still leaving, etc.);
//   4. failure fills BallModel::diag with which stage rejected what, so the
//      calibration UI can say WHY instead of a generic shrug.
inline BallModel fitBallModel(const std::vector<cv::Mat> &ballFrames,
                              const BackgroundModel &bg)
{
    BallModel ball;
    if (!bg.valid() || ballFrames.empty()) return ball;

    struct Obs { cv::Point2f center; float radius; double gain; int frameIdx; };
    std::vector<Obs> obs;
    int framesNoDiff = 0, sizeRejects = 0, contrastRejects = 0;
    const double maxRadius = tuning::kMaxCandRadiusRel
                             * std::min(bg.meanGray.cols, bg.meanGray.rows);

    for (size_t i = 0; i < ballFrames.size(); ++i) {
        const cv::Mat &f = ballFrames[i];
        if (f.empty()) continue;
        cv::Mat gray32 = detail::toGray32f(f);
        if (gray32.size() != bg.meanGray.size()) continue;

        const double g = detail::computeGain(gray32, bg);
        cv::Mat gained = gray32 * g;
        cv::Mat diff32;
        cv::absdiff(gained, bg.meanGray, diff32);

        int pick = -1;
        std::vector<detail::Candidate> cands;
        for (double k : {tuning::kDiffSigmaK, tuning::kDiffSigmaRelax}) {
            cands = detail::contourCandidates(detail::diffMask(gained, bg, k));
            pick  = detail::pickDominantBlob(cands, diff32, maxRadius, &sizeRejects,
                                             &contrastRejects);
            if (pick >= 0) break;
        }
        if (pick < 0) {
            if (cands.empty()) ++framesNoDiff;
            continue;
        }
        obs.push_back({cands[static_cast<size_t>(pick)].center,
                       cands[static_cast<size_t>(pick)].radius, g, static_cast<int>(i)});
    }

    // Cross-frame positional consensus: the ball is stationary, so the picks
    // must cluster. Median centre/radius, then drop outliers.
    if (obs.size() >= 3) {
        std::vector<float> cxs, cys, rads;
        for (const auto &o : obs) {
            cxs.push_back(o.center.x); cys.push_back(o.center.y); rads.push_back(o.radius);
        }
        std::nth_element(cxs.begin(),  cxs.begin()  + cxs.size() / 2,  cxs.end());
        std::nth_element(cys.begin(),  cys.begin()  + cys.size() / 2,  cys.end());
        std::nth_element(rads.begin(), rads.begin() + rads.size() / 2, rads.end());
        const float medX = cxs[cxs.size() / 2], medY = cys[cys.size() / 2];
        const float tol  = std::max(2.f * rads[rads.size() / 2], 20.f);
        obs.erase(std::remove_if(obs.begin(), obs.end(), [&](const Obs &o) {
            const float dx = o.center.x - medX, dy = o.center.y - medY;
            return dx * dx + dy * dy > tol * tol;
        }), obs.end());
    }

    if (obs.size() < std::max<size_t>(3, ballFrames.size() / 3)) {
        ball.diag = "agreed in " + std::to_string(obs.size()) + "/"
                  + std::to_string(ballFrames.size()) + " frames ("
                  + std::to_string(framesNoDiff) + " saw no change vs the empty area, "
                  + std::to_string(contrastRejects) + " low-contrast rejects, "
                  + std::to_string(sizeRejects) + " size rejects)";
        return ball;
    }

    // Robust radius: median + MAD.
    std::vector<float> radii;
    for (const auto &o : obs) radii.push_back(o.radius);
    std::nth_element(radii.begin(), radii.begin() + radii.size() / 2, radii.end());
    ball.radiusPx = radii[radii.size() / 2];
    std::vector<float> devs;
    for (float r : radii) devs.push_back(std::abs(r - static_cast<float>(ball.radiusPx)));
    std::nth_element(devs.begin(), devs.begin() + devs.size() / 2, devs.end());
    ball.radiusSigma = std::max(1.4826 * devs[devs.size() / 2],
                                tuning::kRadiusSigmaRel * ball.radiusPx);

    // Median centre.
    std::vector<float> xs, ys;
    for (const auto &o : obs) { xs.push_back(o.center.x); ys.push_back(o.center.y); }
    std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
    std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
    ball.calibCenter = {xs[xs.size() / 2], ys[ys.size() / 2]};

    // Colour statistics over ball pixels (gain-normalised), pooled across frames.
    cv::Vec3d mean(0, 0, 0);
    std::vector<cv::Vec3f> samples;
    double contrastSum = 0.0; int contrastN = 0;
    for (const auto &o : obs) {
        const cv::Mat &f = ballFrames[static_cast<size_t>(o.frameIdx)];
        cv::Mat bgr32;
        f.convertTo(bgr32, CV_32FC3);
        bgr32 *= o.gain;
        cv::Mat gray32 = detail::toGray32f(f) * o.gain;

        const float r2 = o.radius * o.radius * 0.64f;   // inner 80% — avoid edge mix
        const int x0 = std::max(0, static_cast<int>(o.center.x - o.radius));
        const int x1 = std::min(bgr32.cols - 1, static_cast<int>(o.center.x + o.radius));
        const int y0 = std::max(0, static_cast<int>(o.center.y - o.radius));
        const int y1 = std::min(bgr32.rows - 1, static_cast<int>(o.center.y + o.radius));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = x - o.center.x, dy = y - o.center.y;
                if (dx * dx + dy * dy > r2) continue;
                const cv::Vec3f px = bgr32.at<cv::Vec3f>(y, x);
                samples.push_back(px);
                mean += cv::Vec3d(px[0], px[1], px[2]);
                contrastSum += std::abs(gray32.at<float>(y, x) - bg.meanGray.at<float>(y, x));
                ++contrastN;
            }
        }
    }
    if (samples.size() < 16) {
        ball.diag = "too few ball pixels to learn its appearance";
        return ball;
    }
    mean *= 1.0 / static_cast<double>(samples.size());
    ball.colourMean = cv::Vec3f(static_cast<float>(mean[0]), static_cast<float>(mean[1]),
                                static_cast<float>(mean[2]));
    ball.minContrast = contrastN > 0 ? contrastSum / contrastN : 0.0;

    cv::Matx33d cov = cv::Matx33d::zeros();
    for (const auto &px : samples) {
        const cv::Vec3d d(px[0] - mean[0], px[1] - mean[1], px[2] - mean[2]);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                cov(r, c) += d[r] * d[c];
    }
    cov *= 1.0 / static_cast<double>(samples.size());
    for (int d = 0; d < 3; ++d) cov(d, d) += 4.0;   // regularise
    const cv::Matx33d covInv = cov.inv(cv::DECOMP_SVD);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            ball.colourCovInv(r, c) = static_cast<float>(covInv(r, c));

    // Template: the observation whose radius is closest to the median, patch
    // of 2r + 50% margin around the centre, gain-normalised gray.
    const Obs *tmplObs = &obs[0];
    for (const auto &o : obs)
        if (std::abs(o.radius - ball.radiusPx) < std::abs(tmplObs->radius - ball.radiusPx))
            tmplObs = &o;
    {
        const cv::Mat &f = ballFrames[static_cast<size_t>(tmplObs->frameIdx)];
        cv::Mat gray32 = detail::toGray32f(f) * tmplObs->gain;
        cv::Mat gray8;
        gray32.convertTo(gray8, CV_8U);
        const int half = static_cast<int>(std::lround(ball.radiusPx * 1.5));
        const int cx = static_cast<int>(std::lround(tmplObs->center.x));
        const int cy = static_cast<int>(std::lround(tmplObs->center.y));
        cv::Mat padded;
        cv::copyMakeBorder(gray8, padded, half, half, half, half, cv::BORDER_REPLICATE);
        ball.template8u = padded(cv::Rect(cx, cy, 2 * half + 1, 2 * half + 1)).clone();
    }

    // The learned radius is whatever the ball measures — tiny is fine, the
    // validation rounds prove (or disprove) reliability at that scale. Only
    // a degenerate sub-noise model is invalid.
    ball.valid = ball.radiusPx >= tuning::kMinCandRadius;
    if (!ball.valid)
        ball.diag = "learned blob is at the noise floor ("
                  + std::to_string(ball.radiusPx).substr(0, 4) + "px radius)";
    return ball;
}

// ── Threshold derivation (§3) ───────────────────────────────────────────────

// theta = maxEmpty + kThetaBias * (minBall - maxEmpty), biased toward the
// empty side because a false "ball present" is worse than slow acquisition.
// ROBUST endpoints: the 10th percentile of the ball scores and the 90th of
// the empty scores (nearest-rank) — a single bad capture frame (auto-exposure
// flicker, the user's shadow crossing) must not define the whole result. With
// fewer than ~10 samples the percentiles equal min/max.
inline ThresholdResult deriveThreshold(const std::vector<double> &ballScores,
                                       const std::vector<double> &emptyScores)
{
    ThresholdResult r;
    if (ballScores.empty()) return r;

    std::vector<double> bs = ballScores;
    std::sort(bs.begin(), bs.end());
    r.minBall = bs[static_cast<size_t>(0.10 * (bs.size() - 1))];

    if (!emptyScores.empty()) {
        std::vector<double> es = emptyScores;
        std::sort(es.begin(), es.end());
        r.maxEmpty = es[static_cast<size_t>(std::ceil(0.90 * (es.size() - 1)))];
    }

    r.margin = r.minBall - r.maxEmpty;
    r.theta  = r.maxEmpty + tuning::kThetaBias * r.margin;
    r.pass   = r.margin >= tuning::kMinMargin && r.minBall >= tuning::kMinBallScore;
    if (!r.pass && r.margin <= 0.0)
        r.theta = r.minBall;   // overlapping distributions: never a confident theta
    return r;
}

// Soft-drift severity for the runtime monitor (§6): |ln(gain)|, flagged when
// beyond kGainDriftLog (≈ ±35% illumination change vs calibration).
inline double driftSeverity(double gain)
{
    return std::abs(std::log(std::max(gain, 1e-6)));
}

}  // namespace pinpoint::ballcal
