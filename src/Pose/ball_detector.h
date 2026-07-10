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

#ifdef HAVE_OPENCV

#include "ball_temporal.h"

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>

struct BallDetection {
    bool    found     = false;
    float   x         = 0.f;   // normalized [0,1] within the full frame (centre)
    float   y         = 0.f;
    float   radius    = 0.f;   // radius normalised to frame width
    qint64  detectMs  = 0;     // wall-clock duration of detect() in ms (0 = skipped)
    float   score     = 0.f;   // at-spot response / L0 while present (0 otherwise)
};
Q_DECLARE_METATYPE(BallDetection)

// Snapshot of the learned empty-mat baseline B, captured on the detector thread
// at seed completion (see baselineReady). The exporter persists it per swing so
// offline re-analysis (BallRunner) can reconstruct the TemporalBallTracker from
// the exact baseline the studio session learned, instead of self-seeding over the
// swing window's opening frames — where the ball is already placed.
struct BallBaselineSnapshot {
    cv::Mat B;              // CV_32F, ROI-sized mean DoG response (deep copy)
    double  noise0 = 1.0;   // robustNoise of the last seed frame
    double  rHat   = 0.0;   // radiusForWidth(fw) at seed time
    double  fps    = 0.0;   // m_trackerFps — PROVENANCE ONLY (offline re-measures its own rate)
    QRectF  roi;            // full-frame normalized ROI B was seeded over (m_roi)
    bool valid() const { return !B.empty() && !roi.isEmpty(); }
};
Q_DECLARE_METATYPE(BallBaselineSnapshot)

// Detects a golf ball within the hitting-area ROI using the v2 temporal
// matched-filter core (ball_temporal.h — docs/design/ball_detection_v2.md):
// self-calibrating, no user calibration profile. The empty-mat baseline is
// seeded on demand (setRoi / relearnBaseline, Option A); a placed ball is then
// acquired against it and its presence read per frame.
//
// Receives preprocessed BGR frames from VideoPreprocessorOpenCV. Only searches
// within the ROI supplied via setRoi(); with no ROI it emits detectionSkipped().
//
// Wire into the pipeline and move to a dedicated QThread:
//
//   connect(preprocessor, &VideoPreprocessorOpenCV::framePreprocessed,
//           detector,     &BallDetector::detect,
//           Qt::QueuedConnection);

class BallDetector : public QObject
{
    Q_OBJECT

public:
    explicit BallDetector(QObject *parent = nullptr);

    // Thread-safe master enable — callable directly from any thread (atomic).
    // When disabled, detect() emits detectionSkipped() instead of ballDetected()
    // so the FrameThrottle is released without touching ball-present state
    // (a found=false ballDetected here would fire a spurious ball-lost replay).
    void setEnabled(bool on) { m_enabled.store(on, std::memory_order_relaxed); }
    bool isEnabled() const   { return m_enabled.load(std::memory_order_relaxed); }


public slots:
    // Receives a BGR CV_8UC3 frame from VideoPreprocessorOpenCV.
    // No-op when no ROI is set.
    void detect(const cv::Mat &frame);

    // Update the search region. Safe to call via queued connection. Changing the
    // ROI restarts the empty-mat baseline seed (the mat under a new box differs).
    void setRoi(QRectF roi);

    // Camera frame rate for the temporal tracker's timing (lock/collapse frame
    // counts, accumulator EMA). Changing it re-seeds. Default 100 Hz; the live
    // path self-measures the actual (throttle-gated) rate instead.
    void setFrameRate(double fps);

    // Re-learn the empty-mat baseline B from the next ~1 s of frames (design
    // §4.2 / Option A). The wizard "learn the hitting area" / re-learn action:
    // call with the mat empty; baselineReady() fires when B is set, after which
    // a placed ball is detected. Safe via queued connection.
    void relearnBaseline();

signals:
    void ballDetected(const BallDetection &result);

    // Emitted instead of ballDetected() when the detector is disabled —
    // FrameThrottle connects this to clearBusy() alongside ballDetected().
    void detectionSkipped();

    // ── v2 temporal path signals (additional; NOT part of the throttle contract,
    //    which counts only ballDetected/detectionSkipped) ──────────────────────

    // The empty-mat baseline B has been seeded — the hitting area is "learned"
    // (Option A wizard badge). Detection of a placed ball follows. Carries a
    // deep-copied snapshot of B + its seed context so the exporter can persist it
    // and offline re-analysis can reconstruct the tracker from the learned
    // baseline rather than self-seeding over an already-placed ball.
    void baselineReady(const BallBaselineSnapshot &snap);

    // A stationary ball has been acquired at (x,y) (normalized to the full frame),
    // radiusNorm normalized to frame width. Fires once per acquisition.
    void ballLocked(float x, float y, float radiusNorm);

    // The locked ball was struck — a per-frame collapse cliff (design §4.5).
    // launchAgeUs is how long BEFORE now (this detect() moment) the true impact
    // occurred: (frames since the collapse × frame interval) + the ball-departure
    // latency. The consumer (CameraInstance) converts it to an absolute impact
    // time on the EventBuffer clock and feeds the shot arbiter as a candidate
    // (conf < the self-commit floor, so it can only corroborate IMU/acoustic).
    void ballLaunched(qint64 launchAgeUs, float x, float y);

    // ROI over-exposure health (satFrac = fraction of ROI luma >= 250), edge-
    // triggered on crossing the warn threshold. Drives the wizard exposure hint.
    void exposureWarning(double satFrac);

private:
    // v2 temporal matched-filter path (ball_temporal.h). Emits exactly one
    // ballDetected per call (throttle contract), plus the temporal signals.
    void detectTemporal(const cv::Mat &frame, int rx, int ry, int rw, int rh,
                        int fw, int fh, const QElapsedTimer &t);
    void startSeed();                              // (re)begin the empty-mat baseline seed

    std::atomic<bool> m_enabled{true};

    // Only accessed on the detector's own thread (all slots arrive via queued
    // connections, so they are serialised by the event loop).
    QRectF m_roi;

    // ── v2 temporal path state (detector thread only) ─────────────────────────
    double m_fps = 100.0;            // explicit override (tests / setFrameRate)
    bool   m_fpsExplicit = false;    // true once setFrameRate() is called
    QElapsedTimer m_frameClock;      // measures the actual (throttle-gated) detect rate
    double m_measuredFps = 0.0;      // smoothed measured rate
    double m_trackerFps  = 100.0;    // fps the live tracker was built with
    std::unique_ptr<pinpoint::balltemporal::TemporalBallTracker> m_tracker;
    cv::Mat m_seedAccum;             // CV_64F running sum of R over the seed window
    int     m_seedHave   = 0;
    int     m_seedTarget = 0;        // >0 = seeding the baseline
    double  m_seedNoise  = 1.0;      // fallback noise for the tracker
    bool    m_locked     = false;    // ballLocked emitted for the current acquisition
    bool    m_present    = false;    // last emitted presence
    int     m_absentFrames = 0;      // consecutive absent frames while locked
    bool    m_exposureWarned = false;
};

#endif // HAVE_OPENCV
