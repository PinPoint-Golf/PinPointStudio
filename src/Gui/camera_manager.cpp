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

#include "camera_manager.h"

#include "video_controller.h"
#include "video_input_factory.h"
#include "event_buffer.h"
#include "app_settings.h"
#include "../Video/camera_capabilities.h"
#include <algorithm>

CameraManager::CameraManager(pinpoint::EventBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_eventBuffer(buffer)
{
    VideoInputFactory::enumerateDevices();

    const QList<Device> allDevices = DeviceEnumerator::instance()->devices();
    for (const Device &dev : allDevices) {
        if (dev.type != DeviceType::VideoInput) continue;
        CameraEntry entry;
        entry.device = dev;
        m_cameras.append(entry);
    }

    // Apply persisted per-device settings keyed by serial number.
    AppSettings settings;
    const QStringList excluded    = settings.cameraExcluded();
    const QVariantMap targetFps   = settings.cameraTargetFps();
    const QVariantMap triggerMode = settings.cameraTriggerMode();

    for (auto &cam : m_cameras) {
        const QString key = cameraKey(cam);
        if (!key.isEmpty()) {
            if (excluded.contains(key))    cam.excluded    = true;
            if (targetFps.contains(key))   cam.targetFps   = targetFps.value(key).toDouble();
            if (triggerMode.contains(key)) cam.triggerMode = triggerMode.value(key).toString();
        }
    }

}

CameraManager::~CameraManager()
{
    if (m_eventBuffer) {
        if (m_eventBuffer->state() == pinpoint::BufferState::Capturing)
            m_eventBuffer->pause();
        if (m_eventBuffer->state() == pinpoint::BufferState::Paused) {
            for (auto &cam : m_cameras) {
                if (cam.controller)
                    cam.controller->deregisterFromBuffer();
            }
        }
        // If Idle (stop() already called via aboutToQuit), EventBuffer cleans
        // up its own sources — skip deregistration.
    }
    for (auto &cam : m_cameras) {
        delete cam.controller;
        cam.controller = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

QVariantList CameraManager::cameraList() const
{
    QVariantList list;
    for (int i = 0; i < m_cameras.size(); ++i) {
        const CameraEntry &cam = m_cameras[i];
        const CameraCapabilities &cap = cam.device.capabilities;
        QVariantMap entry;
        entry[QStringLiteral("index")]       = i;
        entry[QStringLiteral("description")] = cam.device.description;
        entry[QStringLiteral("selected")]    = cam.selected;
        entry[QStringLiteral("vendorName")]  = cap.vendorName;
        entry[QStringLiteral("modelName")]   = cap.modelName;
        entry[QStringLiteral("serialNumber")] = cap.serialNumber;
        entry[QStringLiteral("cameraKey")]   = cameraKey(cam);
        entry[QStringLiteral("interface")]   = interfaceString(cap.connectionInterface);
        entry[QStringLiteral("maxWidth")]    = cap.resolution.defaultResolution.width;
        entry[QStringLiteral("maxHeight")]   = cap.resolution.defaultResolution.height;
        entry[QStringLiteral("pixelFormat")] = cap.pixelFormat.defaultFormat.nativeKey;
        entry[QStringLiteral("bitsPerPixel")] = cap.pixelFormat.defaultFormat.bitsPerPixel;
        entry[QStringLiteral("maxFps")]      = cap.frameRate.range.max;
        entry[QStringLiteral("roiSupported")] = cap.roi.supported;
        entry[QStringLiteral("hwTrigger")]   = cap.trigger.hasHardwareInput;
        entry[QStringLiteral("enabled")]     = !cam.excluded;

        // --- Ring buffer sizing fields (mirror VideoController's allocation logic) ---
        // Slot width/height: largest supported resolution, not just the default.
        int slotW = 0, slotH = 0;
        for (const Resolution &r : cap.resolution.presets) {
            if (r.width * r.height > slotW * slotH) { slotW = r.width; slotH = r.height; }
        }
        if (slotW == 0) { slotW = cap.resolution.defaultResolution.width;
                          slotH = cap.resolution.defaultResolution.height; }
        if (slotW == 0) { slotW = 1920; slotH = 1080; }

        // Slot BPP: worst-case across all supported formats, minimum 2 bytes/pixel.
        int maxBpp = 2;
        for (const PixelFormat &pf : cap.pixelFormat.supported) {
            int bpp = pf.bitsPerPixel > 0 ? (pf.bitsPerPixel + 7) / 8 : 0;
            if (bpp == 0) {
                switch (pf.encoding) {
                case PixelEncoding::RGBA8:
                case PixelEncoding::BGR8:          bpp = 4; break;
                case PixelEncoding::BayerRG16:
                case PixelEncoding::BayerGB16:
                case PixelEncoding::YUV422_YUYV:
                case PixelEncoding::YUV422_UYVY:   bpp = 2; break;
                default:                           bpp = 2; break;
                }
            }
            maxBpp = std::max(maxBpp, bpp);
        }

        // Slot FPS: GenICam cameras use 200 fps as the conservative upper bound.
        double slotFps = cap.frameRate.range.max > 0.0 ? cap.frameRate.range.max : 60.0;
        if (cam.device.backend == VideoInputFactory::Backend::Aravis ||
            cam.device.backend == VideoInputFactory::Backend::Spinnaker)
            slotFps = 200.0;

        entry[QStringLiteral("slotWidth")]        = slotW;
        entry[QStringLiteral("slotHeight")]       = slotH;
        entry[QStringLiteral("slotBytesPerPixel")] = maxBpp;
        entry[QStringLiteral("slotFps")]          = slotFps;

        list.append(entry);
    }
    return list;
}

QVariantList CameraManager::instances() const
{
    QVariantList list;
    for (const auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            list.append(QVariant::fromValue(static_cast<QObject *>(cam.controller)));
    }
    return list;
}

VideoController *CameraManager::controllerFor(const QString &deviceId) const
{
    for (const auto &cam : m_cameras)
        if (cam.device.id == deviceId && cam.controller)
            return cam.controller;
    return nullptr;
}

bool CameraManager::isRecording() const
{
    return m_recording;
}

bool CameraManager::anySelected() const
{
    for (const auto &cam : m_cameras)
        if (cam.selected) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Invokables
// ---------------------------------------------------------------------------

void CameraManager::setSelected(int index, bool selected)
{
    if (index < 0 || index >= m_cameras.size()) return;
    if (m_cameras[index].selected == selected) return;

    m_cameras[index].selected = selected;

    // Snapshot buffer state before touching anything.
    bool wasCapturing = m_eventBuffer &&
                        m_eventBuffer->state() == pinpoint::BufferState::Capturing;

    // Any change to device selection requires the buffer to be Paused so that
    // registerSource() / deregisterSource() are called in a safe state.
    // Note: pause() → resume() clears all ring buffers. Any data captured
    // before this device change is discarded. This is intentional.
    if (wasCapturing) m_eventBuffer->pause();

    if (selected) {
        m_cameras[index].controller = createController(m_cameras[index].device);
        if (m_recording && m_cameras[index].controller)
            m_cameras[index].controller->startRecording();
    } else {
        VideoController *ctrl = m_cameras[index].controller;
        m_cameras[index].controller = nullptr;
        if (ctrl) {
            if (m_recording) ctrl->stopRecording();
            ctrl->deregisterFromBuffer();
            // Notify QML so the Repeater removes the delegate while the
            // controller is still alive; deleteLater() defers the actual
            // destruction until bindings have already been torn down.
            emit cameraListChanged();
            emit instancesChanged();
            ctrl->deleteLater();
        }
    }

    // Resume only if ball detection currently warrants it; don't blindly restore.
    if (wasCapturing && m_recording && m_ballPresentCount > 0)
        m_eventBuffer->resume();

    emit cameraListChanged();
    emit instancesChanged();
}

void CameraManager::startAll()
{
    if (m_recording) return;
    m_recording = true;
    emit isRecordingChanged();

    // Pause the buffer so ball detection drives capture: resume fires on first ball.
    pauseBuffer();
    m_ballPresentCount = 0;

    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->startRecording();
    }

    // Sync with current detector state: ballPresentChanged() only fires on a
    // transition, so if a ball was already present before recording started the
    // signal won't fire again and m_ballPresentCount would stay 0.
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller && cam.controller->ballPresent())
            ++m_ballPresentCount;
    }
    if (m_ballPresentCount > 0)
        resumeBuffer();

    emit bufferStateChanged();
}

void CameraManager::stopAll()
{
    if (!m_recording) return;
    if (m_replaying)
        stopReplay(false); // tear down replay without resuming the buffer
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->stopRecording();
    }
    m_recording = false;
    m_ballPresentCount = 0;
    emit isRecordingChanged();
    // Pause the buffer to freeze any captured swing data for analysis.
    pauseBuffer();
    emit bufferStateChanged();
}

