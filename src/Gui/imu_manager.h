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

#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariantList>

#include "app_settings.h"
#include "device_enumerator.h"
#include "imu_instance.h"
#include "types.h"

namespace pinpoint { class EventBuffer; }

class ShotProcessor;

// Manages N ImuInstance objects, one per user-selected IMU device.
// Mirrors the CameraManager pattern: imuList / instances / setSelected().
//
// Device discovery is the authority of DeviceEnumerator — imuList() and
// imuDeviceList() always read from it so they stay current even before
// deviceAdded signals fire. m_selected is a map of device-id → ImuEntry
// tracking only selection state and live instances.
class ImuManager : public QObject
{
    Q_OBJECT

    // ── Per-device chip list (includes runtime connected/connecting state) ──
    Q_PROPERTY(QVariantList imuList     READ imuList     NOTIFY imuListChanged)
    // ── Active instances exposed as QObject* for QML Repeater ─────────────
    Q_PROPERTY(QVariantList instances   READ instances   NOTIFY instancesChanged)
    Q_PROPERTY(bool         anySelected READ anySelected NOTIFY instancesChanged)
    // ── Enumeration / capability list for the Settings ImusPanel ──────────
    Q_PROPERTY(int          imuEnumeratedCount READ imuEnumeratedCount NOTIFY imuEnumeratedCountChanged)
    Q_PROPERTY(QVariantList imuDeviceList      READ imuDeviceList      NOTIFY imuDeviceListChanged)
    // ── Aggregate state (any connected / count) ───────────────────────────
    Q_PROPERTY(bool imuConnected READ imuConnected NOTIFY instancesChanged)
    Q_PROPERTY(int  imuCount     READ imuCount     NOTIFY instancesChanged)
    // Lowest battery % across all *connected* IMUs (−1 when none report a
    // level). Drives the toolbar IMU-pill low-battery warning. Notifies on live
    // battery updates as well as connect/disconnect (both change the minimum).
    Q_PROPERTY(int  lowBatteryPercent READ lowBatteryPercent NOTIFY batteryChanged)
    // ── Per-session IMU enablement (device ids excluded this session) ──────
    // Mirrors CameraManager::sessionCameraExcluded: manager-owned so the start
    // wizard, every toolbar IMU panel and the device rows share ONE list.
    // Seeded from AppSettings::imuExcluded at startup (and re-seeded by the
    // wizard on open); NEVER written back — global enablement is owned by the
    // Settings screen.
    Q_PROPERTY(QStringList sessionImuExcluded READ sessionImuExcluded NOTIFY sessionImuExcludedChanged)

public:
    explicit ImuManager(pinpoint::EventBuffer *buffer = nullptr,
                        AppSettings *appSettings = nullptr,
                        QObject *parent = nullptr);
    ~ImuManager() override;

    // Both list accessors read from DeviceEnumerator directly (the old pattern)
    // so they are always current regardless of deviceAdded signal timing.
    QVariantList imuList()            const;
    QVariantList imuDeviceList()      const;
    QVariantList instances()          const;
    bool         anySelected()        const;
    int          imuEnumeratedCount() const;
    bool         imuConnected()       const;
    int          imuCount()           const;
    int          lowBatteryPercent()  const;

    // Teardown stop-barrier (same contract as CameraManager): deregistering an
    // IMU source while a SwingWindow is live frees ring memory under the shot
    // workers. setSelected(deselect) and the destructor call
    // finishNowBlocking() first. Set from main.cpp.
    void setShotProcessor(ShotProcessor *p) { m_shotProcessor = p; }

    // Toggle selection (= connect/disconnect + EventBuffer register/deregister).
    // index is the position in DeviceEnumerator::devices(DeviceType::Imu).
    Q_INVOKABLE void setSelected(int index, bool selected);

    QStringList sessionImuExcluded() const;

    // Per-session enablement toggle. Disabling a selected/connected device also
    // disconnects it; enabling never auto-connects (Connect does that).
    Q_INVOKABLE void setSessionImuEnabled(const QString &deviceId, bool on);

    // Trigger a new BLE scan to find devices.
    Q_INVOKABLE void rescanImu();

    // Persist a user-visible alias for a device. key = description|id.
    // Pass empty alias to revert to default (device description).
    Q_INVOKABLE void setImuAlias(const QString &key, const QString &alias);

    // Select the local orientation-fusion algorithm ("Madgwick" / "ESKF") for all
    // IMUs: persists to AppSettings and pushes the change to every live instance.
    Q_INVOKABLE void setOrientationFilter(const QString &name);

    // Returns the live ImuInstance QObject* for deviceId, or nullptr if not selected.
    Q_INVOKABLE QObject *instanceFor(const QString &deviceId) const;

    // Snapshot of live per-device stats for monitoring purposes.
    // Avoids exposing ImuInstance to callers that only need metrics.
    struct ImuDeviceStats {
        pinpoint::SourceId sourceId       = pinpoint::kInvalidSourceId;
        double             dataRateHz     = 0.0;
        int                batteryPercent = -1;
        int                gimbalDropCount = 0;
        bool               connected      = false;
        bool               busy           = false;
    };
    ImuDeviceStats liveDeviceStats(const QString &deviceId) const;

    // Aggregate save — writes one log file per active instance.
    Q_INVOKABLE QString saveLog();

    // Zero orientation on all connected instances.
    Q_INVOKABLE void zeroAll();

signals:
    void imuListChanged();
    void instancesChanged();
    void imuEnumeratedCountChanged();
    void imuDeviceListChanged();
    void sessionImuExcludedChanged();
    // Aggregate battery state changed — a connected IMU reported a new level, or
    // the connected set changed. Backs the lowBatteryPercent property.
    void batteryChanged();
    // EventBuffer state may have changed (source register/deregister can pause
    // or auto-resume the shared buffer). Forwarded to
    // CameraManager::applyCaptureIntent in main() — the QML-facing buffer state
    // lives on CameraManager.
    void bufferStateChanged();
    // Aggregated log entries forwarded from all active instances.
    void logEntryAdded(const QString &entry);

    // IMU impact auto-trigger (shot detection P1) — re-emitted from every
    // live ImuInstance; main.cpp routes it to ShotController::triggerShot
    // behind the autoDetectSwing setting.
    void impactDetected(qint64 estImpactUs, float confidence);

private:
    struct ImuEntry {
        bool         selected = false;
        ImuInstance *instance = nullptr;
    };

    ImuInstance *createInstance(const Device &device);
    // swingDetectionSensitivity ("Low"/"Medium"/"High") → detector threshold scale.
    static float impactScaleFor(const QString &sensitivity);

    pinpoint::EventBuffer      *m_eventBuffer  = nullptr;
    AppSettings                *m_appSettings  = nullptr;
    ShotProcessor              *m_shotProcessor = nullptr;
    QMap<QString, ImuEntry>     m_selected;    // keyed by device id

    // Per-session exclusion (device ids). Seeded from AppSettings::imuExcluded;
    // m_lastGlobalExcluded snapshots the global list so mid-session Settings
    // changes can be diffed into the session list (CameraManager::setExcluded
    // does the same sync for cameras).
    QStringList m_sessionExcluded;
    QStringList m_lastGlobalExcluded;
};
