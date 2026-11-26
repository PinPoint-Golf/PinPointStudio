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

// The launch monitor panel's ENCLOSING ELLIPSE — the outline drawn around a club's shots
// on the dispersion, launch × spin, impact map and path × face figures. Pure, header-only,
// no Qt at all. Sibling of lm_session_reductions.h; same rule: QML draws, C++ decides.
//
// WHY NOT AN SD ELLIPSE, which is what the board drew before (lmPairStats). Mark's rule for
// this panel (docs/design/lm_panel_fsx.md, "Ellipse rules"): an ellipse ENCLOSES EVERY SHOT
// IN ITS GROUP. A 1-SD or 2-SD ellipse leaves shots outside it by construction, and on a
// figure whose job is "where did my 7 iron go" a dot sitting outside the outline that
// claims to describe it reads as a different club or a mis-click. FSX draws the outline
// that holds them all, GC Quad owners read it that way, and so does this.
//
// WHICH ENCLOSING ELLIPSE: the minimum-area one (the Löwner–John ellipse), by Khachiyan's
// algorithm — the standard first-order method for the minimum-volume enclosing ellipsoid.
// It is a port of `enclosingEllipse()` in the signed-off mock
// (build/run-me/lm-variants/qml/Panel_A_fsxpro.qml), with the same tolerance, iteration cap,
// regulariser and 2 % floor on the minor axis, so the two draw the same outline.
//
// MADE EXACT. Khachiyan converges to the optimum FROM INSIDE: stopped at a finite tolerance,
// the ellipse it has is a hair too small and the outermost shot sits fractionally outside
// it. So the answer is grown by the largest normalised radius of any point — ≥ 1 only by
// the tolerance — and every point is then inside or on the outline. That step is what turns
// "approximately the minimum" into "certainly encloses", and the second is the promise the
// figure makes.
//
// ⚠ ONE DEPARTURE FROM THE MOCK, AND WHY IT IS NOT A DIFFERENCE. The mock conditions the
// points by ONE scale (the larger of the two axes' spans). That is fine for the dispersion
// figure, where both axes are yards, and wrong for launch × spin, whose axes are degrees and
// rpm: the 2 % minor-axis floor is applied in the conditioned space, and with one common
// scale "2 % of the major axis" becomes 2 % of a few hundred rpm, laid along the degree
// axis — an ellipse tens of degrees fat. Here each axis is conditioned by its OWN span.
// The minimum-volume enclosing ellipse is affine-equivariant (scale an axis and the optimal
// ellipse scales with it) and so is every Khachiyan iterate, because the step is chosen from
// Mahalanobis distances, which an affine map preserves. So for any point set the mock would
// not floor, the two give the same ellipse to the tolerance; the floor is the only place the
// conditioning shows, and per-axis is the conditioning under which it means what it says.
//
// THE OUTPUT IS IN THE POINTS' OWN UNITS. `a` lies along `angleDeg`, `b` across it, and for
// a mixed-unit figure (launch × spin) those numbers are not lengths on screen — the axes are
// drawn at different scales. That is fine and it is the point of returning the ellipse
// rather than pixels: an affine map takes an ellipse to an ellipse, so the figure draws
//     p(t) = (cx, cy) + a·cos t·(cos θ, sin θ) + b·sin t·(−sin θ, cos θ)
// for t over a turn, maps each p through its own axis scales, and gets the correct outline.
//
// FEWER THAN 3 POINTS: NO ELLIPSE (has == false). Mark's rule: two shots are a line, and an
// outline around them would claim a spread nobody has measured.
//
// Unit-tested standalone in src/Analysis/tests/lm_enclosing_ellipse_test.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace pinpoint::analysis {

// One enclosing ellipse, in the units of the points it was fitted to.
struct LmEllipse {
    bool   has      = false;   // n >= 3 finite points
    int    n        = 0;       // the points it encloses
    double cx       = 0.0;     // centre
    double cy       = 0.0;
    double a        = 0.0;     // semi-major axis (a >= b), along angleDeg
    double b        = 0.0;     // semi-minor axis
    // The major axis' direction, from +x toward +y IN THE DATA'S OWN AXES (not screen
    // y-down), in (−90, 90]. A figure that draws y upward uses it as is; one that draws y
    // downward negates it — or, better, maps the parametric points as described above and
    // never handles the angle at all.
    double angleDeg = 0.0;
};

