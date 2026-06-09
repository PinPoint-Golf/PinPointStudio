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
// world-frame choice (Madgwick and ESKF differ by 180° about X; see section D).
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
    //   IMPORTANT CHARACTERIZATION: ESKF seeds gravity (0,0,1) as a 180°-about-X
    //   orientation, NOT identity — a fixed frame-convention difference from Madgwick.
    //   The two filters are runtime-swappable into the SAME downstream A·q·M, so a swap
    //   without re-calibration would flip orientation by this 180°; per-session re-solve
    //   of A absorbs it (docs/IMU_REARCHITECTURE.md §1.4, N1). We therefore characterize
    //   ESKF by rotation RELATIVE to its own seed, which is convention-independent.
    std::printf("\n-- D. ESKF (relative-to-seed; ESKF world differs from Madgwick by 180°·X) --\n");
    {
        EskfOrientationFilter e;
        checkBool("ESKF uninitialised", e.initialized(), false);
        e.initFromAccel(0, 0, 1);
        checkBool("ESKF initialised after seed", e.initialized(), true);
        dumpQuat("ESKF seed(0,0,1)", e);
        const Quat seed = snap(e);
        checkNear("ESKF seed is 180°·X: angle°", quatAngleDeg(seed), 180.0, 0.5);
        checkNear("ESKF seed |x| ≈ 1", std::abs(seed.x), 1.0, 1e-3);
        checkNear("ESKF seed |w| ≈ 0", std::abs(seed.w), 0.0, 1e-3);

        EskfOrientationFilter es;
        es.initFromAccel(0, 0, 1);
        const Quat esSeed = snap(es);
        for (int i = 0; i < 400; ++i) es.update(0, 0, 1, 0, 0, 0, 0.005f);
        const Quat esFin = snap(es);
        dumpQuat("ESKF after 400 still", es);
        checkNear("ESKF static drift° ≈ 0", quatAngleDeg(relToSeed(esSeed, esFin)), 0.0, 0.5);

        EskfOrientationFilter eg;
        eg.initFromAccel(0, 0, 1);
        const Quat egSeed = snap(eg);
        for (int i = 0; i < 200; ++i) eg.update(0, 0, 0, 0, 0, 1.0f, 0.005f);  // 1 rad about +Z
        const Quat egFin = snap(eg);
        const Quat egRel = relToSeed(egSeed, egFin);
        dumpQuat("ESKF Z gyro 1rad/s · 1s", eg);
        std::printf("  [DIAG] ESKF rel-to-seed (w,x,y,z) = (%.5f, %.5f, %.5f, %.5f)\n",
                    egRel.w, egRel.x, egRel.y, egRel.z);
        checkNear("ESKF integrated rel-angle° ≈ 57.3", quatAngleDeg(egRel), 57.2958, 2.5);
        checkNear("ESKF rel rotation about +Z", egRel.z > 0 ? 1.0 : 0.0, 1.0, 0.0);
        checkNear("ESKF rel off-axis |x| ≈ 0", std::abs(egRel.x), 0.0, 0.02);
        checkNear("ESKF rel off-axis |y| ≈ 0", std::abs(egRel.y), 0.0, 0.02);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
