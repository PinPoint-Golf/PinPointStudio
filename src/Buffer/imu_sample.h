/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <cstddef>

namespace pinpoint {

// Fixed 40-byte decoded IMU sample stored in the EventBuffer.
// Schema identifier: "imu_sample_v2".
// All values are in the RAW SENSOR body frame (no axis remap): accel, gyro, AND the
// fused orientation quaternion now share ONE declared frame. (v1 stored the vectors
// in a display frame but the quaternion un-remapped — the two described different
// frames within one struct; v2 resolves that split.) See docs/IMU_FRAME_CONTRACT.md.
// Rotation is a unit quaternion — never stored as Euler angles.
struct ImuSample {
    float accel_x, accel_y, accel_z;        // acceleration (g)
    float gyro_x,  gyro_y,  gyro_z;         // angular velocity (°/s)
    float quat_w,  quat_x,  quat_y, quat_z; // orientation (unit quaternion)
};

static_assert(sizeof(ImuSample) == 40, "ImuSample layout changed");

// SINGLE SOURCE OF TRUTH for the stored frame: build an ImuSample from raw
// sensor-frame inertial data + the fused orientation quaternion. The exporter writes
// these fields verbatim into swing.json data[] in struct order (swing_exporter.cpp),
// so this function defines the on-disk frame.
//
// v2: accel and gyro are stored RAW (sensor body frame), matching the quaternion's
// frame — one declared frame for the whole struct. (v1 remapped the vectors to a
// display frame [sensor X->X, Z->Y, -Y->Z] while leaving the quaternion un-remapped,
// so the two disagreed; the quaternion is the authoritative field, so v2 drops the
// vector remap rather than rotating the quaternion.)
inline ImuSample makeImuSample(float ax, float ay, float az,
                               float gx, float gy, float gz,
                               float qw, float qx, float qy, float qz)
{
    return ImuSample{  ax,  ay,  az,
                       gx,  gy,  gz,
                       qw,  qx,  qy, qz };
}

} // namespace pinpoint
