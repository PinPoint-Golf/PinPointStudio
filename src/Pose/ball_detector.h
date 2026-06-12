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

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <atomic>
#include <opencv2/core.hpp>

struct BallDetection {
    bool    found     = false;
    float   x         = 0.f;   // normalized [0,1] within the full frame (centre)
    float   y         = 0.f;
    float   radius    = 0.f;   // radius normalised to frame width
    qint64  detectMs  = 0;     // wall-clock duration of detect() in ms (0 = skipped)
};
Q_DECLARE_METATYPE(BallDetection)
Q_DECLARE_METATYPE(pinpoint::ballcal::BallCalProfile)

// Detects golf balls within the hitting-area ROI.
//
// Two detection paths (docs/design/ball_detection_calibration.md §4):
//   calibrated — when a valid BallCalProfile is set: background-difference +
//                multi-cue scoring against the learned models (ball_model.h),
//                with the per-frame illumination drift monitor.
//   legacy     — bit-for-bit the original white-HSV/Hough/blob detector;
//                the default when no profile exists, so nothing regresses
//                for users who skip calibration.
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

    // Update the search region. Safe to call via queued connection.
    void setRoi(QRectF roi);

    // Tune detector parameters at runtime. Safe to call via queued connection.
    // LEGACY PATH ONLY — ignored while a calibration profile is active.
    // houghConf: HOUGH_GRADIENT_ALT confidence threshold [0.3, 1.0] (default 0.7)
    // whiteSatCeil: HSV saturation upper bound for white-mask [20, 120] (default 50)
    void setParams(double houghConf, int whiteSatCeil);

    // Swap in / drop the learned calibration profile. Safe to call via queued
    // connection. An invalid profile is equivalent to clearProfile().
    void setProfile(const pinpoint::ballcal::BallCalProfile &profile);
    void clearProfile();

signals:
    void ballDetected(const BallDetection &result);

    // Emitted instead of ballDetected() when the detector is disabled —
    // FrameThrottle connects this to clearBusy() alongside ballDetected().
    void detectionSkipped();

    // Illumination drift vs the calibration envelope (calibrated path only,
    // docs/design/ball_detection_calibration.md §6). Emitted on state change.
    void environmentDrift(bool drifting, double severity);

private:
    void detectLegacy(const cv::Mat &roiMat, int rx, int ry, int fw, int fh,
                      const QElapsedTimer &t);
    void detectCalibrated(const cv::Mat &roiMat, int rx, int ry, int fw, int fh,
                          const QElapsedTimer &t);

    std::atomic<bool> m_enabled{true};

    // Only accessed on the detector's own thread (all slots arrive via queued
    // connections, so they are serialised by the event loop).
    QRectF m_roi;
    double m_houghConf    = 0.7;
    int    m_whiteSatCeil = 50;
    pinpoint::ballcal::BallCalProfile m_profile;   // valid flag gates the path
    bool   m_drifting = false;
};

#endif // HAVE_OPENCV
