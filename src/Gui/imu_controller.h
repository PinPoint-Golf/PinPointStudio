#pragma once

#include <QObject>
#include <QList>
#include <QStringList>
#include <QTimer>
#include "wt9011dcl_ble.h"

class ImuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateLabel   READ stateLabel   NOTIFY stateLabelChanged)
    Q_PROPERTY(bool    imuConnected READ imuConnected NOTIFY imuConnectedChanged)
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
    Q_PROPERTY(int     batteryPercent READ batteryPercent NOTIFY batteryPercentChanged)

public:
    explicit ImuController(QObject *parent = nullptr);

    QString stateLabel()   const { return m_stateLabel; }
    bool    imuConnected() const { return m_connected; }
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
    int     outputRateHz()   const { return m_outputRateHz; }
    double  dataRateHz()     const { return m_dataRateHz; }
    int     batteryPercent() const { return m_batteryPercent; }

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
    void logEntryAdded(const QString &entry);

private:
    static QString timestamp();
    void appendLog(const QString &text);
    void setStateLabel(const QString &s);
    void onStateChanged(WT9011DCL_BLE::State s);
    void onDataRecord();  // call once per received combined frame

    WT9011DCL_BLE *m_imu;
    QStringList    m_logEntries;

    // Retry logic
    QTimer m_retryTimer;
    int    m_retryCount     = 0;
    bool   m_inConnectPhase = false;
    bool   m_attemptingConn = false;

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
