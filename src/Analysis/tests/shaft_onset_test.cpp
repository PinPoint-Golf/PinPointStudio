// Standalone tests for the Stage-A "no-return" veto (W1) and the additive vision
// Takeaway event (W2) in shaft_track_assembly.cpp.
//
// The veto post-processes the A1/A2 onset walk-back so a club bob that DEPARTS
// the address point and RETURNS (a fidget) no longer drags the onset back
// through the whole fidget. It runs on the pure segmentPhases() over synthetic
// grip/φ tracks with a directly-controlled speed profile, so the OFF defect and
// ON fix are hand-reasoned, not golden-fit.
//
// The defect is driven by the φ WITNESS (A2): a bob rotates the club about a
// settled wrist, so |Δφ| stays above threshold and A2 walks the onset back
// through the fidget even though the GRIP has repeatedly returned to address.
// The veto recovers the last grip-static, club-back-at-address settle. (This is
// why the fixtures supply φ — a grip-only bob makes A1 stop at the settle
// already, so the veto is correctly inert; that case is asserted too.)
//
// NOTE: the veto gates atRest on the GRIP being static (spdS < swLow) — the
// literal reading of the estimand "Address = the last STATIC point". The plan's
// literal atRest also required dphiS ≤ phiOnsetDegPerFrame, but that makes the
// veto a provable no-op (it could only re-stop where A2 already stopped); the φ
// condition lives in inBox's angle term (the club is back near the address φ).
//
//   cmake --build build/analyzer-tests --target shaft_onset_test
//   ctest --test-dir build/analyzer-tests -R shaft_onset --output-on-failure

#include "../shaft_track_assembly.h"

#include <QVariantMap>
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

// Velocity-integral track builder: per-frame speed magnitude v[] + vertical
// direction dir[] (gx held constant, so smoothed grip speed == the profile) and
// a per-frame φ rate (deg/frame, signed). gy/φ are the running integrals.
struct Track {
    int nf;
    double fps = 150.0;
    std::vector<double> v, pr;
    std::vector<int>    dir;
    explicit Track(int n) : nf(n), v(n, 0.0), pr(n, 0.0), dir(n, 0) {}
    void seg(int a, int b, double sp, int d, double rate)
    {
        for (int k = a; k < b && k < nf; ++k) { v[k] = sp; dir[k] = d; pr[k] = rate; }
    }
    void ramp(int a, int b, double s0, double s1, int d, double rate)
    {
        for (int k = a; k < b && k < nf; ++k) {
            const double t = (b > a) ? double(k - a) / double(b - a) : 0.0;
            v[k] = s0 + (s1 - s0) * t; dir[k] = d; pr[k] = rate;
        }
    }
    void build(std::vector<double> &gx, std::vector<double> &gy, std::vector<double> &phi,
               double x0 = 200.0, double y0 = 300.0, double phi0 = 10.0) const
    {
        gx.assign(nf, x0); gy.assign(nf, y0); phi.assign(nf, phi0);
        for (int k = 1; k < nf; ++k) { gy[k] = gy[k - 1] + dir[k] * v[k]; phi[k] = phi[k - 1] + pr[k]; }
    }
    std::vector<int64_t> times() const
    {
        std::vector<int64_t> t(nf);
        for (int f = 0; f < nf; ++f) t[f] = int64_t(std::llround(double(f) * 1e6 / fps));
        return t;
    }
};

// A fidget swing: two grip bob loops (grip departs +12 px and returns, ~20 f
// each) with a short settle at address, then a real one-piece takeaway →
// backswing → top → downswing. φ swings away from and back through the address
// angle with each bob (continuous |Δφ| ⇒ A2 over-reaches), then rotates through
// the swing. `settle` = still frames at the end of each bob (address settle).
static Track makeFidgetSwing(int &tkOut, int settle = 4)
{
    Track t(320);
    int f = 40;
    for (int loop = 0; loop < 2; ++loop) {
        t.seg(f, f + 8, 1.5, -1, +2.25);           // grip up, club rotates away (+18)
        t.seg(f + 8, f + 16, 1.5, +1, -2.25);       // grip down to address, club returns (−18)
        t.seg(f + 16, f + 16 + settle, 0.0, 0, +0.5); // settle: grip still, φ slow drift (never rests)
        f += 16 + settle;
    }
    tkOut = f;
    t.ramp(f, f + 18, 0.0, 12.0, -1, -1.5);         // one-piece takeaway
    t.seg(f + 18, f + 52, 12.0, -1, -2.0);          // backswing
    t.seg(f + 52, f + 70, 0.0, 0, 0.0);             // top hold (run gap)
    t.ramp(f + 70, f + 80, 0.0, 14.0, +1, +2.0);    // downswing accel
    t.seg(f + 80, f + 114, 14.0, +1, +2.0);         // downswing (returns through address height)
    t.ramp(f + 114, f + 120, 14.0, 0.0, +1, +1.0);
    return t;
}

