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

// Standalone tests for the Ball Detection v2 temporal matched-filter core
// (src/Pose/ball_temporal.h — docs/design/ball_detection_v2.md §9.2). Exercises
// the pure functions (DoG band-pass, robust noise, is_blob shape gate, sub-pixel
// peak, padded-crop response) and the SEARCH→CANDIDATE→LOCKED→VANISHED state
// machine over deterministic synthetic scenes: appear/persist/vanish on flat/
// noisy/gradient backgrounds, moving-shadow rejection, baseline distractor
// absorption, nudge-vs-launch, scale-space edge rejection, sub-pixel bound.
//
// The NUMERIC-parity gate against the python exemplar is a separate target
// (ball_temporal_parity_test.cpp); this suite pins the algorithm's behaviour on
// synthetic data with no corpus dependency.

#include "../ball_temporal.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <opencv2/imgproc.hpp>

using namespace pinpoint::balltemporal;
using Tracker = TemporalBallTracker;

static int g_fail = 0;

#define CHECK(label, cond)                                                    \
    do {                                                                      \
        const bool ok = (cond);                                              \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);            \
        if (!ok) ++g_fail;                                                  \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                     \
    do {                                                                      \
        const double g = (double)(got), w = (double)(want), t = (double)(tol);\
        const bool ok = std::abs(g - w) <= t;                                \
        std::printf("  [%s] %-40s got %.3f  want %.3f±%.3f\n",              \
                    ok ? "PASS" : "FAIL", label, g, w, t);                  \
        if (!ok) ++g_fail;                                                  \
    } while (0)

// ── Synthetic scene generation (deterministic) ───────────────────────────────

static constexpr int   kW = 160, kH = 160;
static constexpr double kR = 8.0;                 // ball radius (px)
static constexpr double kFps = 20.0;              // t_lock = round(0.5·20) = 10 frames
static constexpr double kBallSig = kR / 1.6;      // scale-matched to the DoG

// Flat (optionally graded) grayscale background + gaussian sensor noise.
static cv::Mat baseGray(double luma, double noiseSigma, uint64_t seed, double gradient = 0.0)
{
    cv::Mat g(kH, kW, CV_32F);
    cv::RNG rng(seed);
    for (int y = 0; y < kH; ++y) {
        float *row = g.ptr<float>(y);
        for (int x = 0; x < kW; ++x)
            row[x] = float(luma + gradient * double(x) / kW + rng.gaussian(noiseSigma));
    }
    return g;
}

// Add a gaussian blob (a bright ball, or a dark shadow if amp<0).
static void addBlob(cv::Mat &g, double cx, double cy, double amp, double sig)
{
    const double twoS2 = 2.0 * sig * sig;
    for (int y = 0; y < g.rows; ++y) {
        float *row = g.ptr<float>(y);
        for (int x = 0; x < g.cols; ++x) {
            const double dx = x - cx, dy = y - cy;
            row[x] += float(amp * std::exp(-(dx * dx + dy * dy) / twoS2));
        }
    }
}

// Add a vertical bright ridge (a "painted line" / shaft distractor), from y0..y1.
static void addRidge(cv::Mat &g, double cx, double amp, double halfWidth, int y0, int y1)
{
    for (int y = std::max(0, y0); y < std::min(g.rows, y1); ++y) {
        float *row = g.ptr<float>(y);
        for (int x = 0; x < g.cols; ++x) {
            const double dx = (x - cx) / halfWidth;
            row[x] += float(amp * std::exp(-0.5 * dx * dx));
        }
    }
}

// Baseline B: mean DoG response over several ball-absent frames (smooths noise),
// exactly the role acceptance.py's tail-seed plays offline.
static cv::Mat seedBaseline(double luma, double noiseSigma, double gradient = 0.0)
{
    cv::Mat acc = cv::Mat::zeros(kH, kW, CV_32F);
    const int n = 8;
    for (int i = 0; i < n; ++i)
        acc += dog(baseGray(luma, noiseSigma, 9000 + i, gradient), kR);
    return acc / n;
}

