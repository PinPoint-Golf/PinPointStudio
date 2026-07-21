// Standalone tests for the kinematic-sequence reduction (src/Analysis/
// kinematic_sequence.h): peak-of-largest-magnitude selection, ordering by peak
// time, inter-node gaps, and the degrade-don't-fake drop of empty series. Pure,
// header-only — no OpenCV, no fixture. Own main()/check() macros.
//
//   cmake --build build/analyzer-tests --target kinematic_sequence_test
//   ctest --test-dir build/analyzer-tests -R kinematic_sequence --output-on-failure

#include "../kinematic_sequence.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static SeqSeries mk(const QString &key, std::vector<int64_t> t, std::vector<double> v)
{
    SeqSeries s;
    s.key = key;
    s.tUs = std::move(t);
    s.value = std::move(v);
    return s;
}

int main()
{
    std::printf("kinematic_sequence_test\n");

    // ── seqPeakIndex: largest magnitude, earliest on tie, -1 when empty ──────────
    {
        check(seqPeakIndex(mk("a", {0, 100000, 200000}, {0.0, 5.0, 3.0})) == 1,
              "peak index = largest magnitude");
        check(seqPeakIndex(mk("a", {0, 100000, 200000}, {0.0, -9.0, 3.0})) == 1,
              "peak index uses magnitude (signed value negative)");
        check(seqPeakIndex(mk("a", {0, 100000}, {4.0, 4.0})) == 0,
              "peak index: earliest wins on magnitude tie");
        check(seqPeakIndex(mk("a", {}, {})) == -1, "peak index = -1 for empty series");
    }

    // ── ordering + gaps: proximal (earlier peak) first, gapMs = Δ/1000 ───────────
    {
        // 'hand' peaks at 100 ms, 'club' peaks at 250 ms — club node should follow.
        std::vector<SeqSeries> in = {
            mk("club", {50000, 150000, 250000, 350000}, {0.0, 2.0, 8.0, 1.0}),
            mk("hand", {0, 100000, 200000}, {0.0, 5.0, 3.0}),
        };
        const auto nodes = kinematicSequenceNodes(in);
        check(nodes.size() == 2, "two data series → two nodes");
        check(nodes[0].key == QString("hand"), "ordered by peak time: hand first");
        check(nodes[1].key == QString("club"), "ordered by peak time: club second");
        check(nodes[0].order == 0 && nodes[1].order == 1, "order indices 0,1");
        check(nodes[0].tPeakUs == 100000 && near(nodes[0].peak, 5.0, 1e-9),
              "hand peak time/value");
        check(nodes[1].tPeakUs == 250000 && near(nodes[1].peak, 8.0, 1e-9),
              "club peak time/value");
        check(near(nodes[0].gapMs, 0.0, 1e-9), "first node gap = 0");
        check(near(nodes[1].gapMs, 150.0, 1e-9), "second node gap = (250-100) ms");
    }

    // ── degrade, don't fake: empty / dataless series are dropped, not zero-nodes ──
    {
        std::vector<SeqSeries> in = {
            mk("hand", {0, 100000}, {0.0, 5.0}),
            mk("club", {}, {}),                 // no data → not a node
        };
        const auto nodes = kinematicSequenceNodes(in);
        check(nodes.size() == 1, "empty series dropped (one node only)");
        check(nodes[0].key == QString("hand"), "surviving node is the one with data");
    }

    // ── zero inputs → zero nodes (Sequence zone then collapses) ──────────────────
    check(kinematicSequenceNodes({}).empty(), "no series → no nodes");

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
