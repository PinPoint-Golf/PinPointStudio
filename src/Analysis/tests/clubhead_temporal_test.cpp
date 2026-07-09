// Standalone tests for the clubhead Stage-2 temporal half
// (src/Analysis/clubhead_track — HeadKf1D + runHeadTemporal). Pure vectors, no
// frames: gate/coast/trim/segment/tier/determinism logic in isolation. The
// per-frame measurement is the separate clubhead_measure_test.
//
//   cmake --build build/analyzer-tests --target clubhead_temporal_test
//   ctest --test-dir build/analyzer-tests -R clubhead_temporal --output-on-failure

#include "../clubhead_track.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// A default all-frames-valid input: θ=0, no measurements, huge ray edge (no off),
// no ball, s1=meas, no flips, dt = 1/100 s.
static HeadTemporalInput baseInput(int nf)
{
    HeadTemporalInput in;
    in.z.assign(nf, kNaN);
    in.zconf.assign(nf, 0.0);
    in.thetaDeg.assign(nf, 0.0);
    in.s1IsMeas.assign(nf, 1);
    in.flipSuspect.assign(nf, 0);
    in.rEdge.assign(nf, 1e6);
    in.lPx = -1.0;
    in.dt = 0.01;
    return in;
}
static void meas(HeadTemporalInput &in, int f, double z, double conf)
{
    in.z[f] = z; in.zconf[f] = conf;
}

