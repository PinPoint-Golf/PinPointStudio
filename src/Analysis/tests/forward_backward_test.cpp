// The forward–backward posterior over the banded θ lattice (shaftshared::forwardBackwardBanded,
// fbSummary; shaft_uncertainty_propagation_design.md §4.6): rows are probability distributions,
// a cold lattice collapses onto the Viterbi path, and two equally cheap structures split the mass.
//
//   ctest --test-dir <build> -R forward_backward --output-on-failure

#include "../shaft_track_shared.h"

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

int main()
{
    std::printf("=== forward–backward ===\n");
    const int NS = 72, nf = 8;            // 5° grid
    const double grid = 5.0;
    std::vector<std::vector<float>> emis(nf, std::vector<float>(NS, 10.f));
    for (int f = 0; f < nf; ++f) {
        const int k = (10 + f) % NS;      // a line rotating 5°/frame
        emis[size_t(f)][size_t(k)] = 0.f;
        emis[size_t(f)][size_t((k + 1) % NS)] = 3.f;
    }
    std::vector<int> wmax(nf, 3), sgn(nf, 0);
    const DPResult dp = shaftshared::viterbiBanded(emis, wmax, sgn, 0.03, grid);
    // rows sum to 1
    {
        const auto m = shaftshared::forwardBackwardBanded(emis, wmax, sgn, 0.03, grid, 1.0);
        bool ok = int(m.size()) == nf;
        for (const auto &r : m) { double s = 0; for (float p : r) s += p; ok = ok && std::abs(s - 1.0) < 1e-5; }
        check(ok, "every frame's marginal sums to 1");
    }
    // cold ⇒ the Viterbi path
    {
        const auto m = shaftshared::forwardBackwardBanded(emis, wmax, sgn, 0.03, grid, 0.01);
        bool ok = true;
        for (int f = 0; f < nf; ++f) {
            int arg = 0;
            for (int k = 1; k < NS; ++k) if (m[size_t(f)][size_t(k)] > m[size_t(f)][size_t(arg)]) arg = k;
            ok = ok && arg == dp.thstar[size_t(f)];
        }
        check(ok, "as T → 0 the marginal's mode is the Viterbi path");
    }
    // two equally cheap, far-apart structures ⇒ pAlt ≈ ½ at the published one
    {
        std::vector<std::vector<float>> e2(nf, std::vector<float>(NS, 10.f));
        for (int f = 0; f < nf; ++f) { e2[size_t(f)][10] = 0.f; e2[size_t(f)][40] = 0.f; }
        std::vector<int> w0(nf, 1);
        const auto m = shaftshared::forwardBackwardBanded(e2, w0, sgn, 0.03, grid, 1.0);
        const shaftshared::FbSummary s = shaftshared::fbSummary(m[4], grid, 10.0 * grid);
        std::printf("    two structures: pAlt %.3f, σ %.3f°\n", s.pAlt, s.sigmaDeg);
        check(std::abs(s.pAlt - 0.5) < 0.05, "pAlt ≈ ½ when another structure is exactly as cheap");
        check(s.sigmaDeg < grid, "σ about the published state stays inside a grid step");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
