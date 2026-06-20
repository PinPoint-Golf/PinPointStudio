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

#include "wt9011dcl_base.h"
#include "pp_debug.h"
#include "event_buffer.h"
#include <QtMath>

WT9011DCL_Base::WT9011DCL_Base(QObject *parent)
    : ImuBase(parent)
{}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

ImuCapabilities WT9011DCL_Base::wt901Defaults()
{
    ImuCapabilities caps;
    caps.vendorName           = QStringLiteral("WitMotion");

    // Sensors common to all WT901 variants
    caps.hasAccelerometer     = true;
    caps.hasGyroscope         = true;
    caps.hasEulerAngles       = true;   // fusion output
    caps.hasQuaternion        = true;   // computed from Euler via eulerToQuat()
    caps.dataIsFiltered       = true;   // onboard Kalman filter

    // Both 6-axis (gyro-only) and 9-axis (with mag) fusion are selectable via AXIS6
    caps.supportsSixAxisFusion  = true;
    caps.supportsNineAxisFusion = true;

    // Fixed measurement ranges (encoded in the protocol divisors)
    caps.accelRange = { -16.0f,  16.0f  };   // /32768 * 16 g
    caps.gyroRange  = { -2000.0f, 2000.0f }; // /32768 * 2000 °/s

    // Rates practical for real-time motion capture (1, 2, 5 Hz omitted —
    // too slow for swing analysis and not useful in any session context)
    caps.supportedRatesHz = { 10, 20, 50, 100, 200 };
    caps.defaultRateHz    = 100;

    // ORIENT register — horizontal and vertical installation both supported
    caps.supportsHorizontalMount = true;
    caps.supportsVerticalMount   = true;

    // Configuration registers available on all WT901 variants
    caps.supportsOutputRateControl    = true;   // RRATE
    caps.supportsAngleReference       = true;   // CALSW=0x0008
    caps.supportsHeadingZero          = true;   // CALSW=0x0004
    caps.supportsAccelGyroCalibration = true;   // CALSW=0x0001
    caps.supportsMagCalibration       = true;   // CALSW=0x0007
    caps.supportsConfigPersistence    = true;   // SAVE register

    caps.queriedAt = QDateTime::currentDateTime();
    return caps;
}

// Default Euler→quaternion: standard ZYX (RPY) convention.
// Device subclasses override this to apply their mounting-specific mapping.
// Returns nullopt when |pitch| (the middle ZYX angle) is near the singularity.
std::optional<WT9011DCL_Base::QuaternionData>
WT9011DCL_Base::eulerToQuat(const EulerAngles &e) const
{
    if (std::abs(e.pitch) >= kGimbalLockThresholdDeg)
        return std::nullopt;
    const float hx = qDegreesToRadians(e.roll)  * 0.5f;
    const float hy = qDegreesToRadians(e.pitch) * 0.5f;
    const float hz = qDegreesToRadians(e.yaw)   * 0.5f;
    const float cx = qCos(hx), sx = qSin(hx);
    const float cy = qCos(hy), sy = qSin(hy);
    const float cz = qCos(hz), sz = qSin(hz);
    return QuaternionData{ cx*cy*cz + sx*sy*sz,
                           sx*cy*cz - cx*sy*sz,
                           cx*sy*cz + sx*cy*sz,
                           cx*cy*sz - sx*sy*cz };
}

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
    cmd[2] = static_cast<char>(RegReadAddr);
    cmd[3] = static_cast<char>(regAddr);
    cmd[4] = static_cast<char>(regCount);
    writeToDevice(cmd);
}

