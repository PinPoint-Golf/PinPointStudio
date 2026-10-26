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

// A synthetic Witmotion on the lead forearm, worn watch-style on the arm strap
// (imu_calibration::nominalArmMount: sensor +X down the arm, distal; +Y across the forearm,
// backward with the USB end forward; +Z out of the label, lateral), through an arm calibration
// and a fast swing-like arm motion (issue #104's ESKF regression check, trunk_turn_path_test).
//
// The sequence, at 200 Hz:
//   settle 2 s arm hanging still → ref 1 s still (the calibration's reference window)
//   → twist 2 s about the forearm (sensor X) → flex 2 s about the elbow (sensor Z)
//   → back 1 s still → swing 2 s → finish 1.5 s still.
// The swing: a backswing, a short pause at the top, a downswing whose forearm roll peaks at
// 1800 °/s about sensor X while the arm sweeps about the across-forearm axes (≤ 600 °/s), and a
// follow-through. Every gyro axis stays inside the device's ±2000 °/s. The forearm's own point
// is carried round a pivot 0.55 m up the arm (0.30 m, the elbow, for the flex), so the
// accelerometer reads several g of centripetal and tangential acceleration on top of gravity.
//
// Truth is integrated per sample exactly as the device reports it (the rate at a sample held
// over the step that ends on it), so a filter's only errors are its own.

#include <QQuaternion>
#include <QVector3D>

#include <cmath>
#include <vector>

#include "imu_calibration.h"

namespace arm_synth {

inline constexpr int    kRateHz = 200;
inline constexpr double kPi     = 3.14159265358979323846;
inline constexpr double kDeg    = kPi / 180.0;

enum Phase { Settle, Ref, Twist, Flex, Back, Swing, Finish };

struct Sample {
    QVector3D   accG;       // specific force, sensor frame, g
    QVector3D   gyroDps;    // sensor frame, °/s
    QQuaternion truth;      // sensor → world (world +Z up)
    Phase       phase;
};

// The arm hanging, golfer facing world +Y: the anatomical long axis e_y (distal) points down,
// e_z (anterior) along +Y, e_x = e_y × e_z. Sensor → world = segment → world · M⁻¹.
inline QQuaternion armDownPose()
{
    const QQuaternion seg = QQuaternion::fromAxes(QVector3D(1, 0, 0), QVector3D(0, 0, -1), QVector3D(0, 1, 0));
    return (seg * imu_calibration::nominalArmMount().conjugated()).normalized();
}

namespace detail {
inline double bump(double t, double T) { const double s = std::sin(kPi * t / T); return s * s; }   // 0 → 1 → 0

// The arm's rotation rate in the sensor frame (°/s) at time t into a phase, and the radius of
// the pivot it turns about (0: a roll about the forearm's own axis moves no point on it).
inline QVector3D omegaDps(Phase p, double t, double *pivotM, QVector3D *rollDps)
{
    *pivotM = 0.0;
    *rollDps = QVector3D();
    switch (p) {
    case Twist: *rollDps = QVector3D(float(180.0 * std::sin(2 * kPi * t / 1.0)), 0, 0); return QVector3D();
    case Flex:  *pivotM = 0.30; return QVector3D(0, 0, float(120.0 * std::sin(2 * kPi * t / 1.0)));
    case Swing: {
        *pivotM = 0.55;
        // backswing 0.9 s, pause 0.15 s, downswing 0.35 s, follow-through 0.6 s
        if (t < 0.9)  { const double s = bump(t, 0.9);
                        *rollDps = QVector3D(float(-150 * s), 0, 0);
                        return QVector3D(0, float(60 * s), float(-250 * s)); }
        t -= 0.9;
        if (t < 0.15) return QVector3D();
        t -= 0.15;
        if (t < 0.35) { const double s = bump(t, 0.35);
                        *rollDps = QVector3D(float(1800 * s), 0, 0);
                        return QVector3D(0, float(-200 * s), float(600 * s)); }
        t -= 0.35;
        if (t < 0.6)  { const double s = bump(t, 0.6);
                        *rollDps = QVector3D(float(600 * s), 0, 0);
                        return QVector3D(0, float(100 * s), float(400 * s)); }
        return QVector3D();
    }
    default: return QVector3D();
    }
}
} // namespace detail

inline std::vector<Sample> generate()
{
    struct Seg { Phase p; double seconds; };
    const Seg segs[] = { { Settle, 2.0 }, { Ref, 1.0 }, { Twist, 2.0 }, { Flex, 2.0 },
                         { Back, 1.0 }, { Swing, 2.0 }, { Finish, 1.5 } };
    const double dt = 1.0 / kRateHz;
    std::vector<Sample> out;
    QQuaternion R = armDownPose();
    for (const Seg &sg : segs) {
        const int n = int(std::lround(sg.seconds * kRateHz));
        for (int i = 1; i <= n; ++i) {
            const double t = i * dt;
            double r = 0.0;
            QVector3D roll;
            const QVector3D sweep = detail::omegaDps(sg.p, t, &r, &roll);
            const QVector3D w = sweep + roll;                                   // °/s, sensor frame
            const float ang = w.length() * float(dt);
            if (ang > 0.0f)
                R = (R * QQuaternion::fromAxisAndAngle(w.normalized(), ang)).normalized();
            // The sweep carries the sensor (at +r X from its pivot) round; the roll does not move it.
            QVector3D aLin;
            if (r > 0.0) {
                const double h = 1e-4;
                double rr; QVector3D dummy;
                const QVector3D wa = (detail::omegaDps(sg.p, t + h, &rr, &dummy) - detail::omegaDps(sg.p, t - h, &rr, &dummy))
                                     * float(kDeg / (2 * h));                   // rad/s²
                const QVector3D ws = sweep * float(kDeg);                       // rad/s
                const QVector3D pos(float(r), 0, 0);
                aLin = QVector3D::crossProduct(wa, pos) + QVector3D::crossProduct(ws, QVector3D::crossProduct(ws, pos));
                aLin /= 9.80665f;
            }
            Sample s;
            s.accG    = R.conjugated().rotatedVector(QVector3D(0, 0, 1)) + aLin;
            s.gyroDps = w;
            s.truth   = R;
            s.phase   = sg.p;
            out.push_back(s);
        }
    }
    return out;
}

// The dominant axis of a run of gyro samples (sign free): power iteration on Σ ω ωᵀ.
inline QVector3D principalAxis(const std::vector<QVector3D> &w)
{
    double S[3][3] = {};
    for (const QVector3D &v : w)
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                S[a][b] += double(v[a]) * double(v[b]);
    double x[3] = { 0.577, 0.577, 0.577 };
    for (int it = 0; it < 100; ++it) {
        double y[3] = {};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                y[a] += S[a][b] * x[b];
        const double n = std::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
        if (!(n > 0.0)) break;
        for (int a = 0; a < 3; ++a) x[a] = y[a] / n;
    }
    return QVector3D(float(x[0]), float(x[1]), float(x[2]));
}

} // namespace arm_synth
