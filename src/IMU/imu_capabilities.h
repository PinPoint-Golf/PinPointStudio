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
#include <QString>
#include <QList>
#include <QDateTime>
#include <QVariantMap>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct ImuSensorRange {
    float min = 0.0f;
    float max = 0.0f;
};

// ---------------------------------------------------------------------------
// ImuCapabilities — top-level capabilities descriptor for an IMU device.
//
// Returned by ImuBase::capabilities() on each concrete device class.
// All fields default to false / zero / empty (= not supported / unknown).
// ---------------------------------------------------------------------------

struct ImuCapabilities {

    // --- Identity ---
    QString vendorName;
    QString modelName;
    QString serialNumber;
    QString firmwareVersion;

    // --- Transport ---
    ImuBase::Transport transport = ImuBase::Transport::Serial;

    // --- Sensors present ---
    bool hasAccelerometer = false;   // 3-axis linear acceleration
    bool hasGyroscope     = false;   // 3-axis angular velocity
    bool hasMagnetometer  = false;   // 3-axis magnetic field
    bool hasTemperature   = false;   // onboard temperature (embedded in accel/gyro packets)
    bool hasBarometer     = false;   // barometric pressure / altitude
    bool hasGPS           = false;   // GNSS position and ground speed
    bool hasBattery       = false;   // battery level readable from device

    // --- Computed / fusion outputs ---
    bool hasEulerAngles = false;     // roll/pitch/yaw from onboard fusion algorithm
    bool hasQuaternion  = false;     // unit quaternion from onboard fusion algorithm
    bool hasGroundSpeed = false;     // GPS-derived ground speed

    // --- Data quality ---
    bool dataIsFiltered = false;     // onboard Kalman / complementary filter applied to outputs

    // --- Fusion algorithm options (set via AXIS6 register) ---
    bool supportsSixAxisFusion  = false;  // AXIS6=1: gyro-only, no magnetometer drift
    bool supportsNineAxisFusion = false;  // AXIS6=0: 9-axis with magnetometer

    // --- Fixed sensor measurement ranges ---
    // These are determined by firmware/protocol and are not user-configurable
    // on the WT901 series. Accel is scaled as raw/32768*accelRange.max.
    ImuSensorRange accelRange;   // g-force,  e.g. { -16.0f,  16.0f }
    ImuSensorRange gyroRange;    // °/s,      e.g. { -2000.0f, 2000.0f }
    ImuSensorRange magRange;     // raw ADC units (÷120 ≈ µT); 0 if no magnetometer

    // --- Output data rate (RRATE register) ---
    // Integer Hz values only. Sub-Hz rates (0.1, 0.5 Hz) exist on WT901 hardware
    // but are not listed here; use extensions["wt.subHzRates"] if needed.
    QList<int> supportedRatesHz;
    int        defaultRateHz = 100;

    // --- Installation options (ORIENT register) ---
    bool supportsHorizontalMount = false;  // ORIENT=0x00: device lying flat
    bool supportsVerticalMount   = false;  // ORIENT=0x01: device on a vertical shaft

    // --- Configuration capabilities ---
    bool supportsOutputRateControl    = false;  // RRATE register: change output frequency
    bool supportsBaudRateControl      = false;  // BAUD register: serial only
    bool supportsOutputDataSelection  = false;  // RSW register: choose which packet types to emit
                                                // NOTE: must NOT be used on WT901BLE67 —
                                                // calling setOutputData() disrupts the 0x61
                                                // combined-frame stream
    bool supportsAngleReference       = false;  // CALSW=0x0008: zero roll/pitch to current position
    bool supportsHeadingZero          = false;  // CALSW=0x0004: zero yaw/heading to current
    bool supportsAccelGyroCalibration = false;  // CALSW=0x0001: 6-point accel+gyro bias cal (all 6 faces) — WitMotion app only; see docs/reference/IMU_AXIS_REFERENCE.md
    bool supportsMagCalibration       = false;  // CALSW=0x0007: spherical magnetic field calibration
    bool supportsConfigPersistence    = false;  // SAVE register: write settings to onboard flash

    // --- Metadata ---
    QDateTime   queriedAt;

    // Device-specific extras that don't fit the schema.
    // Keys are namespaced, e.g. "wt.combinedFrameType", "wt.batteryRegister".
    QVariantMap extensions;
};
