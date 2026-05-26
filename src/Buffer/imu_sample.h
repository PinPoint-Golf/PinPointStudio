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
// Schema identifier: "imu_sample_v1".
// All values expressed in the display/world coordinate frame
// (axis-remapped from the raw sensor frame at write time).
// Rotation is a unit quaternion — never stored as Euler angles.
struct ImuSample {
    float accel_x, accel_y, accel_z;        // acceleration (g)
    float gyro_x,  gyro_y,  gyro_z;         // angular velocity (°/s)
    float quat_w,  quat_x,  quat_y, quat_z; // orientation (unit quaternion)
};

static_assert(sizeof(ImuSample) == 40, "ImuSample layout changed");

} // namespace pinpoint