int main()
{
    const ClubheadConfig cfg;

    // ── HeadKf1D: 3σ innovation gate ─────────────────────────────────────────
    std::printf("=== HeadKf1D 3σ gate ===\n");
    {
        HeadKf1D k1(0.01, cfg); k1.init(100.0);
        check(k1.step(true, 120.0, 8.0), "small innovation accepted");
        HeadKf1D k2(0.01, cfg); k2.init(100.0);
        check(!k2.step(true, 500.0, 8.0), "large innovation rejected by the gate");
        HeadKf1D k3(0.01, cfg); k3.init(100.0);
        check(!k3.step(false, 0.0, 8.0), "no measurement ⇒ not accepted (coast)");
    }

    // ── HeadKf1D: trimTail + rts sanity ──────────────────────────────────────
    std::printf("=== HeadKf1D trim + rts ===\n");
    {
        HeadKf1D k(0.01, cfg); k.init(100.0);
        for (int i = 0; i < 6; ++i) k.step(true, 100.0, 8.0);
        const std::size_t n0 = k.size();
        k.trimTail(2);
        check(k.size() == n0 - 2, "trimTail drops the last n steps");
        std::vector<double> rs, vs; k.rts(rs, vs);
        check(rs.size() == k.size() && !rs.empty(), "rts spans the (trimmed) history");
        bool flat = true; for (double r : rs) if (!near(r, 100.0, 2.0)) flat = false;
        check(flat, "constant measurements ⇒ smoothed r ≈ constant");
    }

    // ── confirmed run reaches the meas tier ──────────────────────────────────
    std::printf("=== runHeadTemporal: confirmed meas run ===\n");
    {
        HeadTemporalInput in = baseInput(16);
        for (int f = 0; f < 16; ++f) meas(in, f, 100.0, 0.8);
        const auto out = runHeadTemporal(in);
        check(out.size() == 16, "one result per frame");
        int nMeas = 0; for (auto &r : out) if (r.tier == HeadTier::Meas) ++nMeas;
        check(nMeas >= 10, "most frames of a strong run reach meas tier");
        check(out[8].tier == HeadTier::Meas && near(out[8].rOut, 100.0, 5.0), "mid-run frame is meas at r≈100");
        check(std::isfinite(out[8].sigmaR), "meas frame carries a posterior σ");
    }

    // ── posterior-σ tier: a low-conf frame inside a converged run is meas ─────
    std::printf("=== runHeadTemporal: posterior-σ tier ===\n");
    {
        HeadTemporalInput in = baseInput(24);
        for (int f = 0; f < 24; ++f) meas(in, f, 200.0, 0.9);
        meas(in, 12, 200.0, 0.40);   // instantaneous conf dips below 0.5 but ≥ CONF_MEAS_MIN
        const auto out = runHeadTemporal(in);
        check(out[12].tier == HeadTier::Meas, "converged low-conf frame promoted via posterior σ");
        check(out[12].sigmaR <= cfg.sigMeasMax, "its posterior σ_r is label-grade (≤ SIG_MEAS_MAX)");
        // a lone low-conf frame (no run) must NOT be meas
        HeadTemporalInput solo = baseInput(24);
        meas(solo, 12, 200.0, 0.40);
        const auto os = runHeadTemporal(solo);
        check(os[12].tier != HeadTier::Meas, "lone low-conf frame is not blessed by persistence");
    }

    // ── stage-1-pred / flip-suspect frames never reach meas ──────────────────
    std::printf("=== runHeadTemporal: stage-1 pred never meas ===\n");
    {
        HeadTemporalInput in = baseInput(14);
        for (int f = 0; f < 14; ++f) meas(in, f, 100.0, 0.8);
        in.s1IsMeas[6] = 0;      // stage-1 ray is a kinematic guess here
        in.flipSuspect[9] = 1;   // 180° flip suspected here
        const auto out = runHeadTemporal(in);
        check(out[6].tier != HeadTier::Meas, "stage-1 pred frame stays pred (radial can't beat the ray)");
        check(out[9].tier != HeadTier::Meas, "flip-suspect frame refused meas blessing");
        check(out[7].tier == HeadTier::Meas, "neighbouring genuine frame still meas");
    }

    // ── off tier: ray exits before offFactor·L_hat ───────────────────────────
    std::printf("=== runHeadTemporal: off tier ===\n");
    {
        HeadTemporalInput in = baseInput(16);
        for (int f = 0; f < 16; ++f) meas(in, f, 300.0, 0.9);
        in.rEdge[8] = 200.0;     // ray exits at 200 < 0.8·300 = 240
        const auto out = runHeadTemporal(in);
        check(out[8].tier == HeadTier::Off, "head expected outside the frame ⇒ off");
        check(std::isnan(out[8].rOut), "off tier emits no radial position");
    }

    // ── RTS never crosses a segment break (stage-1 θ jump) ───────────────────
    std::printf("=== runHeadTemporal: RTS respects segment breaks ===\n");
    {
        HeadTemporalInput in = baseInput(20);
        for (int f = 0; f < 10; ++f)  { meas(in, f, 100.0, 0.8); in.thetaDeg[f] = 10.0; }
        for (int f = 10; f < 20; ++f) { meas(in, f, 300.0, 0.8); in.thetaDeg[f] = 40.0; }  // +30°/frame jump at f10
        const auto out = runHeadTemporal(in);
        check(std::isfinite(out[9].rOut) && near(out[9].rOut, 100.0, 25.0),
              "segment A end stays at ~100 (not pulled toward B)");
        check(std::isfinite(out[10].rOut) && near(out[10].rOut, 300.0, 25.0),
              "segment B start at ~300 (independent smoothing)");
        check(std::abs(out[10].rOut - out[9].rOut) > 120.0, "the 200-px jump proves two segments");
    }

    // ── coast budget switches at |ṙ| > FAST_RDOT ─────────────────────────────
    std::printf("=== runHeadTemporal: speed-aware coast budget ===\n");
    {
        // gap of 8 dropout frames [10..17] between two measurement runs.
        auto build = [&](double step, double resume0) {
            HeadTemporalInput in = baseInput(30);
            for (int f = 0; f <= 9; ++f)   meas(in, f, step * f, 0.8);          // establish ṙ
            for (int f = 18; f <= 27; ++f) meas(in, f, resume0 + step * (f - 18), 0.8);
            return in;
        };
        // fast: ṙ ≈ 1500 px/s (> 800) ⇒ budget 4 < gap 8 ⇒ segment breaks, gap NOT smoothed
        const auto fast = runHeadTemporal(build(/*step=*/15.0, /*resume0=*/180.0));
        check(std::isnan(fast[13].sigmaR), "fast motion: 8-frame gap exceeds the fast budget ⇒ no smoothed r");
        // slow: ṙ ≈ 100 px/s (< 800) ⇒ budget 12 > gap 8 ⇒ segment bridges the gap
        const auto slow = runHeadTemporal(build(/*step=*/1.0, /*resume0=*/18.0));
        check(std::isfinite(slow[13].sigmaR), "slow motion: the slow budget bridges the gap ⇒ smoothed r");
    }

    // ── coasted tails are trimmed before RTS ─────────────────────────────────
    std::printf("=== runHeadTemporal: coasted-tail trim ===\n");
    {
        HeadTemporalInput in = baseInput(30);
        for (int f = 0; f <= 9; ++f) meas(in, f, double(f), 0.8);   // slow, then a long dropout 10..29
        const auto out = runHeadTemporal(in);
        check(std::isfinite(out[9].sigmaR), "last accepted frame keeps a smoothed σ");
        check(std::isnan(out[15].sigmaR) && std::isnan(out[25].sigmaR),
              "the coasted tail is trimmed (no fabricated smoothed r)");
    }

    // ── determinism + degenerate inputs ──────────────────────────────────────
    std::printf("=== runHeadTemporal: determinism + degenerate ===\n");
    {
        HeadTemporalInput in = baseInput(20);
        for (int f = 0; f < 20; ++f) meas(in, f, 120.0 + 3.0 * f, 0.7);
        const auto a = runHeadTemporal(in);
        const auto b = runHeadTemporal(in);
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same = (a[i].tier == b[i].tier) && (a[i].headConf == b[i].headConf)
                && ((std::isnan(a[i].rOut) && std::isnan(b[i].rOut)) || a[i].rOut == b[i].rOut)
                && ((std::isnan(a[i].sigmaR) && std::isnan(b[i].sigmaR)) || a[i].sigmaR == b[i].sigmaR);
        check(same, "byte-identical across two identical runs");

        check(runHeadTemporal(baseInput(0)).empty(), "empty input ⇒ empty output");
        HeadTemporalInput allNan = baseInput(5);
        for (int f = 0; f < 5; ++f) allNan.thetaDeg[f] = kNaN;   // no valid θ anywhere
        const auto none = runHeadTemporal(allNan);
        check(none.size() == 5, "all-NaN θ ⇒ result per frame, no crash");
        bool noMeas = true; for (auto &r : none) if (r.tier == HeadTier::Meas) noMeas = false;
        check(noMeas, "no participating frame ⇒ nothing reaches meas");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
