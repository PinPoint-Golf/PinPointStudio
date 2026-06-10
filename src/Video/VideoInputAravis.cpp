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

// Include Aravis headers before anything else and handle the 'signals' keyword conflict
#ifdef HAVE_ARAVIS
#undef signals
#include <arv.h>
#define signals public
#endif

#include "VideoInputAravis.h"
#include "frame_crop.h"
#include <QVideoFrame>
#include <QDateTime>
#include "pp_debug.h"
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

VideoInputAravis::VideoInputAravis(QObject *parent)
    : VideoInputBase(parent)
{
}

VideoInputAravis::~VideoInputAravis()
{
    stop();
}

bool VideoInputAravis::start(const QString &deviceId)
{
    stop();
    m_abort = false;

#ifdef HAVE_ARAVIS
    arv_update_device_list();
    if (arv_get_n_devices() == 0) {
        emit errorOccurred(tr("No Aravis devices found."));
        return false;
    }

    // Open device. The QByteArray must outlive the constData() pointer —
    // calling constData() on the toLocal8Bit() temporary dangles immediately.
    const QByteArray idBytes = deviceId.toLocal8Bit();
    const char *id = deviceId.isEmpty() ? nullptr : idBytes.constData();
    m_camera = arv_camera_new(id, nullptr);
    if (!m_camera) {
        emit errorOccurred(tr("Failed to open Aravis camera."));
        return false;
    }

    // Configure camera (typical high-speed defaults)
    ArvCamera *cam = (ArvCamera*)m_camera;

    // Hardware ROI: full sensor by default, or the primed crop region snapped
    // DOWN to the device increments so a delivered frame never exceeds the
    // ring slot sized from the same (ceil'd) crop fraction upstream.
    {
        // The camera retains the previous ROI across connections, and the
        // Width/Height bounds are offset-dependent (max = WidthMax - OffsetX)
        // — reset the offsets BEFORE reading bounds, or the region would
        // compound-shrink on every connect.
        ArvDevice *arvDev = arv_camera_get_device(cam);
        if (arvDev) {
            arv_device_set_integer_feature_value(arvDev, "OffsetX", 0, nullptr);
            arv_device_set_integer_feature_value(arvDev, "OffsetY", 0, nullptr);
        }

        gint wMin = 0, wMax = 0, hMin = 0, hMax = 0;
        arv_camera_get_width_bounds (cam, &wMin, &wMax, nullptr);
        arv_camera_get_height_bounds(cam, &hMin, &hMax, nullptr);
        const gint wInc = std::max(1, arv_camera_get_width_increment (cam, nullptr));
        const gint hInc = std::max(1, arv_camera_get_height_increment(cam, nullptr));

        gint rx = 0, ry = 0, rw = wMax, rh = hMax;
        if (pp_crop::cropIsActive(m_cropRegion) && wMax > 0 && hMax > 0) {
            auto snapDown = [](gint v, gint inc) { return (v / inc) * inc; };
            rw = qBound(wMin, snapDown(gint(std::lround(m_cropRegion.width()  * wMax)), wInc), wMax);
            rh = qBound(hMin, snapDown(gint(std::lround(m_cropRegion.height() * hMax)), hInc), hMax);
            rx = snapDown(qBound(gint(0), gint(std::lround(m_cropRegion.x() * wMax)), gint(wMax - rw)), wInc);
            ry = snapDown(qBound(gint(0), gint(std::lround(m_cropRegion.y() * hMax)), gint(hMax - rh)), hInc);
        }
        // Size first, then offsets: arv_camera_set_region writes the offsets
        // before the size, which can trip the OffsetX <= WidthMax - Width
        // constraint while the previous (larger) width is still programmed.
        GError *regionErr = nullptr;
        if (arvDev) {
            if (!regionErr) arv_device_set_integer_feature_value(arvDev, "Width",   rw, &regionErr);
            if (!regionErr) arv_device_set_integer_feature_value(arvDev, "Height",  rh, &regionErr);
            if (!regionErr) arv_device_set_integer_feature_value(arvDev, "OffsetX", rx, &regionErr);
            if (!regionErr) arv_device_set_integer_feature_value(arvDev, "OffsetY", ry, &regionErr);
        } else {
            arv_camera_set_region(cam, rx, ry, rw, rh, &regionErr);
        }
        if (regionErr) {
            ppWarn() << "[VideoInputAravis] set_region failed:" << regionErr->message
                     << "- using full sensor; software crop will engage";
            g_clear_error(&regionErr);
            arv_camera_set_region(cam, 0, 0, wMax, hMax, nullptr);
        } else {
            ppInfo() << "[VideoInputAravis] region" << rw << "x" << rh
                     << "at" << rx << "," << ry;
        }
    }
    arv_camera_set_frame_rate(cam, 60.0, nullptr);        // Target 60 FPS
    arv_camera_set_pixel_format(cam, ARV_PIXEL_FORMAT_MONO_8, nullptr); // Raw Bayer or Mono

    // Create stream
    m_stream = arv_camera_create_stream(cam, nullptr, nullptr, nullptr);
    if (!m_stream) {
        emit errorOccurred(tr("Failed to create Aravis stream."));
        g_object_unref(m_camera);
        m_camera = nullptr;
        return false;
    }

    // Add buffers to stream
    ArvStream *stream = (ArvStream*)m_stream;
    for (int i = 0; i < 10; i++) {
        arv_stream_push_buffer(stream, arv_buffer_new(arv_camera_get_payload(cam, nullptr), nullptr));
    }

    arv_camera_start_acquisition(cam, nullptr);
    m_streaming = true;
    m_state = State::Active;
    emit stateChanged(State::Active);

    // Start capture loop in a background thread. Keep the future so stop()
    // can join the loop before freeing the stream it dereferences.
    m_captureFuture = QtConcurrent::run([this]() { captureLoop(); });

    return true;
#else
    Q_UNUSED(deviceId)
    return false;
#endif
}

