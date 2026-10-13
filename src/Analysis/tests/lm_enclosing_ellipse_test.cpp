// Standalone tests for the launch monitor panel's enclosing ellipse (src/Analysis/
// lm_enclosing_ellipse.h): it encloses EVERY point (Mark's rule — never an SD ellipse), it is
// the minimum-area one to Khachiyan's tolerance (checked against the two closed forms there
// are: a regular polygon's circumcircle and a triangle's Steiner ellipse), fewer than three
// points draw nothing, non-finite readings are dropped, the answer is affine-equivariant,
// a mixed-unit figure (launch × spin) is not fattened by the minor-axis floor, and on the
// 4 Aug fixture's carries it agrees with the signed-off mock's own JS to the tolerance.
// Pure — no Qt, no fixture on disk. Own main()/check() macros.
//
//   cmake --build build/tests --target lm_enclosing_ellipse_test
//   ctest --test-dir build/tests -R lm_enclosing_ellipse --output-on-failure

#include "../lm_enclosing_ellipse.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

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
    std::printf("  [%s] %s (got %.5f, want %.5f ± %.5f)\n",
                ok ? "PASS" : "FAIL", label, got, want, tol);
    if (!ok) ++g_fail;
}

using Pts = std::vector<std::array<double, 2>>;

static bool enclosesAll(const LmEllipse &e, const Pts &pts)
{
    for (const auto &p : pts)
        if (std::isfinite(p[0]) && std::isfinite(p[1]) && !lmEllipseContains(e, p[0], p[1], 1e-9))
            return false;
    return true;
}

// The mock's enclosingEllipse(), transcribed line for line from Panel_A_fsxpro.qml (ONE
// common conditioning scale), so the C++ can be held to what the signed-off panel drew.
// Returns { cx, cy, a, b, angRad }.
struct MockE { bool has; double cx, cy, a, b, ang; };
static MockE mockEnclosingEllipse(const Pts &P)
{
    const int n = int(P.size());
    if (n < 3) return { false, 0, 0, 0, 0, 0 };
    double mx = 0, my = 0;
    for (const auto &p : P) { mx += p[0]; my += p[1]; }
    mx /= n; my /= n;
    double sc = 0;
    for (const auto &p : P) sc = std::max({ sc, std::fabs(p[0] - mx), std::fabs(p[1] - my) });
    sc = sc > 0 ? sc : 1;
    Pts R;
    for (const auto &p : P) R.push_back({ (p[0] - mx) / sc, (p[1] - my) / sc });
    std::vector<double> u(size_t(n), 1.0 / n);
    for (int it = 0; it < 2000; ++it) {
        double a = 1e-12, b = 0, c = 0, d = 1e-12, e = 0, k = 1e-12;
        for (int i = 0; i < n; ++i) { const double x = R[i][0], y = R[i][1], w = u[i];
            a += w * x * x; b += w * x * y; c += w * x; d += w * y * y; e += w * y; k += w; }
        const double A11 = d * k - e * e, A12 = -(b * k - c * e), A13 = b * e - c * d;
        const double A22 = a * k - c * c, A23 = -(a * e - b * c), A33 = a * d - b * b;
        const double det = a * A11 + b * A12 + c * A13;
        if (!(std::fabs(det) > 1e-300)) break;
        int jmax = 0; double mmax = -1;
        for (int i = 0; i < n; ++i) { const double x = R[i][0], y = R[i][1];
            const double m = (A11 * x * x + A22 * y * y + A33 + 2 * (A12 * x * y + A13 * x + A23 * y)) / det;
            if (m > mmax) { mmax = m; jmax = i; } }
        const double step = (mmax - 3) / (3 * (mmax - 1));
        if (!(step > 0)) break;
        double err = 0;
        for (int i = 0; i < n; ++i) { const double nu = (1 - step) * u[i] + (i == jmax ? step : 0); err += (nu - u[i]) * (nu - u[i]); u[i] = nu; }
        if (std::sqrt(err) < 1e-4) break;
    }
    double cx = 0, cy = 0;
    for (int i = 0; i < n; ++i) { cx += u[i] * R[i][0]; cy += u[i] * R[i][1]; }
    double sxx = 0, syy = 0, sxy = 0;
    for (int i = 0; i < n; ++i) { const double x = R[i][0] - cx, y = R[i][1] - cy; sxx += u[i] * x * x; syy += u[i] * y * y; sxy += u[i] * x * y; }
    const double tr = sxx + syy, dt = sxx * syy - sxy * sxy, disc = std::sqrt(std::max(0.0, tr * tr / 4 - dt));
    const double l1 = std::max(1e-12, tr / 2 + disc), l2 = std::max(1e-12, tr / 2 - disc);
    const double ang = 0.5 * std::atan2(2 * sxy, sxx - syy), ca = std::cos(ang), sa = std::sin(ang);
    double A = std::sqrt(2 * l1), B = std::sqrt(2 * l2);
    B = std::max(B, A * 0.02);
    double kmax = 1;
    for (const auto &r : R) { const double x = r[0] - cx, y = r[1] - cy, p1 = (x * ca + y * sa) / A, p2 = (-x * sa + y * ca) / B;
                              kmax = std::max(kmax, std::sqrt(p1 * p1 + p2 * p2)); }
    return { true, mx + cx * sc, my + cy * sc, A * kmax * sc, B * kmax * sc, ang };
}

