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
#include <QQuaternion>
#include <QStringList>
#include <QTimer>

#include "device_enumerator.h"
#include "types.h"
#include "wt9011dcl_ble.h"

namespace pinpoint { class EventBuffer; }

// Manages the BLE connection lifecycle and live data for a single IMU device.
// Created and owned by ImuManager; one instance per selected device.
class ImuInstance : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString stateLabel     READ stateLabel     NOTIFY stateLabelChanged)
    Q_PROPERTY(bool    imuConnected   READ imuConnected   NOTIFY imuConnectedChanged)
    Q_PROPERTY(bool    busy           READ busy           NOTIFY busyChanged)
    Q_PROPERTY(float   quatW          READ quatW          NOTIFY quatChanged)
    Q_PROPERTY(float   quatX          READ quatX          NOTIFY quatChanged)
    Q_PROPERTY(float   quatY          READ quatY          NOTIFY quatChanged)
    Q_PROPERTY(float   quatZ          READ quatZ          NOTIFY quatChanged)
    Q_PROPERTY(float   accelX         READ accelX         NOTIFY accelChanged)
    Q_PROPERTY(float   accelY         READ accelY         NOTIFY accelChanged)
    Q_PROPERTY(float   accelZ         READ accelZ         NOTIFY accelChanged)
    Q_PROPERTY(int     outputRateHz   READ outputRateHz   NOTIFY outputRateHzChanged)
    Q_PROPERTY(double  dataRateHz     READ dataRateHz     NOTIFY dataRateHzChanged)
    Q_PROPERTY(int     batteryPercent  READ batteryPercent  NOTIFY batteryPercentChanged)
    Q_PROPERTY(int     gimbalDropCount READ gimbalDropCount NOTIFY gimbalDropCountChanged)
    Q_PROPERTY(bool  stable             READ stable             NOTIFY stableChanged)
    Q_PROPERTY(float angularVelocityDps READ angularVelocityDps NOTIFY angularVelocityDpsChanged)
    Q_PROPERTY(QString     deviceId          READ deviceId          CONSTANT)
    Q_PROPERTY(QString     deviceDescription READ deviceDescription CONSTANT)
    Q_PROPERTY(QStringList logEntries        READ logEntries        CONSTANT)
    Q_PROPERTY(bool        calibrated    READ calibrated    NOTIFY calibratedChanged)
    Q_PROPERTY(QQuaternion calibArmDown  READ calibArmDown  NOTIFY calibratedChanged)
    Q_PROPERTY(QQuaternion calibArmTPose READ calibArmTPose NOTIFY calibratedChanged)

public:
    explicit ImuInstance(const Device &device,
                         pinpoint::EventBuffer *buffer,
                         QObject *parent = nullptr);
    ~ImuInstance() override;

    // Identification (C++ callers and ResourceMonitor)
    QString            deviceId()          const { return m_deviceId; }
    QString            deviceDescription() const { return m_deviceDescription; }
    QStringList        logEntries()        const { return m_logEntries; }
    pinpoint::SourceId sourceId()          const { return m_imuSourceId; }

    // State
    QString stateLabel()     const { return m_stateLabel; }
    bool    imuConnected()   const { return m_connected; }
    bool    busy()           const { return m_busy; }
    float   quatW()          const { return m_quatW; }
    float   quatX()          const { return m_quatX; }
    float   quatY()          const { return m_quatY; }
    float   quatZ()          const { return m_quatZ; }
    float   accelX()         const { return m_accelX; }
    float   accelY()         const { return m_accelY; }
    float   accelZ()         const { return m_accelZ; }
    int     outputRateHz()      const { return m_outputRateHz; }
    double  dataRateHz()        const { return m_dataRateHz; }
    int     batteryPercent()    const { return m_batteryPercent; }
    int     gimbalDropCount()   const { return m_gimbalDropCount; }
    bool    stable()            const { return m_stable; }
    float   angularVelocityDps() const { return m_angularVelocityDps; }
    bool        calibrated()    const { return m_calibrated; }
    QQuaternion calibArmDown()  const { return m_calibArmDown; }
    QQuaternion calibArmTPose() const { return m_calibArmTPose; }

    // Lifecycle — called by ImuManager
    void start();                // begin BLE connection
    void stop();                 // disconnect and cancel any retry
    void deregisterFromBuffer(); // call while EventBuffer is paused

    // QML-invokable per-device actions
    Q_INVOKABLE void    zeroOrientation();
    Q_INVOKABLE QString saveLog();
    Q_INVOKABLE void    setOutputRateHz(int hz);
    Q_INVOKABLE void    setCalibration(const QQuaternion &armDown, const QQuaternion &tPose);
    Q_INVOKABLE void    clearCalibration();

