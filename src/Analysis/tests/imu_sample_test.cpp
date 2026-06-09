// Characterization of the stored IMU sample frame (Track B, Phase 1.2).
//
// makeImuSample (imu_sample.h) is the single source of truth for the on-disk frame:
// the swing.json exporter writes the struct fields verbatim, in order
// (accel_x,y,z, gyro_x,y,z, quat_w,x,y,z — swing_exporter.cpp:394-397), so freezing
// makeImuSample's output IS freezing the exported data[] frame.
//
// This is the swing_exporter vector byte-golden the plan requires: it captures the
// CURRENT (v1) display-frame remap as a baseline, so the v2 raw-frame change lands as
// a single reviewable diff (the accel/gyro values move; the quaternion does not).

#include "../../Buffer/imu_sample.h"

#include <cstdio>

using namespace pinpoint;

static int g_fail = 0;
static void checkEq(const char *label, float got, float want)
{
    const bool ok = (got == want);
    std::printf("  [%s] %-28s got %8.3f  want %8.3f\n", ok ? "PASS" : "FAIL", label, got, want);
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("=== ImuSample stored-frame characterization (v1) ===\n\n");

    static_assert(sizeof(ImuSample) == 40, "ImuSample layout changed");
    std::printf("-- layout --\n  [PASS] sizeof(ImuSample) == 40\n");

    // v1 frame: accel/gyro remapped (X→X, Z→Y, −Y→Z); quaternion stored un-remapped.
    std::printf("\n-- makeImuSample v1 display-frame remap --\n");
    {
        const ImuSample s = makeImuSample(1, 2, 3,           // raw accel
                                          10, 20, 30,        // raw gyro
                                          0.1f, 0.2f, 0.3f, 0.4f);
        checkEq("accel_x = raw ax",  s.accel_x,  1);
        checkEq("accel_y = raw az",  s.accel_y,  3);
        checkEq("accel_z = -raw ay", s.accel_z, -2);
        checkEq("gyro_x = raw gx",   s.gyro_x,  10);
        checkEq("gyro_y = raw gz",   s.gyro_y,  30);
        checkEq("gyro_z = -raw gy",  s.gyro_z, -20);
        checkEq("quat_w (raw)",      s.quat_w, 0.1f);
        checkEq("quat_x (raw)",      s.quat_x, 0.2f);
        checkEq("quat_y (raw)",      s.quat_y, 0.3f);
        checkEq("quat_z (raw)",      s.quat_z, 0.4f);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
