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

// Throttle-contract test for BallDetector (design §9 test 6): every detect()
// call must emit EXACTLY ONE of ballDetected / detectionSkipped on EVERY code
// path — disabled, no-ROI, empty frame, uncalibrated (silent found=false; the
// legacy Hough path is retired), calibrated found / not-found, and the
// profile-geometry-mismatch fallback. The shared FrameThrottle
// (consumerCount=2) deadlocks if zero fire and runs ahead if two fire.

#include "ball_detector.h"

#include <QCoreApplication>
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

// ── Synthetic scenes ────────────────────────────────────────────────────────

static constexpr int kFW = 640, kFH = 480;          // full frame
// ROI (0.25, 0.25, 0.5, 0.5) → px rect (160, 120, 320, 240)
static const QRectF kRoi(0.25, 0.25, 0.5, 0.5);
static constexpr int kRW = 320, kRH = 240;

static cv::Mat fullFrame(double luma, double noise, uint64_t seed)
{
    cv::Mat f(kFH, kFW, CV_8UC3);
    cv::RNG rng(seed);
    for (int y = 0; y < kFH; ++y)
        for (int x = 0; x < kFW; ++x)
            for (int c = 0; c < 3; ++c)
                f.at<cv::Vec3b>(y, x)[c] =
                    cv::saturate_cast<uchar>(luma + rng.gaussian(noise));
    return f;
}

// Ball at the ROI centre (full-frame coords).
static void drawBall(cv::Mat &f, double luma, float radius = 25.f)
{
    cv::circle(f, cv::Point(kFW / 2, kFH / 2), cvRound(radius),
               cv::Scalar(luma, luma, luma), cv::FILLED, cv::LINE_AA);
}

static cv::Mat roiOf(const cv::Mat &full)
{
    return full(cv::Rect(160, 120, kRW, kRH)).clone();
}

// Synthetic dim-studio calibration (bg 60 / ball 115) in ROI space.
static BallCalProfile makeProfile()
{
    BallCalProfile p;
    for (int i = 0; i < 10; ++i)
        p.background.accumulate(roiOf(fullFrame(60.0, 3.0, 100 + i)));
    if (!p.background.finalize()) return p;

    std::vector<cv::Mat> ballFrames;
    for (int i = 0; i < 10; ++i) {
        cv::Mat f = fullFrame(60.0, 3.0, 200 + i);
        drawBall(f, 115.0);
        ballFrames.push_back(roiOf(f));
    }
    p.ball = fitBallModel(ballFrames, p.background);
    if (!p.ball.valid) return p;

    std::vector<double> ballScores, emptyScores;
    for (const auto &f : ballFrames)
        ballScores.push_back(detect(f, p.background, p.ball, 2.0).score);
    for (int i = 0; i < 10; ++i)
        emptyScores.push_back(
            detect(roiOf(fullFrame(60.0, 3.0, 300 + i)), p.background, p.ball, 2.0).score);

    const ThresholdResult t = deriveThreshold(ballScores, emptyScores);
    p.theta  = t.theta;
    p.margin = t.margin;
    p.valid  = t.pass;
    return p;
}

// ── Signal counting harness ─────────────────────────────────────────────────

struct Spy {
    int  detections = 0, skips = 0;
    bool lastFound  = false;

    explicit Spy(BallDetector &d)
    {
        QObject::connect(&d, &BallDetector::ballDetected,
                         [this](const BallDetection &r) { ++detections; lastFound = r.found; });
        QObject::connect(&d, &BallDetector::detectionSkipped,
                         [this]() { ++skips; });
    }
    int  total() const { return detections + skips; }
    void reset() { detections = skips = 0; lastFound = false; }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::printf("throttle contract — exactly one signal per detect():\n");

    BallDetector det;
    Spy spy(det);

    // 1. Disabled → detectionSkipped, ball state untouched.
    det.setRoi(kRoi);
    det.setEnabled(false);
    det.detect(fullFrame(60.0, 3.0, 1));
    CHECK("disabled: 1 signal, skipped", spy.total() == 1 && spy.skips == 1);
    det.setEnabled(true);

    // 2. No ROI → detectionSkipped.
    spy.reset();
    det.setRoi(QRectF());
    det.detect(fullFrame(60.0, 3.0, 2));
    CHECK("no ROI: 1 signal, skipped", spy.total() == 1 && spy.skips == 1);
    det.setRoi(kRoi);

    // 3. Empty frame → detectionSkipped.
    spy.reset();
    det.detect(cv::Mat());
    CHECK("empty frame: 1 signal, skipped", spy.total() == 1 && spy.skips == 1);

    // 4. Uncalibrated, no ball → silent found=false.
    spy.reset();
    det.detect(fullFrame(40.0, 3.0, 3));
    CHECK("uncalibrated miss: 1 signal, found=false",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 5. Uncalibrated, even a BRIGHT ball → still found=false: detection is
    //    calibration-gated (the legacy Hough path is retired — no circles or
    //    presence tings from generic parameters).
    spy.reset();
    {
        cv::Mat f = fullFrame(40.0, 3.0, 4);
        drawBall(f, 200.0);
        det.detect(f);
    }
    CHECK("uncalibrated bright ball: 1 signal, found=false (legacy retired)",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 6. Calibrated path (dim studio: legacy CANNOT see this ball).
    const BallCalProfile profile = makeProfile();
    CHECK("synthetic profile valid", profile.valid);
    det.setProfile(profile);

    spy.reset();
    {
        cv::Mat f = fullFrame(60.0, 3.0, 5);
        drawBall(f, 115.0);
        det.detect(f);
    }
    CHECK("calibrated hit (dim ball): 1 signal, found=true",
          spy.total() == 1 && spy.detections == 1 && spy.lastFound);

    spy.reset();
    det.detect(fullFrame(60.0, 3.0, 6));
    CHECK("calibrated miss: 1 signal, found=false",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 7. Profile geometry mismatch (ROI moved/resized after calibration) →
    //    silent found=false, still exactly one signal.
    det.setProfile(profile);
    det.setRoi(QRectF(0.1, 0.1, 0.3, 0.3));
    spy.reset();
    det.detect(fullFrame(60.0, 3.0, 8));
    CHECK("profile/ROI mismatch: 1 signal, found=false",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 8. Invalid profile via setProfile == clearProfile → silent.
    det.setRoi(kRoi);
    det.setProfile(BallCalProfile{});
    spy.reset();
    {
        cv::Mat f = fullFrame(60.0, 3.0, 9);
        drawBall(f, 115.0);
        det.detect(f);
    }
    CHECK("invalid profile: 1 signal, found=false",
          spy.total() == 1 && !spy.lastFound);

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