void WT9011DCL_Base::requestBattery()
{
    // FF AA 27 64 00 — write READADDR (register 0x27) with address 0x64 (battery voltage).
    // Count byte is 0x00; the device always returns a fixed 20-byte 0x71 response regardless.
    m_batteryReadInFlight = true;   // arm: only honour a VBat 0x71 we actually asked for
    readRegisters(RegVBat, 0);
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
    // Emit raw bytes for EventBuffer before any parsing.
    // Timestamp taken here — first moment of observation.
    if (!data.isEmpty())
        emit rawPacketReady(data, static_cast<qint64>(pinpoint::EventBuffer::nowMicros()));

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
            // This frame carries NO checksum, so a dropped/split notification can
            // leave indexOf(0x55) locked onto a payload byte. Two cheap resync
            // guards before trusting 20 bytes: (a) in a contiguous stream the NEXT
            // frame must start with 0x55 — if the byte after this one isn't a
            // header we're mis-aligned; (b) the accel magnitude must be physically
            // plausible. On either failure, drop ONE byte and resync rather than
            // consuming (and fusing / impact-testing) a garbage 20-byte frame.
            const bool nextIsHeader = m_buffer.size() <= CombinedFrameSize
                || static_cast<quint8>(m_buffer[CombinedFrameSize]) == FrameHeader;
            if (nextIsHeader && combinedFrameAccelPlausible(m_buffer)) {
                dispatchCombinedPacket(m_buffer.left(CombinedFrameSize));
                m_buffer.remove(0, CombinedFrameSize);
            } else {
                m_buffer.remove(0, 1);   // false header — resync one byte
            }
            continue;
        }

        // WT901BLE67 register-read response:
        //   0x55 0x71 [startReg lo/hi] [8 register values × 2 bytes LE]
        // No checksum. Triggered by writing register 0x27 (READADDR) with the target address.
        if (static_cast<quint8>(m_buffer[1]) == ReadRespFrameType) {
            if (m_buffer.size() < ReadRespFrameSize)
                return;
            // Same checksum-less resync guard: the following frame must be a header.
            const bool nextIsHeader = m_buffer.size() <= ReadRespFrameSize
                || static_cast<quint8>(m_buffer[ReadRespFrameSize]) == FrameHeader;
            if (nextIsHeader) {
                dispatchReadResponse(m_buffer.left(ReadRespFrameSize));
                m_buffer.remove(0, ReadRespFrameSize);
            } else {
                m_buffer.remove(0, 1);   // false header — resync one byte
            }
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

    // Bytes 14-19: euler angles xyz (int16 LE, /32768 * 180 °).
    //
    // Orientation is fused locally from the raw gyro + accel above (Madgwick
    // 6-axis), NOT taken from these device Euler angles. The device's on-board
    // Euler output proved non-rigid: reconstructing a quaternion from it (any axis
    // convention) reproduces joint rotation axes ~15-50 deg off the truth and
    // gimbal-locks near pitch = +-90, whereas the raw gyro+accel are faithful.
    // The Euler values are still emitted for display labels only (see CLAUDE.md:
    // Euler is acceptable solely for human-readable UI, never for computation).
    const qint16 roll  = le16(frame, 14);
    const qint16 pitch = le16(frame, 16);
    const qint16 yaw   = le16(frame, 18);
    EulerAngles euler;
    euler.roll  = roll  / 32768.0f * 180.0f;
    euler.pitch = pitch / 32768.0f * 180.0f;
    euler.yaw   = yaw   / 32768.0f * 180.0f;
    m_euler = euler;
    emit eulerAnglesUpdated(euler);   // display labels only

    // Fuse orientation locally from the raw gyro+accel (ImuBase, shared across
    // device types). Gyro converted deg/s -> rad/s to match the filter contract.
    m_quat = fuseRawImu(m_accel.x, m_accel.y, m_accel.z,
                        qDegreesToRadians(m_gyro.x),
                        qDegreesToRadians(m_gyro.y),
                        qDegreesToRadians(m_gyro.z));
    emit quaternionUpdated(m_quat);
}