void VideoInputAravis::stop()
{
    m_abort = true;

#ifdef HAVE_ARAVIS
    if (m_camera && m_streaming) {
        arv_camera_stop_acquisition((ArvCamera*)m_camera, nullptr);
        m_streaming = false;
    }

    // Join the pool-thread capture loop BEFORE freeing the stream/camera it
    // dereferences. The loop uses a timed pop, so it observes m_abort within
    // 100 ms even when no buffer ever arrives after stop_acquisition. The
    // loop's frame hand-off is a queued invoke (never blocks on this thread),
    // so waiting here cannot deadlock.
    m_captureFuture.waitForFinished();

    if (m_stream) {
        g_object_unref(m_stream);
        m_stream = nullptr;
    }

    if (m_camera) {
        // Leave the camera at full frame: the ROI is retained across
        // connections, and a stale crop poisons later bounds reads (the
        // Width/Height max is offset-dependent — see start()).
        ArvCamera *cam = (ArvCamera*)m_camera;
        ArvDevice *arvDev = arv_camera_get_device(cam);
        if (arvDev) {
            arv_device_set_integer_feature_value(arvDev, "OffsetX", 0, nullptr);
            arv_device_set_integer_feature_value(arvDev, "OffsetY", 0, nullptr);
        }
        gint wMin = 0, wMax = 0, hMin = 0, hMax = 0;
        arv_camera_get_width_bounds (cam, &wMin, &wMax, nullptr);
        arv_camera_get_height_bounds(cam, &hMin, &hMax, nullptr);
        if (wMax > 0 && hMax > 0)
            arv_camera_set_region(cam, 0, 0, wMax, hMax, nullptr);

        g_object_unref(m_camera);
        m_camera = nullptr;
    }
#endif

    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        emit stateChanged(State::Stopped);
    }
}

void VideoInputAravis::suspend()
{
    if (m_state == State::Active) {
        m_state = State::Suspended;
        emit stateChanged(State::Suspended);
    }
}

void VideoInputAravis::resume()
{
    if (m_state == State::Suspended) {
        m_state = State::Active;
        emit stateChanged(State::Active);
    }
}

bool VideoInputAravis::isActive() const
{
    return m_state == State::Active;
}

QVideoFrameFormat VideoInputAravis::frameFormat() const
{
    return QVideoFrameFormat();
}

