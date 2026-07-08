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

// Ball Detection v2 — temporal matched-filter core (docs/design/ball_detection_v2.md).
//
// Header-only, OpenCV-only (no Qt) so the standalone test suite in src/Pose/tests
// builds it directly (mirrors ball_model.h). Everything here is a pure function
// over cv::Mat plus one small causal state machine (TemporalBallTracker) — the
// threading, signals, frame plumbing and ROI/pose corridor live in BallDetector
// (V2) and CameraInstance.
//
// This is the C++ port of the research exemplar tools/balllab/ball_state_machine.py.
// The exemplar is the executable spec and the regression oracle; this port MUST
// reproduce it numerically (the parity test src/Pose/tests/ball_temporal_parity_test.cpp
// gates it). The reasoning, the physics and the must-preserve list are documented
// in docs/design/ball_detection_v2_exemplar_explained.md (the "bible", §12). Do NOT
// re-tune the algorithm here: iterate in the python harness, re-parity, re-port.
//
// The one idea, in one line: a golf ball is the only ball-scale feature that
// APPEARS in the hitting area, sits PERFECTLY MOTIONLESS for seconds, then
// VANISHES within two frames. Everything below exploits staticness + disappearance,
// never brightness or colour (a white ball on a bright/clipped mat has near-zero
// per-frame luma contrast — the accumulator is what makes it detectable).
//
//   R      = DoG band-pass at the ball radius, on a PADDED crop, sliced to the ROI
//   A      = fast EMA of R (acquisition accumulator; the static ball reinforces)
//   D      = A - B      accumulated novelty vs the static-scene baseline B
//   N_acc  = D / (1.4826·MAD(D))   novelty in scene-noise units (SEARCH runs on this)
//   LOCKED : monitor the PER-FRAME R at the frozen spot (instant present/absent + cliff)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace pinpoint::balltemporal {

// Physical golf-ball diameter (R&A/USGA spec, 1.68"). The single code constant
// for the ball's real-world size — the px→mm scale primitive at the ball's
// ground-plane depth: mm/px = kBallDiameterMm / (2·radiusPx). Consumed by the
// deferred low-point-ahead-of-ball metric (docs/design/low_point_metric_design.md).
// (Moved here from ball_model.h as part of the v1→v2 migration; ball_model.h is
// slated for deletion in V3.)
inline constexpr double kBallDiameterMm = 42.67;

// ── Tuning constants (design §8 / bible §9; fixed by corpus evidence only) ────
namespace tuning {
inline constexpr float  kAppear          = 5.0f;   // novelty σ for a SEARCH peak to be a candidate
inline constexpr float  kLock            = 5.0f;   // median novelty over T_lock to LOCK
inline constexpr double kTLockS          = 0.5;    // candidate must hold this long (s) to lock
inline constexpr double kLockStabilityPx = 2.0;    // candidate peak must stay within this radius (px)
inline constexpr double kCollapseFloor   = 0.25;   // launch: at-spot R below this·L0 ...
inline constexpr double kCollapseFrom    = 0.80;   // ... having been at/above this·L0 the frame before
inline constexpr int    kCollapseFrames  = 2;      // ... for this many consecutive frames (the cliff)
inline constexpr double kNmsRadiusMult   = 2.0;    // non-max-suppression radius = this·r_hat
inline constexpr double kTAccS           = 0.35;   // acquisition accumulator EMA window (s)
inline constexpr int    kPeaks           = 3;      // concurrent novelty peaks tracked (K_PEAKS)
inline constexpr int    kMaxScanPeaks    = 12;     // local maxima scanned per frame to fill kPeaks blobs
inline constexpr double kBlobMaxElong    = 3.0;    // shape gate: max eigenvalue-ratio (disc vs ridge)
inline constexpr double kBlobSizeLo      = 0.35;   // shape gate: min blob extent vs r/2 (spike reject)
inline constexpr double kBlobSizeHi      = 2.2;    // shape gate: max blob extent vs r/2 (oversize reject)
inline constexpr double kBlobWinMult     = 1.8;    // shape-gate window half-width = this·r_hat
inline constexpr double kRHatPer1280     = 9.5;    // ball radius (px) at 1280px frame width
inline constexpr double kPadMult         = 6.0;    // padded-crop margin = this·r_hat
}  // namespace tuning

