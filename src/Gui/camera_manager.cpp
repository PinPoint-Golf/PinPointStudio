#include "camera_manager.h"

#include "video_controller.h"
#include "video_input_factory.h"

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
{
    VideoInputFactory::enumerateDevices();

    const QList<Device> allDevices = DeviceEnumerator::instance()->devices();
    for (const Device &dev : allDevices) {
        if (dev.type == DeviceType::VideoInput)
            m_cameras.append({dev, false, nullptr});
    }

    // Auto-select and create controller for the first camera.
    if (!m_cameras.isEmpty()) {
        m_cameras[0].selected = true;
        m_cameras[0].controller = createController(m_cameras[0].device);
    }
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
    if (index < 0 || index >= m_cameras.size())
        return;
    if (m_cameras[index].selected == selected)
        return;

    m_cameras[index].selected = selected;

    if (selected) {
        m_cameras[index].controller = createController(m_cameras[index].device);
        if (m_recording)
            m_cameras[index].controller->startRecording();
    } else {
        VideoController *ctrl = m_cameras[index].controller;
        m_cameras[index].controller = nullptr;

        // Notify QML first so the Repeater removes the delegate while the
        // controller object is still alive.  deleteLater() then runs after the
        // current event-loop turn, by which point the CameraView bindings are
        // already gone and cannot dereference the deleted pointer.
        emit cameraListChanged();
        emit instancesChanged();

        if (ctrl) {
            if (m_recording)
                ctrl->stopRecording();
            ctrl->deleteLater();
        }
        return;
    }

    emit cameraListChanged();
    emit instancesChanged();
}

void CameraManager::startAll()
{
    if (m_recording)
        return;
    m_recording = true;
    emit isRecordingChanged();
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->startRecording();
    }
}

void CameraManager::stopAll()
{
    if (!m_recording)
        return;
    for (auto &cam : m_cameras) {
        if (cam.selected && cam.controller)
            cam.controller->stopRecording();
    }
    m_recording = false;
    emit isRecordingChanged();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

VideoController *CameraManager::createController(const Device &device)
{
    return new VideoController(device, this);
}