// ── Test 1: appear / persist / vanish on a flat background ────────────────────
static void testAppearPersistVanish()
{
    std::printf("\n[appear/persist/vanish — flat bg]\n");
    const double luma = 60.0, noise = 2.0, amp = 90.0;
    const cv::Point2d ball(84.0, 92.0);
    cv::Mat B = seedBaseline(luma, noise);

    Tracker trk(kR, kFps, B, 1.0);
    int lockIdx = -1, launchIdx = -1;

    // 40 present frames -> lock
    for (int i = 0; i < 40; ++i) {
        cv::Mat g = baseGray(luma, noise, i + 1);
        addBlob(g, ball.x, ball.y, amp, kBallSig);
        trk.push(dog(g, kR));
        if (lockIdx < 0 && trk.state() == Tracker::State::Locked) lockIdx = trk.frameIndex();
    }
    CHECK("locks on the static ball", trk.locked().valid);
    CHECK("locked (not yet launched)", !trk.launched().valid);
    if (trk.locked().valid) {
        CHECK_NEAR("locked x", trk.locked().x, ball.x, 1.5);
        CHECK_NEAR("locked y", trk.locked().y, ball.y, 1.5);
        CHECK("lock before frame 25", lockIdx >= 0 && lockIdx < 25);
    }

    // remove the ball -> cliff -> launch
    for (int i = 0; i < 10; ++i) {
        cv::Mat g = baseGray(luma, noise, 500 + i);   // no ball
        trk.push(dog(g, kR));
        if (launchIdx < 0 && trk.launched().valid) launchIdx = trk.launched().idx;
    }
    CHECK("launches when the ball vanishes", trk.launched().valid);
    CHECK("launch is a 2-frame cliff after removal", launchIdx >= 40 && launchIdx <= 43);
    CHECK("state is VANISHED", trk.state() == Tracker::State::Vanished);
}

// ── Test 2: noisy background still locks near truth ───────────────────────────
static void testNoisyBackground()
{
    std::printf("\n[noisy bg — still locks]\n");
    const double luma = 70.0, noise = 6.0, amp = 90.0;
    const cv::Point2d ball(90.0, 80.0);
    cv::Mat B = seedBaseline(luma, noise);

    Tracker trk(kR, kFps, B, 1.0);
    for (int i = 0; i < 45; ++i) {
        cv::Mat g = baseGray(luma, noise, 200 + i);
        addBlob(g, ball.x, ball.y, amp, kBallSig);
        trk.push(dog(g, kR));
    }
    CHECK("locks despite sensor noise", trk.locked().valid);
    if (trk.locked().valid) {
        CHECK_NEAR("locked x (noisy)", trk.locked().x, ball.x, 2.0);
        CHECK_NEAR("locked y (noisy)", trk.locked().y, ball.y, 2.0);
    }
}

// ── Test 3: large illumination gradient is band-passed away ───────────────────
static void testGradientRejected()
{
    std::printf("\n[gradient illumination — DoG removes it]\n");
    const double luma = 40.0, noise = 2.0, amp = 90.0, grad = 120.0;
    const cv::Point2d ball(78.0, 86.0);
    cv::Mat B = seedBaseline(luma, noise, grad);

    Tracker trk(kR, kFps, B, 1.0);
    for (int i = 0; i < 40; ++i) {
        cv::Mat g = baseGray(luma, noise, 300 + i, grad);
        addBlob(g, ball.x, ball.y, amp, kBallSig);
        trk.push(dog(g, kR));
    }
    CHECK("locks under a strong gradient", trk.locked().valid);
    if (trk.locked().valid) {
        CHECK_NEAR("locked x (gradient)", trk.locked().x, ball.x, 1.5);
        CHECK_NEAR("locked y (gradient)", trk.locked().y, ball.y, 1.5);
    }
}