// Ball radius estimate in pixels for a given frame width (corpus: r ≈ 9–10 px @1280).
inline double radiusForWidth(int frameWidth)
{
    return tuning::kRHatPer1280 * (double(frameWidth) / 1280.0);
}

// ── Internal helpers ─────────────────────────────────────────────────────────
namespace detail {

// Round-half-to-even to the nearest integer — matches Python 3's round()/int(round()),
// which the exemplar uses for window/nms/lock-frame sizing. Relies on the default
// FE_TONEAREST rounding mode (never changed in this process).
inline int pyRound(double x) { return int(std::nearbyint(x)); }

// numpy-style median of a float vector: for an even count, the mean of the two
// central order statistics (np.median semantics). Modifies `v` (partial sort).
inline double numpyMedian(std::vector<float> &v)
{
    const size_t n = v.size();
    if (n == 0) return 0.0;
    const size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double hi = double(v[mid]);
    if (n & 1u) return hi;
    // even: average the top of the lower half (max of [0,mid)) with v[mid]
    const double lo = double(*std::max_element(v.begin(), v.begin() + mid));
    return 0.5 * (lo + hi);
}

// Same, for a std::vector<double> (candidate novelty history at lock).
inline double numpyMedian(std::vector<double> v)
{
    const size_t n = v.size();
    if (n == 0) return 0.0;
    const size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double hi = v[mid];
    if (n & 1u) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + mid);
    return 0.5 * (lo + hi);
}

// Flatten a CV_32F Mat into a contiguous float vector (row-wise, ROI-safe).
inline std::vector<float> flatten32f(const cv::Mat &m)
{
    std::vector<float> out;
    out.reserve(size_t(m.rows) * size_t(m.cols));
    for (int y = 0; y < m.rows; ++y) {
        const float *row = m.ptr<float>(y);
        out.insert(out.end(), row, row + m.cols);
    }
    return out;
}

// Row-major argmax of a CV_32F Mat, first occurrence of the strict maximum
// (matches numpy.argmax on a C-order array). Returns the (x,y) of the max.
inline cv::Point argmax32f(const cv::Mat &m, float &outVal)
{
    float best = -std::numeric_limits<float>::infinity();
    cv::Point bestpt(0, 0);
    for (int y = 0; y < m.rows; ++y) {
        const float *row = m.ptr<float>(y);
        for (int x = 0; x < m.cols; ++x) {
            if (row[x] > best) { best = row[x]; bestpt = cv::Point(x, y); }
        }
    }
    outVal = best;
    return bestpt;
}

}  // namespace detail

// ── Response math (bible §4) ─────────────────────────────────────────────────

// Scale-matched band-pass at ball radius r (design §4.1). Difference of two
// Gaussians (σ₁ = r/1.6, σ₂ = 3.2·σ₁): responds to ball-scale blobs, rejects
// large-scale illumination structure (shadows, light pools) and fine speckle.
// Insensitive to absolute brightness — it measures local contrast at ball scale.
inline cv::Mat dog(const cv::Mat &gray32, double r)
{
    const double s1 = std::max(1.0, r / 1.6);
    cv::Mat b1, b2;
    cv::GaussianBlur(gray32, b1, cv::Size(0, 0), s1);
    cv::GaussianBlur(gray32, b2, cv::Size(0, 0), s1 * 3.2);
    return b1 - b2;
}

// 1.4826·MAD(R) — a robust (outlier-proof) estimate of the response noise σ.
// Returns 1.0 for a degenerate (flat) frame, matching the exemplar.
inline double robustNoise(const cv::Mat &R)
{
    std::vector<float> v = detail::flatten32f(R);
    if (v.empty()) return 1.0;
    const double med = detail::numpyMedian(v);
    for (float &x : v) x = float(std::abs(double(x) - med));
    const double mad = detail::numpyMedian(v);
    return mad > 0.0 ? 1.4826 * mad : 1.0;
}

