// Anchor + property tests for the parametric swing-reference model core
// (src/Models/swing_reference.{h,cpp}). Self-contained (own main() + CHECK macros;
// no googletest), the same style as src/Analysis/tests/wrist_angles_test.cpp.
//
//   cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
//   cmake --build build/analyzer-tests && ctest --test-dir build/analyzer-tests --output-on-failure
//
// The seven mandatory anchor tests (brief §"Anchor unit tests") PIN the model's sign
// conventions; they are exact tolerances, never widened — a failure means fixing the
// geometry math, not the tolerance. The remaining tests pin the interpolation, the
// sampler, and the one-plane (Δθ_bs=0) degeneracy.
//
// PINNED SIGN CONVENTIONS (verified below, for the downstream comparator/projection):
//   * Frame: origin=ball, X=target line, Z=up, Y=Z×X (RH golfer hub on −Y).
//   * α: arm rotation about the plane normal n, applied to the down-plane vector −e2;
//     evaluate() uses angle sgn·α (sgn=+1 RH, −1 LH).
//   * β: interior arm→shaft angle, applied as a sgn·(180−β) rotation of the arm about n.
//   * Shaft ∥ +X ⟺ β = α−90; shaft ∥ −X ⟺ β = α+90 (plane-inclination-independent).
//   * Butt-side convention: at P1 the butt (hands/grip end) is on the −Y side (hub side).
//   * P7 forward lean tilts the butt toward the target (+X) → butt.x > 0 for lean > 0.
//   * LH is the exact y→−y mirror (sgn flips both the plane-basis y-components and the
//     rotation angles).

#include "swing_reference.h"

#include <QVector3D>
#include <QQuaternion>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace pinpoint::swingref;

static int g_fail = 0;