bool WT9011DCL_Base::combinedFrameAccelPlausible(const QByteArray &frame)
{
    // Bytes 2-7: accel xyz (int16 LE, /32768 * 16 g). Reject |a| beyond the
    // sensor's physical range — a mis-aligned lock-on yields random int16s whose
    // magnitude routinely exceeds it; a real (even saturated) strike never does.
    const float ax = le16(frame, 2) / 32768.0f * 16.0f;
    const float ay = le16(frame, 4) / 32768.0f * 16.0f;
    const float az = le16(frame, 6) / 32768.0f * 16.0f;
    return (ax * ax + ay * ay + az * az) <= kCombinedAccelMaxG * kCombinedAccelMaxG;
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
    EulerAngles euler;
    euler.roll  = le16(d, 0) / 32768.0f * 180.0f;
    euler.pitch = le16(d, 2) / 32768.0f * 180.0f;
    euler.yaw   = le16(d, 4) / 32768.0f * 180.0f;
    const auto q = eulerToQuat(euler);
    if (!q) { m_gimbalDropCount.fetch_add(1, std::memory_order_relaxed); return; }
    m_euler = euler;
    emit eulerAnglesUpdated(euler);
    m_quat = *q;
    emit quaternionUpdated(m_quat);
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

// 0x71 register-read response (WT901BLE67-specific):
//   frame[0..1] = 0x55 0x71 (header)
//   frame[2..3] = start register address (2-byte LE, echoed from request)
//   frame[4..5] = first register value (BATVAL when startReg = 0x64), int16 LE
//   frame[6..19]= next 7 register values
//
// Source: WitmotionSDK Bwt901bleProcessor.cs — reads register 0x64, parses at returnData[4].
// Percentage lookup table also from SDK (Bwt901bleProcessor.cs lines 294-352).
void WT9011DCL_Base::dispatchReadResponse(const QByteArray &frame)
{
    const quint8 startReg = static_cast<quint8>(frame[2]);
    const qint16 raw      = le16(frame, 4);

    emit diagnosticInfo(QStringLiteral("[0x71] startReg=0x%1 raw=%2")
                        .arg(startReg, 2, 16, QLatin1Char('0'))
                        .arg(raw));

    if (startReg == RegCalSw) {
        // Only honour the fence if we actually issued the RegCalSw read
        // (zeroToCurrentPose armed it). An unsolicited / mis-framed RegCalSw 0x71
        // must not prematurely satisfy the calibration-zeroing fence.
        if (!m_zeroFenceInFlight) {
            emit diagnosticInfo(QStringLiteral(
                "[calsw] unsolicited RegCalSw 0x71 (readback=0x%1) — ignored")
                .arg(static_cast<quint16>(raw), 4, 16, QLatin1Char('0')));
            return;
        }
        m_zeroFenceInFlight = false;
        ppWarn() << "[IMU] zeroing fence response: RegCalSw readback=0x"
                 << Qt::hex << raw << Qt::dec
                 << "— hardware ACK received, zero confirmed";
        emit diagnosticInfo(QStringLiteral("[calsw] readback=0x%1 — hardware ACK received")
                            .arg(static_cast<quint16>(raw), 4, 16, QLatin1Char('0')));
        emit zeroingConfirmed();
        return;
    }

    if (startReg == RegOrient) {
        // Verify that vertical installation (0x0001) was accepted.
        if (raw == 0x0001) {
            emit diagnosticInfo(QStringLiteral(
                "[orient] ORIENT=0x0001 confirmed — vertical installation active"));
        } else {
            emit diagnosticInfo(QStringLiteral(
                "[orient] WARNING: ORIENT readback=0x%1 — expected 0x0001 (vertical). "
                "Euler output may be in the wrong frame.")
                .arg(raw, 4, 16, QLatin1Char('0')));
        }
        return;
    }

    if (startReg == RegAxis6) {
        // Verify that 6-axis fusion (0x0001) was accepted, so every device comes
        // up in the same known algorithm state (gyro+accel only, no magnetometer).
        if (raw == 0x0001) {
            emit diagnosticInfo(QStringLiteral(
                "[axis6] AXIS6=0x0001 confirmed — 6-axis fusion active"));
        } else {
            emit diagnosticInfo(QStringLiteral(
                "[axis6] WARNING: AXIS6 readback=0x%1 — expected 0x0001 (6-axis). "
                "Device may be in 9-axis (magnetometer) mode.")
                .arg(raw, 4, 16, QLatin1Char('0')));
        }
        return;
    }

    if (startReg != RegVBat)
        return;

    // Only honour a battery response we requested — drops mis-framed VBat 0x71s.
    if (!m_batteryReadInFlight)
        return;

    if (raw == 0) {
        emit batteryReadRetry();   // keep armed — the retry re-requests
        return;
    }
    m_batteryReadInFlight = false;

    // Stepped lookup table from WitmotionSDK — units of 0.01 V.
    int percent;
    if      (raw >= 396) percent = 100;
    else if (raw >= 393) percent = 90;
    else if (raw >= 387) percent = 75;
    else if (raw >= 382) percent = 60;
    else if (raw >= 379) percent = 50;
    else if (raw >= 377) percent = 40;
    else if (raw >= 373) percent = 30;
    else if (raw >= 370) percent = 20;
    else if (raw >= 368) percent = 15;
    else if (raw >= 350) percent = 10;
    else if (raw >= 340) percent = 5;
    else                 percent = 0;

    emit batteryUpdated(percent);
}
