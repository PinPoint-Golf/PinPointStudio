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

// Standalone tests for the environment-calibrated ball detection core
// (src/Pose/ball_model.h — docs/design/ball_detection_calibration.md §9
// tests 1-4): background/ball model fitting, threshold derivation, detection
// scoring (incl. THE dim-studio case the V>=170 white mask fails), gain
// compensation, drift severity.

#include "../ball_model.h"

#include <cstdio>
#include <opencv2/imgproc.hpp>

using namespace pinpoint::ballcal;

static int g_fail = 0;

#define CHECK(label, cond)                                                    \
    do {                                                                      \
        const bool ok = (cond);                                               \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);              \
        if (!ok) ++g_fail;                                                    \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                     \
    do {                                                                      \
        const double g = (double)(got), w = (double)(want), t = (double)(tol);\
        const bool ok = std::abs(g - w) <= t;                                 \
        std::printf("  [%s] %-44s got %.3f  want %.3f±%.3f\n",                \
                    ok ? "PASS" : "FAIL", label, g, w, t);                    \
        if (!ok) ++g_fail;                                                    \
    } while (0)

// ── Synthetic scene generation (deterministic) ──────────────────────────────

static constexpr int kW = 320, kH = 240;

// Flat-luma BGR background + gaussian sensor noise (deterministic seed).
static cv::Mat makeFrame(double luma, double noiseSigma, uint64_t seed,
                         double gradient = 0.0)
{
    cv::Mat f(kH, kW, CV_8UC3);
    cv::RNG rng(seed);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const double base = luma + gradient * x / kW;
            for (int c = 0; c < 3; ++c) {
                const double v = base + rng.gaussian(noiseSigma);
                f.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uchar>(v);
            }
        }
    }
    return f;
}

static void drawBall(cv::Mat &f, cv::Point2f center, float radius, double luma)
{
    cv::circle(f, cv::Point(cvRound(center.x), cvRound(center.y)),
               cvRound(radius), cv::Scalar(luma, luma, luma), cv::FILLED, cv::LINE_AA);
}

// A club-shaft-like intrusion: a bright elongated rotated bar.
static void drawShaft(cv::Mat &f, double luma)
{
    cv::RotatedRect bar(cv::Point2f(kW / 2.f, kH / 2.f), cv::Size2f(220.f, 9.f), 30.f);
    cv::Point2f pts[4];
    bar.points(pts);
    cv::Point ipts[4];
    for (int i = 0; i < 4; ++i) ipts[i] = pts[i];
    cv::fillConvexPoly(f, ipts, 4, cv::Scalar(luma, luma, luma), cv::LINE_AA);
}

// Multiplicative exposure change — what camera auto-exposure approximates.
static cv::Mat scaleExposure(const cv::Mat &f, double k)
{
    cv::Mat out;
    f.convertTo(out, CV_8UC3, k, 0.0);
    return out;
}

struct CalibScene {
    BackgroundModel bg;
    BallModel       ball;
    double          theta = 0.0, margin = 0.0;
    bool            pass = false;
};

// Full synthetic calibration at a given illumination: fit bg from one block of
// empty frames, ball from ball frames, score a SECOND empty block (never the
// fitting block) + the ball frames, derive theta.
static CalibScene calibrate(double bgLuma, double ballLuma, double noise,
                            cv::Point2f at = {kW / 2.f, kH / 2.f}, float radius = 20.f)
{
    CalibScene s;
    for (int i = 0; i < 12; ++i)
        s.bg.accumulate(makeFrame(bgLuma, noise, 100 + i));
    if (!s.bg.finalize()) return s;

    std::vector<cv::Mat> ballFrames;
    for (int i = 0; i < 12; ++i) {
        cv::Mat f = makeFrame(bgLuma, noise, 200 + i);
        drawBall(f, at, radius, ballLuma);
        ballFrames.push_back(f);
    }
    s.ball = fitBallModel(ballFrames, s.bg);
    if (!s.ball.valid) return s;

    std::vector<double> ballScores, emptyScores;
    for (const auto &f : ballFrames)
        ballScores.push_back(detect(f, s.bg, s.ball, 2.0).score);
    for (int i = 0; i < 12; ++i)
        emptyScores.push_back(detect(makeFrame(bgLuma, noise, 300 + i), s.bg, s.ball, 2.0).score);

    const ThresholdResult t = deriveThreshold(ballScores, emptyScores);
    s.theta = t.theta; s.margin = t.margin; s.pass = t.pass;
    return s;
}