int main()
{
    std::printf("lm_enclosing_ellipse_test\n");

    // ── Fewer than three: nothing ───────────────────────────────────────────────
    {
        check(!lmEnclosingEllipse({}).has, "no points, no ellipse");
        check(!lmEnclosingEllipse({ { 1, 2 } }).has, "one point, no ellipse");
        const LmEllipse two = lmEnclosingEllipse({ { 1, 2 }, { 3, 4 } });
        check(!two.has, "two points, no ellipse (Mark's rule: a pair is a line, not a spread)");
        check(two.n == 2, "the count is still reported for two points");

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        const LmEllipse sparse = lmEnclosingEllipse({ { 0, 0 }, { 1, 0 }, { nan, 3 }, { 2, inf } });
        check(!sparse.has, "two finite points among four: no ellipse (non-finite ones are not shots)");
    }

    // ── Closed forms: the minimum is the minimum ────────────────────────────────
    {
        // A square's corners: the circumscribed circle, radius = half the diagonal.
        const LmEllipse sq = lmEnclosingEllipse({ { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } });
        check(sq.has, "square corners give an ellipse");
        checkNear(sq.cx, 0.0, 1e-6, "square: centred");
        checkNear(sq.a, std::sqrt(2.0), 0.01 * std::sqrt(2.0), "square: semi-major is the circumradius (±1 %)");
        checkNear(sq.b, std::sqrt(2.0), 0.01 * std::sqrt(2.0), "square: semi-minor is the circumradius (±1 %)");

        // A regular hexagon of radius 3 about (10, -4).
        Pts hex;
        for (int i = 0; i < 6; ++i) hex.push_back({ 10 + 3 * std::cos(i * M_PI / 3), -4 + 3 * std::sin(i * M_PI / 3) });
        const LmEllipse h = lmEnclosingEllipse(hex);
        checkNear(h.cx, 10.0, 1e-3, "hexagon: centre x");
        checkNear(h.cy, -4.0, 1e-3, "hexagon: centre y");
        checkNear(h.a, 3.0, 0.03, "hexagon: semi-major = circumradius (±1 %)");
        checkNear(h.b, 3.0, 0.03, "hexagon: semi-minor = circumradius (±1 %)");

        // A triangle: its Steiner circumellipse, area 4π/(3√3) × the triangle's, centred on
        // the centroid. Khachiyan stops at a tolerance and the answer is then grown to be
        // exact, so it may be a little LARGER than the optimum, never smaller.
        const Pts tri{ { 0, 0 }, { 8, 1 }, { 3, 6 } };
        const double triArea = 0.5 * std::fabs(8 * 6 - 1 * 3);
        const double steiner = 4.0 * M_PI / (3.0 * std::sqrt(3.0)) * triArea;
        const LmEllipse t = lmEnclosingEllipse(tri);
        const double area = M_PI * t.a * t.b;
        check(area >= steiner * (1.0 - 1e-9), "triangle: never smaller than the Steiner ellipse");
        check(area <= steiner * 1.02, "triangle: within 2 % of the Steiner ellipse's area");
        checkNear(t.cx, 11.0 / 3.0, 0.05, "triangle: centred on the centroid (x)");
        checkNear(t.cy, 7.0 / 3.0, 0.05, "triangle: centred on the centroid (y)");
        check(enclosesAll(t, tri), "triangle: every vertex inside or on");
    }

    // ── Encloses every point, every time ────────────────────────────────────────
    {
        std::mt19937 rng(20261013);
        std::normal_distribution<double> nx(150.0, 8.0), ny(0.0, 12.0);
        int allIn = 0, runs = 200;
        for (int r = 0; r < runs; ++r) {
            Pts pts;
            const int n = 3 + int(rng() % 40);
            const double rho = (int(rng() % 200) - 100) / 110.0;
            for (int i = 0; i < n; ++i) {
                const double x = nx(rng), z = ny(rng);
                pts.push_back({ x, z + rho * (x - 150.0) });
            }
            const LmEllipse e = lmEnclosingEllipse(pts);
            if (e.has && enclosesAll(e, pts) && e.a >= e.b && e.b > 0.0) ++allIn;
        }
        std::printf("    %d of %d random sets fully enclosed\n", allIn, runs);
        check(allIn == runs, "200 random correlated sets: every point inside, a >= b > 0");
    }

    // ── Degenerate sets still draw, and still enclose ───────────────────────────
    {
        const Pts line{ { 0, 0 }, { 1, 1 }, { 2, 2 }, { 5, 5 } };
        const LmEllipse e = lmEnclosingEllipse(line);
        check(e.has, "collinear points still give an ellipse");
        check(enclosesAll(e, line), "collinear: every point inside");
        checkNear(e.angleDeg, 45.0, 0.5, "collinear: the major axis lies along the line");
        check(e.b > 0.0 && e.b < 0.1 * e.a, "collinear: a thin but visible minor axis (the 2 % floor)");

        const Pts same{ { 3, 4 }, { 3, 4 }, { 3, 4 } };
        const LmEllipse s = lmEnclosingEllipse(same);
        check(s.has && enclosesAll(s, same), "three identical shots: an ellipse that holds them");
    }

    // ── Affine-equivariant: rescaling an axis rescales the answer ───────────────
    {
        const Pts base{ { 140, -10 }, { 152, 4 }, { 160, 12 }, { 148, -3 }, { 155, -8 }, { 145, 9 } };
        Pts stretched;
        for (const auto &p : base) stretched.push_back({ p[0], 250.0 * p[1] });   // e.g. deg → rpm-sized
        const LmEllipse e1 = lmEnclosingEllipse(base), e2 = lmEnclosingEllipse(stretched);
        const double area1 = M_PI * e1.a * e1.b, area2 = M_PI * e2.a * e2.b;
        checkNear(area2 / area1, 250.0, 250.0 * 1e-3, "scaling y by 250 scales the area by 250");
        checkNear(e2.cy, 250.0 * e1.cy, std::fabs(250.0 * e1.cy) * 1e-3 + 1e-6, "and the centre with it");
        check(enclosesAll(e2, stretched), "the stretched set is enclosed too");
    }

    // ── Mixed units: the floor must not fatten the short axis ───────────────────
    //
    // Launch × spin: a few degrees across, a thousand rpm up. With ONE conditioning scale
    // (the mock's) a near-collinear set floors its minor axis at 2 % of a ~1000 rpm major —
    // tens of "degrees" wide. Per-axis conditioning keeps it honest.
    {
        const Pts ls{ { 16.0, 4500 }, { 17.0, 4700 }, { 18.0, 4900 }, { 19.0, 5100 }, { 20.0, 5300 } };
        const LmEllipse e = lmEnclosingEllipse(ls);
        check(e.has && enclosesAll(e, ls), "launch × spin line: enclosed");
        // The outline's extent along x (degrees): for an ellipse, the half-width along x is
        // sqrt(a² cos² θ + b² sin² θ).
        const double th = e.angleDeg * M_PI / 180.0;
        const double halfX = std::sqrt(e.a * e.a * std::cos(th) * std::cos(th) + e.b * e.b * std::sin(th) * std::sin(th));
        std::printf("    launch × spin half-width in degrees: %.3f\n", halfX);
        check(halfX < 4.0, "launch × spin: the outline spans a few degrees, not tens");
    }

    // ── Agreement with the signed-off mock on the 4 Aug fixture ─────────────────
    //
    // The 17 7-iron shots of 2026-08-04 Wrist_05 that carry carry + offline (yards both),
    // raw from the documents. Same units on both axes, so the mock's single conditioning
    // scale and this one's per-axis scales must give the same ellipse to the tolerance.
    {
        const Pts carryOff{
            { 125.844, 9.814 }, { 152.701, 11.315 }, { 183.928, -3.693 }, { 185.598, -1.756 },
            { 152.069, 6.318 }, { 158.317, -5.943 }, { 157.696, -6.815 }, { 163.130, -19.903 },
            { 150.202, -0.355 }, { 148.438, 15.448 }, { 148.524, 2.605 }, { 167.048, -10.703 },
            { 160.511, -9.246 }, { 154.999, 3.119 }, { 161.076, -6.092 }, { 164.533, -3.105 },
            { 164.745, -6.996 },
        };
        const LmEllipse e = lmEnclosingEllipse(carryOff);
        const MockE m = mockEnclosingEllipse(carryOff);
        std::printf("    C++  cx %.3f cy %.3f a %.3f b %.3f ang %.3f°\n", e.cx, e.cy, e.a, e.b, e.angleDeg);
        std::printf("    mock cx %.3f cy %.3f a %.3f b %.3f ang %.3f°\n", m.cx, m.cy, m.a, m.b, m.ang * 180 / M_PI);
        check(e.has && m.has, "both give an ellipse for the fixture's carries");
        check(enclosesAll(e, carryOff), "fixture carries: every shot inside");
        checkNear(e.cx, m.cx, 0.02 * m.a, "fixture: centre x agrees with the mock (2 % of a)");
        checkNear(e.cy, m.cy, 0.02 * m.a, "fixture: centre y agrees with the mock");
        checkNear(e.a, m.a, 0.02 * m.a, "fixture: semi-major agrees with the mock (±2 %)");
        checkNear(e.b, m.b, 0.02 * m.b, "fixture: semi-minor agrees with the mock (±2 %)");
        double dAng = std::fabs(e.angleDeg - m.ang * 180 / M_PI);
        if (dAng > 90) dAng = 180 - dAng;
        check(dAng < 1.0, "fixture: major-axis angle agrees with the mock (< 1°)");
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
