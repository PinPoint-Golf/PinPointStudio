#pragma once

#include <QObject>
#include "wt9011dcl_ble.h"

class ImuController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stateLabel  READ stateLabel  NOTIFY stateLabelChanged)
    Q_PROPERTY(bool    imuConnected READ imuConnected NOTIFY imuConnectedChanged)
    Q_PROPERTY(bool    busy        READ busy        NOTIFY busyChanged)

public:
    explicit ImuController(QObject *parent = nullptr);

    QString stateLabel()  const { return m_stateLabel; }
    bool    imuConnected() const { return m_connected; }
    bool    busy()         const { return m_busy; }

    Q_INVOKABLE void connectImu();
    Q_INVOKABLE void disconnectImu();

signals:
    void stateLabelChanged();
    void imuConnectedChanged();
    void busyChanged();
    void logEntryAdded(const QString &entry);

private:
    static QString timestamp();
    void appendLog(const QString &text);
    void setStateLabel(const QString &s);
    void onStateChanged(WT9011DCL_BLE::State s);

    WT9011DCL_BLE *m_imu;
    QString m_stateLabel      = QStringLiteral("Disconnected");
    bool    m_connected       = false;
    bool    m_busy            = false;
    bool    m_attemptingConn  = false;  // prevent multiple connect attempts per scan
};
