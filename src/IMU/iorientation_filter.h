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

// Abstract orientation-filter interface.
//
// Orientation for the WT901BLE67's 0x61 combined frame is fused locally from
// raw gyro + accelerometer (the device's on-board Euler output is non-rigid —
// see orientation_filter.h). This interface lets the fusion algorithm be chosen
// at runtime: the canonical Madgwick gradient-descent filter (MadgwickFilter,
// orientation_filter.h) or an Error-State Kalman Filter (EskfOrientationFilter,
// eskf_orientation_filter.h, wrapping the vendored hobbeshunter/IMU_EKF).
//
// Kept deliberately free of Qt and Eigen so wt9011dcl_base.h can hold an
// IOrientationFilter* without dragging Eigen into every translation unit that
// includes it. Concrete filters are constructed only in wt9011dcl_base.cpp.
//
// Convention matches the existing Madgwick surface so callers are unchanged:
// gyro in rad/s, accelerometer in any consistent units (g for the WT901),
// dt in seconds. The produced quaternion is in the sensor body frame.
class IOrientationFilter
{
public:
    virtual ~IOrientationFilter() = default;

    // Reset to an uninitialised identity orientation. The next update() should
    // be preceded by initFromAccel() once a gravity sample is available.
    virtual void reset() = 0;

    // True once seeded (via initFromAccel) — drives the first-packet seeding in
    // dispatchCombinedPacket().
    virtual bool initialized() const = 0;

    // Seed orientation from a single gravity (accelerometer) sample so tracking
    // starts with correct roll/pitch (yaw arbitrary). Units are irrelevant — the
    // vector is normalised internally.
    virtual void initFromAccel(float ax, float ay, float az) = 0;

    // One filter step. gx/gy/gz in rad/s; ax/ay/az any consistent units; dt in s.
    virtual void update(float ax, float ay, float az,
                        float gx, float gy, float gz, float dt) = 0;

    // Fused quaternion components (sensor body frame).
    virtual float w() const = 0;
    virtual float x() const = 0;
    virtual float y() const = 0;
    virtual float z() const = 0;

    // Human-readable filter name for diagnostics/UI ("Madgwick" / "ESKF").
    virtual const char *name() const = 0;
};

// User-selectable fusion algorithm. Persisted as a string in AppSettings
// (key imu/orientationFilter) and chosen in the Settings -> IMUs panel.
enum class OrientationFilterType {
    Madgwick,
    Eskf,
};

inline const char *toString(OrientationFilterType t)
{
    switch (t) {
    case OrientationFilterType::Eskf:     return "ESKF";
    case OrientationFilterType::Madgwick: break;
    }
    return "Madgwick";
}

inline OrientationFilterType orientationFilterFromString(const char *s)
{
    // Case-tolerant match on the ESKF token; everything else (incl. null/empty)
    // falls back to Madgwick, the historical default.
    if (s && (s[0] == 'E' || s[0] == 'e'))
        return OrientationFilterType::Eskf;
    return OrientationFilterType::Madgwick;
}