// Max response in a (2k+1)² neighbourhood of (x,y) — the LOCKED per-frame
// monitor (design §4.2). Clamped to the Mat bounds.
inline float atSpot(const cv::Mat &R, int x, int y, int k = 2)
{
    const int x0 = std::max(0, x - k), x1 = std::min(R.cols, x + k + 1);
    const int y0 = std::max(0, y - k), y1 = std::min(R.rows, y + k + 1);
    if (x0 >= x1 || y0 >= y1) return 0.0f;
    float best = -std::numeric_limits<float>::infinity();
    for (int yy = y0; yy < y1; ++yy) {
        const float *row = R.ptr<float>(yy);
        for (int xx = x0; xx < x1; ++xx)
            best = std::max(best, row[xx]);
    }
    return best;
}

// 2-D quadratic refine of a discrete peak (cx,cy) over M's 3×3 neighbourhood
// (design §4.4). Returns the sub-pixel (x,y); each offset clamped to ±1.
inline cv::Point2d subpixelPeak(const cv::Mat &M, int cx, int cy)
{
    const int w = M.cols, h = M.rows;
    if (cx <= 0 || cy <= 0 || cx >= w - 1 || cy >= h - 1)
        return cv::Point2d(double(cx), double(cy));
    const double c  = M.at<float>(cy, cx);
    const double xp = M.at<float>(cy, cx + 1), xm = M.at<float>(cy, cx - 1);
    const double yp = M.at<float>(cy + 1, cx), ym = M.at<float>(cy - 1, cx);
    const double dx = xp - xm, dxx = xp - 2.0 * c + xm;
    const double dy = yp - ym, dyy = yp - 2.0 * c + ym;
    double ox = dxx != 0.0 ? -0.5 * dx / dxx : 0.0;
    double oy = dyy != 0.0 ? -0.5 * dy / dyy : 0.0;
    ox = std::max(-1.0, std::min(1.0, ox));
    oy = std::max(-1.0, std::min(1.0, oy));
    return cv::Point2d(cx + ox, cy + oy);
}

// Shape / scale-space test (design §4.4) on the background-subtracted novelty D.
// A golf ball is an ISOTROPIC ball-scale disc; the painted line and the address
// shaft are elongated ridges, and a normalization spike is a near-point. Weight
// the positive novelty in a window by its value and take its 2-D covariance:
// eigenvalue ratio = elongation (disc≈1, ridge≫1); sqrt(smaller eigenvalue) ≈
// blob radius (≈ r/2 for a ball, ≈0 for a spike). AMPLITUDE-INVARIANT — passes a
// weak but round ball that an amplitude-sensitive ring test wrongly rejects.
inline bool isBlob(const cv::Mat &D, int cx, int cy, double r,
                   double maxElong = tuning::kBlobMaxElong,
                   double sizeLo   = tuning::kBlobSizeLo,
                   double sizeHi   = tuning::kBlobSizeHi)
{
    const int w = D.cols, h = D.rows;
    const int win = detail::pyRound(tuning::kBlobWinMult * r);
    if (cx - win < 0 || cy - win < 0 || cx + win >= w || cy + win >= h)
        return false;

    // Value-weighted 2-D second moments of P = max(D, 0) over the window.
    double m = 0.0, sx = 0.0, sy = 0.0;
    for (int yy = -win; yy <= win; ++yy) {
        const float *row = D.ptr<float>(cy + yy);
        for (int xx = -win; xx <= win; ++xx) {
            const double p = std::max(0.0f, row[cx + xx]);
            if (p <= 0.0) continue;
            const double lx = double(xx + win), ly = double(yy + win);
            m += p; sx += p * lx; sy += p * ly;
        }
    }
    if (m <= 1e-6) return false;
    const double mx = sx / m, my = sy / m;
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int yy = -win; yy <= win; ++yy) {
        const float *row = D.ptr<float>(cy + yy);
        for (int xx = -win; xx <= win; ++xx) {
            const double p = std::max(0.0f, row[cx + xx]);
            if (p <= 0.0) continue;
            const double lx = double(xx + win) - mx, ly = double(yy + win) - my;
            sxx += p * lx * lx; syy += p * ly * ly; sxy += p * lx * ly;
        }
    }
    sxx /= m; syy /= m; sxy /= m;
    const double tr = sxx + syy;
    const double disc = std::max(0.0, (tr / 2.0) * (tr / 2.0) - (sxx * syy - sxy * sxy));
    const double l1 = tr / 2.0 + std::sqrt(disc);
    const double l2 = tr / 2.0 - std::sqrt(disc);
    if (l2 <= 1e-6) return false;
    const double elong = std::sqrt(l1 / l2);
    const double size  = std::sqrt(l2) / (r / 2.0);
    return elong <= maxElong && size >= sizeLo && size <= sizeHi;
}

