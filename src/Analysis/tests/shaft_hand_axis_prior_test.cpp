// Standalone test for the WB4 hand-axis θ prior DP unary (src/Analysis/
// shaft_track_assembly.cpp addHandAxisPrior) + its "shaft.handAxisPrior.*"
// override plumbing. Pure math over an emission cost row — no frames, no ridge
// evidence. Own main()/check() macros.
//
//   cmake --build build/analyzer-tests --target shaft_hand_axis_prior_test
//   ctest --test-dir build/analyzer-tests -R shaft_hand_axis_prior --output-on-failure

#include "../shaft_track_assembly.h"

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
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static std::vector<float> gridDegrees(int NS)
{
    std::vector<float> g(NS);
    for (int k = 0; k < NS; ++k) g[k] = float(k);   // 1° grid
    return g;
}

int main()
{
    std::printf("shaft_hand_axis_prior_test\n");
    constexpr int NS = 360;
    const std::vector<float> gridDeg = gridDegrees(NS);
    const std::vector<float> zeros(NS, 0.f);

    HandAxisPriorConfig cfg;   // defaults: enabled=false, weight=6, confMin=0.3, maxDeg=35

    // ── OFF ⇒ bit-identical (every guarded off-case) ────────────────────────────
    {
        std::vector<float> em = zeros;
        addHandAxisPrior(em, gridDeg, 90.0, 0.9, cfg);         // enabled=false
        check(em == zeros, "disabled ⇒ zero contribution (bit-identical)");

        cfg.enabled = true;
        addHandAxisPrior(em, gridDeg, 90.0, 0.10, cfg);        // conf < confMin
        check(em == zeros, "conf < confMin ⇒ zero contribution");

        addHandAxisPrior(em, gridDeg, std::numeric_limits<double>::quiet_NaN(), 0.9, cfg);
        check(em == zeros, "NaN handAxisDeg ⇒ zero contribution");
    }

    // ── ON ⇒ penalise only states beyond maxDeg from the axis ───────────────────
    {
        cfg.enabled = true; cfg.weight = 6.0; cfg.confMin = 0.30; cfg.maxDeg = 35.0;
        std::vector<float> em = zeros;
        addHandAxisPrior(em, gridDeg, 90.0, 0.5, cfg);
        const float pen = float(cfg.weight * 0.5);            // 3.0
        check(near(em[90], 0.0, 1e-9),  "on-axis state (90°) unpenalised");
        check(near(em[90 + 35], 0.0, 1e-9), "state at exactly maxDeg unpenalised");
        check(near(em[90 + 60], pen, 1e-6), "state 60° off-axis penalised by weight×conf");
        check(near(em[90 - 90], pen, 1e-6), "state 90° off-axis penalised");
    }

    // ── wrap: axis near 360° still measures the short arc ────────────────────────
    {
        cfg.enabled = true; cfg.weight = 6.0; cfg.confMin = 0.30; cfg.maxDeg = 35.0;
        std::vector<float> em = zeros;
        addHandAxisPrior(em, gridDeg, 350.0, 1.0, cfg);
        check(near(em[10], 0.0, 1e-9), "wrap: state 10° (20° from 350°) unpenalised");
        check(near(em[60], 6.0, 1e-6), "wrap: state 60° (70° from 350°) penalised");
    }

    // ── fromOverrides plumbing for the shaft.handAxisPrior.* keys ────────────────
    {
        QVariantMap ov;
        ov["shaft.handAxisPrior.enabled"] = true;
        ov["shaft.handAxisPrior.weight"]  = 9.5;
        ov["shaft.handAxisPrior.confMin"] = 0.42;
        ov["shaft.handAxisPrior.maxDeg"]  = 22.0;
        const ShaftV3Config c = ShaftV3Config::fromOverrides(ov);
        check(c.handAxisPrior.enabled, "override: enabled");
        check(near(c.handAxisPrior.weight, 9.5, 1e-9), "override: weight");
        check(near(c.handAxisPrior.confMin, 0.42, 1e-9), "override: confMin");
        check(near(c.handAxisPrior.maxDeg, 22.0, 1e-9), "override: maxDeg");

        const ShaftV3Config d = ShaftV3Config::fromOverrides(QVariantMap{});
        check(!d.handAxisPrior.enabled, "default: disabled (dark)");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
