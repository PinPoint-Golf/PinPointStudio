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

#include <cmath>

#include "iorientation_filter.h"

// Madgwick 6-axis (IMU: gyroscope + accelerometer) orientation filter.
//
// Why this exists: the WT901BLE67 streams an on-board fused orientation as Euler
// angles in its 0x61 frame. Empirically that output is NOT a rigid representation
// of true motion — converting it to a quaternion (any axis convention) reproduces
// joint rotation axes ~15-50 deg off the truth and gimbal-locks near pitch = +-90.
// The raw gyroscope and accelerometer, by contrast, are faithful (gyro-derived
// joint axes came out mutually orthogonal to within 0.2 deg). So we fuse
// orientation locally from the raw inertial data instead.
//
// This is the canonical Madgwick gradient-descent IMU update (no magnetometer):
// the gyro integrates the quaternion; the accelerometer's gravity direction
// corrects roll and pitch each step. Yaw is unobservable from gravity alone and
// will slowly drift — acceptable for per-session use, and matches the device's
// own 6-axis (AXIS6) mode. The produced quaternion is in the sensor body frame
// (the gyro/accel hardware axes); mapping to an anatomical frame is a separate,
// downstream calibration step.
//
// Header-only and free of Qt/UI dependencies so it can be unit-tested in
// isolation and reused by other IMU drivers.
class MadgwickFilter : public IOrientationFilter
{
public:
    explicit MadgwickFilter(float beta = 0.05f) : m_beta(beta) {}

    // Filter gain: higher trusts the accelerometer more (better gravity tracking,
    // more sensitive to linear-acceleration noise during fast motion); lower
    // trusts the gyro more. ~0.03-0.1 is typical for consumer MEMS.
    // Madgwick-specific extra — not part of the IOrientationFilter interface.
    void  setBeta(float beta) { m_beta = beta; }
    float beta() const        { return m_beta; }

    bool  initialized() const override { return m_initialized; }
    void  reset() override { m_q[0] = 1.0f; m_q[1] = m_q[2] = m_q[3] = 0.0f; m_initialized = false; }

    // Seed orientation from a single gravity (accelerometer) sample so tracking
    // starts with correct roll/pitch (yaw arbitrary). Units of ax/ay/az are
    // irrelevant — the vector is normalised internally.
    void initFromAccel(float ax, float ay, float az) override
    {
        const float n = std::sqrt(ax * ax + ay * ay + az * az);
        if (n < 1e-6f) { reset(); m_initialized = true; return; }
        ax /= n; ay /= n; az /= n;

        // Shortest-arc quaternion p taking world up (0,0,1) to the measured gravity
        // direction in the body frame; the orientation is its conjugate.
        const float cx = -ay, cy = ax, cz = 0.0f;     // (0,0,1) x (ax,ay,az)
        const float w  = 1.0f + az;                   // 1 + (0,0,1).(ax,ay,az)
        const float qn = std::sqrt(w * w + cx * cx + cy * cy + cz * cz);
        if (qn < 1e-6f) {                              // gravity ~ (0,0,-1): 180 deg about X
            m_q[0] = 0.0f; m_q[1] = 1.0f; m_q[2] = 0.0f; m_q[3] = 0.0f;
        } else {
            m_q[0] =  w  / qn; m_q[1] = -cx / qn; m_q[2] = -cy / qn; m_q[3] = -cz / qn;
        }
        m_initialized = true;
    }

    // Warm-start to an exact orientation (offline re-fusion — see IOrientationFilter).
    bool setOrientation(float w, float x, float y, float z) override
    {
        const float n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n < 1e-9f) return false;
        m_q[0] = w / n; m_q[1] = x / n; m_q[2] = y / n; m_q[3] = z / n;
        m_initialized = true;
        return true;
    }

    // One filter step. gx/gy/gz in rad/s; ax/ay/az any consistent units; dt in seconds.
    void update(float ax, float ay, float az, float gx, float gy, float gz, float dt) override
    {
        float q0 = m_q[0], q1 = m_q[1], q2 = m_q[2], q3 = m_q[3];

        // Rate of change of quaternion from the gyroscope.
        float qd0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qd1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        float qd2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        float qd3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

        const float an = std::sqrt(ax * ax + ay * ay + az * az);
        if (an > 1e-6f) {
            ax /= an; ay /= an; az /= an;

            const float _2q0 = 2.0f * q0, _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3;
            const float _4q0 = 4.0f * q0, _4q1 = 4.0f * q1, _4q2 = 4.0f * q2;
            const float _8q1 = 8.0f * q1, _8q2 = 8.0f * q2;
            const float q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;

            // Gradient-descent corrective step toward the measured gravity direction.
            float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
            float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1
                     + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
            float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2
                     + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
            float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

            const float sn = std::sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
            if (sn > 1e-9f) {
                s0 /= sn; s1 /= sn; s2 /= sn; s3 /= sn;
                qd0 -= m_beta * s0; qd1 -= m_beta * s1; qd2 -= m_beta * s2; qd3 -= m_beta * s3;
            }
        }

        q0 += qd0 * dt; q1 += qd1 * dt; q2 += qd2 * dt; q3 += qd3 * dt;

        const float nq = std::sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        if (nq > 1e-9f) { q0 /= nq; q1 /= nq; q2 /= nq; q3 /= nq; }
        m_q[0] = q0; m_q[1] = q1; m_q[2] = q2; m_q[3] = q3;
    }

    float w() const override { return m_q[0]; }
    float x() const override { return m_q[1]; }
    float y() const override { return m_q[2]; }
    float z() const override { return m_q[3]; }

    const char *name() const override { return "Madgwick"; }

private:
    float m_q[4]        = { 1.0f, 0.0f, 0.0f, 0.0f };
    float m_beta;
    bool  m_initialized = false;
};
