// Standalone tests for the Stage A true-onset segmentation
// (swing_span_bounding_plan.md §4 / §7.1): the bs0 walk-back (A1 speed
// hysteresis + A2 φ witness), the impact-anchored clamp (A3), the swLow<=0
// legacy path, and the estimateSwingSpanUs Stage B helper. Synthetic grip
// tracks with a directly-controlled speed profile (speed == |Δgy|), so the
// expected onset behaviour is hand-reasoned rather than golden-fit.
//
//   cmake --build build/analyzer-tests --target swing_onset_test
//   ctest --test-dir build/analyzer-tests -R swing_onset --output-on-failure

#include "../shaft_track_assembly.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

// Grip-track builder. Segments write a per-frame speed magnitude v[] and a
// vertical direction dir[] (−1 up, +1 down); gy is the integral so that the
// smoothed grip speed segmentPhases sees is exactly the profile written here
// (gx is held constant, so speed = |Δgy| = v). tUs is a fixed-fps grid.
struct Builder {
    int nf;
    double fps;
    std::vector<double> v;
    std::vector<int>    dir;
    explicit Builder(int n, double f = 150.0) : nf(n), fps(f), v(n, 0.0), dir(n, 0) {}
    void constSeg(int f0, int f1, double mag, int d) { for (int f = f0; f < f1; ++f) { v[f] = mag; dir[f] = d; } }
    void ramp(int f0, int f1, double m0, double m1, int d)
    {
        for (int f = f0; f < f1; ++f) {
            const double t = (f1 > f0) ? double(f - f0) / double(f1 - f0) : 0.0;
            v[f] = m0 + (m1 - m0) * t; dir[f] = d;
        }
    }
    // single isolated jitter spike (killed by the median5 stage)
    void spike(int f, double mag, int d) { if (f >= 0 && f < nf) { v[f] = mag; dir[f] = d; } }
    void build(std::vector<double> &gx, std::vector<double> &gy, double x0 = 200.0, double y0 = 300.0) const
    {
        gx.assign(nf, x0); gy.assign(nf, y0);
        for (int f = 1; f < nf; ++f) gy[f] = gy[f - 1] + dir[f] * v[f];
    }
    std::vector<int64_t> times() const
    {
        std::vector<int64_t> t(nf);
        for (int f = 0; f < nf; ++f) t[f] = int64_t(std::llround(double(f) * 1e6 / fps));
        return t;
    }
};

// Canonical two-run swing: address hold, takeaway ramp, backswing (up), top hold
// (the quiet gap that splits the two runs), downswing (down through address
// height), finish hold. down-displacement > up-displacement so the hands-only
// impact derivation finds a grip-return frame.
static Builder makeSwing()
{
    Builder b(260);
    b.ramp(60, 78, 0.0, 12.0, -1);    // takeaway ramp up
    b.constSeg(78, 110, 12.0, -1);    // backswing up
    b.constSeg(110, 130, 0.0, 0);     // top hold (run gap)
    b.ramp(130, 140, 0.0, 14.0, +1);  // downswing accel
    b.constSeg(140, 172, 14.0, +1);   // downswing down (returns through address)
    b.ramp(172, 178, 14.0, 0.0, +1);
    return b;
}

