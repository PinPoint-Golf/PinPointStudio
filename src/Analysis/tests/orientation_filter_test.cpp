// Standalone characterization test for the two host-fusion orientation filters
// (Track A, Phase 0.4): MadgwickFilter (default) and EskfOrientationFilter.
//
// Run via CTest (src/Analysis/tests/CMakeLists.txt). Freezes today's fusion
// behaviour — gravity seeding, static stability, and constant-gyro integration —
// so a frame-contract change that perturbs the fused quaternion fails loudly.
//
// Both filters implement IOrientationFilter and are constructed directly here, so
// this test needs NEITHER ImuBase NOR the Buffer/whisper stack — only Eigen (for
// the ESKF .cpp). The exact gravity-seed edges are asserted from first principles;
// the integrator/ESKF values are captured-from-current-code goldens.

#include "../../IMU/orientation_filter.h"        // MadgwickFilter (header-only)
#include "../../IMU/eskf_orientation_filter.h"   // EskfOrientationFilter (Eigen-backed)

#include <algorithm>
#include <cmath>
#include <cstdio>

static int g_fail = 0;

static void checkNear(const char *label, double got, double want, double tol)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  [%s] %-34s got %9.4f  want %9.4f  (tol %.4g)\n",
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

// Recovered rotation angle (deg) of a unit quaternion, in [0,180].
static double angleDeg(const IOrientationFilter &f)
{
    const double v = std::sqrt(double(f.x()) * f.x() + double(f.y()) * f.y() + double(f.z()) * f.z());
    return 2.0 * std::atan2(v, std::abs(double(f.w()))) * 180.0 / M_PI;
}

static void dumpQuat(const char *label, const IOrientationFilter &f)
{
    std::printf("  [DIAG] %-22s (w,x,y,z) = (%.5f, %.5f, %.5f, %.5f)  angle=%.3f°\n",
                label, f.w(), f.x(), f.y(), f.z(), angleDeg(f));
}

// Minimal quaternion algebra to characterize a filter by the rotation RELATIVE to
// its own seed — convention-robust, so it works regardless of each filter's absolute
// world-frame choice.
struct Quat { double w, x, y, z; };
static Quat snap(const IOrientationFilter &f) { return { f.w(), f.x(), f.y(), f.z() }; }
static Quat conj(const Quat &q)               { return { q.w, -q.x, -q.y, -q.z }; }
static Quat mul(const Quat &a, const Quat &b)
{
    return { a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
             a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
             a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
             a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w };
}
static double quatAngleDeg(const Quat &q)
{
    const double v = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z);
    return 2.0 * std::atan2(v, std::abs(q.w)) * 180.0 / M_PI;
}
static Quat relToSeed(const Quat &seed, const Quat &fin) { return mul(conj(seed), fin); }  // body-frame rel