// ── Tests ───────────────────────────────────────────────────────────────────

static void testBackgroundModel()
{
    std::printf("background model:\n");

    BackgroundModel bg;
    for (int i = 0; i < 10; ++i)
        bg.accumulate(makeFrame(90.0, 3.0, 42 + i, /*gradient=*/40.0));
    CHECK("finalize succeeds", bg.finalize());
    CHECK("model valid", bg.valid());
    CHECK_NEAR("mean at left edge", bg.meanGray.at<float>(kH / 2, 4), 90.0, 4.0);
    CHECK_NEAR("mean at right edge", bg.meanGray.at<float>(kH / 2, kW - 4), 130.0, 4.0);
    CHECK_NEAR("sigma near sensor noise", bg.sigma.at<float>(kH / 2, kW / 2), 3.0, 1.5);

    // A noise-free capture must still respect the sigma floor.
    BackgroundModel quiet;
    for (int i = 0; i < 6; ++i)
        quiet.accumulate(makeFrame(90.0, 0.0, 7));
    CHECK("quiet finalize", quiet.finalize());
    CHECK_NEAR("sigma floor", quiet.sigma.at<float>(10, 10), tuning::kSigmaFloor, 1e-3);

    BackgroundModel few;
    few.accumulate(makeFrame(90.0, 3.0, 1));
    CHECK("too few frames rejected", !few.finalize());
}

static void testBallModelFit()
{
    std::printf("ball model fitting:\n");

    // Bright studio: bg 90, ball 200.
    CalibScene bright = calibrate(90.0, 200.0, 3.0);
    CHECK("bright: ball model valid", bright.ball.valid);
    CHECK_NEAR("bright: learned radius", bright.ball.radiusPx, 20.0, 2.5);
    CHECK_NEAR("bright: learned colour (B)", bright.ball.colourMean[0], 200.0, 12.0);
    CHECK_NEAR("bright: centre x", bright.ball.calibCenter.x, kW / 2.0, 2.0);

    // THE dim-studio case: bg 60, ball 115 — V≈115 fails today's V>=170 mask.
    CalibScene dim = calibrate(60.0, 115.0, 3.0);
    CHECK("dim studio: ball model valid", dim.ball.valid);
    CHECK_NEAR("dim studio: learned radius", dim.ball.radiusPx, 20.0, 2.5);
    CHECK("dim studio: calibration passes", dim.pass);
    CHECK("dim studio: positive margin", dim.margin >= tuning::kMinMargin);

    // No ball placed → invalid model, no crash.
    std::vector<cv::Mat> empties;
    for (int i = 0; i < 8; ++i) empties.push_back(makeFrame(60.0, 3.0, 900 + i));
    BallModel none = fitBallModel(empties, dim.bg);
    CHECK("no ball: model invalid", !none.valid);
}

static void testThresholdDerivation()
{
    std::printf("threshold derivation:\n");

    ThresholdResult t = deriveThreshold({0.80, 0.85, 0.90}, {0.20, 0.30, 0.25});
    CHECK("clean gap passes", t.pass);
    CHECK_NEAR("theta 40% into the gap", t.theta, 0.30 + 0.40 * 0.50, 1e-9);
    CHECK_NEAR("margin", t.margin, 0.50, 1e-9);

    t = deriveThreshold({0.55, 0.60}, {0.50, 0.45});
    CHECK("thin margin fails", !t.pass);

    t = deriveThreshold({0.45, 0.50}, {0.55, 0.60});
    CHECK("overlap fails", !t.pass);
    CHECK("overlap: theta not below all ball scores", t.theta >= 0.45 - 1e-9);

    t = deriveThreshold({0.30, 0.35}, {0.05});
    CHECK("low ball scores fail kMinBallScore sanity", !t.pass);

    t = deriveThreshold({}, {0.1});
    CHECK("no ball scores fails", !t.pass);

    t = deriveThreshold({0.8, 0.9}, {});
    CHECK("no empty scores still derives", t.pass);
}

