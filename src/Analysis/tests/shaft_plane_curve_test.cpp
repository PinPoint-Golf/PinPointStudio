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

// Standalone tests for the swing plane curve (src/Club/shaft_plane_curve.h).
//   1. The view-plane angle IS the plane through the target line: a shaft anywhere on a 55° plane
//      through the down-the-line view ray reads 55°, and a mirrored image angle reads the same.
//   2. The fit is smooth through what it should be smooth through: a known curve under 1° noise
//      comes back within a degree; a 120 ms hole is bridged close to the truth and marked bridged;
//      a run of frames 35° off (a mirrored band's signature) is rejected rather than followed;
//      frames below the conditioning floor do not enter; too few frames is no curve.
// Pure std, no fixture.

#include "../../Club/shaft_plane_curve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pinpoint::analysis;
using namespace pinpoint::analysis::planecurve;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static double truth(int64_t t) { return 55.0 + 10.0 * std::sin(double(t) / 1.0e6 * 3.0); }

// A deterministic ±1 pseudo-noise, so the test never flakes.
static double noise(int i) { return ((i * 7919) % 199) / 99.0 - 1.0; }

int main()
{
    std::printf("shaft_plane_curve_test\n");

    // ── 1. the view-plane angle ─────────────────────────────────────────────────────────────
    {
        const fusion::Camera cd = fusion::dtlCamera(0.0, 0.0, 0.0);
        const double inc = 55.0 * kPi / 180.0;
        // In-plane basis: the view ray (the target direction) and the plane's up-slope direction.
        const fusion::Vec3 a = cd.d, m { 0.0, -std::cos(inc), std::sin(inc) };
        bool all = true, mirror = true;
        for (double phi : { 20.0, 45.0, 70.0, 100.0, 135.0, 160.0 }) {
            const double p = phi * kPi / 180.0;
            const fusion::Vec3 u = a * std::cos(p) + m * std::sin(p);
            double th = 0, rho = 0;
            fusion::project(cd, u, th, rho);
            if (std::fabs(viewPlaneInclDeg(cd, th) - 55.0) > 1e-6) all = false;
            if (std::fabs(viewPlaneInclDeg(cd, kPi - th) - 55.0) > 1e-6) mirror = false;
        }
        check(all, "a shaft anywhere on a 55-degree plane through the target line reads 55 degrees");
        check(mirror, "and a mirrored image angle reads the same plane");
        check(viewPlaneInclSigmaDeg(cd, 1.0, 0.5) > 0.0, "the plane angle carries a sigma from the image angle's");
    }

    // ── 2. the fit ──────────────────────────────────────────────────────────────────────────
    const int64_t from = 0, to = 1000000;
    std::vector<Obs> obs;
    int i = 0;
    for (int64_t t = 0; t <= to; t += 4000, ++i) {
        if (t >= 400000 && t < 520000) continue;                       // a 120 ms hole
        Obs o { t, truth(t) + noise(i), 1.0, 1.0 };
        if (t >= 700000 && t < 740000) o.inclDeg = truth(t) + 35.0;   // ten frames, a mirrored band
        obs.push_back(o);
    }
    for (int64_t t = 600000; t < 640000; t += 4000)                    // badly conditioned garbage
        obs.push_back({ t, 5.0, 1.0, 0.1 });
    std::sort(obs.begin(), obs.end(), [](const Obs &x, const Obs &y) { return x.t_us < y.t_us; });

    const Curve c = fitCurve(obs, from, to, Config{});
    check(c.fitted, "the curve fits");
    double worstClean = 0, worstHole = 0, worstBand = 0, worstGarbage = 0;
    bool holeBridged = true, cleanSupported = true, sigmaOk = true;
    for (size_t k = 0; k < c.t_us.size(); ++k) {
        const int64_t t = c.t_us[k];
        const double e = std::fabs(c.inclDeg[k] - truth(t));
        if (!(c.sigmaDeg[k] > 0.0) || !std::isfinite(c.sigmaDeg[k])) sigmaOk = false;
        if (t >= 430000 && t < 490000) { worstHole = std::max(worstHole, e); if (c.supported[k]) holeBridged = false; }
        else if (t >= 700000 && t < 740000) worstBand = std::max(worstBand, e);
        else if (t >= 600000 && t < 640000) worstGarbage = std::max(worstGarbage, e);
        else if (t >= 40000 && t <= 960000 && !(t >= 380000 && t < 540000)) {
            worstClean = std::max(worstClean, e);
            if (!c.supported[k]) cleanSupported = false;
        }
    }
    std::printf("    worst error: clean %.2f, hole %.2f, mirrored run %.2f, garbage %.2f; rejected %d\n",
                worstClean, worstHole, worstBand, worstGarbage, c.nRejected);
    check(worstClean < 1.0, "the clean stretches come back within a degree of the truth");
    check(cleanSupported, "and are marked as measured");
    check(worstHole < 2.0, "a 120 ms hole is bridged within two degrees of the truth");
    check(holeBridged, "and is marked bridged");
    check(worstBand < 2.0, "a run of ten frames 35 degrees off is not followed");
    check(c.nRejected >= 10, "and those frames are rejected");
    check(worstGarbage < 1.0, "frames below the conditioning floor do not enter");
    check(sigmaOk, "every point carries a finite, positive sigma");

    std::vector<Obs> few(obs.begin(), obs.begin() + 5);
    check(!fitCurve(few, from, to, Config{}).fitted, "five frames is no curve");

    // ── 3. the hand plane ───────────────────────────────────────────────────────────────────
    {
        const fusion::Camera cd = fusion::dtlCamera(0.0, 0.0, 0.0);
        // At address the shaft runs from the ball to the grip, so the hand plane is the shaft's.
        const double bx = 520, by = 880, gx = 344, gy = 638;
        const double shaftTheta = std::atan2(by - gy, bx - gx);   // grip → head, the tracker's sense
        check(std::fabs(handPlaneInclDeg(cd, gx, gy, bx, by) - viewPlaneInclDeg(cd, shaftTheta)) < 1e-9,
              "at address the hand plane is the shaft's plane");
        // Hands that move out toward the ball at the same height steepen it.
        check(handPlaneInclDeg(cd, gx + 30, gy, bx, by) > handPlaneInclDeg(cd, gx, gy, bx, by),
              "hands moved out toward the ball line steepen the hand plane");
        check(handPlaneInclSigmaDeg(cd, gx, gy, bx, by, 4.0) > 0.0, "the hand plane carries a sigma");

        // Steepening: hands rise 300 px; going up the plane reads 60°, coming down 66°.
        std::vector<HandFrame> fr;
        const int64_t tk = 100000, top = 900000, imp = 1200000;
        for (int64_t t = 0; t < tk; t += 5000) fr.push_back({ t, 600.0, 55.0 });
        for (int64_t t = tk; t < top; t += 5000)
            fr.push_back({ t, 600.0 - 300.0 * double(t - tk) / double(top - tk), 60.0 });
        for (int64_t t = top; t <= imp; t += 5000)
            fr.push_back({ t, 300.0 + 300.0 * double(t - top) / double(imp - top), 66.0 });
        const Steepening st = handPlaneSteepening(fr, tk, top, imp);
        std::printf("    steepening %.2f ± %.2f (%d up, %d down)\n", st.deg, st.sigmaDeg, st.nUp, st.nDown);
        check(st.ok && std::fabs(st.deg - 6.0) < 1e-9, "the hands coming down 6 degrees steeper read +6");
        check(st.nUp > 3 && st.nDown > 3, "and both halves are read over 40-70 % of the rise only");
        std::vector<HandFrame> flat(fr);
        for (HandFrame &f : flat) f.gripY = 600.0;
        check(!handPlaneSteepening(flat, tk, top, imp).ok, "hands that never rise are not read");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
