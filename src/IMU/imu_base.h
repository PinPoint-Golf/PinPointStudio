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
#include <QByteArray>
#include <QList>

#include <atomic>
#include <memory>

#include "iorientation_filter.h"

struct ImuCapabilities;

// Abstract base class for all IMU devices, transport-agnostic.
//
// Concrete subclasses implement transport-specific connection and data
// acquisition. Use ImuFactory::create() to instantiate.
//
// Class hierarchy:
//   ImuBase
//     └─ WT9011DCL_Base  (WitMotion packet protocol, transport-agnostic)
//          ├─ WT9011DCL       (UART/serial transport)
//          └─ WT9011DCL_BLE   (WT901 BLE config; delegates connection to BleImuTransport)
//
//   BleImuTransport  (generic BLE state machine — scan, GATT, Linux fix;
//                     owned by composition inside WT9011DCL_BLE)

class ImuBase : public QObject
{
    Q_OBJECT

public:
    enum class Transport { Serial, Ble };
    Q_ENUM(Transport)

    struct AccelData      { float x = 0, y = 0, z = 0, temperature = 0; };
    struct GyroData       { float x = 0, y = 0, z = 0, temperature = 0; };
    struct EulerAngles    { float roll = 0, pitch = 0, yaw = 0; };
    struct MagData        { float x = 0, y = 0, z = 0, temperature = 0; };
    struct QuaternionData { float w = 1, x = 0, y = 0, z = 0; };

    explicit ImuBase(QObject *parent = nullptr);
    ~ImuBase() override = default;

    virtual Transport transport() const = 0;

    // Returns a fully-populated description of what this device supports.
    // The result is static per device type — call once and cache.
    virtual ImuCapabilities capabilities() const = 0;

    virtual AccelData      accelData()      const = 0;
    virtual GyroData       gyroData()       const = 0;
    virtual MagData        magData()        const = 0;
    virtual QuaternionData quaternionData() const = 0;

    virtual void reinitialize()      {}
    virtual void requestBattery()    {}

    // Zero the sensor's orientation reference to the current physical pose.
    // Implementations send device-specific commands and emit zeroingConfirmed()
    // when the device acknowledges. Default no-op for devices without zeroing
    // support — ImuInstance's 30 s timeout will fire and emit zeroingFailed().
    virtual void zeroToCurrentPose() {}

    // -----------------------------------------------------------------------
    // Local orientation fusion — shared by all device types.
    //
    // Devices whose native orientation output is unreliable (e.g. the WT901's
    // non-rigid Euler stream) fuse orientation locally from raw gyro + accel:
    // the driver calls fuseRawImu() from its packet handler. The fusion
    // algorithm (Madgwick / ESKF) is selectable at runtime — see
    // iorientation_filter.h. A device that trusts its hardware quaternion simply
    // never calls fuseRawImu() and reports the native value instead.
    // -----------------------------------------------------------------------

    // Select the fusion algorithm. Thread-safe: the filter object is rebuilt on
    // the packet-consumer thread at the next fuseRawImu() call (see
    // applyPendingFilterSwap), so any thread may call this without locking the
    // hot fusion path.
    void setOrientationFilter(OrientationFilterType type)
    {
        m_pendingFilterType.store(type, std::memory_order_relaxed);
        m_filterSwapPending.store(true, std::memory_order_release);
    }
    OrientationFilterType orientationFilterType() const
    {
        return m_pendingFilterType.load(std::memory_order_relaxed);
    }

    // Re-seed the filter (drops orientation state) so tracking restarts from the
    // current pose. Call on (re)connection. Clears the stillness-seed counter so
    // the next seed re-evaluates the stillness gate.
    void resetOrientationFilter() { if (m_fusion) m_fusion->reset(); m_initSeedAttempts = 0; }

    // Inform the fusion of the device's configured output rate (Hz). The fusion
    // integrates gyro with the NOMINAL sample period (1/rate) rather than
    // host-arrival deltas — BLE batches several samples per connection interval,
    // so arrival deltas alias to ~0 within a burst and a large gap across it,
    // which under-/over-integrates the gyro exactly during fast motion. A fixed
    // nominal dt is burst-immune and matches the device's true average cadence.
    // Called from ImuInstance::setOutputRateHz on the I/O thread (same thread as
    // fuseRawImu), so no locking is needed.
    void setNominalSampleRateHz(int hz) { if (hz > 0) m_nominalDtS = 1.0f / static_cast<float>(hz); }

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    void diagnosticInfo(const QString &message);

    void accelUpdated(const ImuBase::AccelData &data);
    void gyroUpdated(const ImuBase::GyroData &data);
    void eulerAnglesUpdated(const ImuBase::EulerAngles &angles);
    void magUpdated(const ImuBase::MagData &data);
    void quaternionUpdated(const ImuBase::QuaternionData &quat);
    void batteryUpdated(int percent);
    void batteryReadRetry();
    void zeroingConfirmed(); // device confirmed zeroing was applied

    void rawPacketReady(const QByteArray &data, qint64 timestamp_us);

protected:
    // Feed one raw IMU sample to the active orientation filter and return the
    // fused quaternion (sensor body frame). Accel in any consistent units (g for
    // the WT901); gyro in rad/s. dt is computed internally from a software clock
    // (clamped to absorb transport stalls). Applies any pending filter swap and
    // seeds from gravity on the first call. Drivers call this from their packet
    // handler, then store/emit the result.
    QuaternionData fuseRawImu(float ax, float ay, float az,
                              float gxRad, float gyRad, float gzRad);

private:
    // Rebuilds m_fusion if a setOrientationFilter() request is pending. Called by
    // fuseRawImu() so the swap happens on the packet-consumer thread.
    void applyPendingFilterSwap();

    // Owned/used only on the packet-consumer thread; swapped via the atomics below.
    std::unique_ptr<IOrientationFilter> m_fusion;
    std::atomic<OrientationFilterType>  m_pendingFilterType{ OrientationFilterType::Madgwick };
    std::atomic<bool>                   m_filterSwapPending{ false };

    // Nominal integration period (seconds), kept in lockstep with the device's
    // configured output rate via setNominalSampleRateHz(). Default 100 Hz.
    float                               m_nominalDtS = 0.01f;

    // Stillness-gated seeding: defer initFromAccel() until the device is roughly
    // at rest (gravity-only accel, near-zero gyro) so the filter isn't seeded from
    // a sample taken mid-motion (wrong gravity → slow convergence). Falls back to
    // seeding unconditionally after kInitMaxSeedAttempts samples.
    int                                 m_initSeedAttempts = 0;
    static constexpr float kInitAccelTolG     = 0.15f;   // |a| within 1g ± this
    static constexpr float kInitGyroMaxRadps  = 0.5f;    // ~28 °/s
    static constexpr int   kInitMaxSeedAttempts = 200;   // ~1 s @200 Hz, ~2 s @100 Hz
};
