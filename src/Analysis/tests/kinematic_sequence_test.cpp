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

    // ══ Layer 2: resolveKinematicSequence — the verdict, and when it is withheld ═════
    //
    // Nodes are built by hand here (segment_rates_test drives them from a synthetic swing) so the
    // ORDERING RULES can be pinned on exact numbers: the σ gate, the five verdicts, the unplaced
    // tail, the deceleration flag and the route summary.
    const auto node = [](SeqSegment s, double beforeMs, double sigmaMs, double peak, bool direct,
                         bool placed = true) {
        KsNode n;
        n.segment  = s;
        n.placed   = placed;
        n.tPeakUs  = 500000 - int64_t(beforeMs * 1000.0);
        n.peakDps  = peak;
        n.tSigmaMs = sigmaMs;
        n.direct   = direct;
        n.routeId  = direct ? QStringLiteral("imu") : QStringLiteral("faceOn");
        return n;
    };
    // The professional pattern from Cheetham (2008), with IMU-sized σ.
    {
        const KinematicSequence ks = resolveKinematicSequence(
            // σ = 1 ms on every node: the benchmark's 3 ms thorax→arm gap resolves only against a
            // combined σ under 3 ms, which is an IMU-only precision — see the k-knob block below.
            { node(SeqSegment::Club, 0, 1, 2254, true), node(SeqSegment::Pelvis, 87, 1, 477, true),
              node(SeqSegment::LeadArm, 65, 1, 980, true), node(SeqSegment::Thorax, 68, 1, 727, true) },
            500000);
        check(ks.valid && ks.nodes.size() == 4, "L2 four nodes in → valid, four nodes out");
        check(ks.nodes[0].segment == SeqSegment::Pelvis && ks.nodes[3].segment == SeqSegment::Club,
              "L2 nodes are returned in SeqSegment order regardless of input order");
        check(ks.order.size() == 4 && ks.order[0] == SeqSegment::Pelvis && ks.order[1] == SeqSegment::Thorax
                  && ks.order[2] == SeqSegment::LeadArm && ks.order[3] == SeqSegment::Club,
              "L2 order is by peak time: pelvis, thorax, arm, club");
        check(ks.orderResolved, "L2 IMU-sized σ: every gap exceeds its combined σ ⇒ resolved");
        check(ks.verdict == QString("proximalToDistal"), "L2 verdict proximalToDistal");
        check(ks.gapsMs.size() == 3 && near(ks.gapsMs[0], 19.0, 1e-9) && near(ks.gapsMs[1], 3.0, 1e-9)
                  && near(ks.gapsMs[2], 65.0, 1e-9),
              "L2 gaps 19 / 3 / 65 ms");
        check(ks.gainsDps.size() == 3 && near(ks.gainsDps[0], 250.0, 1e-9) && near(ks.gainsDps[2], 1274.0, 1e-9),
              "L2 gains: pelvis→thorax 250, arm→club 1274 °/s (the benchmark's own numbers)");
        check(ks.pelvisDecelerates == 1, "L2 pelvis peaked 87 ms out with σ 3 ⇒ decelerates before impact");
        check(ks.routeSummary == QString("direct"), "L2 all Direct ⇒ routeSummary direct");
        check(near(ks.nodes[0].beforeImpactMs, 87.0, 1e-9), "L2 beforeImpactMs is filled from impactUs");
    }
    // The same pattern read by a camera whose σ is wider than the 3 ms thorax→arm gap.
    {
        const KinematicSequence ks = resolveKinematicSequence(
            { node(SeqSegment::Pelvis, 87, 3, 477, true), node(SeqSegment::Thorax, 68, 12, 727, false),
              node(SeqSegment::LeadArm, 65, 4, 980, false), node(SeqSegment::Club, 0, 2, 2254, false) },
            500000);
        check(ks.order.size() == 4, "L2 wide σ: every node still placed and ordered");
        check(!ks.orderResolved && ks.verdict == QString("unresolved"),
              "L2 wide σ: a gap inside k·sqrt(σ²+σ²) withholds the verdict");
        check(ks.routeSummary == QString("mixed"), "L2 one Direct + three Estimated ⇒ mixed");
    }
    // The amateur signature, resolved.
    {
        const KinematicSequence ks = resolveKinematicSequence(
            { node(SeqSegment::Pelvis, 78, 3, 395, true), node(SeqSegment::LeadArm, 64, 2, 763, true),
              node(SeqSegment::Thorax, 50, 3, 583, true), node(SeqSegment::Club, 0, 2, 1790, true) },
            500000);
        check(ks.orderResolved && ks.verdict == QString("armBeforeThorax"),
              "L2 arm before thorax ⇒ armBeforeThorax");
        check(ks.order[1] == SeqSegment::LeadArm && ks.order[2] == SeqSegment::Thorax,
              "L2 …and the order says so");
    }
    // Unplaced nodes: kept, skipped by the order, and the verdict is partial when what remains
    // is in canonical order.
    {
        const KinematicSequence ks = resolveKinematicSequence(
            { node(SeqSegment::Pelvis, 87, 90, 300, false, /*placed*/ false),
              node(SeqSegment::Thorax, 68, 90, 300, false, false),
              node(SeqSegment::LeadArm, 65, 4, 980, false), node(SeqSegment::Club, 0, 2, 2254, false) },
            500000);
        check(ks.nodes.size() == 4 && ks.order.size() == 2, "L2 two unplaced nodes stay in nodes[], leave order[]");
        check(ks.verdict == QString("partial"), "L2 placed nodes in canonical order, fewer than four ⇒ partial");
        check(ks.pelvisDecelerates == -1, "L2 an unplaced pelvis leaves the deceleration flag unknown");
        check(ks.routeSummary == QString("estimated"), "L2 all placed nodes Estimated ⇒ estimated");
    }
    // Out of order with all four placed ⇒ other; a single placed node ⇒ unresolved.
    {
        const KinematicSequence ks = resolveKinematicSequence(
            { node(SeqSegment::Pelvis, 30, 2, 400, true), node(SeqSegment::Thorax, 90, 2, 600, true),
              node(SeqSegment::LeadArm, 60, 2, 900, true), node(SeqSegment::Club, 0, 2, 2000, true) },
            500000);
        check(ks.verdict == QString("other"), "L2 thorax, arm, pelvis, club ⇒ other (thorax before arm, so not the amateur pattern)");
        const KinematicSequence one = resolveKinematicSequence({ node(SeqSegment::Club, 0, 2, 2000, false) }, 500000);
        check(one.valid && one.verdict == QString("unresolved") && one.order.size() == 1,
              "L2 one placed node: valid, unresolved");
        check(!resolveKinematicSequence({}, 500000).valid, "L2 no nodes ⇒ not valid");
    }
    // The sigmaK knob scales the gate.
    {
        std::vector<KsNode> in = { node(SeqSegment::Pelvis, 87, 5, 477, true), node(SeqSegment::Thorax, 68, 5, 727, true),
                                   node(SeqSegment::LeadArm, 65, 1, 980, true), node(SeqSegment::Club, 0, 1, 2254, true) };
        check(!resolveKinematicSequence(in, 500000, 1.0).orderResolved,
              "L2 k=1: a 3 ms gap against sqrt(5²+1²) = 5.1 ms is NOT resolved");
        check(resolveKinematicSequence(in, 500000, 0.5).orderResolved,
              "L2 k=0.5: the same gap against 2.55 ms IS resolved — the knob scales the gate");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
