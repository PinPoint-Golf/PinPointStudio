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

// Standalone test for resynthesizeLayerC (shaft_track_assembly.h): the synth tier a
// REUSED track gets, rebuilt from its own samples and anchors.
//
// What is pinned: the rebuilt synth follows the samples' grip (the hands) through a bend
// the anchors alone cannot express; it fills strictly between the anchors on the
// configured cadence; every tick is a rigid line of the interpolated length; the θ path
// runs from the first anchor's θ to the last's; and a track with fewer than two anchors
// or fewer than two samples gets an EMPTY synth rather than an invented one.

#include "../shaft_track_assembly.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr double kPi = 3.14159265358979323846;

// A backswing-shaped track at 120 fps: the hands travel right-to-left along a bent path
// while the shaft rotates 0.3 → 2.1 rad. Three anchors (P1, P2, P4) sit ON samples.
static ShaftTrack2D makeTrack()
{
    ShaftTrack2D t;
    t.valid = true; t.frameWidth = 1280; t.frameHeight = 1024;
    const int64_t t0 = 1'000'000, t1 = 1'700'000;
    for (int64_t ts = t0; ts <= t1; ts += 8'333) {
        const double u = double(ts - t0) / double(t1 - t0);
        ShaftSample2D s;
        s.t_us     = ts;
        s.gripPx   = QPointF(700.0 - 250.0 * u, 650.0 - 90.0 * std::sin(u * kPi));   // the bend
        s.thetaRad = 0.3 + 1.8 * u;
        s.visibleLenPx = 300.0;
        s.headPx   = QPointF(s.gripPx.x() + 300.0 * std::cos(s.thetaRad), s.gripPx.y() + 300.0 * std::sin(s.thetaRad));
        s.conf     = 0.7f;
        s.flags    = ShaftMeasured;
        t.samples.push_back(s);
    }
    const auto anchorAt = [&](int p, size_t i) {
        ShaftPosition a; const ShaftSample2D &s = t.samples[i];
        a.p = p; a.t_us = s.t_us; a.gripPx = s.gripPx; a.headPx = s.headPx; a.thetaRad = s.thetaRad;
        a.lenPx = 300.0; a.conf = 0.8f; return a;
    };
    t.positions = { anchorAt(1, 0), anchorAt(2, 30), anchorAt(4, t.samples.size() - 1) };
    return t;
}

int main()
{
    std::printf("shaft_resynth_test\n");
    ShaftV3Config cfg;
    cfg.synth.enabled = true;

    {
        ShaftTrack2D t = makeTrack();
        t.synth.push_back(t.samples[3]);   // stale synth from a previous producer — must be replaced
        resynthesizeLayerC(t, cfg);
        check(!t.synth.empty(), "a track with samples and ≥ 2 anchors gets a synth tier");
        const int64_t a0 = t.positions.front().t_us, aN = t.positions.back().t_us;
        bool inside = true, rigid = true, onHands = true, flagged = true, ascending = true;
        double worst = 0.0;
        for (size_t i = 0; i < t.synth.size(); ++i) {
            const ShaftSample2D &s = t.synth[i];
            if (s.t_us <= a0 || s.t_us >= aN) inside = false;
            if (i && s.t_us <= t.synth[i - 1].t_us) ascending = false;
            if (!(s.flags & ShaftSynthesized)) flagged = false;
            if (!near(std::hypot(s.headPx.x() - s.gripPx.x(), s.headPx.y() - s.gripPx.y()), s.visibleLenPx, 1e-6)) rigid = false;
            const double u = double(s.t_us - 1'000'000) / 700'000.0;
            const double ex = 700.0 - 250.0 * u, ey = 650.0 - 90.0 * std::sin(u * kPi);
            const double d = std::hypot(s.gripPx.x() - ex, s.gripPx.y() - ey);
            worst = std::max(worst, d);
            if (d > 1.5) onHands = false;
        }
        check(inside, "synth ticks lie strictly between the first and last anchor");
        check(ascending, "…ascending in time");
        check(flagged, "…every tick flagged ShaftSynthesized");
        check(rigid, "…every tick a rigid line of its length");
        std::printf("    worst grip distance from the hand path: %.2f px over %zu ticks\n", worst, t.synth.size());
        check(onHands, "the rebuilt grip follows the samples' hand path through the bend (≤ 1.5 px)");
        // At 240 Hz over 700 ms minus the anchor ticks: ~165 ticks.
        check(t.synth.size() > 120 && t.synth.size() < 200, "cadence is the configured 240 Hz grid");
        // θ runs from the first anchor's to the last's, monotone (the path is monotone).
        bool mono = true;
        for (size_t i = 1; i < t.synth.size(); ++i) if (t.synth[i].thetaRad < t.synth[i - 1].thetaRad - 1e-9) mono = false;
        check(mono && t.synth.front().thetaRad > 0.3 && t.synth.back().thetaRad < 2.1, "θ interpolates monotonically between the anchors");
    }
    {
        ShaftTrack2D one = makeTrack();
        one.positions.resize(1);
        resynthesizeLayerC(one, cfg);
        check(one.synth.empty(), "one anchor ⇒ no synth (nothing to bridge)");
        ShaftTrack2D few = makeTrack();
        few.samples.resize(1);
        resynthesizeLayerC(few, cfg);
        check(few.synth.empty(), "one sample ⇒ no synth");
        ShaftTrack2D off = makeTrack();
        ShaftV3Config dark = cfg; dark.synth.enabled = false;
        resynthesizeLayerC(off, dark);
        check(off.synth.empty(), "synth disabled ⇒ empty, and the stale tier is cleared");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
