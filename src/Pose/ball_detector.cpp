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

#ifdef HAVE_OPENCV

#include "ball_detector.h"

#include <QElapsedTimer>
#include <opencv2/imgproc.hpp>

#include <cmath>

#include "pp_profiler.h"

namespace {
// v2 temporal-path live constants (design §4.2/§4.5; not the parity-locked
// algorithm constants, which live in ball_temporal.h::tuning).
constexpr int    kSeedFrames       = 24;     // empty-mat baseline seed window (frames)
constexpr float  kPresenceFrac     = 0.40f;  // present if at-spot R >= this * L0
constexpr double kReacquireSeconds = 0.30;   // absent this long -> re-arm to re-search
constexpr int    kSatThresh        = 250;    // ROI luma at/above this is clipped
constexpr double kSatWarn          = 0.25;   // satFrac above this -> exposure warning
// Ball-departure latency: the visible collapse lands ~20-27 ms AFTER true impact
// (the ball compresses/holds before it visibly departs — measured in V0). Sibling
// of ImpactDetector's bleLatencyUs; back-dates the launch estimate.
constexpr qint64 kBallLaunchLatencyUs = 24'000;
}  // namespace

BallDetector::BallDetector(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BallDetection>();
    qRegisterMetaType<BallBaselineSnapshot>();
}

void BallDetector::setRoi(QRectF roi)
{
    const bool changed = roi != m_roi;
    m_roi = roi;
    if (changed && !m_roi.isEmpty())
        startSeed();     // the mat under a new box differs — re-learn the baseline
}

void BallDetector::setFrameRate(double fps)
{
    if (fps <= 0.0) return;
    const bool changed = !m_fpsExplicit || !qFuzzyCompare(fps, m_fps);
    m_fps = fps;
    m_fpsExplicit = true;             // an explicit rate overrides the measured one
    if (changed && !m_roi.isEmpty()) startSeed();
}

void BallDetector::relearnBaseline()
{
    if (!m_roi.isEmpty()) startSeed();
}

void BallDetector::startSeed()
{
    m_seedAccum.release();
    m_seedHave   = 0;
    m_seedTarget = kSeedFrames;       // fixed count; the tracker's fps is measured/explicit
    m_seedNoise  = 1.0;
    m_tracker.reset();
    m_locked = m_present = false;
    m_absentFrames = 0;
}

void BallDetector::detect(const cv::Mat &frame, qint64 frameTUs)
{
    if (!isEnabled()) {     // disabled by method — release the throttle, keep ball state
        emit detectionSkipped();
        return;
    }

    if (m_roi.isEmpty() || frame.empty()) {
        // No hitting area configured (or degenerate frame) — release the
        // throttle without a result. Emitting ballDetected here would feed a
        // spurious miss into the presence-smoothing window and fire a fake
        // ball-lost transition (see the early-out contract in CLAUDE.md).
        emit detectionSkipped();
        return;
    }

    PP_PROFILE_SCOPE("Ball.detect");   // ROI extraction + temporal detection (per frame)

    QElapsedTimer t;
    t.start();

    const int fw = frame.cols;
    const int fh = frame.rows;

    // Map the normalized ROI to pixel coordinates and clamp to frame bounds.
    const int rx = qBound(0, static_cast<int>(m_roi.x()      * fw), fw - 1);
    const int ry = qBound(0, static_cast<int>(m_roi.y()      * fh), fh - 1);
    const int rw = qBound(1, static_cast<int>(m_roi.width()  * fw), fw - rx);
    const int rh = qBound(1, static_cast<int>(m_roi.height() * fh), fh - ry);

    detectTemporal(frame, rx, ry, rw, rh, fw, fh, t, frameTUs);
}