// ── Test 4: a sweeping shadow never causes a false lock or launch ─────────────
static void testMovingShadowRejected()
{
    std::printf("\n[moving shadow — no false lock/launch]\n");
    const double luma = 80.0, noise = 2.0, amp = 90.0;
    const cv::Point2d ball(96.0, 84.0);
    cv::Mat B = seedBaseline(luma, noise);

    Tracker trk(kR, kFps, B, 1.0);
    for (int i = 0; i < 45; ++i) {
        cv::Mat g = baseGray(luma, noise, 400 + i);
        addBlob(g, ball.x, ball.y, amp, kBallSig);          // static ball
        addBlob(g, 10.0 + 3.0 * i, 60.0, -70.0, 42.0);      // large dark shadow sweeping across
        trk.push(dog(g, kR));
    }
    CHECK("locks on the ball, not the shadow", trk.locked().valid);
    if (trk.locked().valid) {
        CHECK_NEAR("locked x is the ball", trk.locked().x, ball.x, 2.5);
        CHECK_NEAR("locked y is the ball", trk.locked().y, ball.y, 2.5);
    }
    CHECK("no launch fired by the shadow", !trk.launched().valid);
    CHECK("no false (during-address) launch", trk.falseLaunches() == 0);
}

// ── Test 5: a static distractor in the baseline is absorbed ───────────────────
static void testBaselineLineAbsorption()
{
    std::printf("\n[baseline line absorption]\n");
    const double luma = 55.0, noise = 2.0, amp = 90.0;
    const cv::Point2d ball(100.0, 96.0);
    const double lineX = 40.0;

    // The painted line is present in EVERY frame, including the ones B is seeded
    // from — so it lives in the baseline and cancels in the novelty D.
    cv::Mat B = cv::Mat::zeros(kH, kW, CV_32F);
    for (int i = 0; i < 8; ++i) {
        cv::Mat g = baseGray(luma, noise, 9100 + i);
        addRidge(g, lineX, 100.0, 2.5, 0, kH);
        B += dog(g, kR);
    }
    B /= 8;

    Tracker trk(kR, kFps, B, 1.0);
    for (int i = 0; i < 40; ++i) {
        cv::Mat g = baseGray(luma, noise, 600 + i);
        addRidge(g, lineX, 100.0, 2.5, 0, kH);              // the same static line
        addBlob(g, ball.x, ball.y, amp, kBallSig);          // the novel ball
        trk.push(dog(g, kR));
    }
    CHECK("locks on the ball, not the painted line", trk.locked().valid);
    if (trk.locked().valid) {
        CHECK_NEAR("locked x is the ball", trk.locked().x, ball.x, 2.0);
        CHECK("locked x is not the line", std::abs(trk.locked().x - lineX) > 20.0);
    }
}

// ── Test 6: an occlusion dip is not a launch; a real cliff is ─────────────────
static void testNudgeVsLaunch()
{
    std::printf("\n[nudge (occlusion) vs launch (cliff)]\n");
    const double luma = 60.0, noise = 2.0, amp = 90.0;
    const cv::Point2d ball(84.0, 92.0);
    cv::Mat B = seedBaseline(luma, noise);

    Tracker trk(kR, kFps, B, 1.0);
    auto pushPresent = [&](uint64_t seed) {
        cv::Mat g = baseGray(luma, noise, seed);
        addBlob(g, ball.x, ball.y, amp, kBallSig);
        trk.push(dog(g, kR));
    };
    auto pushAbsent = [&](uint64_t seed) {
        trk.push(dog(baseGray(luma, noise, seed), kR));
    };

    for (int i = 0; i < 30; ++i) pushPresent(700 + i);   // lock
    CHECK("locked before the occlusion test", trk.locked().valid);

    // single-frame dip that recovers -> NOT a launch (needs 2 consecutive)
    pushPresent(800);
    pushAbsent(801);        // one frame occluded
    pushPresent(802);       // recovers
    pushPresent(803);
    CHECK("single-frame occlusion is not a launch", !trk.launched().valid);

    // genuine 2-frame cliff -> launch
    pushPresent(810);       // pre (>= 0.8·L0)
    pushAbsent(811);
    pushAbsent(812);
    CHECK("a 2-frame cliff launches", trk.launched().valid);
}