static void testDetection()
{
    std::printf("detection scoring (dim studio bg=60 ball=115):\n");

    CalibScene s = calibrate(60.0, 115.0, 3.0);
    CHECK("calibration passes", s.pass);

    // Ball at the calibration spot.
    cv::Mat f = makeFrame(60.0, 3.0, 500);
    drawBall(f, {kW / 2.f, kH / 2.f}, 20.f, 115.0);
    Detection d = detect(f, s.bg, s.ball, s.theta);
    CHECK("ball at calib spot found", d.found);
    CHECK_NEAR("centre x", d.centerPx.x, kW / 2.0, 3.0);
    CHECK_NEAR("radius", d.radiusPx, 20.0, 3.0);

    // Ball moved within the ROI (position must NOT be locked for presence).
    f = makeFrame(60.0, 3.0, 501);
    drawBall(f, {80.f, 70.f}, 20.f, 115.0);
    d = detect(f, s.bg, s.ball, s.theta);
    CHECK("moved ball found", d.found);
    CHECK_NEAR("moved centre x", d.centerPx.x, 80.0, 3.0);

    // Empty frame.
    d = detect(makeFrame(60.0, 3.0, 502), s.bg, s.ball, s.theta);
    CHECK("empty frame not found", !d.found);

    // Club-shaft intrusion: bright elongated bar — wrong size+shape, rejected.
    f = makeFrame(60.0, 3.0, 503);
    drawShaft(f, 115.0);
    d = detect(f, s.bg, s.ball, s.theta);
    CHECK("shaft intrusion rejected", !d.found);

    // Ball + shaft both in frame: ball must win.
    f = makeFrame(60.0, 3.0, 504);
    drawShaft(f, 115.0);
    drawBall(f, {80.f, 180.f}, 20.f, 115.0);
    d = detect(f, s.bg, s.ball, s.theta);
    CHECK("ball found despite shaft", d.found);
    CHECK_NEAR("ball (not shaft) selected, x", d.centerPx.x, 80.0, 4.0);

    // Profile/frame size mismatch → clean not-found.
    cv::Mat small(kH / 2, kW / 2, CV_8UC3, cv::Scalar(60, 60, 60));
    d = detect(small, s.bg, s.ball, s.theta);
    CHECK("size mismatch → not found", !d.found);
}

static void testGainInvariance()
{
    std::printf("illumination gain invariance (±30%%):\n");

    CalibScene s = calibrate(60.0, 115.0, 3.0);

    cv::Mat f = makeFrame(60.0, 3.0, 600);
    drawBall(f, {kW / 2.f, kH / 2.f}, 20.f, 115.0);

    Detection dUp = detect(scaleExposure(f, 1.30), s.bg, s.ball, s.theta);
    CHECK("+30% exposure: found", dUp.found);
    CHECK_NEAR("+30%: gain compensates", dUp.gain, 1.0 / 1.30, 0.06);

    Detection dDn = detect(scaleExposure(f, 0.70), s.bg, s.ball, s.theta);
    CHECK("-30% exposure: found", dDn.found);
    CHECK_NEAR("-30%: gain compensates", dDn.gain, 1.0 / 0.70, 0.09);

    // Empty frame at shifted exposure must NOT false-positive.
    Detection dEmpty = detect(scaleExposure(makeFrame(60.0, 3.0, 601), 1.30),
                              s.bg, s.ball, s.theta);
    CHECK("+30% empty: not found", !dEmpty.found);

    std::printf("drift severity:\n");
    CHECK("nominal gain → no drift", driftSeverity(1.0) < tuning::kGainDriftLog);
    CHECK("±30%% gain → no drift flag", driftSeverity(1.0 / 1.30) < tuning::kGainDriftLog);
    CHECK("2x gain → drift flagged", driftSeverity(2.0) > tuning::kGainDriftLog);
    CHECK("0.5x gain → drift flagged", driftSeverity(0.5) > tuning::kGainDriftLog);
}

int main()
{
    testBackgroundModel();
    testBallModelFit();
    testThresholdDerivation();
    testDetection();
    testGainInvariance();

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
