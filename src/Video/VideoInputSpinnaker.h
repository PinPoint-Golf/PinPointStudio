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

#include "video_input_base.h"

#include <QFuture>
#include <atomic>

// Spinnaker-based backend for Teledyne/FLIR industrial cameras.
// Only supported on Windows.
//
// Uses the Spinnaker C++ API to capture frames.

class VideoInputSpinnaker : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputSpinnaker(QObject *parent = nullptr);
    ~VideoInputSpinnaker() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()      const override;
    QVideoFrameFormat frameFormat()   const override;
    bool              emitsRawBayer() const override { return m_emitRaw; }
    double            lastMeasuredExposureUs() const override { return m_lastExposureUs.load(std::memory_order_relaxed); }
    int               lastExposureAutoMode()   const override { return m_lastExposureAuto.load(std::memory_order_relaxed); }
    CameraCapabilities queryCapabilities() const override;

    // GenICam OffsetX/OffsetY/Width/Height nodes applied on the next start().
    bool supportsHardwareCrop() const override { return true; }
    void setCropRegion(const QRectF &norm) override { m_cropRegion = norm; }
    // AcquisitionFrameRate / ExposureTime written on the next start(), after
    // the ROI (the rate's maximum depends on it). 0 = leave the camera alone.
    void setCaptureRate(double fps) override { m_captureFps = fps; }
    void setExposureUs(double us)   override { m_exposureUs = us; }
    // Gain / Gamma / Line1 strobe written on the next start() (impact camera
    // tuning, impact_camera_design.md §10.3); -1 dB / 0 gamma = leave alone.
    void setGainDb(double db)       override { m_gainDb = db; }
    void setGamma(double g)         override { m_gamma = g; }
    void setStrobeOutput(bool on)   override { m_strobe = on; }
    bool applyLiveTuning(double exposureUs, double gainDb, double gamma) override;
    double appliedGainDb() const override { return m_appliedGainDb.load(std::memory_order_relaxed); }
    double appliedGamma()  const override { return m_appliedGamma.load(std::memory_order_relaxed); }

private:
    void captureLoop();
    // Writes whichever of exposure / gain / gamma is asked for (see the
    // skip rules on VideoInputBase::applyLiveTuning) to the node map and
    // reads the held values back into m_applied*. Used at start() and live.
    void writeTuningNodes(void *nodeMap, double exposureUs, double gainDb, double gamma);

    void *m_system     = nullptr; // Spinnaker::SystemPtr*
    void *m_camera     = nullptr; // Spinnaker::CameraPtr*
    void *m_logHandler = nullptr; // SpinLogHandler* (Windows/HAVE_SPINNAKER only)
    bool  m_streaming = false;
    // Set on the caller's thread, read by the pool-thread capture loop.
    std::atomic_bool m_abort{false};
    // The running captureLoop(); stop() joins it before deleting the CameraPtr.
    QFuture<void> m_captureFuture;
    int   m_bayerPattern = 0;   // RawVideoFrame::BayerPattern int, valid when Bayer format selected
    bool  m_emitRaw      = false; // true when camera runs a Bayer pixel format
    QRectF m_cropRegion;          // normalized crop; empty = full sensor
    double m_captureFps = 0.0;    // requested AcquisitionFrameRate; 0 = camera default
    double m_exposureUs = 0.0;    // requested ExposureTime (auto off); 0 = camera default
    double m_gainDb     = -1.0;   // requested Gain in dB (GainAuto off); < 0 = camera default
    double m_gamma      = 0.0;    // requested Gamma; 0 = camera default
    bool   m_strobe     = false;  // Line1 = ExposureActive output
    // Read back from the nodes after every write (the write is clamped to the
    // node's range); published for the provenance stamp on the clip.
    std::atomic<double> m_appliedGainDb{-1.0};
    std::atomic<double> m_appliedGamma{0.0};

    // Exposure chunk data (set in start(), read in captureLoop()).
    bool  m_chunkExposureEnabled = false; // ChunkExposureTime successfully enabled
    int   m_exposureAuto         = -1;    // cached ExposureAuto mode: -1 unknown, 0 Off, 1 auto
    // Most recent per-frame exposure, published for the QVideoFrame-path virtuals.
    std::atomic<double> m_lastExposureUs{0.0};
    std::atomic<int>    m_lastExposureAuto{-1};
};
