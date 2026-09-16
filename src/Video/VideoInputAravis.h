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
#include "device_clock_mapper.h"

#include <QFuture>
#include <atomic>

// Aravis-based backend for industrial cameras (GigE Vision / USB3 Vision).
//
// Uses the Aravis 0.8 C API to capture frames. Since industrial cameras often
// output raw Bayer data, this backend typically emits frames in Format_Grayscale8
// (representing the raw Bayer mosaic) and relies on a GPU shader for debayering.

class VideoInputAravis : public VideoInputBase
{
    Q_OBJECT

public:
    explicit VideoInputAravis(QObject *parent = nullptr);
    ~VideoInputAravis() override;

    bool              start(const QString &deviceId = {}) override;
    void              stop()    override;
    void              suspend() override;
    void              resume()  override;
    bool              isActive()    const override;
    QVideoFrameFormat frameFormat() const override;
    CameraCapabilities queryCapabilities() const override;

    // GenICam region (ROI) is applied in hardware on the next start().
    bool supportsHardwareCrop() const override { return true; }
    void setCropRegion(const QRectF &norm) override { m_cropRegion = norm; }
    // Frame rate / exposure applied on the next start(); 0 = backend default.
    void setCaptureRate(double fps) override { m_captureFps = fps; }
    void setExposureUs(double us)   override { m_exposureUs = us; }
    // Gain / Gamma / Line1 strobe applied on the next start() (impact camera
    // tuning, impact_camera_design.md §10.3); -1 dB / 0 gamma = leave alone.
    void setGainDb(double db)       override { m_gainDb = db; }
    void setGamma(double g)         override { m_gamma = g; }
    void setStrobeOutput(bool on)   override { m_strobe = on; }
    bool applyLiveTuning(double exposureUs, double gainDb, double gamma) override;

    // Aravis delivers the camera's own buffer timestamp (ns), mapped onto our clock by the same fit the
    // Spinnaker path uses. ⚠ UNTESTED ON HARDWARE, as the rest of this backend is.
    bool providesDeviceTimestamps() const override { return true; }
    bool clockStats(pinpoint::DeviceClockStats *out) const override;
    double appliedGainDb() const override { return m_appliedGainDb.load(std::memory_order_relaxed); }
    double appliedGamma()  const override { return m_appliedGamma.load(std::memory_order_relaxed); }

private:
    void captureLoop();
    // Writes whichever of exposure / gain / gamma is asked for to the open
    // camera and reads the held values back. Used at start() and live.
    void writeTuning(void *camera, double exposureUs, double gainDb, double gamma);

    void *m_camera    = nullptr; // ArvCamera*
    void *m_stream    = nullptr; // ArvStream*
    bool  m_streaming = false;
    // Set on the caller's thread, read by the pool-thread capture loop.
    std::atomic_bool m_abort{false};
    // The running captureLoop(); stop() joins it before freeing the stream.
    QFuture<void> m_captureFuture;
    // The camera clock → host clock mapping; fed on the capture thread, reset at every start().
    pinpoint::DeviceClockMapper m_clock;
    QRectF m_cropRegion;         // normalized crop; empty = full sensor
    double m_captureFps = 0.0;   // requested frame rate; 0 = the 60 fps default below
    double m_exposureUs = 0.0;   // requested exposure (auto off); 0 = camera default
    double m_gainDb     = -1.0;  // requested gain in dB (auto off); < 0 = camera default
    double m_gamma      = 0.0;   // requested gamma; 0 = camera default
    bool   m_strobe     = false; // Line1 = ExposureActive output
    std::atomic<double> m_appliedGainDb{-1.0};
    std::atomic<double> m_appliedGamma{0.0};
};
