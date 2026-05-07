#include "wt9011dcl_base.h"

WT9011DCL_Base::WT9011DCL_Base(QObject *parent)
    : QObject(parent)
{}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void WT9011DCL_Base::saveConfiguration()              { sendCommand(RegSave,  0x0000); }
void WT9011DCL_Base::setOutputRate(OutputRate r)       { sendCommand(RegRRate, static_cast<quint16>(r)); }
void WT9011DCL_Base::setDeviceBaudRate(BaudRate r)     { sendCommand(RegBaud,  static_cast<quint16>(r)); }
void WT9011DCL_Base::setOutputData(OutputFlags f)      { sendCommand(RegRSW,   static_cast<quint16>(f)); }

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

void WT9011DCL_Base::startAccelGyroCalibration()       { sendCommand(RegCalSw, 0x0001); }
void WT9011DCL_Base::startMagCalibration()              { sendCommand(RegCalSw, 0x0007); }
void WT9011DCL_Base::endCalibration()                   { sendCommand(RegCalSw, 0x0000); }
void WT9011DCL_Base::resetAltitude()                    { sendCommand(RegCalSw, 0x0003); }

// ---------------------------------------------------------------------------
// Register read
// ---------------------------------------------------------------------------

void WT9011DCL_Base::readRegisters(quint8 regAddr, quint8 regCount)
{
    QByteArray cmd(5, Qt::Uninitialized);
    cmd[0] = 0xFF;
    cmd[1] = 0xAA;
    cmd[2] = 0x27;
    cmd[3] = static_cast<char>(regAddr);
    cmd[4] = static_cast<char>(regCount);
    writeToDevice(cmd);
}

// ---------------------------------------------------------------------------
// Command builder
// ---------------------------------------------------------------------------

void WT9011DCL_Base::sendCommand(quint8 reg, quint16 value)
{
    QByteArray cmd(5, Qt::Uninitialized);
    cmd[0] = 0xFF;
    cmd[1] = 0xAA;
    cmd[2] = static_cast<char>(reg);
    cmd[3] = static_cast<char>(value & 0xFF);
    cmd[4] = static_cast<char>((value >> 8) & 0xFF);
    writeToDevice(cmd);
}

// ---------------------------------------------------------------------------
// Incoming data entry point (called by subclasses)
// ---------------------------------------------------------------------------

void WT9011DCL_Base::receiveData(const QByteArray &data)
{
    m_buffer.append(data);
    processBuffer();
}

// ---------------------------------------------------------------------------
// Packet parsing
// ---------------------------------------------------------------------------

void WT9011DCL_Base::processBuffer()
{
    while (true) {
        const int headerPos = m_buffer.indexOf(FrameHeader);
        if (headerPos < 0) {
            m_buffer.clear();
            return;
        }

        if (headerPos > 0)
            m_buffer.remove(0, headerPos);

        if (m_buffer.size() < 2)
            return;

        // WT901BLE67 sends a single 20-byte combined frame per BLE notification:
        //   0x55 0x61 [accel_x accel_y accel_z] [gyro_x gyro_y gyro_z] [angle_x angle_y angle_z]
        // Each value is a signed int16 LE. No checksum.
        if (static_cast<quint8>(m_buffer[1]) == CombinedFrameType) {
            if (m_buffer.size() < CombinedFrameSize)
                return;
            dispatchCombinedPacket(m_buffer.left(CombinedFrameSize));
            m_buffer.remove(0, CombinedFrameSize);
            continue;
        }

        // Standard 11-byte type-specific packet (WT9011DCL and similar).
        if (m_buffer.size() < FrameSize)
            return;

        const QByteArray frame = m_buffer.left(FrameSize);
        if (verifyChecksum(frame))
            dispatchPacket(frame);

        m_buffer.remove(0, 1);
    }
}

