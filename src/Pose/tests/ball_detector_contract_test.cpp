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
// path — disabled, no-ROI, empty frame, and the v2 temporal path (seeding the
// baseline, searching, locked/present, and removal). The shared FrameThrottle
// (consumerCount=2) deadlocks if zero fire and runs ahead if two. The extra
// temporal signals (baselineReady/ballLocked/ballLaunched/exposureWarning) are
// NOT part of the contract and are counted separately.

#include "ball_detector.h"

#include <QCoreApplication>
#include <cstdio>
#include <opencv2/imgproc.hpp>

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

    // 4. No profile → v2 temporal path, still seeding the baseline → found=false.
    spy.reset();
    det.detect(fullFrame(40.0, 3.0, 3));
    CHECK("temporal seeding: 1 signal, found=false",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 5. A ball on a single frame cannot detect (needs a seeded baseline + a
    //    sustained lock) — still exactly one signal, found=false.
    spy.reset();
    {
        cv::Mat f = fullFrame(40.0, 3.0, 4);
        drawBall(f, 200.0);
        det.detect(f);
    }
    CHECK("temporal single frame: 1 signal, found=false",
          spy.total() == 1 && spy.detections == 1 && !spy.lastFound);

    // 6. v2 temporal path end-to-end: seed an empty mat, lock a ball, then clear
    //    it — the throttle stays at exactly one ballDetected per detect() the
    //    whole way through.
    std::printf("\nv2 temporal path — seed / lock / presence / removal:\n");
    BallDetector td;
    int tDet = 0, tSkip = 0, tBaseline = 0, tLocks = 0;
    bool tFound = false;
    BallBaselineSnapshot tSnap;   // last baselineReady payload
    QObject::connect(&td, &BallDetector::ballDetected,
                     [&](const BallDetection &r) { ++tDet; tFound = r.found; });
    QObject::connect(&td, &BallDetector::detectionSkipped, [&]() { ++tSkip; });
    QObject::connect(&td, &BallDetector::baselineReady,
                     [&](const BallBaselineSnapshot &s) { ++tBaseline; tSnap = s; });
    QObject::connect(&td, &BallDetector::ballLocked, [&](float, float, float) { ++tLocks; });
    td.setFrameRate(20.0);        // tracker fps=20 → lock after ~10 frames
    td.setRoi(kRoi);

    // Seed: 30 empty frames (> the fixed seed window) → one signal each, no
    // detection, baselineReady exactly once when the baseline is set.
    int mark = tDet + tSkip;
    for (int i = 0; i < 30; ++i) td.detect(fullFrame(60.0, 3.0, 1000 + i));
    CHECK("seeding: one signal per frame", tDet + tSkip - mark == 30 && tSkip == 0);
    CHECK("seeding: baselineReady fired once", tBaseline == 1);
    CHECK("seeding: no detection while learning", !tFound);

    // The snapshot payload: B is the truncation-denormalized ROI (kRoi over the
    // 640x480 frame → 320x240 px), CV_32F, carrying the ROI it was seeded over
    // and a positive noise floor. This is what the exporter persists so offline
    // re-analysis can reconstruct the tracker from the learned baseline.
    CHECK("snapshot: B is ROI-sized CV_32F",
          tSnap.B.rows == kRH && tSnap.B.cols == kRW && tSnap.B.type() == CV_32F);
    CHECK("snapshot: roi matches the set ROI", tSnap.roi == kRoi);
    CHECK("snapshot: noise0 > 0", tSnap.noise0 > 0.0);

    // Ball present: a ball-scale disc (radius ≈ r_hat @640 px) locks + reports present.
    mark = tDet + tSkip;
    bool everFound = false;
    for (int i = 0; i < 40; ++i) {
        cv::Mat f = fullFrame(60.0, 3.0, 2000 + i);
        drawBall(f, 200.0, 5.f);
        td.detect(f);
        if (tFound) everFound = true;
    }
    CHECK("ball present: one signal per frame", tDet + tSkip - mark == 40);
    CHECK("temporal locks + reports present", everFound && tLocks >= 1);

    // Removal: presence returns to false, still one signal per frame.
    mark = tDet + tSkip;
    for (int i = 0; i < 20; ++i) td.detect(fullFrame(60.0, 3.0, 3000 + i));
    CHECK("removal: one signal per frame", tDet + tSkip - mark == 20);
    CHECK("removal: presence cleared", !tFound);

    // A second setRoi (new rect) re-seeds and re-emits the snapshot with the NEW
    // rect + dims (the mat under a new box differs — the baseline is rebuilt).
    std::printf("\nre-seed on ROI change — new rect re-emits the snapshot:\n");
    const QRectF newRoi(0.1, 0.1, 0.4, 0.4);   // px rect (64, 48, 256, 192)
    const int nRW = 256, nRH = 192;
    const int baselineMark = tBaseline;
    td.setRoi(newRoi);
    for (int i = 0; i < 30; ++i) td.detect(fullFrame(60.0, 3.0, 4000 + i));
    CHECK("re-seed: baselineReady fired again", tBaseline == baselineMark + 1);
    CHECK("re-seed: snapshot carries the new rect + dims",
          tSnap.roi == newRoi && tSnap.B.rows == nRH && tSnap.B.cols == nRW);

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
