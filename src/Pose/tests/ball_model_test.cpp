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

    // No ball placed → invalid model, no crash, and a diagnosis for the UI.
    std::vector<cv::Mat> empties;
    for (int i = 0; i < 8; ++i) empties.push_back(makeFrame(60.0, 3.0, 900 + i));
    BallModel none = fitBallModel(empties, dim.bg);
    CHECK("no ball: model invalid", !none.valid);
    CHECK("no ball: diagnosis filled", !none.diag.empty());
}

static void testBallIsolationTriesHard()
{
    std::printf("ball isolation (shape-ranked, ladder, consensus):\n");

    BackgroundModel bg;
    for (int i = 0; i < 12; ++i) bg.accumulate(makeFrame(60.0, 3.0, 1000 + i));
    bg.finalize();

    // 1. Ball ON A TEE: the stalk merges into the blob and drags circularity
    //    below the old 0.55 gate — must still isolate.
    std::vector<cv::Mat> teeFrames;
    for (int i = 0; i < 12; ++i) {
        cv::Mat f = makeFrame(60.0, 3.0, 1100 + i);
        cv::rectangle(f, cv::Point(kW / 2 - 3, kH / 2 + 16), cv::Point(kW / 2 + 3, kH / 2 + 34),
                      cv::Scalar(115, 115, 115), cv::FILLED);      // tee stalk
        drawBall(f, {kW / 2.f, kH / 2.f}, 20.f, 115.0);
        teeFrames.push_back(f);
    }
    BallModel tee = fitBallModel(teeFrames, bg);
    CHECK("tee: model valid", tee.valid);
    // The stalk merges into the learned blob — radius lands between the bare
    // ball and ball+stalk. Self-consistent: runtime sees the same blob.
    CHECK("tee: radius plausible (ball..ball+stalk)",
          tee.radiusPx >= 18.0 && tee.radiusPx <= 32.0);
    {
        cv::Mat probe = makeFrame(60.0, 3.0, 1150);
        cv::rectangle(probe, cv::Point(kW / 2 - 3, kH / 2 + 16), cv::Point(kW / 2 + 3, kH / 2 + 34),
                      cv::Scalar(115, 115, 115), cv::FILLED);
        drawBall(probe, {kW / 2.f, kH / 2.f}, 20.f, 115.0);
        Detection d = detect(probe, bg, tee, 0.45);
        CHECK("tee: runtime detect on the same scene", d.found);
    }

    // 2. Ball is NOT the largest change: a big IRREGULAR patch (mat shift /
    //    shadow — genuinely jagged, the way real shadows are) coexists — the
    //    ranking must choose the round ball, not the biggest blob. (A big
    //    ROUND coexisting object is inherently ambiguous and is settled by
    //    the validation rounds, not the per-frame pick.)
    std::vector<cv::Mat> distractFrames;
    for (int i = 0; i < 12; ++i) {
        cv::Mat f = makeFrame(60.0, 3.0, 1200 + i);
        // L-shaped region ~4x the ball's footprint, top-left.
        cv::rectangle(f, cv::Point(20, 30), cv::Point(140, 60),
                      cv::Scalar(100, 100, 100), cv::FILLED);
        cv::rectangle(f, cv::Point(20, 60), cv::Point(55, 130),
                      cv::Scalar(100, 100, 100), cv::FILLED);
        drawBall(f, {230.f, 170.f}, 20.f, 115.0);
        distractFrames.push_back(f);
    }
    BallModel dis = fitBallModel(distractFrames, bg);
    CHECK("distractor: model valid", dis.valid);
    CHECK_NEAR("distractor: picked the BALL, x", dis.calibCenter.x, 230.0, 5.0);
    CHECK_NEAR("distractor: picked the BALL, y", dis.calibCenter.y, 170.0, 5.0);

    // 3. A blob with NO meaningful contrast must REFUSE to calibrate —
    //    calibration considers high-contrast blobs only (the ball WILL
    //    contrast with the background; a +10-luma smudge is not a ball).
    std::vector<cv::Mat> faintFrames;
    for (int i = 0; i < 12; ++i) {
        cv::Mat f = makeFrame(60.0, 3.0, 1300 + i);
        drawBall(f, {kW / 2.f, kH / 2.f}, 20.f, 70.0);   // +10 luma — under the floor
        faintFrames.push_back(f);
    }
    BallModel faint = fitBallModel(faintFrames, bg);
    CHECK("low-contrast blob refuses to calibrate", !faint.valid);
    CHECK("low-contrast refusal explains itself",
          faint.diag.find("low-contrast") != std::string::npos);

    // 4. A REAL golf ball at studio distance is only a few pixels — no
    //    absolute size expectation may reject it. Full pipeline at r=4px.
    CalibScene tiny = calibrate(60.0, 140.0, 3.0, {kW / 2.f, kH / 2.f}, 4.f);
    CHECK("tiny ball (4px): model valid", tiny.ball.valid);
    CHECK_NEAR("tiny ball: learned radius", tiny.ball.radiusPx, 4.0, 1.5);
    CHECK("tiny ball: calibration passes", tiny.pass);
    {
        cv::Mat p2 = makeFrame(60.0, 3.0, 1500);
        drawBall(p2, {200.f, 120.f}, 4.f, 140.0);
        Detection d2 = detect(p2, tiny.bg, tiny.ball, tiny.theta);
        CHECK("tiny ball: runtime detect (moved)", d2.found);
        CHECK_NEAR("tiny ball: centre x", d2.centerPx.x, 200.0, 3.0);
    }
    {
        Detection d3 = detect(makeFrame(60.0, 3.0, 1600), tiny.bg, tiny.ball, tiny.theta);
        CHECK("tiny ball: empty frame stays empty", !d3.found);
    }

    // 5. THE canonical studio scene: near-black background, white ball — the
    //    contrast assumption calibration is allowed to make.
    CalibScene studio = calibrate(25.0, 210.0, 3.0);
    CHECK("dark studio + white ball: calibration passes", studio.pass);
    CHECK("dark studio: strong margin", studio.margin >= 0.25);
    {
        cv::Mat p = makeFrame(25.0, 3.0, 1700);
        drawBall(p, {120.f, 90.f}, 20.f, 210.0);
        Detection d4 = detect(p, studio.bg, studio.ball, studio.theta);
        CHECK("dark studio: detect", d4.found);
    }

    // 6. SIZE IS KNOWN AFTER CALIBRATION: a beach-ball-sized bright blob must
    //    be rejected at runtime — the learned radius hard-gates candidates.
    {
        cv::Mat p = makeFrame(25.0, 3.0, 1800);
        drawBall(p, {kW / 2.f, kH / 2.f}, 60.f, 210.0);   // 3x the calibrated ball
        Detection d5 = detect(p, studio.bg, studio.ball, studio.theta);
        CHECK("beach ball rejected post-calibration", !d5.found);
    }
    {
        cv::Mat p = makeFrame(25.0, 3.0, 1900);
        drawBall(p, {kW / 2.f, kH / 2.f}, 7.f, 210.0);    // a third of the size
        Detection d6 = detect(p, studio.bg, studio.ball, studio.theta);
        CHECK("undersized blob rejected post-calibration", !d6.found);
    }

    // 7. CONTRAST IS KNOWN AFTER CALIBRATION: a ball-SIZED, ball-SHAPED blob
    //    at a fraction of the calibrated ball's contrast (a mark on the mat,
    //    background texture, a shadow patch) must be rejected at runtime —
    //    NCC alone cannot reject it (it is contrast-invariant).
    {
        cv::Mat p = makeFrame(25.0, 3.0, 2000);
        drawBall(p, {kW / 2.f, kH / 2.f}, 20.f, 85.0);    // right size, faint (60 vs ball's 185)
        Detection d7 = detect(p, studio.bg, studio.ball, studio.theta);
        CHECK("faint ball-sized smudge rejected post-calibration", !d7.found);
    }

    // 8. NON-UNIFORM background (gradient + noise): calibrates, detects, and
    //    the texture itself never false-positives.
    {
        CalibScene tex;
        for (int i = 0; i < 12; ++i)
            tex.bg.accumulate(makeFrame(35.0, 3.0, 2100 + i, /*gradient=*/50.0));
        tex.bg.finalize();
        std::vector<cv::Mat> ballFrames;
        for (int i = 0; i < 12; ++i) {
            cv::Mat f = makeFrame(35.0, 3.0, 2200 + i, 50.0);
            drawBall(f, {kW / 2.f, kH / 2.f}, 20.f, 200.0);
            ballFrames.push_back(f);
        }
        tex.ball = fitBallModel(ballFrames, tex.bg);
        CHECK("textured bg: ball model valid", tex.ball.valid);
        std::vector<double> bs, es;
        for (const auto &f : ballFrames) bs.push_back(detect(f, tex.bg, tex.ball, 2.0).score);
        for (int i = 0; i < 12; ++i)
            es.push_back(detect(makeFrame(35.0, 3.0, 2300 + i, 50.0), tex.bg, tex.ball, 2.0).score);
        const ThresholdResult t = deriveThreshold(bs, es);
        CHECK("textured bg: calibration passes", t.pass);
        Detection d8 = detect(makeFrame(35.0, 3.0, 2400, 50.0), tex.bg, tex.ball, t.theta);
        CHECK("textured bg: empty frame stays empty", !d8.found);
    }
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

    // Robust endpoints: ONE bad capture frame (occlusion, AE flicker) must
    // not define the result — the 10th/90th percentiles trim it.
    std::vector<double> ball12(12, 0.85);
    ball12[5] = 0.10;                       // single occluded frame
    std::vector<double> empty12(12, 0.05);
    empty12[7] = 0.80;                      // single foot-through-frame
    t = deriveThreshold(ball12, empty12);
    CHECK("single bad ball frame trimmed", t.minBall >= 0.80);
    CHECK("single bad empty frame trimmed", t.maxEmpty <= 0.10);
    CHECK("outlier-resistant derivation passes", t.pass);
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
    testBallIsolationTriesHard();
    testThresholdDerivation();
    testDetection();
    testGainInvariance();

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