// Padded-crop response for a hitting-area ROI within a full-frame gray image
// (design §4.1 / bible §12.1). Computes the DoG on the ROI grown by a margin
// (kPadMult·r_hat, clamped to the frame) then slices out the ROI interior — so
// the GaussianBlur crop-boundary artifact (which otherwise mislocks onto the
// shaft where it enters the ROI top) never reaches the search region. The
// padding is NOT optional. `roi` is in full-frame pixels; the returned R is
// roi.height × roi.width, CV_32F.
inline cv::Mat paddedResponse(const cv::Mat &grayFull32, const cv::Rect &roi, double rHat)
{
    const int W = grayFull32.cols, H = grayFull32.rows;
    const int x0 = std::max(0, roi.x), y0 = std::max(0, roi.y);
    const int x1 = std::min(W, roi.x + roi.width), y1 = std::min(H, roi.y + roi.height);
    const int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0) return cv::Mat();

    const int pad = int(std::ceil(tuning::kPadMult * rHat));
    const int px0 = std::max(0, x0 - pad), py0 = std::max(0, y0 - pad);
    const int px1 = std::min(W, x1 + pad), py1 = std::min(H, y1 + pad);
    const int ox = x0 - px0, oy = y0 - py0;

    // .clone() ISOLATES the padded crop: cv::GaussianBlur on a bare submatrix
    // reads the parent's pixels beyond the ROI border (not isolated), which would
    // both defeat the padding and diverge from the python exemplar — whose crop
    // (cv2.cvtColor of a slice) is a fresh array that reflects at its own border.
    // Cloning makes the DoG reflect at the padded border exactly as python does.
    const cv::Mat crop = grayFull32(cv::Rect(px0, py0, px1 - px0, py1 - py0)).clone();
    const cv::Mat Rpad = dog(crop, rHat);
    return Rpad(cv::Rect(ox, oy, bw, bh)).clone();
}

// ── State machine (design §4.2) ──────────────────────────────────────────────

// Incremental, causal SEARCH→CANDIDATE→LOCKED→VANISHED tracker over an ROI.
// Frames are pushed one at a time as the precomputed band DoG response R (the
// caller owns the ROI + padding — see paddedResponse). The baseline B and a
// fallback noise scale are supplied at construction (offline B is seeded from the
// ball-absent tail; live B is a slow EMA of the between-shots empty mat — the
// two differ but parity is measured given the SAME B, bible §12).
//
// SEARCH tracks up to kPeaks novelty peaks concurrently: at address the chrome
// clubhead is also novel and can be the momentary brightest peak, but it MOVES,
// so its candidate keeps resetting while the stationary ball's candidate accrues
// hold and locks first. A single-argmax tracker mislocks onto the club.
class TemporalBallTracker {
public:
    enum class State { Search, Candidate, Locked, Vanished };

    struct Lock {
        bool  valid = false;
        int   idx = -1;         // frame index at acquisition
        double x = 0.0, y = 0.0; // sub-pixel centre in ROI-local pixels
        int   ix = 0, iy = 0;   // integer monitor spot
        float L0 = 0.0f;        // pre-launch at-spot level (for the cliff test)
        double medN = 0.0;      // median novelty of the winning candidate at lock
    };
    struct Launch {
        bool  valid = false;
        int   idx = -1;         // first below-floor frame (the collapse)
        double x = 0.0, y = 0.0;
    };

    TemporalBallTracker(double rHat, double fps, const cv::Mat &baseline, double noiseScale)
        : r_(rHat), fps_(fps), noise0_(noiseScale)
    {
        baseline.convertTo(B_, CV_32F);
        tLockFrames_ = std::max(1, detail::pyRound(tuning::kTLockS * fps));
        nms_         = std::max(2, detail::pyRound(tuning::kNmsRadiusMult * rHat));
        af_          = std::min(1.0, 1.0 / std::max(1.0, tuning::kTAccS * fps));
    }

    // Launches before this frame index are counted as false (during-address) —
    // offline gate bookkeeping only. -1 disables the check (default).
    void setAddressEndIdx(int idx) { addressEndIdx_ = idx; }