// The tuning, named so a test can quote it and a future change is a visible edit. Every
// value is the mock's.
struct LmEllipseTuning {
    double tolerance      = 1e-4;    // Khachiyan stops when the weight vector moves less than this
    int    maxIterations  = 2000;    // and gives up here regardless (never reached in practice)
    double regulariser    = 1e-12;   // keeps a collinear set's moment matrix invertible
    double minorFloor     = 0.02;    // a collinear set's minor axis: 2 % of the major, not zero
};

// The minimum-area ellipse enclosing every FINITE point in `pts`, grown to be exact.
// Non-finite points are dropped first (a NaN that reached a reading must not poison the
// outline of every other shot); what remains must number at least 3.
inline LmEllipse lmEnclosingEllipse(const std::vector<std::array<double, 2>> &pts,
                                    const LmEllipseTuning &t = {})
{
    LmEllipse out;

    std::vector<std::array<double, 2>> P;
    P.reserve(pts.size());
    for (const auto &p : pts)
        if (std::isfinite(p[0]) && std::isfinite(p[1]))
            P.push_back(p);
    const int n = int(P.size());
    out.n = n;
    if (n < 3)
        return out;

    // ── Condition: centre on the mean, scale each axis by its own span ──────────────
    double mx = 0.0, my = 0.0;
    for (const auto &p : P) { mx += p[0]; my += p[1]; }
    mx /= n; my /= n;
    double sx = 0.0, sy = 0.0;
    for (const auto &p : P) {
        sx = std::max(sx, std::fabs(p[0] - mx));
        sy = std::max(sy, std::fabs(p[1] - my));
    }
    if (!(sx > 0.0)) sx = 1.0;     // every x identical: nothing to scale, and a 0 would divide
    if (!(sy > 0.0)) sy = 1.0;

    std::vector<std::array<double, 2>> R(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        R[size_t(i)] = { (P[size_t(i)][0] - mx) / sx, (P[size_t(i)][1] - my) / sy };

    // ── Khachiyan ───────────────────────────────────────────────────────────────────
    // Weights u on the points; X = Σ u_i q_i q_iᵀ with the lifted q_i = (x, y, 1). Each step
    // moves weight toward the point with the largest Mahalanobis distance under X⁻¹; at the
    // optimum every distance is ≤ d + 1 = 3.
    std::vector<double> u(size_t(n), 1.0 / n);
    for (int it = 0; it < t.maxIterations; ++it) {
        double a = t.regulariser, b = 0.0, c = 0.0, d = t.regulariser, e = 0.0, k = t.regulariser;
        for (int i = 0; i < n; ++i) {
            const double x = R[size_t(i)][0], y = R[size_t(i)][1], w = u[size_t(i)];
            a += w * x * x; b += w * x * y; c += w * x;
            d += w * y * y; e += w * y;     k += w;
        }
        // The adjugate of [[a b c][b d e][c e k]] — symmetric, so six entries.
        const double A11 = d * k - e * e, A12 = -(b * k - c * e), A13 = b * e - c * d;
        const double A22 = a * k - c * c, A23 = -(a * e - b * c), A33 = a * d - b * b;
        const double det = a * A11 + b * A12 + c * A13;
        if (!(std::fabs(det) > 1e-300))
            break;
        int    jmax = 0;
        double mmax = -1.0;
        for (int i = 0; i < n; ++i) {
            const double x = R[size_t(i)][0], y = R[size_t(i)][1];
            const double m = (A11 * x * x + A22 * y * y + A33
                              + 2.0 * (A12 * x * y + A13 * x + A23 * y)) / det;
            if (m > mmax) { mmax = m; jmax = i; }
        }
        const double step = (mmax - 3.0) / (3.0 * (mmax - 1.0));
        if (!(step > 0.0))
            break;                          // every point already inside: optimal
        double err = 0.0;
        for (int i = 0; i < n; ++i) {
            const double nu = (1.0 - step) * u[size_t(i)] + (i == jmax ? step : 0.0);
            err += (nu - u[size_t(i)]) * (nu - u[size_t(i)]);
            u[size_t(i)] = nu;
        }
        if (std::sqrt(err) < t.tolerance)
            break;
    }

    // ── The ellipse the weights describe, in conditioned space ──────────────────────
    // (p − c)ᵀ (2·S)⁻¹ (p − c) ≤ 1, S = Σ u (p − c)(p − c)ᵀ — the d = 2 case of Khachiyan's
    // closing formula. Its semi-axes are √(2λ) for S's eigenvalues λ.
    double ccx = 0.0, ccy = 0.0;
    for (int i = 0; i < n; ++i) { ccx += u[size_t(i)] * R[size_t(i)][0]; ccy += u[size_t(i)] * R[size_t(i)][1]; }
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = R[size_t(i)][0] - ccx, y = R[size_t(i)][1] - ccy;
        sxx += u[size_t(i)] * x * x; syy += u[size_t(i)] * y * y; sxy += u[size_t(i)] * x * y;
    }
    const double tr = sxx + syy, dt = sxx * syy - sxy * sxy;
    const double disc = std::sqrt(std::max(0.0, tr * tr / 4.0 - dt));
    const double l1 = std::max(1e-12, tr / 2.0 + disc), l2 = std::max(1e-12, tr / 2.0 - disc);
    const double ang = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    const double ca = std::cos(ang), sa = std::sin(ang);
    double A = std::sqrt(2.0 * l1), B = std::sqrt(2.0 * l2);
    B = std::max(B, A * t.minorFloor);     // a collinear set keeps a visible minor axis

    // Exact: grow by the largest normalised radius, so every point is inside or on it.
    double kmax = 1.0;
    for (const auto &r : R) {
        const double x = r[0] - ccx, y = r[1] - ccy;
        const double p1 = (x * ca + y * sa) / A, p2 = (-x * sa + y * ca) / B;
        kmax = std::max(kmax, std::sqrt(p1 * p1 + p2 * p2));
    }
    A *= kmax;
    B *= kmax;

    // ── Back to the data's own units ────────────────────────────────────────────────
    // The conditioned ellipse's "covariance" C = U·diag(A², B²)·Uᵀ maps to D·C·D with
    // D = diag(sx, sy); the data ellipse's semi-axes are the square roots of ITS
    // eigenvalues and its major axis is the leading eigenvector. With sx == sy this is the
    // identity on the angle and a plain scale on the axes, which is the mock's case.
    const double A2 = A * A, B2 = B * B;
    const double c11 = (A2 * ca * ca + B2 * sa * sa) * sx * sx;
    const double c22 = (A2 * sa * sa + B2 * ca * ca) * sy * sy;
    const double c12 = (A2 - B2) * ca * sa * sx * sy;
    const double dtr = c11 + c22, ddt = c11 * c22 - c12 * c12;
    const double ddisc = std::sqrt(std::max(0.0, dtr * dtr / 4.0 - ddt));
    const double e1 = std::max(0.0, dtr / 2.0 + ddisc), e2 = std::max(0.0, dtr / 2.0 - ddisc);
    double theta = 0.5 * std::atan2(2.0 * c12, c11 - c22) * 180.0 / M_PI;
    if (theta <= -90.0) theta += 180.0;
    if (theta > 90.0)   theta -= 180.0;

    out.has      = true;
    out.cx       = mx + ccx * sx;
    out.cy       = my + ccy * sy;
    out.a        = std::sqrt(e1);
    out.b        = std::sqrt(e2);
    out.angleDeg = theta;
    return out;
}

// Whether (x, y) lies inside or on `e`, to a relative slack — the test's enclosure check,
// and the one a caller would use to ask "is this shot inside its club's outline".
inline bool lmEllipseContains(const LmEllipse &e, double x, double y, double slack = 1e-9)
{
    if (!e.has || !(e.a > 0.0) || !(e.b > 0.0))
        return false;
    const double th = e.angleDeg * M_PI / 180.0, c = std::cos(th), s = std::sin(th);
    const double dx = x - e.cx, dy = y - e.cy;
    const double p1 = (dx * c + dy * s) / e.a, p2 = (-dx * s + dy * c) / e.b;
    return p1 * p1 + p2 * p2 <= 1.0 + slack;
}

} // namespace pinpoint::analysis
