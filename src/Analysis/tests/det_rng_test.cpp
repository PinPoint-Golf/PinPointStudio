// The deterministic random source the uncertainty Monte Carlo and bootstraps run on
// (src/Analysis/det_rng.h; shaft_uncertainty_propagation_design.md principle 7). Pure std.
//
//   ctest --test-dir <build> -R det_rng --output-on-failure

#include "../det_rng.h"

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
    std::printf("=== det_rng ===\n");
    // mt19937_64's first raw output for the standard's default seed 5489 is fixed by the standard
    // (14514284786278117030); uniform() is its top 53 bits / 2^53 — the cross-platform pin.
    {
        DetRng r(5489u);
        const double u = r.uniform();
        const double want = double(14514284786278117030ull >> 11) / 9007199254740992.0;
        check(u == want, "uniform() is the top 53 bits of the standard's first mt19937_64 word");
    }
    {
        DetRng a(42u), b(42u);
        bool same = true;
        for (int i = 0; i < 1000; ++i) same = same && a.normal() == b.normal() && a.below(17) == b.below(17);
        check(same, "same seed ⇒ the same normals and integers");
    }
    {
        DetRng r(7u);
        const int n = 40000;
        double m = 0.0, v = 0.0, umin = 1.0, umax = 0.0;
        for (int i = 0; i < n; ++i) { const double x = r.normal(); m += x; v += x * x; }
        m /= n; v = v / n - m * m;
        for (int i = 0; i < n; ++i) { const double u = r.uniform(); umin = std::min(umin, u); umax = std::max(umax, u); }
        check(std::abs(m) < 0.02, "Box–Muller mean ≈ 0");
        check(std::abs(v - 1.0) < 0.03, "Box–Muller variance ≈ 1");
        check(umin >= 0.0 && umax < 1.0, "uniform() in [0, 1)");
        bool inRange = true;
        for (int i = 0; i < 1000; ++i) inRange = inRange && r.below(5) < 5;
        check(inRange, "below(n) < n");
    }
    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
