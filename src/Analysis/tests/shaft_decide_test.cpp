// Standalone tests for the shaft v3.0-r1 deciding half
// (src/Analysis/shaft_track_assembly — phase model, φ smoothing, C2 geometry,
// per-frame DP emission, banded Viterbi, ψ-isotonic reconcile). Synthetic
// inputs with hand-computable expectations. Full NUMERIC parity vs the Python
// exemplar is the separate shaft_parity_test (Phase 5).
//
//   cmake --build build/analyzer-tests --target shaft_decide_test
//   ctest --test-dir build/analyzer-tests -R shaft_decide --output-on-failure

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
static bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }
static constexpr double kPi = 3.14159265358979323846;

int main()
{
    const ShaftV3Config cfg;

    // ── exact weighted PAVA ──────────────────────────────────────────────────
    std::printf("=== pava ===\n");
    {
        // y=[1,3,2,4] increasing: 3>2 pool → [1,2.5,2.5,4]
        const std::vector<double> y = {1, 3, 2, 4}, w = {1, 1, 1, 1};
        const std::vector<double> x = pava(y, w, true);
        check(near(x[0], 1, 1e-9) && near(x[1], 2.5, 1e-9) && near(x[2], 2.5, 1e-9) && near(x[3], 4, 1e-9),
              "increasing pool of a single violation");
        // already-monotone increasing input is unchanged
        const std::vector<double> mono = {0, 1, 2, 3};
        const std::vector<double> xm = pava(mono, w, true);
        check(near(xm[3], 3, 1e-9) && near(xm[0], 0, 1e-9), "monotone input unchanged");
        // decreasing direction
        const std::vector<double> yd = {4, 2, 3, 1};
        const std::vector<double> xd = pava(yd, w, false);
        check(near(xd[1], 2.5, 1e-9) && near(xd[2], 2.5, 1e-9), "decreasing pool");
    }

    // ── robust isotonic down-weights an outlier ──────────────────────────────
    std::printf("=== robustIsotonic ===\n");
    {
        std::vector<double> y = {0, 1, 2, 3, 4, 5, 6, 7};
        std::vector<double> w(8, 1.0);
        y[4] = 40.0;   // single wild outlier
        const std::vector<double> x = robustIsotonic(y, w, true, cfg);
        // the fit stays near the clean line at the flanks, not dragged to 40
        check(x[3] < 6 && x[5] < 10, "outlier down-weighted (flanks stay low)");
        check(x[7] >= x[0], "still monotone increasing");
    }

    // ── banded Viterbi picks the decreasing well path (downswing) ────────────
    std::printf("=== viterbiDP ===\n");
    {
        const int NS = 16, nf = 3;
        std::vector<std::vector<float>> emis(nf, std::vector<float>(NS, 10.f));
        emis[0][8] = 0.f; emis[1][7] = 0.f; emis[2][6] = 0.f;   // decreasing bins
        const std::vector<SwingPhase> phase(nf, SwingPhase::Downswing);
        const DPResult dp = viterbiDP(emis, phase, cfg);
        check(dp.thstar.size() == 3 && dp.thstar[0] == 8 && dp.thstar[1] == 7 && dp.thstar[2] == 6,
              "path follows the decreasing wells 8→7→6");
    }

    // ── frameEmission: band well LAST + arm veto ─────────────────────────────
    std::printf("=== frameEmission ===\n");
    {
        const int NS = 360;
        std::vector<float> gridRad(NS), gridDeg(NS);
        for (int k = 0; k < NS; ++k) { gridDeg[k] = float(k); gridRad[k] = float(k * kPi / 180.0); }
        std::vector<float> evMax(NS, 0.2f), rawNorm(NS, 0.f);
        evMax[100] = 0.9f;
        BandMatch bm; bm.ok = true; bm.thetaDeg = 100.f; bm.n = 5;
        std::vector<float> em, inside;
        frameEmission(em, inside, evMax, rawNorm, bm, /*phiSDeg=*/0.0, SwingPhase::Downswing,
                      /*chir=*/1, /*gx=*/300, /*gy=*/300, /*poly=*/nullptr, cv::Mat(),
                      gridRad, gridDeg, cfg);
        check(near(em[100], -cfg.wBand, 1e-4), "band bin = -wBand (applied last)");
        check(near(em[180], cfg.wE2 * 0.8 + cfg.wArm, 1e-3), "arm-veto bin (φ+180) = wE2·(1-ev)+wArm");
        check(near(em[90], cfg.wE2 * 0.8, 1e-3), "neutral bin = wE2·(1-ev)");
    }

    // ── body geometry: half-plane inside/outside ─────────────────────────────
    std::printf("=== bodyPolys ===\n");
    {
        // a square torso 100..300 in x and y (one frame)
        std::vector<std::vector<cv::Point2d>> joints = {{{100, 100}, {300, 100}, {300, 300}, {100, 300}}};
        const std::vector<BodyPoly> polys = bodyPolys(joints);
        check(polys.size() == 1 && polys[0].n.size() == 4, "one 4-edge hull");
        auto insideFrac = [&](double px, double py) {
            double mx = -1e30;
            for (size_t e = 0; e < polys[0].n.size(); ++e)
                mx = std::max(mx, polys[0].n[e][0] * px + polys[0].n[e][1] * py - polys[0].d[e]);
            return mx;   // ≤ margin ⇒ inside
        };
        check(insideFrac(200, 200) <= cfg.bodyMargin, "centre inside");
        check(insideFrac(500, 200) > cfg.bodyMargin, "far-right point outside");
    }

    // ── segmentPhases on a synthetic swing ───────────────────────────────────
    std::printf("=== segmentPhases ===\n");
    {
        const int nf = 170;
        std::vector<double> gx(nf), gy(nf);
        double x = 200, y = 300;
        for (int f = 0; f < nf; ++f) {
            if (f >= 40 && f < 70)      { x -= 4; y -= 9; }   // backswing (speed ≈ 9.8 > swSpd)
            else if (f >= 77 && f < 109){ x += 3; y += 9; }   // downswing
            gx[f] = x; gy[f] = y;
        }
        const PhaseModel pm = segmentPhases(gx, gy, nf, 150.0, -1, cfg);
        check(pm.phase.size() == size_t(nf), "phase per frame");
        check(pm.phase.front() == SwingPhase::Addr, "starts at address");
        check(pm.phase.back() == SwingPhase::Finish, "ends at finish");
        check(pm.bs0 > 30 && pm.bs0 < 50, "takeaway near f40");
        check(pm.top > pm.bs0 && pm.impact > pm.top && pm.fin0 >= pm.impact, "landmarks ordered");
        bool hasImpact = false, hasBackswing = false;
        for (SwingPhase p : pm.phase) { if (p == SwingPhase::Impact) hasImpact = true; if (p == SwingPhase::Backswing) hasBackswing = true; }
        check(hasImpact && hasBackswing, "impact + backswing phases present");
    }

    // ── reconcilePsi: monotone ψ untouched; impact counterfeit reconstructed ─
    std::printf("=== reconcilePsi ===\n");
    {
        const int nf = 20;
        std::vector<double> theta(nf), phi(nf, 0.0), evAt(nf, 0.6);
        std::vector<SwingPhase> phase(nf, SwingPhase::Downswing);
        std::vector<char> bandOk(nf, 0);
        for (int f = 0; f < nf; ++f) theta[f] = 100.0 - 5.0 * f;   // strictly decreasing ψ (φ=0)
        const ReconResult a = reconcilePsi(theta, phi, phase, bandOk, evAt, /*top=*/1000, nf, cfg);
        bool noRecon = true; for (char r : a.recon) if (r) noRecon = false;
        check(noRecon, "monotone ψ ⇒ no reconstruction");
        check(near(a.thetaOut[10], theta[10], 1e-6), "θ preserved when monotone");

        std::vector<double> theta2 = theta;
        theta2[10] = 200.0;                     // wild impact-frame counterfeit
        std::vector<SwingPhase> phase2 = phase; phase2[10] = SwingPhase::Impact;
        const ReconResult b = reconcilePsi(theta2, phi, phase2, bandOk, evAt, 1000, nf, cfg);
        check(b.recon[10] == 1, "impact counterfeit flagged recon");
        check(std::abs(b.thetaOut[10] - 200.0) > 20.0, "θ pulled back toward the monotone rail");
    }

    // ── vision-only segmentation mapping ─────────────────────────────────────
    std::printf("=== phasesToSegmentation ===\n");
    {
        PhaseModel pm;
        pm.bs0 = 40; pm.top = 80; pm.impact = 110; pm.fin0 = 150;
        const int nf = 200;
        std::vector<int64_t> tUs(nf);
        for (int i = 0; i < nf; ++i) tUs[i] = int64_t(i) * 6700;   // ~149 fps
        const Segmentation seg = phasesToSegmentation(pm, tUs, 0.5f);
        check(seg.events.size() == 4, "four ladder events");
        check(seg.conf == 0.5f, "vision-grade conf");
        bool ordered = true;
        for (size_t i = 1; i < seg.events.size(); ++i) if (seg.events[i].t_us < seg.events[i - 1].t_us) ordered = false;
        check(ordered, "events time-ordered");
        check(seg.events[0].phase == Phase::Address && seg.events[0].t_us == tUs[40], "Address at bs0");
        check(seg.events[2].phase == Phase::Impact && seg.events[2].t_us == tUs[110], "Impact at impact frame");
        check(seg.swingStartUs >= tUs.front() && seg.swingStartUs < tUs[40], "swingStart padded + clamped");
        check(seg.swingEndUs <= tUs.back() && seg.swingEndUs > tUs[150], "swingEnd padded + clamped");
        // degenerate (conf 0) still returns bounds but no swing claim
        const Segmentation deg = phasesToSegmentation(pm, tUs, 0.0f);
        check(deg.conf == 0.0f, "conf 0 passthrough (no swing)");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
