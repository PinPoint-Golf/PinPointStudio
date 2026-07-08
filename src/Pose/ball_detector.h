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

#include "ball_model.h"
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
    float   score     = 0.f;   // calibrated path: best candidate's multi-cue total
                               // (also when found=false); legacy path: 0
};
Q_DECLARE_METATYPE(BallDetection)
Q_DECLARE_METATYPE(pinpoint::ballcal::BallCalProfile)

// Detects golf balls within the hitting-area ROI.
//
// Detection requires a calibration profile (docs/design/ball_detection_calibration.md):
// background-difference + multi-cue scoring against the learned models
// (ball_model.h), with the per-frame illumination drift monitor. WITHOUT a
// profile the detector emits found=false — the legacy white-HSV/Hough/blob
// path was retired (it was unusable in dim studios and its generic-parameter
// false positives were noise); calibration is THE way to enable detection.
//
// Receives preprocessed BGR frames from VideoPreprocessorOpenCV.
// Only searches within the ROI supplied via setRoi(); if no ROI is set
// the detector is silent (emits nothing).
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
    // counts, accumulator EMA). Changing it re-seeds. Default 100 Hz.
    void setFrameRate(double fps);

    // Re-learn the empty-mat baseline B from the next ~1 s of frames (design
    // §4.2 / Option A). The wizard "learn the hitting area" / re-learn action:
    // call with the mat empty; baselineReady() fires when B is set, after which
    // a placed ball is detected. Safe via queued connection.
    void relearnBaseline();

    // Swap in / drop the learned calibration profile. Safe to call via queued
    // connection. An invalid profile is equivalent to clearProfile().
    void setProfile(const pinpoint::ballcal::BallCalProfile &profile);
    void clearProfile();

    // Calibration capture mode (BallCalibrationController only). While armed,
    // each detect() ALSO emits a cloned ROI mat via calibFrame() — detection
    // and the throttle contract continue untouched (overlays stay live).
    // targetFrames > 0: self-disarms after that many frames (calibCaptureDone).
    // targetFrames <= 0: open-ended — streams until cancelCalibCapture()
    // (calibFrame's target arrives as -1); the controller gates on scene
    // stillness and does its own counting.
    void beginCalibCapture(int targetFrames);
    void cancelCalibCapture();

signals:
    void ballDetected(const BallDetection &result);

    // Emitted instead of ballDetected() when the detector is disabled —
    // FrameThrottle connects this to clearBusy() alongside ballDetected().
    void detectionSkipped();

    // ── v2 temporal path signals (additional; NOT part of the throttle contract,
    //    which counts only ballDetected/detectionSkipped) ──────────────────────

    // The empty-mat baseline B has been seeded — the hitting area is "learned"
    // (Option A wizard badge). Detection of a placed ball follows.
    void baselineReady();

    // A stationary ball has been acquired at (x,y) (normalized to the full frame),
    // radiusNorm normalized to frame width. Fires once per acquisition.
    void ballLocked(float x, float y, float radiusNorm);

    // The locked ball was struck — a per-frame collapse cliff (design §4.5).
    // estImpactUs is the back-dated impact time (-1 until frame timestamps are
    // plumbed in V4); feeds the shot arbiter as a candidate (conf < self-commit).
    void ballLaunched(qint64 estImpactUs, float x, float y);

    // ROI over-exposure health (satFrac = fraction of ROI luma >= 250), edge-
    // triggered on crossing the warn threshold. Drives the wizard exposure hint.
    void exposureWarning(double satFrac);

    // Illumination drift vs the calibration envelope (calibrated path only,
    // docs/design/ball_detection_calibration.md §6). Emitted on state change.
    void environmentDrift(bool drifting, double severity);

    // Calibration capture stream (cloned ROI-space BGR mats).
    void calibFrame(const cv::Mat &roiBgr, int have, int target);
    void calibCaptureDone();

private:
    void detectCalibrated(const cv::Mat &roiMat, int rx, int ry, int fw, int fh,
                          const QElapsedTimer &t);
    // v2 temporal matched-filter path (ball_temporal.h). Emits exactly one
    // ballDetected per call (throttle contract), plus the temporal signals.
    void detectTemporal(const cv::Mat &frame, int rx, int ry, int rw, int rh,
                        int fw, int fh, const QElapsedTimer &t);
    void startSeed();                              // (re)begin the empty-mat baseline seed

    std::atomic<bool> m_enabled{true};

    // Only accessed on the detector's own thread (all slots arrive via queued
    // connections, so they are serialised by the event loop).
    QRectF m_roi;
    pinpoint::ballcal::BallCalProfile m_profile;   // valid flag gates the calibrated (v1) path
    bool   m_drifting = false;
    int    m_calibTarget = 0;                      // >0 = capture mode armed
    int    m_calibHave   = 0;

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
