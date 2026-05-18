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

CameraManager::CameraManager(pinpoint::EventBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_eventBuffer(buffer)
{
    VideoInputFactory::enumerateDevices();

    const QList<Device> allDevices = DeviceEnumerator::instance()->devices();
    for (const Device &dev : allDevices) {
        if (dev.type == DeviceType::VideoInput)
            m_cameras.append({dev, false, nullptr});
    }

    // Auto-select the first camera via setSelected() so the buffer is correctly
    // paused around registerSource() even when start() has already been called.
    if (!m_cameras.isEmpty())
        setSelected(0, true);
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
        QVariantMap entry;
        entry[QStringLiteral("index")]       = i;
        entry[QStringLiteral("description")] = m_cameras[i].device.description;
        entry[QStringLiteral("selected")]    = m_cameras[i].selected;
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

void CameraManager::setPerspective(QObject *rawController, int perspective)
{
    auto *target = qobject_cast<VideoController *>(rawController);
    if (!target)
        return;

    // Clear this perspective from every other camera that currently holds it.
    if (perspective > 0) {
        for (auto &cam : m_cameras) {
            if (cam.controller && cam.controller != target
                && cam.controller->perspective() == perspective)
            {
                cam.controller->setPerspective(0);
            }
        }
    }

    target->setPerspective(perspective);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

VideoController *CameraManager::createController(const Device &device)
{
    auto *ctrl = new VideoController(device, m_eventBuffer, this);
    connect(ctrl, &VideoController::ballPresentChanged,
            this, &CameraManager::onCameraBallPresenceChanged);
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
