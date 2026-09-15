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

#include <QObject>
#include <QRectF>
#include <QVideoFrameFormat>
#include "raw_video_frame.h"
#include "camera_capabilities.h"

// Abstract base for camera / video capture.
//
// Subclasses implement the transport (Qt6 QCamera, platform-specific, etc.)
// and emit videoFrameReady() whenever a decoded frame arrives.
//
// Typical usage:
//   VideoInput *in = new VideoInput(this);
//   in->start();                           // default camera
//   in->start("Front Camera");             // named device

class VideoInputBase : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Active,
        Suspended,
        Error,
    };
    Q_ENUM(State)

    explicit VideoInputBase(QObject *parent = nullptr);
    ~VideoInputBase() override = default;

    // -----------------------------------------------------------------------
    // Transport control
    // -----------------------------------------------------------------------

    // Start capture on the named device; empty string selects the system default.
    // deviceId is matched against QCameraDevice::id() (byte string) or description().
    virtual bool start(const QString &deviceId = {}) = 0;
    virtual void stop() = 0;
    virtual void suspend() = 0;
    virtual void resume() = 0;

    // -----------------------------------------------------------------------
    // Status
    // -----------------------------------------------------------------------

    virtual bool              isActive()      const = 0;
    virtual QVideoFrameFormat frameFormat()   const = 0;
    virtual State             state()         const;
    // Returns true when the backend emits rawVideoFrameReady (raw Bayer bytes)
    // rather than videoFrameReady (pre-decoded frames).  Valid after start().
    virtual bool              emitsRawBayer() const { return false; }

    // Per-frame exposure side channel for the QVideoFrame (pre-decoded) path,
    // where the exposure cannot travel on the frame itself. Industrial backends
    // that read exposure from frame chunk data override these; others inherit
    // the defaults so CameraInstance derives exposure from the frame rate.
    // Valid after start(); reflect the most recently delivered frame.
    virtual double            lastMeasuredExposureUs() const { return 0.0; } // us; 0 = unknown
    virtual int               lastExposureAutoMode()   const { return -1;  } // -1 unknown, 0 Off, 1 auto

    // Per-frame CAPTURE INSTANT side channel, for the same reason exposure has
    // one: it is a fact about the frame that cannot travel on the frame. A
    // local camera has none — its frames are stamped on arrival, which for a
    // camera on this machine's bus is close enough — and inherits the 0 below.
    // A backend whose frames carry a time of their own (work package H4: a PPCP
    // peer, whose Captures arrive with per-frame instants and may cross a slow
    // link long after they were exposed) overrides this, and a consumer that
    // ignored it would be stamping network arrival time on a swing.
    //
    // Microseconds on the EventBuffer clock (steady_clock), valid immediately
    // after the frame signal fires and read on the same thread it fired on.
    // 0 means "this backend has no instant of its own for that frame" — which
    // is a DIFFERENT answer from "time zero", and is why 0 and not -1: the
    // buffer's clock is a process-relative monotonic count that is never 0 in
    // practice, and a backend that cannot map its instant onto it must say so
    // rather than offer a plausible number.
    virtual qint64            lastFrameInstantUs()     const { return 0; }

    // Prime the backend with a target device ID before start() so that
    // queryCapabilities() can enumerate that device's formats without opening
    // a live camera handle.  Default is a no-op; VideoInput overrides it.
    virtual void prepareDevice(const QString &) {}

    // True when the backend can apply a sensor ROI in hardware at start()
    // (GenICam region / offset+size nodes). Backends returning false are
    // cropped in software by CameraInstance as frames arrive.
    virtual bool supportsHardwareCrop() const { return false; }

    // Prime a normalized (0..1) crop region to be applied on the NEXT
    // start(). An empty or unit rect means full sensor. Must be called on
    // the object's thread — CameraInstance invokes it inside the queued
    // start lambda. Default is a no-op (software-cropped backends).
    virtual void setCropRegion(const QRectF &) {}

    // Frame rate (fps) and exposure (microseconds) to apply on the NEXT
    // start(), for backends that can set them (GenICam). 0 means leave the
    // camera as it is, which is what every camera gets except the impact
    // camera (impact_camera_design.md §10.2: a crop, a rate AND a locked
    // exposure make the mode). A non-zero exposure turns auto-exposure off.
    // Same threading rule as setCropRegion(). Default is a no-op.
    virtual void setCaptureRate(double) {}
    virtual void setExposureUs(double) {}

    // The impact camera's tuning beyond exposure (impact_camera_design.md
    // §10.3). Sensor gain in dB, auto-gain off (< 0 = leave the camera alone):
    // applied before the ADC, so it lifts a dark club body above the 8-bit
    // floor rather than stretching a floor that is already there. In-camera
    // gamma (0 = leave alone): applied to the sensor's full bit depth, so a
    // value below 1 lifts the shadows the club lives in while the ball stays
    // unclipped. The strobe output: Line1 driven by ExposureActive, for an
    // LED strobe driver. Primed before start() like the rate and exposure;
    // same threading rule as setCropRegion(). Defaults are no-ops.
    virtual void setGainDb(double) {}
    virtual void setGamma(double) {}
    virtual void setStrobeOutput(bool) {}

    // Re-tune a STREAMING camera. ExposureTime, Gain and Gamma are writable
    // during acquisition on GenICam cameras, so the operator can turn a knob
    // and watch the tile instead of reconnecting. exposureUs ≤ 0, gainDb < 0
    // and gamma ≤ 0 are each skipped. Returns false when nothing could be
    // written (not streaming, no such nodes). Must be called on the object's
    // thread — CameraInstance invokes it there.
    virtual bool applyLiveTuning(double /*exposureUs*/, double /*gainDb*/, double /*gamma*/) { return false; }

    // What the camera actually holds after the last prime or live apply, read
    // back from the device (a write is clamped to the node's range, so the
    // request is not the fact). -1 dB / 0 gamma = unknown or never written.
    // Recorded per clip as provenance next to the measured exposure.
    virtual double appliedGainDb() const { return -1.0; }
    virtual double appliedGamma()  const { return 0.0; }

    // Query what this camera can do. Returns a default-constructed
    // CameraCapabilities (all fields Unavailable / zero) if the camera has
    // not been opened yet or the backend does not support introspection.
    // Implementations should call this before start() to enumerate presets,
    // or after start() to read live/active values from the device.
    virtual CameraCapabilities queryCapabilities() const = 0;

signals:
    // Emitted for every decoded camera frame (non-Bayer backends).
    void videoFrameReady(const QVideoFrame &frame);

    // Emitted by Bayer backends instead of videoFrameReady.  Data is packed
    // (stride == width) so the GPU upload path needs no row-stride adjustment.
    void rawVideoFrameReady(const RawVideoFrame &frame);

    void stateChanged(VideoInputBase::State state);
    void errorOccurred(const QString &message);

protected:
    State m_state = State::Stopped;
};