CameraCapabilities VideoInputAravis::queryCapabilities() const
{
    CameraCapabilities caps;
    caps.queriedAt   = QDateTime::currentDateTime();
    caps.driverVersion = "Aravis GigE/USB3 Vision";

#ifdef HAVE_ARAVIS
    // Do not open a live connection before start() — return empty so that
    // callers can safely query capabilities pre-start without side-effects.
    if (!m_camera) return caps;
    auto doQuery = [&caps](ArvCamera *cam) {
        GError *err = nullptr;

        // --- Identity ---
        const char *vendor = arv_camera_get_vendor_name(cam, &err);
        if (!err && vendor) caps.vendorName = QString::fromLocal8Bit(vendor);
        g_clear_error(&err);

        const char *model = arv_camera_get_model_name(cam, &err);
        if (!err && model) caps.modelName = QString::fromLocal8Bit(model);
        g_clear_error(&err);

        // --- Interface type ---
        if (arv_camera_is_gv_device(cam))
            caps.connectionInterface = CameraCapabilities::Interface::GigE;
        else if (arv_camera_is_uv_device(cam))
            caps.connectionInterface = CameraCapabilities::Interface::USB3;

        // --- Resolution ---
        gint wMin, wMax, hMin, hMax, wInc, hInc;
        arv_camera_get_width_bounds(cam, &wMin, &wMax, &err);
        if (!err) {
            g_clear_error(&err);
            arv_camera_get_height_bounds(cam, &hMin, &hMax, &err);
            if (!err) {
                wInc = arv_camera_get_width_increment(cam, nullptr);
                hInc = arv_camera_get_height_increment(cam, nullptr);
                gint curW, curH;
                arv_camera_get_region(cam, nullptr, nullptr, &curW, &curH, nullptr);
                // Width/Height bounds are offset-dependent (max = WidthMax -
                // OffsetX) — prefer the WidthMax/HeightMax features so the
                // range max reports the full (binned) sensor even while a
                // crop ROI is active.
                ArvDevice *arvDev = arv_camera_get_device(cam);
                if (arvDev) {
                    GError *mErr = nullptr;
                    gint64 wm = arv_device_get_integer_feature_value(arvDev, "WidthMax", &mErr);
                    if (!mErr && wm > 0) wMax = (gint)wm;
                    g_clear_error(&mErr);
                    gint64 hm = arv_device_get_integer_feature_value(arvDev, "HeightMax", &mErr);
                    if (!mErr && hm > 0) hMax = (gint)hm;
                    g_clear_error(&mErr);
                }
                caps.resolution.kind     = CapabilityKind::Range;
                caps.resolution.writable = true;
                caps.resolution.widthRange  = { wMin, wMax, wInc, curW };
                caps.resolution.heightRange = { hMin, hMax, hInc, curH };
                caps.resolution.defaultResolution = { curW, curH };

                // Hardware ROI via arv_camera_set_region (applied in start()).
                caps.roi.supported    = true;
                caps.roi.widthRange   = caps.resolution.widthRange;
                caps.roi.heightRange  = caps.resolution.heightRange;
                caps.roi.offsetXRange = { 0, wMax, wInc, 0 };
                caps.roi.offsetYRange = { 0, hMax, hInc, 0 };
            }
        }
        g_clear_error(&err);

        // --- Frame rate ---
        double fpsMin, fpsMax;
        arv_camera_get_frame_rate_bounds(cam, &fpsMin, &fpsMax, &err);
        if (!err) {
            double curFps = arv_camera_get_frame_rate(cam, nullptr);
            caps.frameRate.kind               = CapabilityKind::Range;
            caps.frameRate.readable           = true;
            caps.frameRate.writable           = true;
            caps.frameRate.range.min          = fpsMin;
            caps.frameRate.range.max          = fpsMax;
            caps.frameRate.range.step         = 0;
            caps.frameRate.range.defaultValue = curFps;
        }
        g_clear_error(&err);

        // --- Exposure time ---
        double expMin, expMax;
        arv_camera_get_exposure_time_bounds(cam, &expMin, &expMax, &err);
        if (!err) {
            double curExp = arv_camera_get_exposure_time(cam, nullptr);
            caps.exposureTime.kind               = CapabilityKind::Range;
            caps.exposureTime.readable           = true;
            caps.exposureTime.writable           = true;
            caps.exposureTime.range.min          = expMin;
            caps.exposureTime.range.max          = expMax;
            caps.exposureTime.range.step         = 0;
            caps.exposureTime.range.defaultValue = curExp;
        }
        g_clear_error(&err);

        // --- Gain ---
        double gainMin, gainMax;
        arv_camera_get_gain_bounds(cam, &gainMin, &gainMax, &err);
        if (!err) {
            double curGain = arv_camera_get_gain(cam, nullptr);
            caps.gain.kind               = CapabilityKind::Range;
            caps.gain.readable           = true;
            caps.gain.writable           = true;
            caps.gain.range.min          = gainMin;
            caps.gain.range.max          = gainMax;
            caps.gain.range.step         = 0;
            caps.gain.range.defaultValue = curGain;
        }
        g_clear_error(&err);

        // --- Pixel format ---
        guint nFormats;
        const char **fmts = arv_camera_dup_available_pixel_formats_as_strings(cam, &nFormats, &err);
        if (!err && fmts) {
            caps.pixelFormat.kind     = CapabilityKind::Discrete;
            caps.pixelFormat.writable = true;
            const char *curFmt = arv_camera_get_pixel_format_as_string(cam, nullptr);
            for (guint i = 0; i < nFormats; ++i) {
                PixelFormat pf;
                pf.nativeKey = QString::fromLocal8Bit(fmts[i]);
                if      (pf.nativeKey == "BayerRG8")   { pf.encoding = PixelEncoding::BayerRG8;  pf.bitsPerPixel = 8; }
                else if (pf.nativeKey == "BayerGB8")   { pf.encoding = PixelEncoding::BayerGB8;  pf.bitsPerPixel = 8; }
                else if (pf.nativeKey == "BayerGR8")   { pf.encoding = PixelEncoding::BayerGR8;  pf.bitsPerPixel = 8; }
                else if (pf.nativeKey == "BayerBG8")   { pf.encoding = PixelEncoding::BayerBG8;  pf.bitsPerPixel = 8; }
                else if (pf.nativeKey == "Mono8")      { pf.encoding = PixelEncoding::Mono8;     pf.bitsPerPixel = 8; }
                else if (pf.nativeKey == "Mono10")     { pf.encoding = PixelEncoding::Mono10;    pf.bitsPerPixel = 10; }
                else if (pf.nativeKey == "Mono12")     { pf.encoding = PixelEncoding::Mono12;    pf.bitsPerPixel = 12; }
                else if (pf.nativeKey == "RGB8Packed") { pf.encoding = PixelEncoding::RGB8;      pf.bitsPerPixel = 24; }
                else if (pf.nativeKey == "BGR8")       { pf.encoding = PixelEncoding::BGR8;      pf.bitsPerPixel = 24; }
                else                                   { pf.encoding = PixelEncoding::Unknown; }
                caps.pixelFormat.supported.append(pf);
                if (curFmt && pf.nativeKey == QString::fromLocal8Bit(curFmt))
                    caps.pixelFormat.defaultFormat = pf;
            }
            g_free(fmts);
        }
        g_clear_error(&err);

        // --- Trigger ---
        ArvDevice *device = arv_camera_get_device(cam);
        if (device) {
            ArvGcNode *trigSrc = arv_device_get_feature(device, "TriggerSource");
            if (trigSrc) {
                caps.trigger.supported       = true;
                caps.trigger.hasTimestamping = true;
                caps.trigger.sources.append(TriggerSource::Software);
                caps.trigger.sources.append(TriggerSource::Hardware);
                caps.trigger.hasHardwareInput = true;
            }
        }
    };

    if (m_camera) {
        doQuery((ArvCamera*)m_camera);
    } else {
        arv_update_device_list();
        if (arv_get_n_devices() > 0) {
            GError *err = nullptr;
            ArvCamera *cam = arv_camera_new(nullptr, &err);
            if (cam) {
                doQuery(cam);
                g_object_unref(cam);
            }
            g_clear_error(&err);
        }
    }
#endif

    return caps;
}

