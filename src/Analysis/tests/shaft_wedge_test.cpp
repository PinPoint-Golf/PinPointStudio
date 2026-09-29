// Standalone tests for the R8-T1 blur-wedge plateau measure
// (src/Analysis/shaft_wedge.h — contiguous-run finder, energy-weighted
// circular centroid, absolute-threshold honesty, arm-smear veto). Pure std,
// synthetic score rows, no fixture.
//
//   cmake --build build/analyzer-tests --target shaft_wedge_test
//   ctest --test-dir build/analyzer-tests -R shaft_wedge --output-on-failure

#include "../shaft_wedge.h"

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
static double wrapd(double d)
{
    d = std::fmod(d + 180.0, 360.0);
    if (d < 0) d += 360.0;
    return d - 180.0;
}

// An envelope arc [center-half, center+half] at 1° steps, flat floor value.
static std::vector<float> arcDeg(double centerDeg, int halfBins)
{
    std::vector<float> deg;
    for (int d = -halfBins; d <= halfBins; ++d) {
        double a = std::fmod(centerDeg + d, 360.0);
        if (a < 0) a += 360.0;
        deg.push_back(float(a));
    }
    return deg;
}

int main()
{
    const WedgeConfig cfg;                       // defaults: threshScale 0.5, minSpanDeg 2
    const double kFloor = 20.0;                  // S1 evAbsFloor reference -> thresh 10
    const double kArmVeto = 12.0;                // cfg.armVetoDeg convention
    const double kPhiFar = 200.0;                // arm at 20° — far from every plateau below

    // ── symmetric plateau: centroid at the middle, width = span ─────────────
    std::printf("=== plateau centroid ===\n");
    {
        const std::vector<float> deg = arcDeg(90.0, 30);       // 60..120
        std::vector<float> raw(deg.size(), 5.f);
        for (size_t j = 0; j < deg.size(); ++j)
            if (std::abs(wrapd(double(deg[j]) - 90.0)) <= 2.0) raw[j] = 30.f;   // 88..92
        const WedgeCandidate c = measureWedge(raw, {}, deg, kPhiFar, kArmVeto, kFloor, cfg);
        check(c.ok, "symmetric plateau found");
        check(c.ok && std::abs(wrapd(c.centroidDeg - 90.0)) <= 1.5, "centroid within ±1.5° of 90");
        check(c.ok && std::abs(c.widthDeg - 4.0) <= 1e-9, "width = 4° (5 bins)");
    }

    // ── asymmetric energy pulls the centroid toward the heavy side ──────────
    {
        const std::vector<float> deg = arcDeg(90.0, 30);
        std::vector<float> raw(deg.size(), 0.f);
        for (size_t j = 0; j < deg.size(); ++j) {
            const double o = wrapd(double(deg[j]) - 90.0);
            if (o >= -2.0 && o <= 2.0) raw[j] = float(30.0 + 10.0 * o);   // 10..50 across 88..92
        }
        const WedgeCandidate c = measureWedge(raw, {}, deg, kPhiFar, kArmVeto, kFloor, cfg);
        check(c.ok && c.centroidDeg > 90.0 && c.centroidDeg < 92.0,
              "energy-weighted centroid shifts toward the brighter flank");
    }

    // ── honesty: flat row / no absolute floor / sub-span all yield nothing ──
    std::printf("=== honesty (never fabricate) ===\n");
    {
        const std::vector<float> deg = arcDeg(90.0, 30);
        const std::vector<float> flat(deg.size(), 5.f);        // everywhere below thresh
        check(!measureWedge(flat, {}, deg, kPhiFar, kArmVeto, kFloor, cfg).ok,
              "flat (drowned) row ⇒ no candidate");

        std::vector<float> raw(deg.size(), 5.f);
        for (size_t j = 25; j <= 35; ++j) raw[j] = 30.f;
        check(!measureWedge(raw, {}, deg, kPhiFar, kArmVeto, 0.0, cfg).ok,
              "absFloorRef <= 0 (no absolute anchor) ⇒ no candidate");

        std::vector<float> narrow(deg.size(), 5.f);
        narrow[30] = 30.f; narrow[31] = 30.f;                  // span 1° < minSpanDeg 2
        check(!measureWedge(narrow, {}, deg, kPhiFar, kArmVeto, kFloor, cfg).ok,
              "sub-minSpan plateau (line-like) ⇒ no candidate");
    }

    // ── arm-smear veto: a plateau at φ+180 is the forearm, not the club ─────
    std::printf("=== arm-smear veto ===\n");
    {
        const std::vector<float> deg = arcDeg(90.0, 30);
        std::vector<float> raw(deg.size(), 5.f);
        for (size_t j = 0; j < deg.size(); ++j)
            if (std::abs(wrapd(double(deg[j]) - 90.0)) <= 2.0) raw[j] = 30.f;
        // φ = 270 ⇒ arm smear direction = 90 — exactly the plateau
        check(!measureWedge(raw, {}, deg, 270.0, kArmVeto, kFloor, cfg).ok,
              "centroid within armVetoDeg of φ+180 ⇒ rejected");
        // the same plateau with the arm elsewhere is accepted
        check(measureWedge(raw, {}, deg, kPhiFar, kArmVeto, kFloor, cfg).ok,
              "same plateau, arm elsewhere ⇒ accepted");
    }

    // ── dif channel alone can carry the plateau (either-channel test) ───────
    std::printf("=== channels ===\n");
    {
        const std::vector<float> deg = arcDeg(90.0, 30);
        const std::vector<float> raw(deg.size(), 2.f);         // raw drowned
        std::vector<float> dif(deg.size(), 2.f);
        for (size_t j = 0; j < deg.size(); ++j)
            if (std::abs(wrapd(double(deg[j]) - 90.0)) <= 2.0) dif[j] = 25.f;
        const WedgeCandidate c = measureWedge(raw, dif, deg, kPhiFar, kArmVeto, kFloor, cfg);
        check(c.ok && std::abs(wrapd(c.centroidDeg - 90.0)) <= 1.5,
              "dif-only plateau found at the same centroid");
    }

    // ── two plateaus: the higher-energy run wins ────────────────────────────
    std::printf("=== best-run selection ===\n");
    {
        const std::vector<float> deg = arcDeg(90.0, 40);       // 50..130
        std::vector<float> raw(deg.size(), 0.f);
        for (size_t j = 0; j < deg.size(); ++j) {
            const double a = double(deg[j]);
            if (a >= 60 && a <= 64)  raw[j] = 15.f;            // weak fan
            if (a >= 110 && a <= 116) raw[j] = 40.f;           // strong fan
        }
        const WedgeCandidate c = measureWedge(raw, {}, deg, kPhiFar, kArmVeto, kFloor, cfg);
        check(c.ok && std::abs(wrapd(c.centroidDeg - 113.0)) <= 1.5,
              "higher-energy plateau wins the frame");
    }

    // ── wrap: an arc spanning 0° centres correctly ──────────────────────────
    std::printf("=== wrap ===\n");
    {
        const std::vector<float> deg = arcDeg(0.0, 20);        // 340..20
        std::vector<float> raw(deg.size(), 0.f);
        for (size_t j = 0; j < deg.size(); ++j)
            if (std::abs(wrapd(double(deg[j]) - 0.0)) <= 2.0) raw[j] = 30.f;   // 358..2
        const WedgeCandidate c = measureWedge(raw, {}, deg, kPhiFar, kArmVeto, kFloor, cfg);
        check(c.ok && std::abs(wrapd(c.centroidDeg - 0.0)) <= 1.5,
              "plateau straddling 0/360 centres at 0");
    }

    // ── THE BLUR'S EDGES (measureWedgeEdges) ────────────────────────────────
    //
    // A ridge detector sees a blurred shaft as two peaks, the sweep's two ends. The shape below is
    // the 08-18 s4 impact frame's: peaks at 106° (exposure start) and 90° (exposure end), the shaft
    // rotating toward SMALLER θ, 16° per exposure.
    std::printf("\n=== blur edges ===\n");
    const auto twoPeaks = [](const std::vector<float>& deg, double a, double b, float ha, float hb) {
        std::vector<float> raw(deg.size(), 2.f);
        for (size_t j = 0; j < deg.size(); ++j) {
            const double da = wrapd(double(deg[j]) - a), db = wrapd(double(deg[j]) - b);
            raw[j] += ha * float(std::exp(-0.5 * (da / 1.5) * (da / 1.5)))
                    + hb * float(std::exp(-0.5 * (db / 1.5) * (db / 1.5)));
        }
        return raw;
    };
    {
        const std::vector<float> deg = arcDeg(100.0, 30);                       // 70..130
        const std::vector<float> raw = twoPeaks(deg, 106.0, 90.0, 40.f, 30.f);  // trailing peak larger
        const WedgeEdges e = measureWedgeEdges(raw, {}, deg, -1, 16.0, kPhiFar, kArmVeto, kFloor, cfg);
        check(e.ok, "two-peak blur measured");
        check(e.ok && std::abs(wrapd(e.leadDeg - 90.0)) <= 0.5,
              "LEAD is the peak further along the rotation (90°), not the larger one (106°)");
        check(e.ok && e.hasTrail && std::abs(wrapd(e.trailDeg - 106.0)) <= 0.5,
              "trailing edge kept at 106° — the separation (16°) is the expected sweep");
        check(e.ok && e.hasTrail && std::abs(wrapd(e.midDeg - 98.0)) <= 0.5, "mid-exposure = midpoint (98°)");

        const WedgeEdges l = measureWedgeEdges(raw, {}, deg, +1, 16.0, kPhiFar, kArmVeto, kFloor, cfg);
        check(l.ok && std::abs(wrapd(l.leadDeg - 106.0)) <= 0.5,
              "rotation reversed (a left-hander's image) ⇒ the leading edge is the other peak");

        const WedgeEdges s = measureWedgeEdges(raw, {}, deg, -1, 4.0, kPhiFar, kArmVeto, kFloor, cfg);
        check(s.ok && !s.hasTrail,
              "slow club (4° expected sweep) ⇒ a peak 16° away is not the blur's other end — no trail");
        check(s.ok && std::abs(wrapd(s.leadDeg - 90.0)) <= 0.5, "…and the leading edge still stands");
    }
    {
        const std::vector<float> deg = arcDeg(100.0, 30);
        std::vector<float> raw(deg.size(), 2.f);
        for (size_t j = 0; j < deg.size(); ++j) {
            const double d = wrapd(double(deg[j]) - 95.0);
            raw[j] += 30.f * float(std::exp(-0.5 * (d / 1.5) * (d / 1.5)));
        }
        const WedgeEdges e = measureWedgeEdges(raw, {}, deg, -1, 12.0, kPhiFar, kArmVeto, kFloor, cfg);
        check(e.ok && !e.hasTrail && std::abs(wrapd(e.leadDeg - 95.0)) <= 0.5,
              "one peak ⇒ a leading edge only");
    }
    {
        const std::vector<float> deg = arcDeg(100.0, 30);
        const std::vector<float> weak = twoPeaks(deg, 106.0, 90.0, 4.f, 3.f);   // max ≈ 6 < thresh 10
        check(!measureWedgeEdges(weak, {}, deg, -1, 16.0, kPhiFar, kArmVeto, kFloor, cfg).ok,
              "below the absolute threshold ⇒ nothing (never fabricate)");
        const std::vector<float> raw = twoPeaks(deg, 106.0, 90.0, 40.f, 30.f);
        check(!measureWedgeEdges(raw, {}, deg, 0, 16.0, kPhiFar, kArmVeto, kFloor, cfg).ok,
              "no direction of rotation ⇒ no leading edge to name");
        check(!measureWedgeEdges(raw, {}, deg, -1, 16.0, 270.0, kArmVeto, kFloor, cfg).ok,
              "leading edge on the trail-arm line (φ+180 = 90°) ⇒ vetoed");
    }
    {
        const std::vector<float> deg = arcDeg(0.0, 30);                          // 330..30 across 0°
        const std::vector<float> raw = twoPeaks(deg, 8.0, 352.0, 40.f, 30.f);
        const WedgeEdges e = measureWedgeEdges(raw, {}, deg, -1, 16.0, kPhiFar, kArmVeto, kFloor, cfg);
        check(e.ok && std::abs(wrapd(e.leadDeg - 352.0)) <= 0.5 && e.hasTrail
                  && std::abs(wrapd(e.trailDeg - 8.0)) <= 0.5 && std::abs(wrapd(e.midDeg - 0.0)) <= 0.5,
              "edges across the 0/360 seam: lead 352°, trail 8°, mid 0°");
    }

    std::printf("\n%s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail;
}