    State   state() const { return state_; }
    const Lock   &locked()   const { return locked_; }
    const Launch &launched() const { return launched_; }
    int     falseLaunches() const { return falseLaunches_; }
    int     frameIndex()    const { return idx_; }

    // Advance one frame with the band DoG response R (caller computes it on a
    // padded crop, sliced to the ROI interior). Acquisition integrates the
    // static ball over a short causal window (A, a fast EMA of R); SEARCH/lock
    // run on the accumulated novelty (A - B). Once LOCKED, monitoring reads the
    // PER-FRAME R at the locked spot so presence + the launch collapse stay
    // instant. Returns the state after this frame.
    State push(const cv::Mat &R)
    {
        ++idx_;
        if (A_.empty()) {
            R.convertTo(A_, CV_32F);
        } else {
            // Fast-EMA accumulator A += af·(R − A), computed in FLOAT32 to match
            // numpy's weak-scalar semantics exactly. OpenCV's `double * Mat` path
            // would apply af in double precision; over hundreds of frames of EMA
            // feedback that ~1-ULP-per-step drift accumulates and shifts the lock
            // frame vs the python oracle (measured on the saturated corpus case).
            const float af32 = float(af_);
            for (int y = 0; y < A_.rows; ++y) {
                float *a = A_.ptr<float>(y);
                const float *r = R.ptr<float>(y);
                for (int x = 0; x < A_.cols; ++x)
                    a[x] = a[x] + af32 * (r[x] - a[x]);
            }
        }

        if (state_ == State::Search || state_ == State::Candidate) {
            cv::Mat D = A_ - B_;                         // accumulated novelty (raw, float32)
            double noise = robustNoise(D);
            if (noise <= 0.0) noise = noise0_;
            // Nacc = D / noise, float32 divide (numpy weak-scalar) — same reason.
            const float noise32 = float(noise);
            cv::Mat Nacc(D.size(), CV_32F);
            for (int y = 0; y < D.rows; ++y) {
                const float *d = D.ptr<float>(y);
                float *nn = Nacc.ptr<float>(y);
                for (int x = 0; x < D.cols; ++x)
                    nn[x] = d[x] / noise32;
            }
            searchStep(R, D, Nacc);
        } else if (state_ == State::Locked) {
            lockedStep(R);                               // per-frame monitor: instant
        }
        // Vanished is terminal for a window (offline gate does not re-acquire).
        return state_;
    }

private:
    struct Cand { double x = 0.0, y = 0.0; int hold = 0; std::vector<double> Ns; };

    // Local maxima of the accumulated novelty Nacc above kAppear (NMS radius
    // nms_), keeping only those whose novelty D is a ball-scale disc (isBlob).
    // Scans up to kMaxScanPeaks maxima to fill kPeaks blobs — a ridge distractor
    // is suppressed and skipped rather than occupying a candidate slot the ball
    // needs. Matches the exemplar's _find_peaks exactly (suppress either way).
    std::vector<cv::Vec3d> findPeaks(const cv::Mat &Nacc, const cv::Mat &D)
    {
        cv::Mat M = Nacc.clone();
        std::vector<cv::Vec3d> out;                      // (x, y, novelty)
        for (int s = 0; s < tuning::kMaxScanPeaks; ++s) {
            float v = 0.0f;
            const cv::Point pk = detail::argmax32f(M, v);
            if (v < tuning::kAppear) break;
            cv::circle(M, pk, nms_, cv::Scalar(-1e9), -1);   // suppress either way
            if (isBlob(D, pk.x, pk.y, r_)) {
                out.emplace_back(double(pk.x), double(pk.y), double(v));
                if (int(out.size()) >= tuning::kPeaks) break;
            }
        }
        return out;
    }