int main()
{
    // ── (a) grip-bob fidget: OFF walks through, ON lands at the takeaway ───────
    std::printf("=== (a) grip-bob fidget veto ===\n");
    {
        int tk = 0;
        Track tr = makeFidgetSwing(tk);            // tk == true takeaway (80)
        std::vector<double> gx, gy, phi; tr.build(gx, gy, phi);
        for (int k = 0; k < 40; ++k) gy[k] = 300.0 + ((k % 2) ? 1.0 : 0.0);   // 1 px hold noise
        const int nf = tr.nf;

        const ShaftV3Config off;                    // veto dark (onsetReturnBoxPx = 0)
        const PhaseModel pOff = segmentPhases(gx, gy, nf, 150.0, -1, off, &phi);
        // DEFECT: the φ witness drags the onset back through the whole fidget
        // (fidget spans [40, tk)); the onset lands before the takeaway.
        check(pOff.bs0 < 45, "OFF: onset walked into/through the fidget (defect documented)");
        check(pOff.bs0 < tk - 30, "OFF: onset is far (>30 f) before the true takeaway");

        ShaftV3Config on = off; on.onsetReturnBoxPx = 7.0;
        const PhaseModel pOn = segmentPhases(gx, gy, nf, 150.0, -1, on, &phi);
        check(pOn.bs0 > pOff.bs0 + 30, "ON: veto moved the onset > 30 f later");
        check(std::abs(pOn.bs0 - tk) <= 5, "ON: onset lands within 5 f of the true takeaway (last settle)");
        // only later, never earlier (the veto post-processes min(A1,A2))
        check(pOn.bs0 >= pOff.bs0, "ON: veto only moves the onset later");
        // stable across the sweep window (box 6–8 px)
        for (double box : {6.0, 8.0}) {
            ShaftV3Config c = off; c.onsetReturnBoxPx = box;
            const PhaseModel p = segmentPhases(gx, gy, nf, 150.0, -1, c, &phi);
            check(std::abs(p.bs0 - tk) <= 5, "ON: onset stable near the takeaway across box 6–8");
        }
        // grip-only (no φ): A1 does NOT over-reach ⇒ the veto is correctly inert
        const PhaseModel gOff = segmentPhases(gx, gy, nf, 150.0, -1, off, nullptr);
        ShaftV3Config gon = off; gon.onsetReturnBoxPx = 7.0;
        const PhaseModel gOn = segmentPhases(gx, gy, nf, 150.0, -1, gon, nullptr);
        check(std::abs(gOff.bs0 - tk) <= 5, "no-φ: A1 alone already lands at the takeaway");
        check(gOn.bs0 == gOff.bs0, "no-φ: veto inert (nothing to recover)");
    }

    // ── (b) φ-bob variant: grip near-still, φ oscillates ±3° ───────────────────
    std::printf("=== (b) φ-bob variant ===\n");
    {
        const int nf = 300;
        Track tr(nf);
        int f = 40;
        for (int loop = 0; loop < 3; ++loop) {      // φ triangle ±3° (period 16)
            tr.seg(f, f + 8, 0.0, 0, +0.75);
            tr.seg(f + 8, f + 16, 0.0, 0, -0.75);
            f += 16;
        }
        const int tk = f;                           // 88
        tr.ramp(tk, tk + 18, 0.0, 12.0, -1, -1.5);
        tr.seg(tk + 18, tk + 50, 12.0, -1, -2.0);
        tr.seg(tk + 50, tk + 66, 0.0, 0, 0.0);
        tr.ramp(tk + 66, tk + 76, 0.0, 14.0, +1, +2.0);
        tr.seg(tk + 76, tk + 108, 14.0, +1, +2.0);
        tr.ramp(tk + 108, tk + 114, 14.0, 0.0, +1, +1.0);
        std::vector<double> gx, gy, phi; tr.build(gx, gy, phi);
        for (int k = 0; k < tk; ++k) gy[k] = 300.0 + ((k % 2) ? 0.6 : 0.0);   // grip near-still

        const ShaftV3Config off;
        const PhaseModel pOff = segmentPhases(gx, gy, nf, 150.0, -1, off, &phi);
        check(pOff.bs0 < tk - 30, "OFF: φ witness over-reaches through the φ fidget");
        ShaftV3Config on = off; on.onsetReturnBoxPx = 7.0;
        const PhaseModel pOn = segmentPhases(gx, gy, nf, 150.0, -1, on, &phi);
        check(std::abs(pOn.bs0 - tk) <= 4, "ON: φ-angle-return veto lands onset at the takeaway");
        check(pOn.bs0 > pOff.bs0, "ON: onset moved later");
    }

    // ── (c) slow one-piece creep: the veto must NOT clip a genuine takeaway ────
    std::printf("=== (c) slow one-piece creep ===\n");
    {
        const int nf = 300;
        Track tr(nf);
        const int creep = 50;
        tr.seg(creep, 110, 0.8, -1, -0.5);          // ~0.8 px/f monotone departure, never returns
        tr.ramp(110, 128, 0.8, 12.0, -1, -1.5);
        tr.seg(128, 158, 12.0, -1, -2.0);
        tr.seg(158, 174, 0.0, 0, 0.0);
        tr.ramp(174, 184, 0.0, 14.0, +1, +2.0);
        tr.seg(184, 216, 14.0, +1, +2.0);
        tr.ramp(216, 222, 14.0, 0.0, +1, +1.0);
        std::vector<double> gx, gy, phi; tr.build(gx, gy, phi);

        const ShaftV3Config off;
        const PhaseModel pOff = segmentPhases(gx, gy, nf, 150.0, -1, off, &phi);
        ShaftV3Config on = off; on.onsetReturnBoxPx = 7.0;
        const PhaseModel pOn = segmentPhases(gx, gy, nf, 150.0, -1, on, &phi);
        // a monotone departure never re-enters the box ⇒ no meaningful forward move
        check(pOn.bs0 <= pOff.bs0 + 2, "ON: creep onset ≤ OFF + 2 (no clipping)");
        check(pOn.bs0 <= creep + 4, "ON: onset stays at the creep foot, not pushed into the swing");
    }

    // ── (d) A3 impact clamp wins over the veto ────────────────────────────────
    std::printf("=== (d) A3 clamp wins ===\n");
    {
        int tk = 0;
        Track tr = makeFidgetSwing(tk);
        std::vector<double> gx, gy, phi; tr.build(gx, gy, phi);
        for (int k = 0; k < 40; ++k) gy[k] = 300.0 + ((k % 2) ? 1.0 : 0.0);
        const int nf = tr.nf;
        const int impact = 190;

        // veto alone (no impact) parks the onset at the takeaway (~tk).
        ShaftV3Config free = ShaftV3Config{}; free.onsetReturnBoxPx = 7.0;
        const PhaseModel pFree = segmentPhases(gx, gy, nf, 150.0, -1, free, &phi);
        check(std::abs(pFree.bs0 - tk) <= 5, "veto-only onset sits at the takeaway (test non-vacuous)");

        // now clamp near-edge tight so hiEdge = impact − bsMin lands BEFORE the
        // veto result — the clamp must override it.
        ShaftV3Config on = free;
        on.bsMinBeforeImpactUs = int64_t(double(impact - 60) / 150.0 * 1e6);  // hiEdge ≈ frame 60
        on.bsMaxBeforeImpactUs = int64_t(double(impact -  0) / 150.0 * 1e6);
        const PhaseModel pClamp = segmentPhases(gx, gy, nf, 150.0, impact, on, &phi);
        const int framesMin = int(std::lround(double(on.bsMinBeforeImpactUs) * 1e-6 * 150.0));
        const int hiEdge = std::max(0, impact - framesMin);
        check(pFree.bs0 > hiEdge, "veto result really violates the near-edge clamp (non-vacuous)");
        check(pClamp.bs0 == hiEdge, "A3 clamp pins the onset to hiEdge (clamp wins over the veto)");
    }

    // ── fromOverrides: the four keys reach ShaftV3Config ──────────────────────
    std::printf("=== fromOverrides key plumbing ===\n");
    {
        const ShaftV3Config def = ShaftV3Config::fromOverrides(QVariantMap{});
        check(def.onsetReturnBoxPx == 0.0 && def.onsetReturnPhiDeg == 1.5
              && def.onsetReturnStillFrames == 3 && def.emitTakeaway == false,
              "empty map → frozen dark defaults (veto off, no Takeaway)");
        QVariantMap ov;
        ov["shaft.onsetReturnBoxPx"] = 7.0;
        ov["shaft.onsetReturnPhiDeg"] = 2.5;
        ov["shaft.onsetReturnStillFrames"] = 5;
        ov["shaft.emitTakeaway"] = true;
        const ShaftV3Config c = ShaftV3Config::fromOverrides(ov);
        check(c.onsetReturnBoxPx == 7.0 && c.onsetReturnPhiDeg == 2.5
              && c.onsetReturnStillFrames == 5 && c.emitTakeaway == true,
              "shaft.onsetReturn* / shaft.emitTakeaway overrides reach the config");
    }

    // ── W2: the additive vision Takeaway event ────────────────────────────────
    std::printf("=== W2 Takeaway event (phasesToSegmentation) ===\n");
    {
        PhaseModel pm;
        pm.bs0 = 40; pm.top = 80; pm.impact = 110; pm.fin0 = 150;
        const int nf = 200;
        std::vector<int64_t> tUs(nf);
        for (int i = 0; i < nf; ++i) tUs[i] = int64_t(i) * 6700;   // ~149 fps

        auto hasPhase = [](const Segmentation &s, Phase p) { return s.eventFor(p) != nullptr; };
        auto monotone = [](const Segmentation &s) {
            for (size_t i = 1; i < s.events.size(); ++i)
                if (s.events[i].t_us < s.events[i - 1].t_us) return false;
            return true;
        };

        // OFF (default): {Address, Top, Impact, Finish}, no Takeaway.
        const Segmentation segOff = phasesToSegmentation(pm, tUs, 0.5f, -1, /*emitTakeaway=*/false);
        check(segOff.events.size() == 4, "OFF: four-event ladder");
        check(hasPhase(segOff, Phase::Address) && hasPhase(segOff, Phase::Top)
              && hasPhase(segOff, Phase::Impact) && hasPhase(segOff, Phase::Finish),
              "OFF: {Address, Top, Impact, Finish}");
        check(!hasPhase(segOff, Phase::Takeaway), "OFF: no Takeaway event");
        check(monotone(segOff), "OFF: events time-ordered");

        // ON: adds Takeaway at bs0.
        const Segmentation segOn = phasesToSegmentation(pm, tUs, 0.5f, -1, /*emitTakeaway=*/true);
        check(segOn.events.size() == 5, "ON: five-event ladder");
        check(hasPhase(segOn, Phase::Address) && hasPhase(segOn, Phase::Takeaway)
              && hasPhase(segOn, Phase::Top) && hasPhase(segOn, Phase::Impact)
              && hasPhase(segOn, Phase::Finish),
              "ON: {Address, Takeaway, Top, Impact, Finish}");
        check(monotone(segOn), "ON: events time-ordered");
        check(segOn.eventFor(Phase::Takeaway)->t_us == tUs[pm.bs0], "ON: Takeaway t_us == bs0 time");
        check(segOn.eventFor(Phase::Address)->t_us <= segOn.eventFor(Phase::Takeaway)->t_us,
              "ON: Address ≤ Takeaway");
        // Address defaults to bs0 (no hold end) ⇒ same time; stable_sort keeps
        // Address before Takeaway.
        check(segOn.events[0].phase == Phase::Address && segOn.events[1].phase == Phase::Takeaway,
              "ON: Address ordered before Takeaway at equal time");

        // With a located hold end (addressFrame < bs0), Address strictly precedes Takeaway.
        const Segmentation segHold = phasesToSegmentation(pm, tUs, 0.5f, /*addressFrame=*/20, true);
        check(segHold.eventFor(Phase::Address)->t_us == tUs[20]
              && segHold.eventFor(Phase::Takeaway)->t_us == tUs[40],
              "ON+hold: Address at hold end, Takeaway at bs0");
        check(segHold.eventFor(Phase::Address)->t_us < segHold.eventFor(Phase::Takeaway)->t_us,
              "ON+hold: Address strictly before Takeaway");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