void CameraManager::pauseBuffer()
{
    if (m_eventBuffer && m_eventBuffer->state() == pinpoint::BufferState::Capturing) {
        m_eventBuffer->pause();
        emit bufferStateChanged();
    }
}

void CameraManager::resumeBuffer()
{
    if (m_eventBuffer && m_eventBuffer->state() == pinpoint::BufferState::Paused) {
        m_eventBuffer->resume();
        emit bufferStateChanged();
    }
}

bool CameraManager::isReplaying() const { return m_replaying; }

QString CameraManager::bufferState() const
{
    if (!m_eventBuffer) return QStringLiteral("unavailable");
    switch (m_eventBuffer->state()) {
    case pinpoint::BufferState::Idle:      return QStringLiteral("idle");
    case pinpoint::BufferState::Capturing: return QStringLiteral("capturing");
    case pinpoint::BufferState::Paused:    return QStringLiteral("paused");
    case pinpoint::BufferState::Stopping:  return QStringLiteral("stopping");
    }
    return QStringLiteral("unknown");
}

void CameraManager::enumerate()
{
    VideoInputFactory::enumerateDevices();

    // Merge any newly discovered video devices into m_cameras.
    // Existing entries are matched by device id and left untouched so their
    // controllers, settings and selected state are preserved.
    const QList<Device> allDevices = DeviceEnumerator::instance()->devices();
    bool added = false;
    for (const Device &dev : allDevices) {
        if (dev.type != DeviceType::VideoInput) continue;
        bool found = false;
        for (const auto &cam : m_cameras) {
            if (cam.device.id == dev.id) { found = true; break; }
        }
        if (!found) {
            CameraEntry entry;
            entry.device = dev;
            // Apply any persisted settings for this device.
            AppSettings s;
            const QString key = cameraKey(entry);
            if (s.cameraExcluded().contains(key))    entry.excluded    = true;
            if (s.cameraTargetFps().contains(key))   entry.targetFps   = s.cameraTargetFps().value(key).toDouble();
            if (s.cameraTriggerMode().contains(key)) entry.triggerMode = s.cameraTriggerMode().value(key).toString();
            m_cameras.append(entry);
            added = true;
        }
    }

    if (added) {
        emit cameraListChanged();
        emit instancesChanged();
    }
}