int main()
{
    // ── A1 clean ramp: bs0 walks back to the sub-swLow takeaway boundary ──────
    std::printf("=== clean ramp (A1) ===\n");
    {
        const ShaftV3Config cfg;                 // swLow = 1.5 (default)
        ShaftV3Config leg = cfg; leg.swLow = 0.0;
        std::vector<double> gx, gy; makeSwing().build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel pm  = segmentPhases(gx, gy, nf, 150.0, -1, cfg);
        const PhaseModel lm  = segmentPhases(gx, gy, nf, 150.0, -1, leg);

        check(pm.bs0 < lm.bs0, "onset walked back earlier than the high-threshold bs0");
        check(pm.bs0 > 0, "onset did not run to the clip start");
        // walk-back boundary invariant: onset is the first sub-swLow frame, and
        // the very next frame is at/above swLow (the walk-back stopped here).
        check(pm.spdSmoothed[pm.bs0] < cfg.swLow, "smoothed speed at onset is below swLow");
        check(pm.spdSmoothed[pm.bs0 + 1] >= cfg.swLow, "smoothed speed just after onset is >= swLow");
        // only the address/backswing boundary moved; the other landmarks match.
        check(pm.top == lm.top && pm.impact == lm.impact && pm.fin0 == lm.fin0,
              "top/impact/fin0 unchanged by the walk-back");
        // the recovered zone: a frame that was Addr under the high threshold is
        // now Backswing.
        check(lm.phase[lm.bs0 - 1] == SwingPhase::Addr, "legacy: frame before bs0 is Addr");
        check(pm.phase[lm.bs0 - 1] == SwingPhase::Backswing, "Stage A: same frame is now Backswing");
    }

    // ── A1 noisy hold: single-frame jitter does not perturb the onset ─────────
    std::printf("=== noisy hold (A1 robustness) ===\n");
    {
        const ShaftV3Config cfg;
        std::vector<double> gxC, gyC; makeSwing().build(gxC, gyC);
        Builder nb = makeSwing();
        for (int f = 10; f < 55; f += 9) nb.spike(f, 5.0, (f / 9) % 2 ? +1 : -1);   // isolated 5 px/f jitter in the hold
        std::vector<double> gxN, gyN; nb.build(gxN, gyN);
        const int nf = int(gxC.size());
        const PhaseModel clean = segmentPhases(gxC, gyC, nf, 150.0, -1, cfg);
        const PhaseModel noisy = segmentPhases(gxN, gyN, nf, 150.0, -1, cfg);

        check(std::abs(noisy.bs0 - clean.bs0) <= 2, "onset within 2 frames of the clean-hold onset");
        check(noisy.bs0 > 0, "jitter did not drag the onset to the clip start");
        check(noisy.spdSmoothed[noisy.bs0] < cfg.swLow, "onset still lands below swLow");
    }

    // ── A1 waggle with a quiet gap: the gap stops the walk-back ───────────────
    std::printf("=== waggle with quiet gap ===\n");
    {
        const ShaftV3Config cfg;
        Builder b(260);
        for (int f = 30; f < 55; ++f) b.v[f] = 5.0, b.dir[f] = ((f / 5) % 2 ? +1 : -1);   // waggle (sub-swSpd)
        // quiet gap [55,75): v=0
        b.ramp(75, 92, 0.0, 12.0, -1);    // real takeaway
        b.constSeg(92, 122, 12.0, -1);    // backswing
        b.constSeg(122, 140, 0.0, 0);     // top
        b.ramp(140, 150, 0.0, 14.0, +1);
        b.constSeg(150, 184, 14.0, +1);   // downswing
        b.ramp(184, 190, 14.0, 0.0, +1);
        std::vector<double> gx, gy; b.build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg);

        check(pm.bs0 >= 60, "onset is past the waggle+gap, at the real takeaway");
        check(pm.bs0 < 92, "onset precedes the high-speed backswing");
        check(pm.spdSmoothed[pm.bs0] < cfg.swLow, "onset below swLow (stopped in the quiet gap/ramp foot)");
    }

    // ── A1 waggle running into the takeaway: no gap ⇒ walk-back reaches back ──
    std::printf("=== waggle into takeaway (no gap) ===\n");
    {
        const ShaftV3Config cfg;
        Builder b(260);
        // still [0,30), waggle [30,60) 5 px/f, then RAMP UP from 5 (no dip to 0)
        for (int f = 30; f < 60; ++f) b.v[f] = 5.0, b.dir[f] = ((f / 5) % 2 ? +1 : -1);
        b.ramp(60, 75, 5.0, 12.0, -1);    // takeaway continues from the waggle
        b.constSeg(75, 110, 12.0, -1);    // backswing
        b.constSeg(110, 128, 0.0, 0);     // top
        b.ramp(128, 138, 0.0, 14.0, +1);
        b.constSeg(138, 172, 14.0, +1);   // downswing
        b.ramp(172, 178, 14.0, 0.0, +1);
        std::vector<double> gx, gy; b.build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg);

        // no quiet frame between waggle and takeaway ⇒ the walk-back reaches the
        // waggle onset (the plan accepts the waggle as part of the motion).
        check(pm.bs0 <= 33, "onset reaches back to the waggle start (no quiet gap)");
        check(pm.bs0 > 0, "still stops at the pre-waggle stillness");
    }

    // ── degenerate: no run at all ⇒ bs0=0 path unchanged ─────────────────────
    std::printf("=== degenerate no-run ===\n");
    {
        const ShaftV3Config cfg;
        Builder b(200);                    // grip completely still
        std::vector<double> gx, gy; b.build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg);
        check(pm.bs0 == 0 && pm.fin0 == nf - 1, "whole-clip address: bs0=0, fin0=nf-1");
        bool allAddr = true; for (SwingPhase p : pm.phase) if (p != SwingPhase::Addr) allAddr = false;
        check(allAddr, "every frame labelled Addr");
        const auto tUs = b.times();
        const SwingSpanEstimate est = estimateSwingSpanUs(gx, gy, tUs, 150.0, -1, cfg);
        check(!est.ok, "estimateSwingSpanUs reports ok=false on the degenerate path");
    }

    // ── A3 clamp, far edge: onset earlier than impact−bsMax ⇒ pin to loEdge ───
    std::printf("=== clamp violation (far edge / onset too early) ===\n");
    {
        ShaftV3Config cfg;
        cfg.bsMaxBeforeImpactUs = 666667;   // 100 frames @ 150 fps
        cfg.bsMinBeforeImpactUs = 400000;   //  60 frames
        Builder b(260);
        for (int f = 30; f < 60; ++f) b.v[f] = 5.0, b.dir[f] = ((f / 5) % 2 ? +1 : -1);   // waggle into takeaway
        b.ramp(60, 75, 5.0, 12.0, -1);
        b.constSeg(75, 110, 12.0, -1);
        b.constSeg(110, 128, 0.0, 0);
        b.ramp(128, 138, 0.0, 14.0, +1);
        b.constSeg(138, 172, 14.0, +1);
        b.ramp(172, 178, 14.0, 0.0, +1);
        std::vector<double> gx, gy; b.build(gx, gy);
        const int nf = int(gx.size());
        const int impactFrame = 165;        // in the downswing, after top
        const PhaseModel free = segmentPhases(gx, gy, nf, 150.0, -1, cfg);           // clamp off
        const PhaseModel clmp = segmentPhases(gx, gy, nf, 150.0, impactFrame, cfg);  // clamp on
        const int framesMax = int(std::lround(double(cfg.bsMaxBeforeImpactUs) * 1e-6 * 150.0));
        const int loEdge = clmp.impact - framesMax;
        check(free.bs0 < loEdge, "unclamped onset really is earlier than impact−bsMax (test non-vacuous)");
        check(clmp.bs0 == loEdge, "clamped onset pinned to the far edge impact−bsMax");
    }

    // ── A3 clamp, near edge: backswing too short ⇒ pin to hiEdge ──────────────
    std::printf("=== clamp violation (near edge / backswing too short) ===\n");
    {
        ShaftV3Config cfg;
        cfg.bsMinBeforeImpactUs = 333333;   // 50 frames @ 150 fps
        cfg.bsMaxBeforeImpactUs = 533333;   // 80 frames
        Builder b(160);                     // compressed swing
        b.ramp(55, 68, 0.0, 12.0, -1);
        b.constSeg(68, 80, 12.0, -1);
        b.constSeg(80, 88, 0.0, 0);
        b.ramp(88, 96, 0.0, 12.0, +1);
        b.constSeg(96, 118, 12.0, +1);
        b.ramp(118, 124, 12.0, 0.0, +1);
        std::vector<double> gx, gy; b.build(gx, gy);
        const int nf = int(gx.size());
        const int impactFrame = 100;
        const PhaseModel free = segmentPhases(gx, gy, nf, 150.0, -1, cfg);
        const PhaseModel clmp = segmentPhases(gx, gy, nf, 150.0, impactFrame, cfg);
        const int framesMin = int(std::lround(double(cfg.bsMinBeforeImpactUs) * 1e-6 * 150.0));
        const int hiEdge = clmp.impact - framesMin;
        check(free.bs0 > hiEdge, "unclamped onset really is later than impact−bsMin (test non-vacuous)");
        check(clmp.bs0 == hiEdge, "clamped onset pinned to the near edge impact−bsMin");
    }

    // ── A2 φ witness: forearm rotates before the grip ⇒ earlier onset ─────────
    std::printf("=== φ-onset witness (A2) ===\n");
    {
        const ShaftV3Config cfg;
        std::vector<double> gx, gy; makeSwing().build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel spdOnly = segmentPhases(gx, gy, nf, 150.0, -1, cfg, nullptr);
        // φ constant through the hold, then rotates 1°/frame from f=48 (before
        // the grip's f=60 takeaway) — |Δφ|=1 > phiOnsetDegPerFrame.
        std::vector<double> phi(nf, 10.0);
        for (int f = 48; f < 110; ++f) phi[f] = phi[f - 1] + 1.0;
        const PhaseModel withPhi = segmentPhases(gx, gy, nf, 150.0, -1, cfg, &phi);
        check(withPhi.bs0 < spdOnly.bs0, "φ witness pulls the onset earlier than the speed-only onset");
        check(withPhi.bs0 >= 42 && withPhi.bs0 <= 50, "φ onset lands near where the forearm started rotating (f≈48)");
        // witness disabled (threshold 0) reproduces the speed-only onset.
        ShaftV3Config off = cfg; off.phiOnsetDegPerFrame = 0.0;
        const PhaseModel disabled = segmentPhases(gx, gy, nf, 150.0, -1, off, &phi);
        check(disabled.bs0 == spdOnly.bs0, "phiOnsetDegPerFrame=0 ⇒ φ witness off");
    }

    // ── swLow=0 legacy equivalence on a ramp track ────────────────────────────
    std::printf("=== swLow=0 legacy equivalence ===\n");
    {
        ShaftV3Config nw;                   // swLow = 1.5
        ShaftV3Config lg = nw; lg.swLow = 0.0;
        std::vector<double> gx, gy; makeSwing().build(gx, gy);
        const int nf = int(gx.size());
        const PhaseModel pn = segmentPhases(gx, gy, nf, 150.0, -1, nw);
        const PhaseModel pl = segmentPhases(gx, gy, nf, 150.0, -1, lg);

        check(pl.bs0 > pn.bs0, "legacy bs0 is later (no walk-back)");
        // legacy bs0 sits exactly at the high-threshold run start.
        check(pl.spdSmoothed[pl.bs0] > nw.swSpd, "legacy bs0 is inside the >swSpd run");
        check(pl.bs0 == 0 || pl.spdSmoothed[pl.bs0 - 1] <= nw.swSpd, "legacy bs0 is the run's first frame");
        // spdSmoothed is computed before the onset block ⇒ identical either way.
        bool sameSpd = pl.spdSmoothed.size() == pn.spdSmoothed.size();
        for (size_t i = 0; sameSpd && i < pl.spdSmoothed.size(); ++i)
            if (pl.spdSmoothed[i] != pn.spdSmoothed[i]) sameSpd = false;
        check(sameSpd, "smoothed speed identical under swLow=0 and swLow=1.5");
        check(pl.top == pn.top && pl.impact == pn.impact && pl.fin0 == pn.fin0, "other landmarks identical");
        // every frame before legacy bs0 is Addr in the legacy labelling.
        bool legacyAddr = true; for (int f = 0; f < pl.bs0; ++f) if (pl.phase[f] != SwingPhase::Addr) legacyAddr = false;
        check(legacyAddr, "legacy labels all pre-bs0 frames Addr");
    }

    // ── estimateSwingSpanUs on a real swing ──────────────────────────────────
    std::printf("=== estimateSwingSpanUs ===\n");
    {
        const ShaftV3Config cfg;
        std::vector<double> gx, gy; makeSwing().build(gx, gy);
        const int nf = int(gx.size());
        const auto tUs = Builder(nf).times();
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg, nullptr);
        const SwingSpanEstimate est = estimateSwingSpanUs(gx, gy, tUs, 150.0, -1, cfg);
        check(est.ok, "ok=true on a detected swing");
        check(est.startUs == tUs[pm.bs0], "span start = onset timestamp");
        check(est.endUs == tUs[pm.fin0], "span end = finish timestamp");
        check(est.startUs < est.endUs, "start precedes end");
        // impactUs supplied ⇒ nearest-frame clamp still yields a valid span.
        const SwingSpanEstimate estImp = estimateSwingSpanUs(gx, gy, tUs, 150.0, tUs[150], cfg);
        check(estImp.ok && estImp.startUs < estImp.endUs, "ok with an impact timestamp");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
