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

#include "wt9011dcl_base.h"
#include <QSerialPort>

// WT9011DCL driver — UART/serial transport.
//
// Default settings: 115200 baud, 8N1.
//
// Usage:
//   WT9011DCL imu;
//   connect(&imu, &WT9011DCL::eulerAnglesUpdated, ...);
//   imu.open("/dev/ttyUSB0");

class WT9011DCL : public WT9011DCL_Base
{
    Q_OBJECT

public:
    explicit WT9011DCL(QObject *parent = nullptr);
    ~WT9011DCL() override;

    bool    open(const QString &portName, qint32 baudRate = 115200);
    void    close();
    bool    isOpen() const;
    QString portName() const;

protected:
    void writeToDevice(const QByteArray &data) override;

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    QSerialPort *m_serial;
};
