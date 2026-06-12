// Standalone characterization test for the WT9011DCL driver's frame parsing and the
// WT901BLE67 axis-remapping eulerToQuat() override (Track A, Phase 0.3).
//
// These two paths are currently UNREACHABLE on the live BLE67 stream (the 0x61
// combined frame is host-fused from raw accel+gyro; the device Euler is display-only
// and the 0x59 quaternion frame is never emitted — docs/implementation/imu_rearchitecture.md §1.1).
// They are nonetheless load-bearing reference math: a future native-quaternion path
// (Track C) or any re-wiring must inherit exactly these byte- and axis-conventions.
// This freezes them.
//
// parseQuaternion is private, so it is exercised through receiveData() (a full
// 0x55 0x59 frame → quaternionData()). eulerToQuat is the protected WT901BLE67
// override; we promote it via a test subclass. WT9011DCL_BLE is constructed directly —
// its BleImuTransport ctor is trivial and needs no Bluetooth adapter.

#include "wt9011dcl_ble.h"

#include <QByteArray>
#include <cmath>
#include <cstdio>

static int g_fail = 0;

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-34s got %9.5f  want %9.5f  (tol %.4g)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}

static void checkBool(const char *label, bool got, bool want)
{
    const bool ok = (got == want);
    std::printf("  [%s] %-34s got %s  want %s\n",
                ok ? "PASS" : "FAIL", label, got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++g_fail;
}

// Promote the protected override + frame-feed entry point for testing.
class TestBle : public WT9011DCL_BLE {
public:
    using WT9011DCL_BLE::eulerToQuat;     // protected WT901BLE67 override → public
    using WT9011DCL_Base::receiveData;    // protected base frame parser → public
};

// Build an 11-byte 0x55 0x59 quaternion frame (payload = four LE int16) with the
// device's checksum = sum(bytes[0..9]) & 0xFF.
static QByteArray quatFrame(qint16 w, qint16 x, qint16 y, qint16 z)
{
    QByteArray f(11, char(0));
    f[0] = 0x55; f[1] = 0x59;
    auto put = [&](int off, qint16 v) {
        f[off]     = char(v & 0xFF);
        f[off + 1] = char((v >> 8) & 0xFF);
    };
    put(2, w); put(4, x); put(6, y); put(8, z);
    quint8 sum = 0;
    for (int i = 0; i < 10; ++i) sum += static_cast<quint8>(f[i]);
    f[10] = char(sum);
    return f;
}

static WT9011DCL_Base::EulerAngles euler(float roll, float pitch, float yaw)
{
    WT9011DCL_Base::EulerAngles e;
    e.roll = roll; e.pitch = pitch; e.yaw = yaw;
    return e;
}

int main()
{
    std::printf("=== WT9011DCL driver frame-parse + WT901BLE67 eulerToQuat ===\n\n");
    TestBle imu;

    // -- A. parseQuaternion byte golden: payload words are w,x,y,z each le16/32768 --
    std::printf("-- A. parseQuaternion (0x55 0x59 frame → quaternionData) --\n");
    {
        imu.receiveData(quatFrame(16384, 16384, 16384, 16384));   // each +0.5
        auto q = imu.quaternionData();
        checkNear("w (16384/32768)", q.w,  0.5, 1e-4);
        checkNear("x (16384/32768)", q.x,  0.5, 1e-4);
        checkNear("y (16384/32768)", q.y,  0.5, 1e-4);
        checkNear("z (16384/32768)", q.z,  0.5, 1e-4);

        imu.receiveData(quatFrame(32767, 0, -16384, 8192));       // asymmetric + negative
        auto q2 = imu.quaternionData();
        checkNear("w (32767/32768)",  q2.w,  0.999969, 1e-4);
        checkNear("x (0)",            q2.x,  0.0,       1e-4);
        checkNear("y (-16384/32768)", q2.y, -0.5,       1e-4);
        checkNear("z (8192/32768)",   q2.z,  0.25,      1e-4);

        // A corrupt checksum must be rejected (quaternionData unchanged from above).
        QByteArray bad = quatFrame(0, 0, 0, 0);
        bad[10] = char(static_cast<quint8>(bad[10]) ^ 0xFF);
        imu.receiveData(bad);
        auto q3 = imu.quaternionData();
        checkNear("bad checksum ignored (w stays)", q3.w, 0.999969, 1e-4);
    }

    // -- B. WT901BLE67 eulerToQuat override: Roll→X, Yaw→Y, −Pitch→Z (ZYX half-angles) --
    std::printf("\n-- B. eulerToQuat axis remap --\n");
    {
        auto rx = imu.eulerToQuat(euler(30, 0, 0));               // roll 30° → R(X,30°)
        checkBool("roll → has_value", rx.has_value(), true);
        checkNear("roll 30 → w=cos15", rx->w, 0.965926, 1e-4);
        checkNear("roll 30 → x=sin15", rx->x, 0.258819, 1e-4);
        checkNear("roll 30 → y=0",     rx->y, 0.0,      1e-4);
        checkNear("roll 30 → z=0",     rx->z, 0.0,      1e-4);

        auto rz = imu.eulerToQuat(euler(0, 20, 0));               // pitch 20° → R(Z,−20°)
        checkNear("pitch 20 → w=cos10",  rz->w,  0.984808, 1e-4);
        checkNear("pitch 20 → x=0",      rz->x,  0.0,      1e-4);
        checkNear("pitch 20 → y=0",      rz->y,  0.0,      1e-4);
        checkNear("pitch 20 → z=−sin10", rz->z, -0.173648, 1e-4);

        auto ry = imu.eulerToQuat(euler(0, 0, 40));               // yaw 40° → R(Y,40°)
        checkNear("yaw 40 → w=cos20", ry->w, 0.939693, 1e-4);
        checkNear("yaw 40 → x=0",     ry->x, 0.0,      1e-4);
        checkNear("yaw 40 → y=sin20", ry->y, 0.342020, 1e-4);
        checkNear("yaw 40 → z=0",     ry->z, 0.0,      1e-4);
    }

    // -- C. gimbal-lock gate: |pitch| ≥ 85° (kGimbalLockThresholdDeg) → nullopt --
    std::printf("\n-- C. gimbal gate (|pitch| ≥ 85°) --\n");
    {
        checkBool("pitch  85 → nullopt", imu.eulerToQuat(euler(0,  85, 0)).has_value(), false);
        checkBool("pitch −85 → nullopt", imu.eulerToQuat(euler(0, -85, 0)).has_value(), false);
        checkBool("pitch  84 → valid",   imu.eulerToQuat(euler(0,  84, 0)).has_value(), true);
        checkBool("pitch  90 → nullopt", imu.eulerToQuat(euler(0,  90, 0)).has_value(), false);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