signals:
    void stateLabelChanged();
    void imuConnectedChanged();
    void busyChanged();
    void quatChanged();
    void accelChanged();
    void outputRateHzChanged();
    void dataRateHzChanged();
    void batteryPercentChanged();
    void gimbalDropCountChanged();
    void stableChanged();
    void angularVelocityDpsChanged();
    void calibratedChanged();
    void logEntryAdded(const QString &entry);

private:
    static QString timestamp();
    void appendLog(const QString &text);
    void setStateLabel(const QString &s);
    void onStateChanged(WT9011DCL_BLE::State s);
    void onDataRecord();

    pinpoint::EventBuffer *m_eventBuffer  = nullptr;
    pinpoint::SourceId     m_imuSourceId  = pinpoint::kInvalidSourceId;

    WT9011DCL_BLE *m_imu               = nullptr;
    Device         m_device;
    QString        m_deviceId;
    QString        m_deviceDescription;
    QStringList    m_logEntries;

    // Retry
    QTimer m_retryTimer;
    int    m_retryCount     = 0;
    bool   m_inConnectPhase = false;
    bool   m_attemptingConn = false;

    // Battery polling
    QTimer m_batteryTimer;
    int    m_batteryRetries = 0;
    static constexpr int kMaxBatteryRetries = 3;

    // Gimbal drop counter polling — fires while connected so the count updates
    // even when all packets are being dropped (no quaternionUpdated to piggyback on).
    QTimer m_gimbalPollTimer;
    static constexpr int kGimbalPollIntervalMs = 200;

    // State
    QString m_stateLabel = QStringLiteral("Disconnected");
    bool    m_connected  = false;
    bool    m_busy       = false;

    // IMU data
    float m_quatW = 1.0f, m_quatX = 0.0f, m_quatY = 0.0f, m_quatZ = 0.0f;
    float m_accelX = 0.0f, m_accelY = 0.0f, m_accelZ = 0.0f;
    int   m_outputRateHz   = 100;
    int   m_batteryPercent  = -1;
    int   m_gimbalDropCount = 0;

    // Rolling 2-second data-rate window
    QList<qint64> m_packetTimes;
    double        m_dataRateHz = 0.0;

    // Stability detection — computed on the main thread via the queued connection
    bool          m_stable             = false;
    float         m_angularVelocityDps = 0.0f;
    QQuaternion   m_prevQuat           { 1.0f, 0.0f, 0.0f, 0.0f };
    QElapsedTimer m_prevQuatTimer;   // elapsed since last quaternion packet
    QElapsedTimer m_stableTimer;     // how long velocity has been below threshold
    bool          m_stableTimerRunning = false;
    static constexpr float kStableThresholdDps = 5.0f;
    static constexpr int   kStableDurationMs   = 2000;

    // Session calibration — arm-down and T-pose reference quaternions.
    // Set by the session wizard; valid only for the life of this instance.
    bool        m_calibrated    = false;
    QQuaternion m_calibArmDown  { 1.0f, 0.0f, 0.0f, 0.0f };
    QQuaternion m_calibArmTPose { 1.0f, 0.0f, 0.0f, 0.0f };

    // Log throttle — summary every 10 s
    QTimer m_logTimer;
    int    m_totalRecords    = 0;
    int    m_recordsSinceLog = 0;

    static constexpr int kMaxRetries      = 1;
    static constexpr int kRetryDelayMs    = 30'000;
    static constexpr int kLogIntervalMs   = 10'000;
    static constexpr int kRollingWindowMs = 2'000;
};
