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

    if (wasCapturing)
        m_eventBuffer->resume();

    emit cameraListChanged();
    emit instancesChanged();
}

void CameraManager::startAll()
{
    if (m_recording) return;
    m_recording = true;
    emit isRecordingChanged();

    // Buffer is already Capturing (started in main.cpp at app launch).
    // Start camera recording pipelines only — do NOT touch the buffer lifecycle.
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->startRecording();
    }

    emit bufferStateChanged();
}

void CameraManager::stopAll()
{
    if (!m_recording) return;
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->stopRecording();
    }
    m_recording = false;
    emit isRecordingChanged();
    // Buffer remains Capturing — do NOT call m_eventBuffer->stop().
    // Ring memory stays allocated. Next startAll() resumes instantly.
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
    return new VideoController(device, m_eventBuffer, this);
}