void BallDetector::detectTemporal(const cv::Mat &frame, int rx, int ry, int rw, int rh,
                                  int fw, int fh, const QElapsedTimer &t, qint64 frameTUs)
{
    using namespace pinpoint::balltemporal;
    const double rHat = radiusForWidth(fw);

    // Measure the ACTUAL detect rate (throttle + consumer gated — not the camera
    // rate), so the tracker's time thresholds (lock 0.5 s, re-acquire 0.3 s) hold
    // in real time. Ignore huge gaps (a pause) and zero-dt duplicates.
    if (m_frameClock.isValid()) {
        const qint64 dtMs = m_frameClock.restart();
        if (dtMs > 0 && dtMs < 2000) {
            const double inst = 1000.0 / double(dtMs);
            m_measuredFps = m_measuredFps > 0.0 ? 0.85 * m_measuredFps + 0.15 * inst : inst;
        }
    } else {
        m_frameClock.start();
    }

    // paddedResponse pads BEYOND the ROI, so it needs the surrounding pixels
    // (the crop-edge note in ball_temporal.h) — hand it the full-frame gray.
    cv::Mat gray8, gray32;
    cv::cvtColor(frame, gray8, cv::COLOR_BGR2GRAY);
    gray8.convertTo(gray32, CV_32F);
    const cv::Rect roiRect(rx, ry, rw, rh);
    const cv::Mat R = paddedResponse(gray32, roiRect, rHat);
    if (R.empty()) {
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f, t.elapsed(), 0.f, frameTUs});
        return;
    }

    // Exposure health over the ROI luma (satFrac), edge-triggered.
    {
        const cv::Mat roiGray = gray8(roiRect);
        int sat = 0;
        for (int y = 0; y < roiGray.rows; ++y) {
            const uchar *row = roiGray.ptr<uchar>(y);
            for (int x = 0; x < roiGray.cols; ++x)
                if (row[x] >= kSatThresh) ++sat;
        }
        const int n = roiGray.rows * roiGray.cols;
        const double satFrac = n > 0 ? double(sat) / n : 0.0;
        const bool warn = satFrac > kSatWarn;
        if (warn != m_exposureWarned) {
            m_exposureWarned = warn;
            emit exposureWarning(satFrac);
        }
    }

    // Seeding the empty-mat baseline (Option A): accumulate R, then build the
    // tracker and announce baselineReady(). No detection while seeding.
    if (m_seedTarget > 0) {
        cv::Mat R64;
        R.convertTo(R64, CV_64F);
        if (m_seedAccum.empty()) m_seedAccum = R64;
        else                     m_seedAccum += R64;
        m_seedNoise = robustNoise(R);
        if (++m_seedHave >= m_seedTarget) {
            const cv::Mat meanR = m_seedAccum / m_seedHave;   // CV_64F mean of R
            cv::Mat B;
            meanR.convertTo(B, CV_32F);
            // Explicit rate wins (tests); else the measured detect rate; else default.
            m_trackerFps = m_fpsExplicit ? m_fps
                         : (m_measuredFps > 5.0 ? m_measuredFps : m_fps);
            m_tracker = std::make_unique<TemporalBallTracker>(rHat, m_trackerFps, B, m_seedNoise);
            m_seedTarget = 0;
            m_seedAccum.release();
            // Deep-copy B (the seed accumulator is released next frame) — snapshot
            // and m_roi are self-consistent here on the detector thread.
            emit baselineReady(BallBaselineSnapshot{B.clone(), m_seedNoise, rHat, m_trackerFps, m_roi});
        }
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f, t.elapsed(), 0.f, frameTUs});
        return;
    }

    if (!m_tracker) {   // no baseline yet and not seeding — silent (throttle intact)
        emit ballDetected(BallDetection{false, 0.f, 0.f, 0.f, t.elapsed(), 0.f, frameTUs});
        return;
    }

    m_tracker->push(R);
    const auto &L  = m_tracker->locked();
    const auto &LA = m_tracker->launched();
    const int reacq = std::max(1, static_cast<int>(std::lround(kReacquireSeconds * m_trackerFps)));

    float nx = 0.f, ny = 0.f, nr = 0.f, score = 0.f;
    bool present = false;
    if (L.valid) {
        nr = static_cast<float>(rHat / fw);
        nx = static_cast<float>((rx + L.x) / fw);
        ny = static_cast<float>((ry + L.y) / fh);
        if (!m_locked) { m_locked = true; emit ballLocked(nx, ny, nr); }
        const float ats = atSpot(R, L.ix, L.iy);          // instantaneous presence
        present = ats >= kPresenceFrac * L.L0;
        score   = L.L0 > 0.f ? std::min(1.0f, ats / L.L0) : 0.f;
    }

    if (LA.valid) {
        // Struck-ball launch (cliff): report how long ago the true impact was —
        // (frames since the collapse × interval) + the ball-departure latency —
        // then re-arm to acquire the next ball. The consumer stamps it to an
        // absolute EventBuffer time.
        const int framesSince = m_tracker->frameIndex() - LA.idx;
        const double intervalUs = m_trackerFps > 0.0 ? 1.0e6 / m_trackerFps : 0.0;
        const qint64 ageUs = static_cast<qint64>(framesSince * intervalUs) + kBallLaunchLatencyUs;
        emit ballLaunched(frameTUs, ageUs, static_cast<float>((rx + LA.x) / fw),
                          static_cast<float>((ry + LA.y) / fh));
        m_tracker->rearm();
        m_locked = false;
        m_absentFrames = 0;
        present = false;
    } else if (m_locked && !present) {
        // Removed / occluded — re-arm after a short absence so re-adding re-locks
        // (the wizard remove/re-add loop). A brief occlusion recovers before this.
        if (++m_absentFrames >= reacq) {
            m_tracker->rearm();
            m_locked = false;
            m_absentFrames = 0;
        }
    } else {
        m_absentFrames = 0;
    }

    m_present = present;
    emit ballDetected(BallDetection{present, nx, ny, nr, t.elapsed(), score, frameTUs});
}

#endif // HAVE_OPENCV
