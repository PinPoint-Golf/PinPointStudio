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

// R2-7 — ESKF gyro-unit regression test (docs/implementation/imu_pipeline_audit.md).
//
// IOrientationFilter::update() takes gyro in rad/s (the Madgwick convention).
// The vendored ESKF's correctGyr() converts deg/s → rad/s internally, so
// EskfOrientationFilter pre-scales rad → deg (kRadToDeg) to cancel it. That
// cancellation is an undocumented coupling to third-party internals: if the
// vendored library ever stops converting, every ESKF orientation would silently
// scale by ~57.3×. This test pins the assumption — drive a known rad/s yaw rate
// and confirm the integrated rotation matches rad/s, not deg/s (a broken
// conversion lands ~57× off and fails).
//
// We rotate about the gravity axis (+Z): yaw is unobservable from the
// accelerometer, so the gravity correction neither helps nor fights it, leaving
// the gyro path as the sole driver of the angle.

#include "eskf_orientation_filter.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) { std::printf("FAIL: %s\n", (msg)); ++g_failures; }        \
        else         { std::printf("ok:   %s\n", (msg)); }                      \
    } while (0)

int main()
{
    EskfOrientationFilter f;

    // Device flat, gravity along +Z (1 g). Seed orientation from gravity.
    f.initFromAccel(0.0f, 0.0f, 1.0f);

    // Constant yaw rate about +Z. 0.5 rad/s for 1 s → ~0.5 rad of yaw, well clear
    // of the ±π wrap and clearly distinct from a deg/s misread (~0.0087 rad) or an
    // over-conversion (a wrapped, large angle).
    const float omega = 0.5f;     // rad/s
    const float dt    = 0.005f;   // 200 Hz
    const int   steps = 200;      // 1 s
    for (int i = 0; i < steps; ++i)
        f.update(0.0f, 0.0f, 1.0f, /*gx*/ 0.0f, /*gy*/ 0.0f, /*gz*/ omega, dt);

    // Yaw from the quaternion. A pure +Z rotation is q = (cos θ/2, 0, 0, sin θ/2),
    // so θ = 2·atan2(z, w). Canonicalise the double-cover sign first: q and −q are
    // the SAME rotation, and the filter may report the antipodal form (w < 0).
    float qw = f.w(), qz = f.z();
    if (qw < 0.0f) { qw = -qw; qz = -qz; }
    const float yaw      = 2.0f * std::atan2(qz, qw);
    const float expected = omega * dt * static_cast<float>(steps);   // 0.5 rad
    std::printf("measured yaw = %.4f rad (%.2f deg), expected ~ %.4f rad\n",
                yaw, yaw * 57.2957795f, expected);

    // Correct rad/s integration lands very near 0.5 rad. A deg/s misread (the
    // conversion bug this guards) would be ~0.009 rad, far below the band; an
    // over-conversion would wrap to a large/!=0.5 angle.
    CHECK(yaw > 0.35f && yaw < 0.65f,
          "ESKF integrates a 0.5 rad/s gyro to ~0.5 rad (rad/s convention held)");

    // Hard floor: a no-compensation bug (~57× too small) would leave |yaw| tiny.
    CHECK(std::fabs(yaw) > 0.05f, "rotation is not ~57x too small (deg/s misread)");

    // Sign sanity: a +Z rate must yaw positively (in the canonical hemisphere).
    CHECK(yaw > 0.0f, "positive yaw rate produces positive rotation");

    std::printf(g_failures ? "\n%d CHECK(s) FAILED\n" : "\nALL CHECKS PASSED\n",
                g_failures);
    return g_failures ? 1 : 0;
}