void WT9011DCL_Base::dispatchCombinedPacket(const QByteArray &frame)
{
    // Bytes 2-7: accel xyz (int16 LE, /32768 * 16 g)
    m_accel.x = le16(frame, 2) / 32768.0f * 16.0f;
    m_accel.y = le16(frame, 4) / 32768.0f * 16.0f;
    m_accel.z = le16(frame, 6) / 32768.0f * 16.0f;
    emit accelUpdated(m_accel);

    // Bytes 8-13: gyro xyz (int16 LE, /32768 * 2000 °/s)
    m_gyro.x = le16(frame, 8)  / 32768.0f * 2000.0f;
    m_gyro.y = le16(frame, 10) / 32768.0f * 2000.0f;
    m_gyro.z = le16(frame, 12) / 32768.0f * 2000.0f;
    emit gyroUpdated(m_gyro);

    // Bytes 14-19: euler angles xyz (int16 LE, /32768 * 180 °)
    const qint16 roll  = le16(frame, 14);
    const qint16 pitch = le16(frame, 16);
    const qint16 yaw   = le16(frame, 18);
    if (roll || pitch || yaw) {
        m_euler.roll  = roll  / 32768.0f * 180.0f;
        m_euler.pitch = pitch / 32768.0f * 180.0f;
        m_euler.yaw   = yaw   / 32768.0f * 180.0f;
        emit eulerAnglesUpdated(m_euler);
    }
}

bool WT9011DCL_Base::verifyChecksum(const QByteArray &packet) const
{
    quint8 sum = 0;
    for (int i = 0; i < FrameSize - 1; ++i)
        sum += static_cast<quint8>(packet[i]);
    return sum == static_cast<quint8>(packet[FrameSize - 1]);
}

void WT9011DCL_Base::dispatchPacket(const QByteArray &packet)
{
    const QByteArray d = packet.mid(2, 8);
    switch (static_cast<quint8>(packet[1])) {
    case PktAccel:      parseAccel(d);      break;
    case PktGyro:       parseGyro(d);       break;
    case PktAngle:      parseEuler(d);      break;
    case PktMag:        parseMag(d);        break;
    case PktQuaternion: parseQuaternion(d); break;
    default: break;
    }
}

qint16 WT9011DCL_Base::le16(const QByteArray &d, int offset)
{
    return static_cast<qint16>(
        (static_cast<quint8>(d[offset + 1]) << 8) |
         static_cast<quint8>(d[offset]));
}

float WT9011DCL_Base::toTemperature(qint16 raw)
{
    return raw / 100.0f;
}

void WT9011DCL_Base::parseAccel(const QByteArray &d)
{
    m_accel.x           = le16(d, 0) / 32768.0f * 16.0f;
    m_accel.y           = le16(d, 2) / 32768.0f * 16.0f;
    m_accel.z           = le16(d, 4) / 32768.0f * 16.0f;
    m_accel.temperature = toTemperature(le16(d, 6));
    emit accelUpdated(m_accel);
}

void WT9011DCL_Base::parseGyro(const QByteArray &d)
{
    m_gyro.x           = le16(d, 0) / 32768.0f * 2000.0f;
    m_gyro.y           = le16(d, 2) / 32768.0f * 2000.0f;
    m_gyro.z           = le16(d, 4) / 32768.0f * 2000.0f;
    m_gyro.temperature = toTemperature(le16(d, 6));
    emit gyroUpdated(m_gyro);
}

void WT9011DCL_Base::parseEuler(const QByteArray &d)
{
    m_euler.roll  = le16(d, 0) / 32768.0f * 180.0f;
    m_euler.pitch = le16(d, 2) / 32768.0f * 180.0f;
    m_euler.yaw   = le16(d, 4) / 32768.0f * 180.0f;
    emit eulerAnglesUpdated(m_euler);
}

void WT9011DCL_Base::parseMag(const QByteArray &d)
{
    m_mag.x           = static_cast<float>(le16(d, 0));
    m_mag.y           = static_cast<float>(le16(d, 2));
    m_mag.z           = static_cast<float>(le16(d, 4));
    m_mag.temperature = toTemperature(le16(d, 6));
    emit magUpdated(m_mag);
}

void WT9011DCL_Base::parseQuaternion(const QByteArray &d)
{
    m_quat.w = le16(d, 0) / 32768.0f;
    m_quat.x = le16(d, 2) / 32768.0f;
    m_quat.y = le16(d, 4) / 32768.0f;
    m_quat.z = le16(d, 6) / 32768.0f;
    emit quaternionUpdated(m_quat);
}
