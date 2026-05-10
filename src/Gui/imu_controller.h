#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include "wt9011dcl_ble.h"

class ImuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateLabel  READ stateLabel  NOTIFY stateLabelChanged)
    Q_PROPERTY(bool    imuConnected READ imuConnected NOTIFY imuConnectedChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY busyChanged)
    Q_PROPERTY(float   quatW       READ quatW       NOTIFY quatChanged)
    Q_PROPERTY(float   quatX       READ quatX       NOTIFY quatChanged)
    Q_PROPERTY(float   quatY       READ quatY       NOTIFY quatChanged)
    Q_PROPERTY(float   quatZ       READ quatZ       NOTIFY quatChanged)
    Q_PROPERTY(float   imuRoll    READ imuRoll    NOTIFY eulerChanged)
    Q_PROPERTY(float   imuPitch   READ imuPitch   NOTIFY eulerChanged)
    Q_PROPERTY(float   imuYaw     READ imuYaw     NOTIFY eulerChanged)
    Q_PROPERTY(int     outputRateHz READ outputRateHz NOTIFY outputRateHzChanged)

public:
    explicit ImuController(QObject *parent = nullptr);

    QString stateLabel()  const { return m_stateLabel; }
    bool    imuConnected() const { return m_connected; }
    bool    busy()         const { return m_busy; }
    float   quatW()        const { return m_quatW; }
    float   quatX()        const { return m_quatX; }
    float   quatY()        const { return m_quatY; }
    float   quatZ()        const { return m_quatZ; }
    float   imuRoll()      const { return m_roll; }
    float   imuPitch()     const { return m_pitch; }
    float   imuYaw()       const { return m_yaw; }
    int     outputRateHz() const { return m_outputRateHz; }

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
    void outputRateHzChanged();
    void logEntryAdded(const QString &entry);

private:
    static QString timestamp();
    void appendLog(const QString &text);
    void setStateLabel(const QString &s);
    void onStateChanged(WT9011DCL_BLE::State s);

    WT9011DCL_BLE *m_imu;
    QStringList m_logEntries;
    QTimer      m_retryTimer;
    int         m_retryCount      = 0;
    bool        m_inConnectPhase  = false; // true from Connecting until Ready/Disconnected
    QString m_stateLabel      = QStringLiteral("Disconnected");
    bool    m_connected       = false;
    bool    m_busy            = false;
    bool    m_attemptingConn  = false;  // prevent multiple connect attempts per scan
    float   m_quatW = 1.0f, m_quatX = 0.0f, m_quatY = 0.0f, m_quatZ = 0.0f;
    float   m_roll = 0.0f, m_pitch = 0.0f, m_yaw = 0.0f;
    int     m_outputRateHz = 100;

    static constexpr int kMaxRetries    = 1;
    static constexpr int kRetryDelayMs  = 45'000;
};
