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

#include "eskf_orientation_filter.h"

// Vendored ESKF (third_party/imu_ekf, on the include path). Pulls in Eigen via
// the compat shim <Eigen.h>; ESKF.h includes the template impl (ESKF.cpp) at its
// foot, so ESKF<float> is fully instantiated in this translation unit only.
#include <ESKF.h>

namespace {
// The vendored correctGyr() converts its inputs from degrees to radians
// internally (it multiplies by DEG_TO_RAD). IOrientationFilter::update() takes
// gyro in rad/s (the Madgwick convention), so undo that here.
constexpr float kRadToDeg = 57.295779513082320876798154814105f;
} // namespace

EskfOrientationFilter::EskfOrientationFilter()
    : m_eskf(std::make_unique<IMU_EKF::ESKF<float>>())
{
}

EskfOrientationFilter::~EskfOrientationFilter() = default;

void EskfOrientationFilter::cacheAttitude()
{
    const IMU_EKF::Quaternion<float> q = m_eskf->getAttitude();
    m_w = q[IMU_EKF::w];
    m_x = q[IMU_EKF::v1];
    m_y = q[IMU_EKF::v2];
    m_z = q[IMU_EKF::v3];
}

void EskfOrientationFilter::reset()
{
    // Full re-initialisation to identity. (The library's own reset() is the
    // error-state injection step, NOT a filter reset — init() is what we want.)
    m_eskf->init();
    m_initialized = false;
    m_w = 1.0f;
    m_x = m_y = m_z = 0.0f;
}

void EskfOrientationFilter::initFromAccel(float ax, float ay, float az)
{
    m_eskf->initWithAcc(ax, ay, az);
    m_initialized = true;
    cacheAttitude();
}

void EskfOrientationFilter::update(float ax, float ay, float az,
                                   float gx, float gy, float gz, float dt)
{
    // Propagate, then fold in the gyro and accelerometer measurements. correctGyr
    // expects deg/s (it converts to rad/s internally); correctAcc expects g.
    m_eskf->predict(dt);
    m_eskf->correctGyr(gx * kRadToDeg, gy * kRadToDeg, gz * kRadToDeg);
    m_eskf->correctAcc(ax, ay, az);
    // (Future: m_eskf->correctMag(...) once a magnetometer is wired in.)
    cacheAttitude();
}
