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

#include "imu_base.h"
#include "imu_capabilities.h"
#include <optional>

// WT9011DCL (MPU9250) IMU protocol layer — transport-agnostic.
//
// Packet protocol:
//   [0]     0x55  (header)
//   [1]     type  (PacketType)
//   [2..9]  four signed int16 payload words, little-endian
//   [10]    checksum = (sum of bytes [0..9]) & 0xFF
//
// Config/read commands sent to the device:
//   FF AA [reg] [lo] [hi]        — write register (value = lo | hi<<8)
//   FF AA 27   [addr_lo] [0x00]  — request register block read (response: 0x71 frame)
//
// Subclasses implement writeToDevice() to send bytes over their transport,
// and call receiveData() whenever bytes arrive from the device.

class WT9011DCL_Base : public ImuBase
{
    Q_OBJECT

public:
    // Source-compatibility aliases — callers may still use WT9011DCL_Base::AccelData etc.
    using AccelData      = ImuBase::AccelData;
    using GyroData       = ImuBase::GyroData;
    using EulerAngles    = ImuBase::EulerAngles;
    using MagData        = ImuBase::MagData;
    using QuaternionData = ImuBase::QuaternionData;

    // -----------------------------------------------------------------------
    // Configuration enumerations
    // -----------------------------------------------------------------------

    enum class OutputRate : quint8 {
        Hz_0_1  = 0x01,
        Hz_0_5  = 0x02,
        Hz_1    = 0x03,
        Hz_2    = 0x04,
        Hz_5    = 0x05,
        Hz_10   = 0x06,
        Hz_20   = 0x07,
        Hz_50   = 0x08,
        Hz_100       = 0x09,
        Hz_200       = 0x0B, // 0x0A is undocumented/reserved
        SingleReturn = 0x0C,
        Off          = 0x0D,
    };

    enum class BaudRate : quint8 {
        Baud_4800   = 0x00,
        Baud_9600   = 0x01,
        Baud_19200  = 0x02,
        Baud_38400  = 0x03,
        Baud_57600  = 0x04,
        Baud_115200 = 0x05,
        Baud_230400 = 0x06,
        Baud_460800 = 0x07,
        Baud_921600 = 0x08,
    };

    enum OutputFlag : quint16 {
        OutputTime        = 0x0001,
        OutputAccel       = 0x0002,
        OutputGyro        = 0x0004,
        OutputAngle       = 0x0008,
        OutputMag         = 0x0010,
        OutputPortStatus  = 0x0020,
        OutputBarometric  = 0x0040,
        OutputGPS         = 0x0080,
        OutputGroundSpeed = 0x0100,
        OutputQuaternion  = 0x0200,
        OutputGPSAccuracy = 0x0400,
    };
    Q_DECLARE_FLAGS(OutputFlags, OutputFlag)

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    explicit WT9011DCL_Base(QObject *parent = nullptr);
    ~WT9011DCL_Base() override = default;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    void saveConfiguration();
    virtual void setOutputRate(OutputRate rate);
    void setDeviceBaudRate(BaudRate rate);
    void setOutputData(OutputFlags flags);

    // Re-sends the full device initialisation sequence (orientation, algorithm,
    // angle zeroing). Call this whenever the device is repositioned.
    // Subclasses override to apply their device-specific init commands.
    void reinitialize() override {}

    // -----------------------------------------------------------------------
    // Calibration
    // -----------------------------------------------------------------------

    // CALSW=0x01: 6-POINT accel/gyro bias calibration — the device must be placed
    // on all six faces in sequence, NOT just held flat. PinPoint does NOT perform
    // this (a flat-still + SAVE attempt corrupted sensors); bias calibration is
    // done in the official WitMotion app only. See docs/IMU_AXIS_REFERENCE.md.
    void startAccelGyroCalibration();

    // Rotate through all orientations for ~30 s; call endCalibration() when done.
    void startMagCalibration();

    void endCalibration();
    void resetAltitude();

    // -----------------------------------------------------------------------
    // On-demand register read
    // -----------------------------------------------------------------------

    void readRegisters(quint8 regAddr, quint8 regCount = 1);

    // Sends a read request for the battery voltage register (0x64).
    // Result arrives asynchronously via batteryUpdated().
    void requestBattery() override;

    // -----------------------------------------------------------------------
    // Transport identity and capabilities — concrete subclasses must implement.
    // -----------------------------------------------------------------------

    Transport       transport()    const override = 0;
    ImuCapabilities capabilities() const override = 0;

    // Returns an ImuCapabilities pre-filled with common WT901 defaults.
    // Used by concrete subclasses and by device enumeration code.
    static ImuCapabilities wt901Defaults();