    void searchStep(const cv::Mat &R, const cv::Mat &D, const cv::Mat &Nacc)
    {
        const std::vector<cv::Vec3d> peaks = findPeaks(Nacc, D);
        const double stab2 = tuning::kLockStabilityPx * tuning::kLockStabilityPx;
        std::vector<char> used(peaks.size(), 0);
        std::vector<Cand> kept;

        // Continue existing candidates that reappear within the stability radius
        // (greedy, in current-candidate order — matches the exemplar).
        for (Cand &c : cands_) {
            int best = -1; double bestd = stab2 + 1e-9;
            for (size_t i = 0; i < peaks.size(); ++i) {
                if (used[i]) continue;
                const double dx = peaks[i][0] - c.x, dy = peaks[i][1] - c.y;
                const double d = dx * dx + dy * dy;
                if (d <= stab2 && d < bestd) { bestd = d; best = int(i); }
            }
            if (best >= 0) {
                used[best] = 1;
                c.x = peaks[best][0]; c.y = peaks[best][1];
                c.hold += 1;
                c.Ns.push_back(peaks[best][2]);
                kept.push_back(std::move(c));
            }
            // else: candidate not seen this frame -> dropped (it moved / vanished)
        }
        // Start new candidates for unmatched peaks (in peak order).
        for (size_t i = 0; i < peaks.size(); ++i) {
            if (!used[i]) {
                Cand nc; nc.x = peaks[i][0]; nc.y = peaks[i][1]; nc.hold = 1;
                nc.Ns.push_back(peaks[i][2]);
                kept.push_back(std::move(nc));
            }
        }
        // Stable sort by hold descending (ties keep prior order — matches Python's
        // stable sort), keep the top kPeaks.
        std::stable_sort(kept.begin(), kept.end(),
                         [](const Cand &a, const Cand &b) { return a.hold > b.hold; });
        if (int(kept.size()) > tuning::kPeaks) kept.resize(tuning::kPeaks);
        cands_ = std::move(kept);
        state_ = cands_.empty() ? State::Search : State::Candidate;

        // Lock the longest-held candidate meeting the criteria (the stationary ball).
        for (const Cand &c : cands_) {
            if (c.hold >= tLockFrames_ && detail::numpyMedian(c.Ns) >= tuning::kLock) {
                const int cx = detail::pyRound(c.x), cy = detail::pyRound(c.y);
                const cv::Point2d f = subpixelPeak(Nacc, cx, cy);
                L0_ = atSpot(R, cx, cy);
                locked_ = Lock{true, idx_, f.x, f.y, cx, cy, L0_, detail::numpyMedian(c.Ns)};
                state_ = State::Locked;
                lhist_.clear();
                lhist_.emplace_back(idx_, L0_);
                break;
            }
        }
    }

    // Cliff test (design §4.5): at-spot R < kCollapseFloor·L0 for kCollapseFrames
    // consecutive frames, the frame BEFORE the run being ≥ kCollapseFrom·L0.
    // Partial/occlusion dips never reach the floor for two frames, so they are
    // ignored without a separate hysteresis counter.
    void lockedStep(const cv::Mat &R)
    {
        const float L = atSpot(R, locked_.ix, locked_.iy);
        lhist_.emplace_back(idx_, L);
        if (int(lhist_.size()) < tuning::kCollapseFrames + 1) return;

        const size_t base = lhist_.size() - (tuning::kCollapseFrames + 1);
        const float preL = lhist_[base].second;
        bool allBelow = true;
        for (int j = 1; j <= tuning::kCollapseFrames; ++j)
            if (lhist_[base + j].second >= tuning::kCollapseFloor * L0_) { allBelow = false; break; }

        if (preL >= tuning::kCollapseFrom * L0_ && allBelow) {
            const int firstCollapse = lhist_[base + 1].first;
            launched_ = Launch{true, firstCollapse, locked_.x, locked_.y};
            if (addressEndIdx_ >= 0 && firstCollapse < addressEndIdx_) ++falseLaunches_;
            state_ = State::Vanished;
        }
    }

    // params
    double r_, fps_, noise0_;
    cv::Mat B_;
    int    tLockFrames_ = 1, nms_ = 2;
    double af_ = 1.0;
    int    addressEndIdx_ = -1;

    // state
    State  state_ = State::Search;
    int    idx_ = -1;
    cv::Mat A_;                        // acquisition accumulator (fast EMA of R)
    std::vector<Cand> cands_;          // up to kPeaks
    Lock   locked_;
    Launch launched_;
    int    falseLaunches_ = 0;
    float  L0_ = 0.0f;
    std::vector<std::pair<int, float>> lhist_;   // (idx, at-spot L) while LOCKED
};

}  // namespace pinpoint::balltemporal
