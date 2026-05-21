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
#include <QList>
#include <QStringList>
#include <QTimer>
#include "wt9011dcl_ble.h"
#include "device_enumerator.h"
#include "types.h"

namespace pinpoint { class EventBuffer; }

class ImuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateLabel   READ stateLabel   NOTIFY stateLabelChanged)
    Q_PROPERTY(bool    imuConnected READ imuConnected NOTIFY imuConnectedChanged)
    Q_PROPERTY(int     imuCount     READ imuCount     NOTIFY imuConnectedChanged)
    Q_PROPERTY(bool    busy         READ busy         NOTIFY busyChanged)
    Q_PROPERTY(float   quatW        READ quatW        NOTIFY quatChanged)
    Q_PROPERTY(float   quatX        READ quatX        NOTIFY quatChanged)
    Q_PROPERTY(float   quatY        READ quatY        NOTIFY quatChanged)
    Q_PROPERTY(float   quatZ        READ quatZ        NOTIFY quatChanged)
    Q_PROPERTY(float   accelX       READ accelX       NOTIFY accelChanged)
    Q_PROPERTY(float   accelY       READ accelY       NOTIFY accelChanged)
    Q_PROPERTY(float   accelZ       READ accelZ       NOTIFY accelChanged)
    Q_PROPERTY(float   imuRoll      READ imuRoll      NOTIFY eulerChanged)
    Q_PROPERTY(float   imuPitch     READ imuPitch     NOTIFY eulerChanged)
    Q_PROPERTY(float   imuYaw       READ imuYaw       NOTIFY eulerChanged)
    Q_PROPERTY(int     outputRateHz  READ outputRateHz  NOTIFY outputRateHzChanged)
    Q_PROPERTY(double  dataRateHz    READ dataRateHz    NOTIFY dataRateHzChanged)
    Q_PROPERTY(int     batteryPercent       READ batteryPercent       NOTIFY batteryPercentChanged)
    Q_PROPERTY(int     imuEnumeratedCount  READ imuEnumeratedCount   NOTIFY imuEnumeratedCountChanged)

public:
    explicit ImuController(pinpoint::EventBuffer *buffer = nullptr,
                           QObject *parent = nullptr);

    QString stateLabel()   const { return m_stateLabel; }
    bool    imuConnected() const { return m_connected; }
    int     imuCount()     const { return m_connected ? 1 : 0; }
    bool    busy()         const { return m_busy; }
    float   quatW()        const { return m_quatW; }
    float   quatX()        const { return m_quatX; }
    float   quatY()        const { return m_quatY; }
    float   quatZ()        const { return m_quatZ; }
    float   accelX()       const { return m_accelX; }
    float   accelY()       const { return m_accelY; }
    float   accelZ()       const { return m_accelZ; }
    float   imuRoll()      const { return m_roll; }
    float   imuPitch()     const { return m_pitch; }
    float   imuYaw()       const { return m_yaw; }
    int     outputRateHz()        const { return m_outputRateHz; }
    double  dataRateHz()          const { return m_dataRateHz; }
    int     batteryPercent()      const { return m_batteryPercent; }
    int     imuEnumeratedCount()  const;

    Q_INVOKABLE void connectImu();
    Q_INVOKABLE void disconnectImu();
    Q_INVOKABLE void zeroOrientation();
    Q_INVOKABLE QString saveLog();
    Q_INVOKABLE void setOutputRateHz(int hz);

signals:
    void stateLabelChanged();
    void imuConnectedChanged();
    void busyChanged();
    void quatChanged();
    void eulerChanged();
    void accelChanged();
    void outputRateHzChanged();
    void dataRateHzChanged();
    void batteryPercentChanged();
    void imuEnumeratedCountChanged();
    void logEntryAdded(const QString &entry);

private:
    static QString timestamp();
    void appendLog(const QString &text);
    void setStateLabel(const QString &s);
    void onStateChanged(WT9011DCL_BLE::State s);
    void onDataRecord();  // call once per received combined frame
    void connectToEnumeratedDevice(const Device &dev);

    pinpoint::EventBuffer *m_eventBuffer  = nullptr;
    pinpoint::SourceId     m_imuSourceId = pinpoint::kInvalidSourceId;

    WT9011DCL_BLE *m_imu;
    QStringList    m_logEntries;

    // Retry logic
    QTimer m_retryTimer;
    int    m_retryCount     = 0;
    bool   m_inConnectPhase = false;
    bool   m_attemptingConn = false;
    bool   m_waitingForScan = false; // true while waiting for startup BLE scan to find a device

    // Battery polling
    QTimer m_batteryTimer;
    int    m_batteryRetries = 0;
    static constexpr int kMaxBatteryRetries = 3;

    // State
    QString m_stateLabel = QStringLiteral("Disconnected");
    bool    m_connected  = false;
    bool    m_busy       = false;

    // IMU data
    float m_quatW = 1.0f, m_quatX = 0.0f, m_quatY = 0.0f, m_quatZ = 0.0f;
    float m_roll  = 0.0f, m_pitch = 0.0f, m_yaw   = 0.0f;
    float m_accelX = 0.0f, m_accelY = 0.0f, m_accelZ = 0.0f;
    int   m_outputRateHz  = 100;
    int   m_batteryPercent = -1;  // -1 = no reading yet

    // Data rate — 30s rolling window of packet arrival timestamps (ms)
    QList<qint64> m_packetTimes;
    double        m_dataRateHz    = 0.0;

    // Log throttle — summary every 10s
    QTimer m_logTimer;
    int    m_totalRecords      = 0;
    int    m_recordsSinceLog   = 0;

    static constexpr int kMaxRetries   = 1;
    static constexpr int kRetryDelayMs = 45'000;
    static constexpr int kLogIntervalMs = 10'000;
    static constexpr int kRollingWindowMs = 2'000;
};