// ── Test 7: is_blob accepts a disc, rejects ridge/spike; band-pass drops oversize
static void testShapeGate()
{
    std::printf("\n[shape gate — disc vs ridge/spike; band-pass vs oversize]\n");
    const int cx = kW / 2, cy = kH / 2;

    cv::Mat disc = cv::Mat::zeros(kH, kW, CV_32F);
    addBlob(disc, cx, cy, 100.0, kR / 2.0);              // ball-scale disc
    CHECK("accepts a ball-scale disc", isBlob(disc, cx, cy, kR));

    cv::Mat ridge = cv::Mat::zeros(kH, kW, CV_32F);
    addRidge(ridge, cx, 100.0, 1.2, cy - 40, cy + 40);   // long thin vertical ridge (line/shaft)
    CHECK("rejects an elongated ridge", !isBlob(ridge, cx, cy, kR));

    cv::Mat spike = cv::Mat::zeros(kH, kW, CV_32F);
    spike.at<float>(cy, cx) = 100.0f;                    // 1-px hot spike (normalization artifact)
    CHECK("rejects a point spike", !isBlob(spike, cx, cy, kR));

    // Oversize blobs are rejected by the DoG BAND-PASS (not is_blob): a feature
    // far larger than the ball gives a much weaker ball-scale response.
    cv::Mat ballF = baseGray(50.0, 0.0, 1);
    addBlob(ballF, cx, cy, 100.0, kBallSig);
    cv::Mat hugeF = baseGray(50.0, 0.0, 1);
    addBlob(hugeF, cx, cy, 100.0, kR * 4.0);
    const double dBall = std::abs(dog(ballF, kR).at<float>(cy, cx));
    const double dHuge = std::abs(dog(hugeF, kR).at<float>(cy, cx));
    std::printf("      DoG centre response: ball-scale=%.2f oversize=%.2f\n", dBall, dHuge);
    CHECK("band-pass suppresses oversize vs ball-scale", dBall > 1.8 * dHuge);
}

// ── Test 8: sub-pixel quadratic refine is bounded and accurate ────────────────
static void testSubpixelBound()
{
    std::printf("\n[sub-pixel peak — bound + accuracy]\n");
    const int cx = 40, cy = 40;
    const double trueOff = 0.3;

    // A downward parabola whose true peak sits at (cx+trueOff, cy).
    cv::Mat M = cv::Mat::zeros(80, 80, CV_32F);
    for (int y = 0; y < 80; ++y) {
        float *row = M.ptr<float>(y);
        for (int x = 0; x < 80; ++x) {
            const double dx = x - (cx + trueOff), dy = y - cy;
            row[x] = float(100.0 - dx * dx - dy * dy);
        }
    }
    const cv::Point2d f = subpixelPeak(M, cx, cy);
    CHECK_NEAR("sub-pixel x recovers the true offset", f.x, cx + trueOff, 0.1);
    CHECK_NEAR("sub-pixel y stays centred", f.y, double(cy), 0.05);
    CHECK("x offset bounded to ±1", std::abs(f.x - cx) <= 1.0 + 1e-9);
    CHECK("y offset bounded to ±1", std::abs(f.y - cy) <= 1.0 + 1e-9);
}

