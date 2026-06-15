// Standalone test for the findings-weighted composite score (v2, design §7.6).
// Run: ctest --test-dir build/analyzer-tests -R composite_score_v2_test --output-on-failure

#include "../reference_bands.h"
#include "../wrist_assessment_engine.h"
#include "../wrist_assessment_fixtures.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label, long got = -99999, long want = -99999)
{
    if (got != -99999) std::printf("  [%s] %-40s got %ld want %ld\n", c ? "PASS" : "FAIL", label, got, want);
    else               std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static FixtureWristAngleSource withSegConf(FixtureWristAngleSource src, float conf)
{
    PpSwingPositionTimeline tl = src.timeline();
    for (int p = 0; p < kNumPos; ++p)
        if (tl.positions[p].present) tl.positions[p].conf = conf;
    src.setTimeline(tl);
    return src;
}

int main()
{
    std::printf("=== Composite Wrist score v2 ===\n");
    ConfigReferenceBandProvider provider;

    // 1. Clean swing → no faults → 100, no contributions.
    {
        std::printf("-- 1. clean → 100 --\n");
        const PpScoreBreakdown b = WristAssessmentEngine::assess(makeCleanSwing(), provider).score;
        check(b.total == 100, "clean overall 100", b.total, 100);
        check(b.contributions.empty(), "no contributions", long(b.contributions.size()), 0);
    }

    // 2. A faulty swing scores below 100, with an explainable breakdown that sums to the score.
    {
        std::printf("-- 2. faulty → breakdown sums --\n");
        const PpScoreBreakdown b = WristAssessmentEngine::assess(makeMockupDemoSwing(), provider).score;
        check(b.total < 100, "faulty swing below 100", b.total, -99999);
        check(!b.contributions.empty(), "has contributions");
        double sum = 0.0;
        for (const PpScoreContribution &c : b.contributions) sum += c.penalty;
        const long rounded = std::lround(b.base - sum);     // engine rounds base − Σpenalty
        check(rounded == b.total, "round(base − Σpenalty) == total", rounded, long(b.total));
    }

    // 3. A low-confidence finding penalises proportionally less → a higher score.
    {
        std::printf("-- 3. low confidence → smaller penalty --\n");
        const int normal = WristAssessmentEngine::assess(makeCastSwing(), provider).score.total;
        const int weak   = WristAssessmentEngine::assess(withSegConf(makeCastSwing(), 0.30f), provider).score.total;
        check(weak > normal, "low-confidence cast costs less", weak, normal);
    }

    // 4. Deterministic.
    {
        std::printf("-- 4. determinism --\n");
        const int a = WristAssessmentEngine::assess(makeMockupDemoSwing(), provider).score.total;
        const int b = WristAssessmentEngine::assess(makeMockupDemoSwing(), provider).score.total;
        check(a == b, "two runs identical", a, b);
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
