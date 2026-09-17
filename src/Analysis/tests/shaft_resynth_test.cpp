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

    // ═══ The synth may not carry on where the club did not (pitch-shot follow-through) ═══
    //
    // A track whose measured samples STOP turning 150 ms after impact while a coasted tail
    // keeps rotating the model, with P7 measured, P8 measured, and a P10 built on the coast.
    std::printf("=== follow-through: the gate and the envelope ===\n");
    {
        ShaftTrack2D t;
        t.valid = true; t.frameWidth = 1280; t.frameHeight = 1024;
        const int64_t imp = 2'000'000;
        for (int64_t ts = imp - 200'000; ts <= imp + 600'000; ts += 8'333) {
            const double ms = double(ts - imp) * 1e-3;
            ShaftSample2D s;
            s.t_us = ts; s.gripPx = QPointF(600.0, 650.0); s.visibleLenPx = 300.0; s.conf = 0.6f;
            if (ms <= 150.0) {                    // measured: turning 1.2 rad/s through impact, then slowing to rest
                s.thetaRad = 1.5 + 1.2e-3 * std::min(ms, 100.0) + (ms > 100.0 ? 0.3e-3 * (ms - 100.0) : 0.0);
                s.flags = ShaftMeasured;
            } else {                               // coasted: the model keeps turning at 1.2 rad/s
                s.thetaRad = 1.635 + 1.2e-3 * (ms - 150.0);
                s.flags = uint16_t(ShaftCoasted | ShaftHeadProjected);
            }
            s.headPx = QPointF(s.gripPx.x() + 300.0 * std::cos(s.thetaRad), s.gripPx.y() + 300.0 * std::sin(s.thetaRad));
            t.samples.push_back(s);
        }
        const auto at = [&](int p, int64_t ts) {
            ShaftPosition a; size_t best = 0;
            for (size_t i = 0; i < t.samples.size(); ++i) if (std::llabs(t.samples[i].t_us - ts) < std::llabs(t.samples[best].t_us - ts)) best = i;
            const ShaftSample2D &s = t.samples[best];
            a.p = p; a.t_us = s.t_us; a.gripPx = s.gripPx; a.headPx = s.headPx; a.thetaRad = s.thetaRad; a.lenPx = 300.0; a.conf = 0.6f;
            return a;
        };
        t.positions = { at(6, imp - 100'000), at(7, imp), at(8, imp + 60'000), at(10, imp + 500'000) };
        resynthesizeLayerC(t, cfg);
        const int64_t p8 = t.positions[2].t_us, p10 = t.positions[3].t_us;
        check(t.positions[3].timing == TimingClass::Proxy && t.positions[2].timing == TimingClass::Measured,
              "anchor timing re-derived from the samples: P8 measured, P10 on the coast is Proxy");
        size_t before = 0, after = 0;
        for (const ShaftSample2D &s : t.synth) (s.t_us < p8 ? before : after)++;
        check(before > 0, "P6→P7→P8 are still bridged");
        check(after == 0, "P8→P10 is NOT bridged: the end anchor rests on no measurement");
        // Now make the P10 measured but the club STOPPED (samples flat after 150 ms): the
        // envelope keeps the synth from sweeping through the stopped club.
        ShaftTrack2D u = t;
        for (ShaftSample2D &s : u.samples) {
            const double ms = double(s.t_us - imp) * 1e-3;
            if (ms > 150.0) { s.thetaRad = 1.635 + 0.05e-3 * (ms - 150.0); s.flags = ShaftMeasured;
                              s.headPx = QPointF(s.gripPx.x() + 300.0 * std::cos(s.thetaRad), s.gripPx.y() + 300.0 * std::sin(s.thetaRad)); }
        }
        u.positions = { at(6, imp - 100'000), at(7, imp), at(8, imp + 60'000) };
        ShaftPosition fin = u.positions[2]; fin.p = 10; fin.t_us = imp + 500'000;
        fin.thetaRad = 1.635 + 0.05e-3 * 350.0 + 1.4;   // a finish anchor 80° past where the club sits — wrong
        fin.headPx = QPointF(fin.gripPx.x() + 300.0 * std::cos(fin.thetaRad), fin.gripPx.y() + 300.0 * std::sin(fin.thetaRad));
        u.positions.push_back(fin);
        resynthesizeLayerC(u, cfg);
        double worstDev = 0.0; size_t inBracket = 0;
        for (const ShaftSample2D &s : u.synth) {
            if (s.t_us <= p8 + 60'000 || s.t_us >= p10 - 60'000) continue;
            ++inBracket;
            const double ms = double(s.t_us - imp) * 1e-3;
            const double measured = 1.635 + 0.05e-3 * (ms - 150.0);
            worstDev = std::max(worstDev, std::fabs(s.thetaRad - measured));
        }
        std::printf("    worst synth deviation from the measured (stopped) club inside P8→P10: %.1f°\n", worstDev * 180.0 / kPi);
        check(inBracket > 10 && worstDev <= (cfg.synth.envelopeTolDeg + 1.0) * kPi / 180.0,
              "inside P8→P10 the synth stays within the tolerance of the measured club — it does not sweep on");
        ShaftV3Config noClamp = cfg; noClamp.synth.envelopeTolDeg = 0.0;
        ShaftTrack2D v = u; resynthesizeLayerC(v, noClamp);
        double worstFree = 0.0;
        for (const ShaftSample2D &s : v.synth) {
            if (s.t_us <= p8 + 60'000 || s.t_us >= p10 - 60'000) continue;
            const double ms = double(s.t_us - imp) * 1e-3;
            worstFree = std::max(worstFree, std::fabs(s.thetaRad - (1.635 + 0.05e-3 * (ms - 150.0))));
        }
        check(worstFree > 30.0 * kPi / 180.0, "…whereas the unclamped Hermite sweeps tens of degrees past it (the defect)");

        // Rule 3: a MEASURED finish anchor that implies an impossible rate past P8 (the tracker
        // captured the lead arm, head confidence and all) is not bridged either.
        ShaftTrack2D w = t;
        for (ShaftSample2D &s : w.samples) { s.flags = ShaftMeasured; }   // everything "measured"
        w.positions = { at(6, imp - 100'000), at(7, imp), at(8, imp + 60'000) };
        ShaftPosition arm = w.positions[2]; arm.p = 10; arm.t_us = imp + 140'000;
        arm.thetaRad = w.positions[2].thetaRad + 3.3;   // 190° in 80 ms
        arm.headPx = QPointF(arm.gripPx.x() + 300.0 * std::cos(arm.thetaRad), arm.gripPx.y() + 300.0 * std::sin(arm.thetaRad));
        w.positions.push_back(arm);
        resynthesizeLayerC(w, cfg);
        size_t pastP8 = 0;
        for (const ShaftSample2D &s : w.synth) if (s.t_us > w.positions[2].t_us) ++pastP8;
        check(pastP8 == 0, "a measured finish anchor 190° away in 80 ms is not bridged: past P8 the club only slows");
        ShaftV3Config noCap = cfg; noCap.synth.maxFollowThroughRateDps = 0.0; noCap.synth.envelopeTolDeg = 0.0;
        ShaftTrack2D w2 = w; resynthesizeLayerC(w2, noCap);
        size_t pastP8b = 0;
        for (const ShaftSample2D &s : w2.synth) if (s.t_us > w2.positions[2].t_us) ++pastP8b;
        check(pastP8b > 0, "…and with the cap off it is (the defect)");
    }

    // ═══ Follow-through plausibility: the samples themselves ══════════════════════════════
    //
    // Shot 13, 15 Sept: after P8 the tracker captured the lead arm with head confidence 0.84
    // and the samples said the shaft flipped 190° in 80 ms, then lay along the forearm. The
    // pass demotes those to coasts, so the anchors read Proxy and nothing bridges into them.
    std::printf("=== follow-through plausibility pass ===\n");
    {
        ShaftTrack2D t;
        t.valid = true; t.frameWidth = 1280; t.frameHeight = 1024;
        const int64_t imp = 2'000'000;
        std::vector<double> forearm;
        for (int64_t ts = imp - 200'000; ts <= imp + 400'000; ts += 8'333) {
            const double ms = double(ts - imp) * 1e-3;
            ShaftSample2D s;
            s.t_us = ts; s.gripPx = QPointF(600.0, 650.0); s.visibleLenPx = 300.0; s.conf = 0.6f; s.flags = ShaftMeasured;
            double th;
            // ≈ 1000 °/s (17.5 rad/s) into and through impact, then the flip: 190° in 80 ms,
            // then a shaft lying along the forearm.
            const double atP8 = 1.5 + 17.5e-3 * 110.0;
            if (ms <= 110.0)      th = 1.5 + 17.5e-3 * ms;
            else if (ms <= 190.0) th = atP8 + 3.3 * (ms - 110.0) / 80.0;
            else                  th = atP8 + 3.3;
            s.thetaRad = th;
            s.headPx = QPointF(s.gripPx.x() + 300.0 * std::cos(th), s.gripPx.y() + 300.0 * std::sin(th));
            t.samples.push_back(s);
            // Forearm (hands→elbow) sits 20° off the final shaft direction: the arm-lock case.
            forearm.push_back((1.5 + 17.5e-3 * 110.0 + 3.3 + 0.35) * 180.0 / kPi);
        }
        ShaftV3Config cfg2 = cfg;
        cfg2.followThrough.minShaftForearmDeg = 35.0;   // the forearm test is opt-in (finish wrap)
        const int demoted = demoteImplausibleFollowThrough(t, forearm, imp, cfg2);
        int flipDemoted = 0, flipN = 0, preTouched = 0, armDemoted = 0, armN = 0;
        for (const ShaftSample2D &s : t.samples) {
            const double ms = double(s.t_us - imp) * 1e-3;
            const bool dem = (s.flags & ShaftImplausible) != 0;
            if (ms <= 110.0) { if (dem) ++preTouched; }
            else if (ms <= 190.0) { ++flipN; if (dem) ++flipDemoted; }
            else { ++armN; if (dem) ++armDemoted; }
            if (dem) check((s.flags & ShaftCoasted) && (s.flags & ShaftHeadProjected) && !(s.flags & ShaftMeasured), "a demoted sample is a coast, projected, and no longer Measured");
        }
        std::printf("    demoted %d: flip %d/%d, arm %d/%d, before/at P8 %d\n", demoted, flipDemoted, flipN, armDemoted, armN, preTouched);
        check(preTouched == 0, "nothing before or at P8 is touched");
        check(flipDemoted == flipN, "every sample of the 190°-in-80 ms flip is demoted (rate above the swing's own peak)");
        check(armDemoted == armN, "every sample lying along the forearm is demoted (a wrist cannot hinge that far)");
        // Anchors located on the demoted stretch read Proxy after re-synthesis, and the
        // synth stops at the last plausible anchor.
        const auto at = [&](int p, int64_t ts) {
            ShaftPosition a; size_t best = 0;
            for (size_t i = 0; i < t.samples.size(); ++i) if (std::llabs(t.samples[i].t_us - ts) < std::llabs(t.samples[best].t_us - ts)) best = i;
            const ShaftSample2D &s = t.samples[best];
            a.p = p; a.t_us = s.t_us; a.gripPx = s.gripPx; a.headPx = s.headPx; a.thetaRad = s.thetaRad; a.lenPx = 300.0; a.conf = 0.6f;
            return a;
        };
        t.positions = { at(6, imp - 100'000), at(7, imp), at(8, imp + 100'000), at(10, imp + 300'000) };
        resynthesizeLayerC(t, cfg2);
        check(t.positions[3].timing == TimingClass::Proxy, "the finish anchor on the demoted stretch reads Proxy");
        size_t past = 0;
        for (const ShaftSample2D &s : t.synth) if (s.t_us > t.positions[2].t_us) ++past;
        check(past == 0, "…and nothing is bridged past P8");
        ShaftV3Config off = cfg; off.followThrough.enabled = false;
        ShaftTrack2D u = t; for (ShaftSample2D &s : u.samples) s.flags = ShaftMeasured;
        check(demoteImplausibleFollowThrough(u, forearm, imp, off) == 0, "disabled ⇒ nothing demoted");
        check(demoteImplausibleFollowThrough(u, forearm, -1, cfg2) == 0, "no impact ⇒ nothing demoted");
        // Default config: the forearm test is off, so the along-the-arm stretch (slow, 0.05 rad/s
        // against its neighbour, but 190° from the last plausible sample over 80+ ms) survives on
        // rate alone only where the rate says so.
        ShaftTrack2D v = t; for (ShaftSample2D &s : v.samples) s.flags = ShaftMeasured;
        const int defDem = demoteImplausibleFollowThrough(v, forearm, imp, cfg);
        int armLeft = 0;
        for (const ShaftSample2D &s : v.samples) if (double(s.t_us - imp) * 1e-3 > 190.0 && !(s.flags & ShaftImplausible)) ++armLeft;
        std::printf("    default config: demoted %d, along-the-arm samples left measured %d\n", defDem, armLeft);
        check(defDem >= flipN, "default config still demotes the whole flip on rate");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
