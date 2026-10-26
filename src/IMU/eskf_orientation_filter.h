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

#include <memory>

#include "iorientation_filter.h"

// Error-State Kalman Filter orientation filter — an alternative to MadgwickFilter,
// selectable at runtime (see IOrientationFilter / OrientationFilterType).
//
// Thin adapter over the vendored hobbeshunter/IMU_EKF library
// (third_party/imu_ekf/, MIT, kept verbatim). The Eigen-heavy ESKF<float> is held
// behind a forward declaration so this header — and everything that includes
// wt9011dcl_base.h — stays free of Eigen. All Eigen/ESKF code lives in the .cpp.
//
// Output convention is MadgwickFilter's: a Hamilton body→world quaternion, world +Z = up
// (gravity reaction). The library's own world is z-down; the .cpp turns it over exactly.
// Each update() runs the full error-state cycle — gyro, predict, gated gravity
// correction, error injection (the library's reset()) — see the .cpp (issue #104).
//
// 6-axis only for now (gyro + accel): the WT901 runs without a magnetometer.
// The underlying library does expose correctMag(); a mag correction can be added
// in update() later without changing this interface.
namespace IMU_EKF { template <typename precision> class ESKF; }

class EskfOrientationFilter : public IOrientationFilter
{
public:
    EskfOrientationFilter();
    ~EskfOrientationFilter() override;   // defined in .cpp where ESKF<float> is complete

    void reset() override;
    bool initialized() const override { return m_initialized; }
    void initFromAccel(float ax, float ay, float az) override;
    bool setOrientation(float w, float x, float y, float z) override;
    void update(float ax, float ay, float az, float gx, float gy, float gz, float dt) override;

    float w() const override { return m_w; }
    float x() const override { return m_x; }
    float y() const override { return m_y; }
    float z() const override { return m_z; }

    const char *name() const override { return "ESKF"; }

private:
    void cacheAttitude();   // pull qref_ out of m_eskf into the cached components

    std::unique_ptr<IMU_EKF::ESKF<float>> m_eskf;
    bool  m_initialized = false;
    float m_trust = 1.0f;   // accelerometer trust 0..1, falls at once, recovers over time
    float m_w = 1.0f, m_x = 0.0f, m_y = 0.0f, m_z = 0.0f;
};