void CameraManager::setExcluded(int index, bool excluded)
{
    if (index < 0 || index >= m_cameras.size()) return;
    if (m_cameras[index].excluded == excluded) return;

    m_cameras[index].excluded = excluded;

    if (excluded && m_cameras[index].selected) {
        setSelected(index, false);
    } else if (!excluded && !m_cameras[index].selected) {
        setSelected(index, true);
    }

    // Persist to AppSettings keyed by serial number.
    const QString key = cameraKey(m_cameras[index]);
    if (!key.isEmpty()) {
        AppSettings settings;
        QStringList excList = settings.cameraExcluded();
        if (excluded)
            excList.append(key);
        else
            excList.removeAll(key);
        settings.setCameraExcluded(excList);
    }

    emit cameraListChanged();
}

void CameraManager::setTargetFps(int index, double fps)
{
    if (index < 0 || index >= m_cameras.size()) return;
    m_cameras[index].targetFps = fps;
    // TODO: apply to live VideoController when setFps() is implemented
}

void CameraManager::setTriggerMode(int index, const QString &mode)
{
    if (index < 0 || index >= m_cameras.size()) return;
    m_cameras[index].triggerMode = mode;
}

void CameraManager::setPerspective(QObject *rawController, int perspective)
{
    auto *target = qobject_cast<VideoController *>(rawController);
    if (!target)
        return;

    // Clear this perspective from every other camera that currently holds it,
    // and remove it from AppSettings so the cleared camera doesn't re-acquire
    // the view on next startup.
    if (perspective > 0) {
        AppSettings settings;
        QVariantMap map = settings.cameraPerspective();
        bool changed = false;
        for (auto &cam : m_cameras) {
            if (cam.controller && cam.controller != target
                && cam.controller->perspective() == perspective)
            {
                cam.controller->setPerspective(0);
                map.remove(cameraKey(cam));
                changed = true;
            }
        }
        if (changed) settings.setCameraPerspective(map);
    }

    target->setPerspective(perspective);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QObject *CameraManager::createPreviewController(int index)
{
    if (index < 0 || index >= m_cameras.size()) return nullptr;

    // nullptr buffer → no ring-buffer registration, no pose/throttle pipeline
    auto *ctrl = new VideoController(m_cameras[index].device, nullptr, this);

    const QString key = cameraKey(m_cameras[index]);
    AppSettings s;

    const QVariantMap roiMap = s.cameraRoi();
    if (roiMap.contains(key)) {
        const QVariantMap r = roiMap.value(key).toMap();
        double x = r.value(QStringLiteral("x")).toDouble();
        double y = r.value(QStringLiteral("y")).toDouble();
        double w = r.value(QStringLiteral("w")).toDouble();
        double h = r.value(QStringLiteral("h")).toDouble();
        if (w > 0 && h > 0)
            ctrl->setCropRoi(QRectF(x, y, w, h));
    }

    return ctrl;
}

void CameraManager::destroyPreviewController(QObject *ctrl)
{
    if (ctrl) ctrl->deleteLater();
}

VideoController *CameraManager::createController(const Device &device)
{
    auto *ctrl = new VideoController(device, m_eventBuffer, this);
    connect(ctrl, &VideoController::ballPresentChanged,
            this, &CameraManager::onCameraBallPresenceChanged);

    // Restore persisted ROI and perspective for this device.
    const QString key = device.description + QStringLiteral("|")
                        + (device.capabilities.serialNumber.isEmpty()
                               ? device.id
                               : device.capabilities.serialNumber);

    AppSettings s;

    const QVariantMap roiMap = s.cameraRoi();
    if (roiMap.contains(key)) {
        const QVariantMap r = roiMap.value(key).toMap();
        double x = r.value(QStringLiteral("x")).toDouble();
        double y = r.value(QStringLiteral("y")).toDouble();
        double w = r.value(QStringLiteral("w")).toDouble();
        double h = r.value(QStringLiteral("h")).toDouble();
        if (w > 0 && h > 0)
            ctrl->setCropRoi(QRectF(x, y, w, h));
    }

    const QVariantMap perspMap = s.cameraPerspective();
    if (perspMap.contains(key)) {
        int p = perspMap.value(key).toInt();
        if (p > 0)
            ctrl->setPerspective(p);
    }

    return ctrl;
}

void CameraManager::onCameraBallPresenceChanged(bool present)
{
    if (present)
        ++m_ballPresentCount;
    else
        --m_ballPresentCount;

    if (m_ballPresentCount < 0)
        m_ballPresentCount = 0;

    if (!m_recording)
        return;

    if (m_ballPresentCount > 0) {
        if (!m_replaying)
            resumeBuffer();
        // else: ball re-appeared during replay; resumeBuffer() fires in stopReplay()
    } else {
        if (!m_replaying) {
            pauseBuffer();
            startReplay();
        }
        // else: already replaying, ignore further ball-lost signals
    }
}

void CameraManager::startReplay()
{
    if (!m_eventBuffer || m_eventBuffer->state() != pinpoint::BufferState::Paused)
        return;

    auto window = m_eventBuffer->captureSwingWindow(std::chrono::milliseconds(5000));

    m_replayTracks.clear();
    for (auto &cam : m_cameras) {
        if (!cam.controller || !cam.selected) continue;
        const pinpoint::SourceId sid = cam.controller->sourceId();
        if (sid == pinpoint::kInvalidSourceId) continue;
        auto entries = window.entriesFor(sid);
        if (entries.empty()) continue;
        ReplayTrack track;
        track.ctrl     = cam.controller;
        track.sourceId = sid;
        track.entries  = std::move(entries);
        m_replayTracks.push_back(std::move(track));
    }

    if (m_replayTracks.empty())
        return; // nothing captured — stay paused, wait for next ball detection

    m_swingWindow.emplace(std::move(window));

    // Anchor to the actual first/last captured entry so replay starts immediately
    // rather than waiting for the (potentially empty) leading portion of the window.
    m_replayWindowStartUs = m_swingWindow->endTimestampUs();
    m_replayWindowEndUs   = m_swingWindow->startTimestampUs();
    for (const auto &track : m_replayTracks) {
        if (!track.entries.empty()) {
            m_replayWindowStartUs = std::min(m_replayWindowStartUs,
                                             track.entries.front().timestamp_us);
            m_replayWindowEndUs   = std::max(m_replayWindowEndUs,
                                             track.entries.back().timestamp_us);
        }
    }

    for (auto &track : m_replayTracks)
        track.ctrl->setReplaying(true);

    m_replaying = true;
    emit isReplayingChanged();
    emit bufferStateChanged();

    m_replayElapsed.start();
    m_replayTimer = new QTimer(this);
    m_replayTimer->setInterval(16); // ~60 Hz drive
    connect(m_replayTimer, &QTimer::timeout, this, &CameraManager::onReplayTick);
    m_replayTimer->start();
}

void CameraManager::onReplayTick()
{
    // Quarter speed: divide real elapsed time by 4 to get virtual footage time.
    const int64_t realElapsedUs   = m_replayElapsed.elapsed() * 1000LL;
    const int64_t virtualTimeUs   = realElapsedUs / 4;
    const int64_t footageDuration = m_replayWindowEndUs - m_replayWindowStartUs;

    if (virtualTimeUs >= footageDuration) {
        stopReplay();
        return;
    }

    for (auto &track : m_replayTracks) {
        // Advance to the newest frame whose offset from the first entry <= virtual time.
        while (track.idx + 1 < track.entries.size()) {
            const int64_t nextOffset =
                track.entries[track.idx + 1].timestamp_us - m_replayWindowStartUs;
            if (nextOffset <= virtualTimeUs)
                ++track.idx;
            else
                break;
        }

        const auto &entry  = track.entries[track.idx];
        const auto  handle = m_swingWindow->payloadOf(entry);
        if (!handle.data) continue;

        const auto &fd = m_swingWindow->formatOf(track.sourceId);
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format)) {
            track.ctrl->displayReplayFrame(
                handle.data,
                handle.bytes,
                static_cast<int>(cfmt->width),
                static_cast<int>(cfmt->height),
                cfmt->pixel_format);
        }
    }
}

void CameraManager::stopReplay(bool autoResume)
{
    if (!m_replaying) return;

    if (m_replayTimer) {
        m_replayTimer->stop();
        m_replayTimer->deleteLater();
        m_replayTimer = nullptr;
    }

    for (auto &track : m_replayTracks)
        track.ctrl->setReplaying(false);
    m_replayTracks.clear();

    m_swingWindow.reset(); // must be destroyed before any resume()
    m_replaying = false;
    emit isReplayingChanged();
    emit bufferStateChanged();

    if (autoResume && m_recording && m_ballPresentCount > 0)
        resumeBuffer();
}
