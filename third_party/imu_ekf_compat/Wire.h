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

// Compatibility shim for the vendored hobbeshunter/IMU_EKF sources
// (third_party/imu_ekf/ESKF.cpp). That file carries an Arduino `#include
// <Wire.h>` (I2C bus) left over from its microcontroller origins; nothing in
// the filter code actually uses Wire. This empty header satisfies the include
// on desktop builds so the vendored source stays unmodified.