#define CHECK(label, cond)                                                              \
    do {                                                                               \
        const bool ok = (cond);                                                        \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                       \
        if (!ok) ++g_fail;                                                             \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                              \
    do {                                                                               \
        const double g = (got), w = (want), t = (tol);                                 \
        const bool ok = std::abs(g - w) <= t;                                          \
        std::printf("  [%s] %-34s got %9.4f  want %9.4f  (tol %.4f)\n",                \
                    ok ? "PASS" : "FAIL", label, g, w, t);                             \
        if (!ok) ++g_fail;                                                             \
    } while (0)

// --- geometry helpers --------------------------------------------------------
static double rad2deg(double r) { return r * 180.0 / M_PI; }

// Angle between the shaft and the horizontal ground plane (deg) = |elevation|.
static double shaftGroundAngleDeg(const ShaftPose& p)
{
    return std::abs(rad2deg(std::asin(std::clamp(double(p.dir.normalized().z()), -1.0, 1.0))));
}

// Angle of the shaft from the X (target-line) axis (deg); 0 ⇒ shaft ∥ ±X.
static double shaftToXAxisDeg(const ShaftPose& p)
{
    return rad2deg(std::acos(std::clamp(std::abs(double(p.dir.normalized().x())), 0.0, 1.0)));
}

// |component of the shaft direction along the FSP normal| — 0 ⇒ shaft lies in the FSP.
static double dotNfsp(const ShaftPose& p, double fspDeg)
{
    const double t = fspDeg * M_PI / 180.0;
    const QVector3D n(0, float(std::sin(t)), float(std::cos(t)));
    return std::abs(double(QVector3D::dotProduct(p.dir.normalized(), n)));
}

// The single-golfer driver fixture that anchors BOTH ends (P1 lie exact + clubhead on
// ball; P7 clubhead on ball on the FSP). armLength is the hub→hands reach that makes the
// arm-length residual vanish — see the model's orientation-first anchor note.
static std::unique_ptr<SwingReferenceModel> makeDriver()
{
    GolferAnthro a{ QVector3D(0.2f, -1.02f, 1.45f), 0.6629, /*rightHanded=*/true };
    ClubSpec c;
    c.length = 1.14; c.lieDeg = 56.0; c.ballOffsetX = 0.0; c.forwardLeanP7Deg = 0.0;
    return makeSwingReferenceModel(a, c, RefConfig{});
}
static constexpr double kDriverClubLen = 1.14;

int main()
{
    std::printf("=== swing_reference.h anchor + property tests ===\n\n");

    // ---------------------------------------------------------------- Test 1: P1 ----
    std::printf("-- 1. P1 (Backswing s=0): clubhead on ball, shaft at lie, butt on -Y --\n");
    {
        auto m = makeDriver();
        const ShaftPose p = m->evaluate(Segment::Backswing, 0.0);
        const QVector3D ch = p.clubhead(kDriverClubLen);
        CHECK_NEAR("clubhead-to-ball (mm)",   double(ch.length()) * 1000.0, 0.0, 5.0);
        CHECK_NEAR("shaft-ground angle (deg)", shaftGroundAngleDeg(p),       56.0, 0.1);
        CHECK("butt on -Y side", p.butt.y() < 0.0f);
    }

    // ---------------------------------------------------------------- Test 2: P7 ----
    std::printf("\n-- 2. P7 (Downswing s=1): clubhead on ball, in FSP, forward lean --\n");
    {
        auto m = makeDriver();
        const double fsp = m->fspInclinationDeg();
        const ShaftPose p = m->evaluate(Segment::Downswing, 1.0);
        const QVector3D ch = p.clubhead(kDriverClubLen);
        CHECK_NEAR("clubhead-to-ball (mm)", double(ch.length()) * 1000.0, 0.0, 5.0);
        CHECK("shaft in FSP (|dot(dir,n)|<1e-3)", dotNfsp(p, fsp) < 1e-3);
        CHECK_NEAR("driver lean → butt.x≈0", double(p.butt.x()), 0.0, 5e-3);

        // A wedge (8° lean) leans the butt toward the target (+X).
        GolferAnthro aw{ QVector3D(0.2f, -0.85f, 1.40f), 0.60, true };
        ClubSpec cw; cw.length = 0.89; cw.lieDeg = 64.0; cw.ballOffsetX = -0.02; cw.forwardLeanP7Deg = 8.0;
        auto mw = makeSwingReferenceModel(aw, cw);
        const ShaftPose pw = mw->evaluate(Segment::Downswing, 1.0);
        CHECK("wedge forward lean → butt.x>0", pw.butt.x() > 0.0f);
        CHECK("wedge P7 shaft in FSP", dotNfsp(pw, mw->fspInclinationDeg()) < 1e-3);
    }

    // ------------------------------------------------ Test 3: keyframe constraints --
    std::printf("\n-- 3. Keyframe constraints: P2/P4/P6 shaft horizontal ∥ target line --\n");
    {
        auto m = makeDriver();
        struct KF { const char* name; Segment seg; double s; };
        for (const KF kf : { KF{"P2 (back  s=0.33)", Segment::Backswing, 0.33},
                             KF{"P4 (back  s=1.00)", Segment::Backswing, 1.00},
                             KF{"P6 (down  s=0.80)", Segment::Downswing, 0.80} }) {
            const ShaftPose p = m->evaluate(kf.seg, kf.s);
            std::printf("   %s:\n", kf.name);
            CHECK_NEAR("  shaft-ground angle (deg)", shaftGroundAngleDeg(p), 0.0, 0.5);
            CHECK_NEAR("  shaft-vs-X-axis (deg)",    shaftToXAxisDeg(p),     0.0, 0.5);
        }
        // P6 is additionally on the FSP (Kwon planar region resumes there).
        CHECK("P6 on FSP", dotNfsp(m->evaluate(Segment::Downswing, 0.80), m->fspInclinationDeg()) < 1e-3);
    }

    // -------------------------------------------------- Test 4: fsp ordering/range --
    std::printf("\n-- 4. fspInclinationDeg(): driver < 7-iron < wedge, in [45,65] --\n");
    {
        auto fspFor = [](double hubY, ClubSpec c) {
            GolferAnthro a{ QVector3D(0.2f, float(hubY), 1.35f), 0.62, true };
            return makeSwingReferenceModel(a, c)->fspInclinationDeg();
        };
        ClubSpec dr; dr.length = 1.14; dr.lieDeg = 56.0;  dr.ballOffsetX =  0.10;
        ClubSpec i7; i7.length = 0.94; i7.lieDeg = 62.5;  i7.ballOffsetX =  0.00;
        ClubSpec wd; wd.length = 0.89; wd.lieDeg = 64.0;  wd.ballOffsetX = -0.02; wd.forwardLeanP7Deg = 8.0;
        // Longer club → stand further (hub further, y more negative) → shallower plane.
        const double fDr = fspFor(-1.25, dr);
        const double f7  = fspFor(-1.00, i7);
        const double fWd = fspFor(-0.85, wd);
        std::printf("   driver=%.2f  7-iron=%.2f  wedge=%.2f\n", fDr, f7, fWd);
        CHECK("driver < 7-iron", fDr < f7);
        CHECK("7-iron < wedge",  f7 < fWd);
        CHECK("all in [45,65]",  fDr >= 45.0 && fWd <= 65.0);
    }

    // ---------------------------------------------------- Test 5: LH mirror (y→−y) --
    std::printf("\n-- 5. LH golfer is the exact y→−y mirror of the RH golfer --\n");
    {
        GolferAnthro ar{ QVector3D(0.2f, -1.02f, 1.45f), 0.6629, true  };
        GolferAnthro al{ QVector3D(0.2f,  1.02f, 1.45f), 0.6629, false };
        ClubSpec c; c.length = 1.14; c.lieDeg = 56.0;
        auto mr = makeSwingReferenceModel(ar, c);
        auto ml = makeSwingReferenceModel(al, c);
        double worstDir = 0.0, worstButt = 0.0;
        for (Segment seg : { Segment::Backswing, Segment::Downswing, Segment::FollowThrough }) {
            for (double s : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
                const ShaftPose r = mr->evaluate(seg, s);
                const ShaftPose l = ml->evaluate(seg, s);
                const QVector3D mDir (r.dir.x(),  -r.dir.y(),  r.dir.z());
                const QVector3D mButt(r.butt.x(), -r.butt.y(), r.butt.z());
                worstDir  = std::max(worstDir,  double((l.dir  - mDir ).length()));
                worstButt = std::max(worstButt, double((l.butt - mButt).length()));
            }
        }
        CHECK_NEAR("max shaft-dir mirror error", worstDir,  0.0, 1e-4);
        CHECK_NEAR("max butt mirror error (m)",  worstButt, 0.0, 1e-4);
    }

    // ---------------------------------------------------------------- Test 6: P8 ----
    std::printf("\n-- 6. P8 (FollowThrough s=1): shaft horizontal ∥ X, in FSP --\n");
    {
        auto m = makeDriver();
        const ShaftPose p = m->evaluate(Segment::FollowThrough, 1.0);
        CHECK_NEAR("shaft-ground angle (deg)", shaftGroundAngleDeg(p), 0.0, 0.5);
        CHECK_NEAR("shaft-vs-X-axis (deg)",    shaftToXAxisDeg(p),     0.0, 0.5);
        CHECK("shaft in FSP (|dot(dir,n)|<1e-3)", dotNfsp(p, m->fspInclinationDeg()) < 1e-3);
    }

    // ---------------------------------------------------- Test 7: C¹ across P7 join -
    std::printf("\n-- 7. C¹ continuity across the P7 join (clubhead velocity dir <1°) --\n");
    {
        auto m = makeDriver();
        const double h = 1e-4;
        const QVector3D vd = m->evaluate(Segment::Downswing, 1.0).clubhead(kDriverClubLen)
                           - m->evaluate(Segment::Downswing, 1.0 - h).clubhead(kDriverClubLen);
        const QVector3D vf = m->evaluate(Segment::FollowThrough, h).clubhead(kDriverClubLen)
                           - m->evaluate(Segment::FollowThrough, 0.0).clubhead(kDriverClubLen);
        CHECK("velocity is well-defined (nonzero) at the join",
              vd.length() > 1e-6f && vf.length() > 1e-6f);
        const double ang = rad2deg(std::acos(std::clamp(
            double(QVector3D::dotProduct(vd.normalized(), vf.normalized())), -1.0, 1.0)));
        CHECK_NEAR("join velocity-direction angle (deg)", ang, 0.0, 1.0);
    }

    // --------------------------------------------- MonotoneCubic monotonicity/clamp -
    std::printf("\n-- 8. MonotoneCubic: monotone (no overshoot) + endpoint clamping --\n");
    {
        const MonotoneCubic mc(Track{ {0.0, 0.0}, {0.3, 2.0}, {0.6, 2.0}, {1.0, 5.0} });
        double prev = mc.eval(0.0), worstDrop = 0.0, worstOver = 0.0;
        for (double s = 0.0; s <= 1.0001; s += 0.005) {
            const double v = mc.eval(s);
            worstDrop = std::min(worstDrop, v - prev);        // non-decreasing data ⇒ ≥0
            worstOver = std::max(worstOver, v - 5.0);         // must not exceed the max knot
            prev = v;
        }
        CHECK("monotone non-decreasing (no dip)", worstDrop >= -1e-6);
        CHECK("no overshoot above max knot",      worstOver <=  1e-6);
        CHECK_NEAR("passes through knot (s=0.3)", mc.eval(0.3), 2.0, 1e-9);
        CHECK_NEAR("clamp below first (s=-1)",    mc.eval(-1.0), 0.0, 1e-12);
        CHECK_NEAR("clamp above last (s=+2)",     mc.eval( 2.0), 5.0, 1e-12);
        const MonotoneCubic single(Track{ {0.0, 3.14} });      // degenerate 1-knot
        CHECK_NEAR("single-knot is constant",     single.eval(0.9), 3.14, 1e-12);
    }

    // ------------------------------------------------ sample() density + ordering ---
    std::printf("\n-- 9. sample(): 3×samplesPerSegment, ordered, s spans [0,1] --\n");
    {
        RefConfig cfg; cfg.samplesPerSegment = 50;
        GolferAnthro a{ QVector3D(0.2f, -1.02f, 1.45f), 0.6629, true };
        ClubSpec c; c.length = 1.14; c.lieDeg = 56.0;
        auto m = makeSwingReferenceModel(a, c, cfg);
        const auto s = m->sample();
        CHECK("size == 3 * samplesPerSegment", s.size() == 150);
        CHECK("segment order Back→Down→Follow",
              s.front().segment == Segment::Backswing &&
              s[50].segment == Segment::Downswing &&
              s[100].segment == Segment::FollowThrough);
        CHECK("each segment spans s∈[0,1]",
              std::abs(s[0].s - 0.0)  < 1e-9 && std::abs(s[49].s  - 1.0) < 1e-9 &&
              std::abs(s[50].s - 0.0) < 1e-9 && std::abs(s[149].s - 1.0) < 1e-9);
    }

    // --------------------------------------------- Δθ_bs=0 one-plane degeneracy -----
    std::printf("\n-- 10. Δθ_bs=0: all three segments collapse onto the FSP --\n");
    {
        RefConfig cfg; cfg.backswingPlaneOffsetDeg = 0.0;
        GolferAnthro a{ QVector3D(0.2f, -1.02f, 1.45f), 0.6629, true };
        ClubSpec c; c.length = 1.14; c.lieDeg = 56.0;
        auto m = makeSwingReferenceModel(a, c, cfg);
        const double fsp = m->fspInclinationDeg();
        double worst = 0.0;
        for (Segment seg : { Segment::Backswing, Segment::Downswing, Segment::FollowThrough })
            for (double s : { 0.0, 0.3, 0.6, 1.0 })
                worst = std::max(worst, dotNfsp(m->evaluate(seg, s), fsp));
        CHECK("every pose lies in the FSP (|dot(dir,n)|<1e-3)", worst < 1e-3);
    }

    // ------------------------------------------------- lieLeanDefaultsFor ordering --
    std::printf("\n-- 11. lieLeanDefaultsFor: driver < irons < wedge; monotone irons --\n");
    {
        const LieLean dr = lieLeanDefaultsFor("Driver");
        const LieLean i3 = lieLeanDefaultsFor("3i");
        const LieLean i7 = lieLeanDefaultsFor("7-iron");
        const LieLean pw = lieLeanDefaultsFor("PW");
        const LieLean wd = lieLeanDefaultsFor("SW");
        CHECK_NEAR("driver lie",  dr.lieDeg, 56.0, 1e-9);
        CHECK_NEAR("driver lean", dr.forwardLeanP7Deg, 0.0, 1e-9);
        CHECK_NEAR("wedge lie",   wd.lieDeg, 64.0, 1e-9);
        CHECK("lie increases driver<3i<7i<PW",
              dr.lieDeg < i3.lieDeg && i3.lieDeg < i7.lieDeg && i7.lieDeg <= pw.lieDeg);
        CHECK("lean increases driver<3i<7i<PW",
              dr.forwardLeanP7Deg < i3.forwardLeanP7Deg &&
              i3.forwardLeanP7Deg < i7.forwardLeanP7Deg &&
              i7.forwardLeanP7Deg <= pw.forwardLeanP7Deg);
    }

    std::printf("\n=== %s (%d assert failures) ===\n",
                g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
