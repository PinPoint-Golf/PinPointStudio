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

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <optional>
#include <vector>

#include "device_enumerator.h"
#include "swing_window.h"
#include "app_settings.h"
#include "../Video/camera_capabilities.h"

namespace pinpoint { class EventBuffer; }
class VideoController;

class CameraManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList cameraList  READ cameraList  NOTIFY cameraListChanged)
    Q_PROPERTY(QVariantList instances   READ instances   NOTIFY instancesChanged)
    Q_PROPERTY(bool isRecording         READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool anySelected         READ anySelected NOTIFY instancesChanged)
    Q_PROPERTY(QString bufferState      READ bufferState NOTIFY bufferStateChanged)
    Q_PROPERTY(bool isReplaying         READ isReplaying NOTIFY isReplayingChanged)

public:
    explicit CameraManager(pinpoint::EventBuffer *buffer = nullptr,
                           AppSettings *appSettings = nullptr,
                           QObject *parent = nullptr);
    ~CameraManager() override;

    QVariantList cameraList() const;
    QVariantList instances()  const;
    bool isRecording()        const;
    bool anySelected()        const;
    QString bufferState()     const;
    bool isReplaying()        const;

    // Returns the live VideoController for a given device ID, or nullptr when
    // the device is enumerated but not selected.  Mirrors ImuManager::instanceFor().
    VideoController *controllerFor(const QString &deviceId) const;

    // Snapshot of live per-device stats for monitoring purposes.
    // Avoids exposing VideoController to callers that only need metrics.
    struct CameraDeviceStats {
        pinpoint::SourceId sourceId  = pinpoint::kInvalidSourceId;
        double             fps       = 0.0;
        int                width     = 0;
        int                height    = 0;
        bool               recording = false;
        QRectF             cropRoi;
    };
    CameraDeviceStats liveDeviceStats(const QString &deviceId) const;

    Q_INVOKABLE void setSelected(int index, bool selected);
    Q_INVOKABLE void startAll();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void setCameraAlias(const QString &key, const QString &alias);
    Q_INVOKABLE void pauseBuffer();
    Q_INVOKABLE void resumeBuffer();
    Q_INVOKABLE void enumerate();

    // Sets the perspective on one camera and clears it from any other camera
    // that currently has the same non-zero perspective value.
    Q_INVOKABLE void setPerspective(QObject *controller, int perspective);

    Q_INVOKABLE void setExcluded(int index, bool excluded);
    Q_INVOKABLE void setTargetFps(int index, double fps);
    Q_INVOKABLE void setTriggerMode(int index, const QString &mode);

    // Creates a lightweight preview-only VideoController (no event buffer, no pose
    // pipeline) for use in the settings panel crop UI.  The returned object is owned
    // by this CameraManager and must be released via destroyPreviewController() when done.
    Q_INVOKABLE QObject *createPreviewController(int index);
    Q_INVOKABLE void     destroyPreviewController(QObject *ctrl);

signals:
    void cameraListChanged();
    void instancesChanged();
    void isRecordingChanged();
    void bufferStateChanged();
    void isReplayingChanged();

private:
    struct CameraEntry {
        Device device;
        bool selected = false;
        VideoController *controller = nullptr;
        bool excluded = false;
        double targetFps = 0.0;
        QString triggerMode = QStringLiteral("freerun");
    };

    // Stable per-device key for settings storage.
    // Format: "ModelName|SerialNumber" so keys are human-readable in QSettings and
    // immune to cross-manufacturer serial-number collisions.
    // Falls back to "ModelName|DeviceId" when no serial is available.
    static QString cameraKey(const CameraEntry &cam)
    {
        const QString model = cam.device.description;
        const QString sn    = cam.device.capabilities.serialNumber;
        const QString id    = sn.isEmpty() ? cam.device.id : sn;
        return model + QStringLiteral("|") + id;
    }

    static QString interfaceString(CameraCapabilities::Interface iface)
    {
        switch (iface) {
        case CameraCapabilities::Interface::USB2:  return QStringLiteral("USB2");
        case CameraCapabilities::Interface::USB3:  return QStringLiteral("USB3");
        case CameraCapabilities::Interface::GigE:
        case CameraCapabilities::Interface::GigE5:
        case CameraCapabilities::Interface::GigE10: return QStringLiteral("GigE");
        default:                                    return QStringLiteral("Unknown");
        }
    }

    struct ReplayTrack {
        VideoController                   *ctrl      = nullptr;
        pinpoint::SourceId                 sourceId  = pinpoint::kInvalidSourceId;
        std::vector<pinpoint::IndexEntry>  entries;
        size_t                             idx       = 0;
    };

    QList<CameraEntry>                   m_cameras;
    bool                                 m_recording        = false;
    int                                  m_ballPresentCount = 0;
    pinpoint::EventBuffer               *m_eventBuffer      = nullptr;
    AppSettings                         *m_appSettings      = nullptr;
    bool                                 m_replaying        = false;
    std::optional<pinpoint::SwingWindow> m_swingWindow;
    int64_t                              m_replayWindowStartUs = 0;
    int64_t                              m_replayWindowEndUs   = 0;
    QElapsedTimer                        m_replayElapsed;
    QTimer                              *m_replayTimer      = nullptr;
    std::vector<ReplayTrack>             m_replayTracks;

    VideoController *createController(const Device &device);
    void startReplay();
    void stopReplay(bool autoResume = true);

private slots:
    void onCameraBallPresenceChanged(bool present);
    void onReplayTick();
};