void VideoInputAravis::captureLoop()
{
#ifdef HAVE_ARAVIS
    // Snapshot the stream once: stop() joins this loop before unreffing it,
    // so the pointer stays valid for the loop's whole lifetime.
    ArvStream *stream = (ArvStream*)m_stream;
    while (!m_abort && stream) {
        // Timed pop (100 ms) rather than the blocking variant: after
        // arv_camera_stop_acquisition no buffer need ever arrive, and a
        // blocking pop would never observe m_abort.
        ArvBuffer *buffer = arv_stream_timeout_pop_buffer(stream, 100000);
        if (buffer) {
            if (arv_buffer_get_status(buffer) == ARV_BUFFER_STATUS_SUCCESS) {
                size_t size;
                const void *data = arv_buffer_get_data(buffer, &size);
                int width, height;
                arv_buffer_get_image_region(buffer, nullptr, nullptr, &width, &height);

                // Create a QVideoFrame from the buffer.
                // We use Format_Grayscale8 for raw Bayer data.
                QImage img((const uchar*)data, width, height, QImage::Format_Grayscale8);
                QVideoFrame frame(img.copy()); // Copy data to ensure it stays valid

                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit videoFrameReady(frame);
                }, Qt::QueuedConnection);
            }
            arv_stream_push_buffer(stream, buffer);
        }
    }
#endif
}
