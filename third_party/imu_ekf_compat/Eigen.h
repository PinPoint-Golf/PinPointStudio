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
// (third_party/imu_ekf/). Those files were written for the Arduino "Eigen"
// library, whose umbrella header is <Eigen.h>. Standard (desktop) Eigen has no
// such header — it exposes <Eigen/Dense>, <Eigen/Core>, <Eigen/LU>, etc. This
// shim makes `#include <Eigen.h>` resolve to the standard Eigen we fetch in
// CMake, so the vendored code compiles unmodified.
//
// _USE_MATH_DEFINES must be defined before <cmath> so that M_PI (used by the
// vendored sources) is available on MSVC.

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#include <Eigen/Dense>
