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

#include "wt9011dcl.h"
#include "imu_capabilities.h"

ImuCapabilities WT9011DCL::capabilities() const
{
    ImuCapabilities caps = wt901Defaults();
    caps.modelName    = QStringLiteral("WT9011DCL");
    caps.transport    = Transport::Serial;

    // Standard 0x5x packet stream includes magnetometer and temperature
    caps.hasMagnetometer = true;
    caps.hasTemperature  = true;
    caps.magRange        = { -1000.0f, 1000.0f }; // raw ADC; ÷120 ≈ µT

    // No battery register on serial-connected device
    caps.hasBattery = false;

    // Serial transport supports baud rate and output data selection
    caps.supportsBaudRateControl     = true;   // BAUD register
    caps.supportsOutputDataSelection = true;   // RSW register

    caps.queriedAt = QDateTime::currentDateTime();
    return caps;
}

WT9011DCL::WT9011DCL(QObject *parent)
    : WT9011DCL_Base(parent)
    , m_serial(new QSerialPort(this))
{
    connect(m_serial, &QSerialPort::readyRead,
            this,     &WT9011DCL::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred,
            this,     &WT9011DCL::onSerialError);
}

WT9011DCL::~WT9011DCL()
{
    close();
}

bool WT9011DCL::open(const QString &portName, qint32 baudRate)
{
    if (m_serial->isOpen())
        m_serial->close();

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_serial->errorString());
        return false;
    }

    emit connected();
    return true;
}

void WT9011DCL::close()
{
    if (m_serial->isOpen()) {
        m_serial->close();
        emit disconnected();
    }
}

bool WT9011DCL::isOpen() const
{
    return m_serial->isOpen();
}

QString WT9011DCL::portName() const
{
    return m_serial->portName();
}

void WT9011DCL::writeToDevice(const QByteArray &data)
{
    m_serial->write(data);
}

void WT9011DCL::onReadyRead()
{
    receiveData(m_serial->readAll());
}

void WT9011DCL::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    emit errorOccurred(m_serial->errorString());

    if (error == QSerialPort::ResourceError)
        close();
}