int main()
{
    std::printf("=== orientation filter characterization (Madgwick + ESKF) ===\n\n");

    // -- A. Madgwick gravity seeding (exact edges, from the shortest-arc construction) --
    std::printf("-- A. Madgwick gravity seed --\n");
    {
        MadgwickFilter m;
        checkBool("uninitialised", m.initialized(), false);

        m.initFromAccel(0, 0, 1);                 // gravity up == world up → identity
        checkBool("initialised after seed", m.initialized(), true);
        checkNear("seed(0,0,1) w", m.w(), 1.0, 1e-4);
        checkNear("seed(0,0,1) x", m.x(), 0.0, 1e-4);
        checkNear("seed(0,0,1) y", m.y(), 0.0, 1e-4);
        checkNear("seed(0,0,1) z", m.z(), 0.0, 1e-4);

        m.reset();
        m.initFromAccel(0, 0, -1);                // gravity down → 180° about X edge
        checkNear("seed(0,0,-1) w", m.w(), 0.0, 1e-4);
        checkNear("seed(0,0,-1) x", m.x(), 1.0, 1e-4);
        checkNear("seed(0,0,-1) y", m.y(), 0.0, 1e-4);
        checkNear("seed(0,0,-1) z", m.z(), 0.0, 1e-4);

        // Tilted gravity → 90° rotation seed. Captured-from-code golden.
        m.reset();
        m.initFromAccel(0, 1, 0);
        dumpQuat("seed(0,1,0)", m);
        checkNear("seed(0,1,0) angle°", angleDeg(m), 90.0, 0.5);
        checkNear("seed(0,1,0) w", m.w(), 0.70711, 1e-3);
        checkNear("seed(0,1,0) x", m.x(), 0.70711, 1e-3);
        checkNear("seed(0,1,0) y", m.y(), 0.0, 1e-3);
        checkNear("seed(0,1,0) z", m.z(), 0.0, 1e-3);
    }

    // -- B. Madgwick static stability: seeded flat + held still → stays at identity --
    std::printf("\n-- B. Madgwick static stability --\n");
    {
        MadgwickFilter m;
        m.initFromAccel(0, 0, 1);
        for (int i = 0; i < 400; ++i) m.update(0, 0, 1, 0, 0, 0, 0.005f);
        dumpQuat("after 400 still steps", m);
        checkNear("static angle° ≈ 0", angleDeg(m), 0.0, 0.2);
    }

    // -- C. Madgwick constant-gyro integration ≈ ω·t (accel=0 → pure integration) --
    std::printf("\n-- C. Madgwick constant-gyro integration --\n");
    {
        // ω = 1 rad/s about +Z for 1.0 s → 57.2958° about Z.
        MadgwickFilter m;
        m.initFromAccel(0, 0, 1);
        for (int i = 0; i < 200; ++i) m.update(0, 0, 0, 0, 0, 1.0f, 0.005f);
        dumpQuat("Z gyro 1rad/s · 1s", m);
        checkNear("integrated angle° ≈ 57.30", angleDeg(m), 57.2958, 0.5);
        checkNear("twist on +Z (z>0)", m.z() > 0 ? 1.0 : 0.0, 1.0, 0.0);

        MadgwickFilter mx;
        mx.initFromAccel(0, 0, 1);
        for (int i = 0; i < 200; ++i) mx.update(0, 0, 0, 0.5f, 0, 0, 0.005f);  // 0.5 rad about X
        dumpQuat("X gyro 0.5rad/s · 1s", mx);
        checkNear("integrated angle° ≈ 28.65", angleDeg(mx), 28.6479, 0.5);
    }

    // -- D. ESKF: seed + static stability + constant-gyro integration --
    //   Issue #104: the ESKF reports Madgwick's world (+Z up, Hamilton body→world). Until then
    //   it seeded a level sensor as 180° about X (the vendored library's world is z-down) and
    //   seeded any tilted reading wrongly (its initWithAcc predicts gravity at (−x, −y, z)).
    std::printf("\n-- D. ESKF (Madgwick's world: a level seed is identity) --\n");
    {
        EskfOrientationFilter e;
        checkBool("ESKF uninitialised", e.initialized(), false);
        e.initFromAccel(0, 0, 1);
        checkBool("ESKF initialised after seed", e.initialized(), true);
        dumpQuat("ESKF seed(0,0,1)", e);
        const Quat seed = snap(e);
        checkNear("ESKF seed(0,0,1) is identity: angle°", quatAngleDeg(seed), 0.0, 0.01);
        checkNear("ESKF seed w = +1", seed.w, 1.0, 1e-5);

        EskfOrientationFilter es;
        es.initFromAccel(0, 0, 1);
        const Quat esSeed = snap(es);
        for (int i = 0; i < 400; ++i) es.update(0, 0, 1, 0, 0, 0, 0.005f);
        const Quat esFin = snap(es);
        dumpQuat("ESKF after 400 still", es);
        checkNear("ESKF static drift° ≈ 0", quatAngleDeg(relToSeed(esSeed, esFin)), 0.0, 0.01);

        EskfOrientationFilter eg;
        eg.initFromAccel(0, 0, 1);
        const Quat egSeed = snap(eg);
        for (int i = 0; i < 200; ++i) eg.update(0, 0, 0, 0, 0, 1.0f, 0.005f);  // 1 rad about +Z
        const Quat egFin = snap(eg);
        const Quat egRel = relToSeed(egSeed, egFin);
        dumpQuat("ESKF Z gyro 1rad/s · 1s", eg);
        checkNear("ESKF integrated rel-angle° ≈ 57.3", quatAngleDeg(egRel), 57.2958, 0.5);
        checkNear("ESKF rel rotation about +Z", egRel.z > 0 ? 1.0 : 0.0, 1.0, 0.0);
        checkNear("ESKF rel off-axis |x| ≈ 0", std::abs(egRel.x), 0.0, 0.002);
        checkNear("ESKF rel off-axis |y| ≈ 0", std::abs(egRel.y), 0.0, 0.002);

        MadgwickFilter mg;
        mg.initFromAccel(0, 0, 1);
        for (int i = 0; i < 200; ++i) mg.update(0, 0, 0, 0, 0, 1.0f, 0.005f);
        checkNear("ESKF == Madgwick after the same yaw°", quatAngleDeg(mul(conj(snap(mg)), egFin)), 0.0, 0.01);
    }

    // -- E. ESKF frame against Madgwick at rest, at several tilts (issue #104) --
    //   Seeded from one reading, then held still at it: both filters must put that reading on
    //   world +Z (the frame contract, imu_axis_reference.md §3), agree on the gravity direction
    //   within 1°, and differ, if at all, only by a heading (a rotation about world Z).
    //   Then a turn under the gyro to a new tilt, held: the same again, since the world must
    //   not depend on the pose the filter was seeded in.
    std::printf("\n-- E. ESKF vs Madgwick world frame at rest, several tilts --\n");
    {
        auto upInWorld = [](const Quat &q, double ax, double ay, double az, double out[3]) {
            const Quat r = mul(mul(q, Quat{ 0, ax, ay, az }), conj(q));
            const double n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
            out[0] = r.x / n; out[1] = r.y / n; out[2] = r.z / n;
        };
        const float tilts[][3] = { { 0, 0, 1 }, { 0.5f, 0.5f, 0.7071f }, { -0.97f, -0.17f, -0.16f },
                                   { 1, 0, 0 }, { 0, -1, 0 }, { 0.3f, -0.9f, 0.3f },
                                   { -0.6f, 0.3f, -0.74f }, { 0.05f, 0.02f, -1 } };
        double worstUpE = 0, worstUpM = 0, worstTilt = 0, worstOffAxis = 0;
        for (const auto &t : tilts) {
            MadgwickFilter m;
            EskfOrientationFilter e;
            m.initFromAccel(t[0], t[1], t[2]);
            e.initFromAccel(t[0], t[1], t[2]);
            for (int i = 0; i < 400; ++i) {
                m.update(t[0], t[1], t[2], 0, 0, 0, 0.005f);
                e.update(t[0], t[1], t[2], 0, 0, 0, 0.005f);
            }
            double ue[3], um[3];
            upInWorld(snap(e), t[0], t[1], t[2], ue);
            upInWorld(snap(m), t[0], t[1], t[2], um);
            const double upE = std::acos(std::min(1.0, ue[2])) * 180.0 / M_PI;
            const double upM = std::acos(std::min(1.0, um[2])) * 180.0 / M_PI;
            // the relative rotation e·m⁻¹ (world side) must be about world Z only
            const Quat rel = mul(snap(e), conj(snap(m)));
            const double offAxis = 2.0 * std::asin(std::min(1.0, std::sqrt(rel.x * rel.x + rel.y * rel.y))) * 180.0 / M_PI;
            // gravity direction in the body frame from each filter, compared
            const double be[3] = { 2 * (e.x() * e.z() - e.w() * e.y()), 2 * (e.y() * e.z() + e.w() * e.x()), 1 - 2 * (e.x() * e.x() + e.y() * e.y()) };
            const double bm[3] = { 2 * (m.x() * m.z() - m.w() * m.y()), 2 * (m.y() * m.z() + m.w() * m.x()), 1 - 2 * (m.x() * m.x() + m.y() * m.y()) };
            const double tilt = std::acos(std::min(1.0, be[0] * bm[0] + be[1] * bm[1] + be[2] * bm[2])) * 180.0 / M_PI;
            std::printf("  [DIAG] reading (%5.2f %5.2f %5.2f): world-up err ESKF %.4f° Madgwick %.4f°, tilt ESKF-vs-Madgwick %.4f°, "
                        "relative rotation off world Z %.4f°\n", t[0], t[1], t[2], upE, upM, tilt, offAxis);
            worstUpE = std::max(worstUpE, upE); worstUpM = std::max(worstUpM, upM);
            worstTilt = std::max(worstTilt, tilt); worstOffAxis = std::max(worstOffAxis, offAxis);
        }
        checkNear("ESKF puts the reading on world +Z°", worstUpE, 0.0, 1.0);
        checkNear("Madgwick puts the reading on world +Z°", worstUpM, 0.0, 1.0);
        checkNear("ESKF vs Madgwick gravity direction°", worstTilt, 0.0, 1.0);
        checkNear("ESKF vs Madgwick differ by heading only°", worstOffAxis, 0.0, 1.0);

        // Seeded level, turned 120° about body X by the gyro over 2 s, then held 3 s: the reading
        // at the new pose must land on world +Z for both, and the two must still agree.
        MadgwickFilter m;
        EskfOrientationFilter e;
        m.initFromAccel(0, 0, 1);
        e.initFromAccel(0, 0, 1);
        const double rate = 120.0 * M_PI / 180.0 / 2.0;   // rad/s about body X
        double ang = 0.0;
        for (int i = 0; i < 400; ++i) {
            ang += rate * 0.005;
            const float ay = float(std::sin(ang)), az = float(std::cos(ang));   // body reading of up after a turn of ang about X
            m.update(0, ay, az, float(rate), 0, 0, 0.005f);
            e.update(0, ay, az, float(rate), 0, 0, 0.005f);
        }
        const float ay = float(std::sin(ang)), az = float(std::cos(ang));
        for (int i = 0; i < 600; ++i) { m.update(0, ay, az, 0, 0, 0, 0.005f); e.update(0, ay, az, 0, 0, 0, 0.005f); }
        double ue[3], um[3];
        upInWorld(snap(e), 0, ay, az, ue);
        upInWorld(snap(m), 0, ay, az, um);
        checkNear("turned 120°·X: ESKF reading on world +Z°", std::acos(std::min(1.0, ue[2])) * 180.0 / M_PI, 0.0, 1.0);
        checkNear("turned 120°·X: Madgwick reading on world +Z°", std::acos(std::min(1.0, um[2])) * 180.0 / M_PI, 0.0, 1.0);
        checkNear("turned 120°·X: ESKF == Madgwick°", quatAngleDeg(mul(conj(snap(m)), snap(e))), 0.0, 1.0);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