    // Gimbal lock gate — packets whose middle Euler angle exceeds this threshold
    // are silently dropped in eulerToQuat() rather than producing a degenerate quaternion.
    static constexpr float kGimbalLockThresholdDeg = 85.0f;

    int  gimbalDropCount()      const { return m_gimbalDropCount; }
    void resetGimbalDropCount()       { m_gimbalDropCount = 0; }

    // Orientation fusion (resetOrientationFilter / setOrientationFilter /
    // orientationFilterType) is provided by ImuBase and shared by all devices.
    // The 0x61 combined frame is fused via ImuBase::fuseRawImu() from raw
    // gyro+accel rather than the device's non-rigid Euler output.

    // -----------------------------------------------------------------------
    // Latest cached values
    // -----------------------------------------------------------------------

    AccelData      accelData()      const override { return m_accel; }
    GyroData       gyroData()       const override { return m_gyro;  }
    MagData        magData()        const override { return m_mag;   }
    QuaternionData quaternionData() const override { return m_quat;  }

protected:
    // Converts device Euler angles to a world-frame quaternion, applying any
    // axis remapping and frame corrections specific to the physical hardware.
    // Returns std::nullopt when the middle Euler angle is within kGimbalLockThresholdDeg
    // of the ZYX singularity — callers must silently drop these packets.
    // Default: standard ZYX (RPY), singularity at |pitch| >= threshold.
    // Override in device subclasses to match their mounting convention.
    virtual std::optional<QuaternionData> eulerToQuat(const EulerAngles &e) const;

    // Subclasses call this whenever bytes arrive from the device.
    void receiveData(const QByteArray &data);

    enum Register : quint8 {
        RegSave     = 0x00,
        RegCalSw    = 0x01,
        RegRSW      = 0x02,
        RegRRate    = 0x03,
        RegBaud     = 0x04,
        RegAxOffset = 0x05,
        RegAyOffset = 0x06,
        RegAzOffset = 0x07,
        RegGxOffset = 0x08,
        RegGyOffset = 0x09,
        RegGzOffset = 0x0A,
        RegHxOffset = 0x0B,
        RegHyOffset = 0x0C,
        RegHzOffset = 0x0D,
        RegOrient   = 0x23, // Installation direction: 0x00=horizontal, 0x01=vertical
        RegAxis6    = 0x24, // Fusion algorithm: 0x00=9-axis (mag), 0x01=6-axis (gyro only)
        RegReadAddr = 0x27, // Register read trigger — send target address as LO byte
        RegVBat     = 0x64, // Battery voltage — confirmed from WitmotionSDK Bwt901bleProcessor.cs
    };

    // Subclasses implement this to write bytes to their transport.
    virtual void writeToDevice(const QByteArray &data) = 0;

    // Shared command builder — calls writeToDevice().
    void sendCommand(quint8 reg, quint16 value);

private:
    enum PacketType : quint8 {
        PktTime        = 0x50,
        PktAccel       = 0x51,
        PktGyro        = 0x52,
        PktAngle       = 0x53,
        PktMag         = 0x54,
        PktPortStatus  = 0x55,
        PktBarometric  = 0x56,
        PktGPS         = 0x57,
        PktGroundSpeed = 0x58,
        PktQuaternion  = 0x59,
        PktGPSAccuracy = 0x5A,
    };


    void processBuffer();
    bool verifyChecksum(const QByteArray &packet) const;
    void dispatchPacket(const QByteArray &packet);
    void dispatchCombinedPacket(const QByteArray &frame);

    void parseAccel(const QByteArray &d);
    void parseGyro(const QByteArray &d);
    void parseEuler(const QByteArray &d);
    void parseMag(const QByteArray &d);
    void parseQuaternion(const QByteArray &d);
    void dispatchReadResponse(const QByteArray &frame);

    static qint16 le16(const QByteArray &d, int offset);
    static float  toTemperature(qint16 raw);

    static constexpr quint8 FrameHeader          = 0x55;
    static constexpr int    FrameSize            = 11;
    // WT901BLE67 combined frame:  0x55 0x61 + accel(6) + gyro(6) + angle(6), no checksum
    static constexpr quint8 CombinedFrameType    = 0x61;
    static constexpr int    CombinedFrameSize    = 20;
    // WT901BLE67 register-read response: 0x55 0x71 + startReg(2) + 8 register values(16), no checksum
    static constexpr quint8 ReadRespFrameType    = 0x71;
    static constexpr int    ReadRespFrameSize    = 20;

    QByteArray     m_buffer;
    AccelData      m_accel;
    GyroData       m_gyro;
    MagData        m_mag;
    QuaternionData m_quat;
    int            m_gimbalDropCount = 0;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(WT9011DCL_Base::OutputFlags)