// ── Test 9: padded-crop response reproduces the reference recipe ──────────────
static void testPaddedCropEdge()
{
    std::printf("\n[padded-crop response — matches the reference recipe]\n");
    // A bright feature near the ROI's top edge (a blob whose centre sits just
    // above it — like the shaft entering the region), so the border handling
    // actually matters here.
    const int roiTop = 60, roiBot = 130, roiX0 = 40, roiX1 = 120;
    cv::Mat full = cv::Mat::zeros(kH, kW, CV_32F);
    addBlob(full, 80.0, roiTop - 6.0, 120.0, kBallSig);

    const cv::Rect roi(roiX0, roiTop, roiX1 - roiX0, roiBot - roiTop);
    const cv::Mat padded = paddedResponse(full, roi, kR);
    CHECK("paddedResponse has ROI dimensions",
          padded.rows == roi.height && padded.cols == roi.width);

    // The reference recipe (acceptance.py / gen_parity_ref.py): DoG on the ROI
    // grown by ceil(kPadMult·r) as an ISOLATED crop, sliced to the ROI interior.
    // paddedResponse must reproduce it byte-for-byte — that IS the parity contract.
    const int pad = int(std::ceil(tuning::kPadMult * kR));
    const int px0 = std::max(0, roi.x - pad), py0 = std::max(0, roi.y - pad);
    const int px1 = std::min(kW, roi.x + roi.width + pad), py1 = std::min(kH, roi.y + roi.height + pad);
    const int ox = roi.x - px0, oy = roi.y - py0;
    const cv::Mat refCrop = full(cv::Rect(px0, py0, px1 - px0, py1 - py0)).clone();
    const cv::Mat refR = dog(refCrop, kR)(cv::Rect(ox, oy, roi.width, roi.height)).clone();

    auto maxAbsDiff = [](const cv::Mat &a, const cv::Mat &b) {
        double mx = 0.0;
        for (int y = 0; y < a.rows; ++y) {
            const float *ra = a.ptr<float>(y), *rb = b.ptr<float>(y);
            for (int x = 0; x < a.cols; ++x) mx = std::max(mx, std::abs(double(ra[x]) - double(rb[x])));
        }
        return mx;
    };
    const double recipeDiff = maxAbsDiff(padded, refR);
    std::printf("      max |paddedResponse − reference recipe| = %.2e\n", recipeDiff);
    CHECK("reproduces the reference padded-crop recipe", recipeDiff < 1e-4);

    // And it genuinely differs from a naive ISOLATED tight crop near the top edge
    // (the padding changes the border handling exactly where the ball can hide).
    const cv::Mat tight = dog(full(roi).clone(), kR);
    double topDiff = 0.0;
    for (int y = 0; y < std::min(3, padded.rows); ++y) {
        const float *rp = padded.ptr<float>(y), *rt = tight.ptr<float>(y);
        for (int x = 0; x < padded.cols; ++x) topDiff = std::max(topDiff, std::abs(double(rp[x]) - double(rt[x])));
    }
    std::printf("      max top-edge |padded − tight| = %.2f\n", topDiff);
    CHECK("padding changes the top-edge response vs a tight crop", topDiff > 1.0);
}

// ── Test 10: robust noise + numpy-median helper sanity ────────────────────────
static void testRobustNoise()
{
    std::printf("\n[robust noise / median helpers]\n");
    // Flat zero response -> mad 0 -> noise falls back to 1.0.
    cv::Mat flat = cv::Mat::zeros(20, 20, CV_32F);
    CHECK_NEAR("flat frame noise falls back to 1.0", robustNoise(flat), 1.0, 1e-9);

    // Even-count numpy median = mean of the two central order statistics.
    std::vector<float> even = {4.f, 1.f, 3.f, 2.f};            // sorted 1,2,3,4 -> 2.5
    CHECK_NEAR("numpyMedian even", detail::numpyMedian(even), 2.5, 1e-9);
    std::vector<float> odd = {5.f, 1.f, 3.f};                  // sorted 1,3,5 -> 3
    CHECK_NEAR("numpyMedian odd", detail::numpyMedian(odd), 3.0, 1e-9);

    // pyRound is round-half-to-even.
    CHECK("pyRound(2.5)=2 (half-even)", detail::pyRound(2.5) == 2);
    CHECK("pyRound(3.5)=4 (half-even)", detail::pyRound(3.5) == 4);
    CHECK("pyRound(17.1)=17", detail::pyRound(17.1) == 17);
}

int main()
{
    std::printf("=== ball_temporal_test (v2 temporal matched filter) ===\n");
    testAppearPersistVanish();
    testNoisyBackground();
    testGradientRejected();
    testMovingShadowRejected();
    testBaselineLineAbsorption();
    testNudgeVsLaunch();
    testShapeGate();
    testSubpixelBound();
    testPaddedCropEdge();
    testRobustNoise();

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
