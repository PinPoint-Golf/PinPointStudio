// Standalone tests for the launch monitor ball flight model (src/Analysis/
// lm_flight_path.h): the drag + Magnus integration against the design brief's reference
// shot and two fitted corpus shots, the asymmetry that makes the curve worth drawing at
// all, and the normalisation that pins the drawn endpoints AND end angles to the device's
// reported ones. Pure — no Qt, no OpenCV, no fixture. Own main()/check() macros.
//
//   cmake --build build/tests --target lm_flight_path_test
//   ctest --test-dir build/tests -R lm_flight_path --output-on-failure

#include "../lm_flight_path.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void checkNear(double got, double want, double tol, const char *label)
{
    const bool ok = std::fabs(got - want) <= tol;
    std::printf("  [%s] %s (got %.4f, want %.4f ± %.4f)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}

int main()
{
    std::printf("lm_flight_path_test\n");

    // ── The reference shot ──────────────────────────────────────────────────────
    //
    // The design brief's own row. The constants that came before were hand-set to this
    // one shot and landed it within a yard; the fitted ones (tools/lm_flight_fit/, 36 GC
    // Quad shots) carry it 6.6 yd long and 8.7 ft high, which is inside the fit's own
    // spread for a mid-spin shot. These pin what the model SAYS, so a change to
    // LmFlightModel that moves them is seen; the device's numbers are in the labels.
    const LmLaunch ref{ 118.1, 13.2, -1.3, 4686.0, 4.7 };
    {
        const LmFlightIntegration f = lmIntegrateFlight(ref);
        check(f.has, "reference shot flies");
        checkNear(f.carryYd,   173.2, 0.6, "reference carry (device says 166.6)");
        checkNear(f.apexFt,     71.7, 0.6, "reference apex (device says 63)");
        checkNear(f.descentDeg, 39.2, 0.4, "reference descent (device says 37.3)");
        checkNear(f.offlineYd,  2.58, 0.1, "reference offline from a 4.7 deg axis");

        // The shape claim the whole card rests on. If the apex ever drifts to the
        // middle, the drawing has stopped being a model and gone back to being an arch.
        check(f.apexFraction > 0.55 && f.apexFraction < 0.70,
              "apex sits in 0.55-0.70 of carry for a normal iron");
        check(f.descentDeg > f.launchDeg + 15.0,
              "descent is markedly steeper than launch");
    }

    // ── The corpus shots the fit was judged on ──────────────────────────────────
    //
    // Session 2026-08-04_Mark-Liversedge_Wrist_05, GC Quad, inputs as the device stored
    // them. Shot 8 is the one the old constants carried 31 yd short (185.7 of 217.1): a
    // fast, low-spin ball, the case a single drag curve set by mid-spin irons gets
    // wrong. The tolerances are what the fit achieves on it, not what we would like —
    // the low-spin shots are the ones it still leaves ~2% short (see LmFlightModel).
    // Shot 18 is a typical mid-spin strike, for contrast.
    const LmLaunch shot8{ 133.408, 11.928, 2.260, 2374.0, 0.193 };
    const LmLaunch shot18{ 105.141, 20.860, -0.579, 4514.0, 0.990 };
    {
        const LmFlightIntegration f = lmIntegrateFlight(shot8);
        check(f.has, "shot 8 flies");
        checkNear(f.carryYd,   217.1, 3.0, "shot 8 carry within 3 yd of the device's 217.1");
        checkNear(f.apexFt,     69.1, 1.0, "shot 8 apex within 1 ft of the device's 69.1");
        checkNear(f.descentDeg, 32.6, 0.3, "shot 8 descent within 0.3 deg of the device's 32.6");

        const LmFlightIntegration g = lmIntegrateFlight(shot18);
        check(g.has, "shot 18 flies");
        checkNear(g.carryYd,   150.2, 2.5, "shot 18 carry within 2.5 yd of the device's 150.2");
        checkNear(g.apexFt,     83.3, 1.0, "shot 18 apex within 1 ft of the device's 83.3");
        checkNear(g.descentDeg, 44.3, 0.3, "shot 18 descent within 0.3 deg of the device's 44.3");
    }

    // ── Lift is held at its peak, never taken away ──────────────────────────────
    //
    // Past the quadratic's peak the lift curve must stay flat. Evaluated bare, the old
    // quadratic fell to zero at S = clS/clS2 — a wedge's spin factor — and would have
    // flown a wedge with no lift at all.
    {
        const LmFlightModel m;
        const double sPeak = m.clS / (2.0 * m.clS2);
        checkNear(m.lift(0.6), m.lift(sPeak), 1e-12, "Cl at S = 0.6 equals Cl at the peak");
        check(m.lift(1.0) > 0.25, "a very high spin factor still lifts");
        check(m.lift(0.1) < m.lift(0.2) && m.lift(0.2) < m.lift(sPeak),
              "Cl rises with S up to the peak");
        check(m.cd0 >= 0.20 && m.cd0 + 0.4 * m.cdS <= 0.35, "Cd stays within 0.20-0.35");
    }

    // ── x is monotonic ──────────────────────────────────────────────────────────
    //
    // The ball never travels backwards downrange. Cheap to assert and it is the first
    // symptom of an integration that has gone unstable at a larger step.
    {
        const LmFlightIntegration f = lmIntegrateFlight(ref);
        bool mono = true;
        for (size_t i = 1; i < f.points.size(); ++i)
            if (f.points[i].x < f.points[i - 1].x - 1e-9) mono = false;
        check(mono, "downrange distance is monotonic");
        check(f.points.size() > 100, "the polyline has enough points to draw");
    }

    // ── Drag-free, spin-free: an exact parabola ─────────────────────────────────
    //
    // The integrator's own correctness check, separated from the aerodynamics. With no
    // drag and no lift the answer is closed-form, so the apex must land at exactly half
    // the carry and the ball must come down at the angle it left at.
    //
    // NOTE this is NOT the brief's "zero spin gives a symmetric parabola" test, which
    // cannot hold as written: a zero-spin ball still has drag, and drag alone already
    // breaks the symmetry (see the case below). Symmetry is a property of the vacuum
    // case, and asserting it of the zero-spin case would have been asserting a bug.
    {
        LmFlightModel vac;
        vac.cd0 = 0.0; vac.cdS = 0.0; vac.clS = 0.0; vac.clS2 = 0.0; vac.clMax = 0.0;
        const LmFlightIntegration f = lmIntegrateFlight({ 118.1, 13.2, 0.0, 0.0, 0.0 }, vac);
        check(f.has, "vacuum shot flies");
        checkNear(f.apexFraction, 0.5,  0.005, "vacuum apex at half the carry");
        checkNear(f.descentDeg,   13.2, 0.10,  "vacuum descent equals launch");
    }

    // ── Zero spin, with drag: where the asymmetry actually comes from ───────────
    //
    // Drag alone already breaks the symmetry — it steepens the descent and nudges the
    // apex past the midpoint — but only slightly. It is LIFT that carries the apex out
    // to the ~63% the card is built around. Asserting both halves separately is what
    // stops the lift term being quietly dropped as "close enough": without it the curve
    // would still lean the right way and would lean nowhere near far enough.
    {
        const LmFlightIntegration drag = lmIntegrateFlight({ 118.1, 13.2, 0.0, 0.0, 0.0 });
        const LmFlightIntegration full = lmIntegrateFlight(ref);
        check(drag.has, "zero-spin shot flies");
        check(drag.apexFraction > 0.5, "drag alone leans the apex past the midpoint");
        check(drag.apexFraction < full.apexFraction - 0.05,
              "but lift is what carries it out to 63%");
        check(drag.descentDeg > 13.2, "drag alone still steepens the descent");
        check(std::fabs(drag.offlineYd) < 0.01, "no spin, no sideways");
        check(drag.carryYd < 120.0, "no lift, far less carry");
    }

    // ── Spin axis drives the side, and its sign ─────────────────────────────────
    {
        const LmFlightIntegration r = lmIntegrateFlight({ 118.1, 13.2, 0.0, 4686.0,  12.0 });
        const LmFlightIntegration l = lmIntegrateFlight({ 118.1, 13.2, 0.0, 4686.0, -12.0 });
        check(r.offlineYd > 0.0, "a right-tilted axis finishes right");
        check(l.offlineYd < 0.0, "a left-tilted axis finishes left");
        checkNear(r.offlineYd, -l.offlineYd, 0.05, "the two mirror each other");
    }

    // ── Normalisation lands the endpoints exactly ───────────────────────────────
    //
    // The contract with the card: the SHAPE is the model's, the ENDPOINTS are the
    // device's. Every one of these is exact, not approximate — a drawn ball that lands
    // a pixel off the carry label it sits under is the defect this replaced.
    {
        const LmFlightIntegration raw = lmIntegrateFlight(ref);
        const LmFlightPath p = lmNormalisedPath(raw, 166.6, 63.0, 5.1, 181.9, 13.2, 37.3);
        check(p.has, "normalised path exists");

        checkNear(p.carryYd,   166.6, 1e-9, "carry is the reported carry");
        checkNear(p.apexFt,     63.0, 1e-9, "apex is the reported apex");
        checkNear(p.offlineYd,   5.1, 1e-9, "offline is the reported offline");
        checkNear(p.totalYd,   181.9, 1e-9, "total is the reported total");

        // 166.6 / 181.9 — the trajectory ends where the roll begins, on one shared axis.
        checkNear(p.carryFraction, 166.6 / 181.9, 1e-6, "carry fraction of total");
        checkNear(p.finish.x, 1.0, 1e-9, "the roll finishes at x = 1");
        checkNear(p.finish.y, 0.0, 1e-9, "the roll finishes on the ground");

        double maxY = 0.0, minY = 1.0;
        bool inBand = true;
        for (const LmPoint &q : p.points) {
            maxY = std::max(maxY, q.y);
            minY = std::min(minY, q.y);
            if (q.x < -1e-9 || q.x > 1.0 + 1e-9 || std::fabs(q.z) > 1.0 + 1e-9)
                inBand = false;
        }
        checkNear(maxY, 1.0, 1e-9, "apex normalises to exactly 1");
        check(minY >= -1e-9, "nothing dips below the ground");
        check(inBand, "every point is inside the normalised box");

        // The landing's lateral position, taken back out of normalised space, is the
        // reported offline. This is the assertion that catches a lateral scale applied
        // to the wrong extent.
        checkNear(p.landing.z * p.lateralExtentYd, 5.1, 1e-6, "landing z decodes to 5.1 yd");
        checkNear(p.apexFractionOfCarry, raw.apexFraction, 1e-12, "apex fraction survives");
        checkNear(p.apexAtX, p.carryFraction * raw.apexFraction, 1e-12, "apex x is consistent");

        // The residual the brief asks to be surfaced rather than hidden.
        checkNear(p.residualOfflineYd, 5.1 - raw.offlineYd, 1e-9, "offline residual is kept");
        check(p.residualOfflineYd > 2.0, "the reference shot's residual is the real one");

        // Tangents: leaving shallower than it arrives, in the drawn space.
        check(p.launchTangent.y > 0.0,  "launch tangent climbs");
        check(p.landingTangent.y < 0.0, "landing tangent descends");
        checkNear(p.descentDeg, 37.3, 1e-12, "descent is the reported descent");
        checkNear(p.modelDescentDeg, raw.descentDeg, 1e-12, "the model's descent is kept beside it");
    }

    // ── The drawn curve leaves and lands at the measured angles ─────────────────
    //
    // Scaling x and y by different factors bends both end angles — for shot 8 to 11.7
    // and 32.0 against a measured 11.9 and 32.6. The height remap turns them back. Read
    // in TRUE units (yards against feet, so ÷ 3), off both the polyline's end segments
    // and the tangent vectors, because the card draws the first and the guides the
    // second. Carry, apex and offline must stay exact while it happens, and the curve
    // must still only climb to the apex and only fall after it.
    {
        struct Case { const char *name; LmLaunch in; double carry, apex, offline, total, descent; };
        const Case cases[] = {
            { "shot 8",  shot8,  217.121, 69.067,  9.823, 240.031, 32.565 },
            { "shot 18", shot18, 150.202, 83.340, -0.355, 162.546, 44.346 },
            { "reference", ref,  166.6,   63.0,    5.1,   181.9,   37.3   },
            // A device far from the model on both ends: the remap's r > 2 branch.
            { "steep extreme", ref, 166.6, 63.0,   5.1,   181.9,   70.0   },
        };
        for (const Case &c : cases) {
            std::printf("  -- %s\n", c.name);
            const LmFlightIntegration raw = lmIntegrateFlight(c.in);
            const LmFlightPath p = lmNormalisedPath(raw, c.carry, c.apex, c.offline, c.total,
                                                    c.in.launchAngleDeg, c.descent);
            check(p.has, "normalised");
            const auto trueAngle = [&](double dxN, double dyN) {   // degrees above horizontal
                return std::atan2(dyN * p.apexFt / 3.0, dxN * p.totalYd) * 180.0 / M_PI;
            };
            const size_t n = p.points.size();
            const LmPoint &a0 = p.points[0], &a1 = p.points[1];
            const LmPoint &b0 = p.points[n - 2], &b1 = p.points[n - 1];
            const double launchSeg  =  trueAngle(a1.x - a0.x, a1.y - a0.y);
            const double descentSeg = -trueAngle(b1.x - b0.x, b1.y - b0.y);
            const double launchTan  =  trueAngle(p.launchTangent.x, p.launchTangent.y);
            const double descentTan = -trueAngle(p.landingTangent.x, p.landingTangent.y);
            std::printf("     drawn launch %.3f (tangent %.3f), descent %.3f (tangent %.3f);"
                        " model alone %.2f / %.2f\n", launchSeg, launchTan, descentSeg,
                        descentTan, c.in.launchAngleDeg, raw.descentDeg);
            checkNear(launchSeg,  c.in.launchAngleDeg, 0.1, "first segment leaves at the measured launch");
            checkNear(descentSeg, c.descent, 0.1, "last segment lands at the measured descent");
            checkNear(launchTan,  c.in.launchAngleDeg, 0.1, "launch tangent is the measured launch");
            checkNear(descentTan, c.descent, 0.1, "landing tangent is the measured descent");
            checkNear(p.descentDeg, c.descent, 1e-12, "descentDeg reports the measured descent");

            checkNear(p.landing.x * p.totalYd, c.carry, 1e-6, "lands at the reported carry");
            checkNear(p.landing.z * p.lateralExtentYd, c.offline, 1e-6, "lands at the reported offline");
            checkNear(p.landing.y, 0.0, 1e-12, "lands on the ground");

            size_t apex = 0;
            for (size_t i = 1; i < n; ++i)
                if (p.points[i].y > p.points[apex].y) apex = i;
            checkNear(p.points[apex].y * p.apexFt, c.apex, 1e-9, "peaks at the reported apex");
            checkNear(p.points[apex].x / p.carryFraction, raw.apexFraction, 0.002,
                      "apex keeps the model's fraction of carry");
            bool up = true, down = true, inside = true;
            for (size_t i = 1; i < n; ++i) {
                if (i <= apex && p.points[i].y < p.points[i - 1].y) up = false;
                if (i >  apex && p.points[i].y > p.points[i - 1].y) down = false;
                if (p.points[i].y < 0.0 || p.points[i].y > 1.0) inside = false;
            }
            check(up,     "climbs monotonically to the apex");
            check(down,   "falls monotonically after it");
            check(inside, "never above the apex, never below the ground");
        }
    }

    // ── A dead-straight model shot still draws a curved-out finish ──────────────
    //
    // Zero spin axis means the model finishes on the target line, but the device may
    // still report an offline (wind, or a soft axis reading). Dividing by the model's
    // zero would be the obvious bug; the fallback is a linear drift, which is the only
    // defensible line through "started straight, finished 5 yd right".
    {
        const LmFlightIntegration raw = lmIntegrateFlight({ 118.1, 13.2, 0.0, 4686.0, 0.0 });
        check(std::fabs(raw.offlineYd) < 1e-6, "model shot is dead straight");
        const LmFlightPath p = lmNormalisedPath(raw, 166.6, 63.0, 5.0, 181.9, 13.2, 37.3);
        check(p.has, "straight model + offset report still normalises");
        checkNear(p.landing.z * p.lateralExtentYd, 5.0, 1e-6, "landing still decodes to 5.0 yd");
        bool finite = true;
        for (const LmPoint &q : p.points)
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z)) finite = false;
        check(finite, "no division by a zero lateral extent");
    }

    // ── Missing reports fall back to the model, never to zero ───────────────────
    {
        const LmFlightIntegration raw = lmIntegrateFlight(ref);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const LmFlightPath p = lmNormalisedPath(raw, nan, nan, nan, nan, nan, nan);
        check(p.has, "a device that reported no distances still draws");
        checkNear(p.carryYd, raw.carryYd, 1e-9, "carry falls back to the model's");
        checkNear(p.apexFt,  raw.apexFt,  1e-9, "apex falls back to the model's");
        checkNear(p.descentDeg, raw.descentDeg, 1e-12, "descent falls back to the model's");
        checkNear(p.totalYd, p.carryYd,   1e-9, "no total means no roll");
        checkNear(p.carryFraction, 1.0,   1e-9, "and the trajectory fills the axis");
        // Nothing reported means nothing to correct: the drawn curve IS the model's,
        // point for point.
        double worst = 0.0;
        for (size_t i = 0; i < p.points.size(); ++i)
            worst = std::max(worst, std::fabs(p.points[i].y * p.apexFt - raw.points[i].y));
        check(worst < 0.05, "with no reports the drawn heights are the model's (within 0.05 ft)");
    }

    // ── Nothing in, nothing out ─────────────────────────────────────────────────
    //
    // The same rule as the reductions header: absence is not zero. A shot the monitor
    // missed must produce no path, so the card draws no flight rather than a flat line
    // that claims the ball never left the ground.
    {
        check(!lmIntegrateFlight({ 0.0, 13.2, 0.0, 4686.0, 4.7 }).has, "no ball speed, no path");
        check(!lmIntegrateFlight({ 118.1, 0.0, 0.0, 4686.0, 4.7 }).has, "no launch angle, no path");
        check(!lmIntegrateFlight({ 118.1, -3.0, 0.0, 4686.0, 4.7 }).has, "negative launch, no path");
        check(!lmNormalisedPath(LmFlightIntegration{}, 166.6, 63.0, 5.1, 181.9, 13.2, 37.3).has,
              "no integration, no normalised path");
    }

    std::printf("%s\n", g_fail == 0 ? "OK" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
