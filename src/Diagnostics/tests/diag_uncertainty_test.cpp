// Measurement uncertainty through the diagnostics DAG (session_diagnostics_design.md §A8).
//
//   D1  the phase grid carries σ, by source, and every reducer propagates it (sidecar round-trip)
//   D2  every finding carries P(fire): Φ against the deviation edge, ANY/ALL combination, the
//       verdict unchanged, a reading with no σ restated as 1/0 and marked unquantified
//   D3  soft session counts: no probability ⇒ exactly today's tier; P(Pattern) deterministic
//   D4  the noisy-OR posterior: negative evidence counts, posteriorRank off ⇒ the old order,
//       stability stamped from draws
//
//   ctest --test-dir <build> -R diag_uncertainty --output-on-failure

#include "../characteristic_engine.h"
#include "../measure_sample.h"
#include "../relation_resolver.h"
#include "../../Analysis/diagnostic_ledger.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <map>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
static double phi(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

// ── D1 fixture: one swing, three phases, three metrics ───────────────────────────────────────
static QJsonObject analysisFixture()
{
    QJsonArray phases;
    for (const auto &[p, t] : { std::pair<int, qint64>{ int(Phase::Address), 0 },
                                { int(Phase::Top), 500000 }, { int(Phase::Impact), 800000 } })
        phases.append(QJsonObject{ { QStringLiteral("phase"), p }, { QStringLiteral("t_us"), t } });

    const auto curve = [](const char *key, double sigma, int kind, bool impactReading) {
        QJsonArray t, v;
        for (int i = 0; i <= 80; ++i) { t.append(qint64(i) * 10000); v.append(double(i) * 0.1); }
        QJsonObject m{ { QStringLiteral("key"), QString::fromLatin1(key) }, { QStringLiteral("unit"), QStringLiteral("°") },
                       { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v } };
        if (sigma > 0) { m.insert(QStringLiteral("sigma"), sigma); if (kind > 0) m.insert(QStringLiteral("sigmaKind"), kind); }
        if (impactReading)
            m.insert(QStringLiteral("phaseSamples"), QJsonArray{ QJsonObject{
                { QStringLiteral("phase"), int(Phase::Impact) }, { QStringLiteral("t_us"), qint64(800000) },
                { QStringLiteral("value"), 8.0 }, { QStringLiteral("sigma"), 2.0 }, { QStringLiteral("grossRisk"), 0.1 } } });
        return m;
    };
    QJsonArray metrics;
    metrics.append(curve("withReading", 1.0, 1, true));   // series σ 1 (Series), impact reading σ 2
    metrics.append(curve("noiseOnly", 0.5, 0, false));    // sigmaKind absent ⇒ Noise
    metrics.append(curve("bare", -1.0, 0, false));        // no σ anywhere
    return QJsonObject{ { QStringLiteral("phases"), phases }, { QStringLiteral("metrics"), metrics } };
}

static Reducer at(Phase p) { Reducer r; r.kind = ReducerKind::At; r.anchor = p; return r; }

// ── D2–D4 fixture: a tiny pack ───────────────────────────────────────────────────────────────
struct FakeSource : IMeasureSource {
    std::map<QString, MeasureReading> m;
    void set(const char *id, double v, double sigma, double gross = -1.0)
    {
        MeasureReading r = MeasureReading::fromCorridor(v, -5.0, 5.0);   // μ 0, norm σ 5 ⇒ deviation edge ±10
        r.measSigma = sigma;
        r.grossRisk = float(gross);
        m[QString::fromLatin1(id)] = r;
    }
    std::optional<MeasureReading> read(const QString &id) const override
    {
        const auto it = m.find(id);
        return it == m.end() ? std::nullopt : std::optional<MeasureReading>(it->second);
    }
};

static CharacteristicPack pack()
{
    CharacteristicPack p;
    p.id = QStringLiteral("t");
    p.schemaVersion = kPackSchemaVersion;
    for (const char *id : { "mA", "mB", "mC" }) {
        Measure m; m.id = QString::fromLatin1(id); m.kind = MeasureKind::Composed;
        m.series = Series{ AnatomyRole::PelvisCentre, Quantity::Distance, AnatomyRole::TrailAnkle };
        m.reducer = at(Phase::Top);
        p.measures.push_back(m);
        Signal s; s.id = QStringLiteral("sig_") + m.id; s.test = SignalTest::OutsideCorridor;
        s.measures = { m.id }; s.direction = Direction::High;
        p.signalDefs.push_back(s);
    }
    const auto obs = [&](const char *id, QStringList sigs, DetectionMode mode, Prominence pr) {
        Condition c; c.id = QString::fromLatin1(id); c.label = c.id;
        c.observability = Observability::Observable; c.confirmedBy = ConfirmedBy::Measured;
        c.detectedBy = sigs; c.detection = mode; c.state = ConditionState::Active; c.prominence = pr;
        p.conditions.push_back(c);
    };
    obs("effA", { QStringLiteral("sig_mA") }, DetectionMode::Any, Prominence::Occasional);
    obs("effB", { QStringLiteral("sig_mB") }, DetectionMode::Any, Prominence::Occasional);
    obs("anyAC", { QStringLiteral("sig_mA"), QStringLiteral("sig_mC") }, DetectionMode::Any, Prominence::Occasional);
    obs("allAC", { QStringLiteral("sig_mA"), QStringLiteral("sig_mC") }, DetectionMode::All, Prominence::Occasional);
    const auto latent = [&](const char *id, Prominence pr) {
        Condition c; c.id = QString::fromLatin1(id); c.label = c.id;
        c.observability = Observability::Latent; c.confirmedBy = ConfirmedBy::Measured;
        c.state = ConditionState::Active; c.prominence = pr;
        p.conditions.push_back(c);
    };
    // causeOne explains effA only; causeTwo explains effA AND effB ("usually").
    latent("causeOne", Prominence::Occasional);
    latent("causeTwo", Prominence::Occasional);
    const auto edge = [&](const char *f, const char *t, Strength s) {
        Edge e; e.from = QString::fromLatin1(f); e.to = QString::fromLatin1(t); e.type = EdgeType::Causes; e.strength = s;
        p.edges.push_back(e);
    };
    edge("causeOne", "effA", Strength::Strong);
    edge("causeTwo", "effA", Strength::Strong);
    edge("causeTwo", "effB", Strength::Strong);
    return p;
}

int main()
{
    // ── D1 ───────────────────────────────────────────────────────────────────────────────────
    std::printf("=== D1: σ through the phase grid ===\n");
    {
        const SwingPhaseGrid g = buildPhaseGrid(analysisFixture());
        const auto imp = reduceOverGridReading(g, QStringLiteral("withReading"), at(Phase::Impact));
        check(imp && near(imp->sigma, 2.0, 1e-12) && imp->sigmaSrc == SigmaSource::Reading
                  && near(imp->grossRisk, 0.1, 1e-6),
              "At Impact takes the impact reading's own σ and gross risk");
        const auto top = reduceOverGridReading(g, QStringLiteral("withReading"), at(Phase::Top));
        check(top && near(top->sigma, 1.0, 1e-12) && top->sigmaSrc == SigmaSource::Series,
              "a phase with no reading of its own takes the series σ");
        const auto vOld = reduceOverGrid(g, QStringLiteral("withReading"), at(Phase::Top));
        check(vOld && top->value == *vOld, "the value is bit-identical to reduceOverGrid's");
        Reducer d; d.kind = ReducerKind::Delta; d.anchor = Phase::Top; d.window = { Phase::Top, Phase::Impact };
        const auto dl = reduceOverGridReading(g, QStringLiteral("withReading"), d);
        check(dl && near(dl->sigma, std::sqrt(5.0), 1e-12) && dl->sigmaSrc == SigmaSource::Series,
              "Delta: the endpoints in quadrature, carrying the weaker source");
        Reducer x; x.kind = ReducerKind::Extremum; x.sense = ExtremumSense::Max; x.window = { Phase::Top, Phase::Impact };
        const auto ex = reduceOverGridReading(g, QStringLiteral("withReading"), x);
        check(ex && near(ex->sigma, 2.0, 1e-12), "Extremum: the winning span's σ, no better known than its endpoints");
        const auto nz = reduceOverGridReading(g, QStringLiteral("noiseOnly"), at(Phase::Top));
        check(nz && nz->sigmaSrc == SigmaSource::Noise && near(nz->sigma, 0.5, 1e-12),
              "a series σ with no sigmaKind is tagged Noise");
        const auto br = reduceOverGridReading(g, QStringLiteral("bare"), at(Phase::Top));
        check(br && !std::isfinite(br->sigma) && br->sigmaSrc == SigmaSource::None, "no σ stated ⇒ unquantified");
        Reducer d2 = d;
        const auto mix = reduceOverGridReading(g, QStringLiteral("bare"), d2);
        check(mix && !std::isfinite(mix->sigma), "a term without σ poisons the budget rather than shrinking it");
        bool ok = false;
        const SwingPhaseGrid back = loadPhaseGrid(savePhaseGrid(g, 1, 2), 1, 2, &ok);
        const auto rt = reduceOverGridReading(back, QStringLiteral("withReading"), at(Phase::Impact));
        check(ok && rt && rt->sigma == imp->sigma && rt->sigmaSrc == imp->sigmaSrc && rt->grossRisk == imp->grossRisk,
              "the sidecar round-trips σ, source and gross risk");
    }

    // ── D2 ───────────────────────────────────────────────────────────────────────────────────
    std::printf("=== D2: a probability on every verdict ===\n");
    const CharacteristicPack P = pack();
    {
        FakeSource src;
        src.set("mA", 12.0, 3.0);          // Mark's example: 12 ± 3 against an edge at 10
        src.set("mB", 9.0, 3.0);
        src.set("mC", 11.0, std::numeric_limits<double>::quiet_NaN());
        const DetectionResult r = detect(P, src);
        const Finding *a = r.find(QStringLiteral("effA"));
        check(a && a->state == FindingState::Fired && near(a->pFire, phi(2.0 / 3.0), 1e-5) && a->quantified,
              "12 ± 3 past an edge at 10: Fired, P(fire) = Φ(2/3) ≈ 0.75");
        const Finding *b = r.find(QStringLiteral("effB"));
        check(b && b->state == FindingState::NotFired && near(b->pFire, phi(-1.0 / 3.0), 1e-5),
              "9 ± 3 short of the edge: NotFired, P(fire) = Φ(−1/3) ≈ 0.37");
        const Finding *any = r.find(QStringLiteral("anyAC"));
        check(any && near(any->pFire, 1.0 - (1.0 - phi(2.0 / 3.0)) * 0.0, 1e-5) && !any->quantified,
              "ANY with an unquantified fired signal: 1, and marked unquantified");
        const Finding *all = r.find(QStringLiteral("allAC"));
        check(all && all->state == FindingState::Fired && near(all->pFire, phi(2.0 / 3.0), 1e-5),
              "ALL: the product (the unquantified conjunct restates 1)");

        FakeSource hard;
        hard.set("mA", 12.0, std::numeric_limits<double>::quiet_NaN());
        hard.set("mB", 9.0, std::numeric_limits<double>::quiet_NaN());
        hard.set("mC", 11.0, std::numeric_limits<double>::quiet_NaN());
        const DetectionResult h = detect(P, hard);
        bool same = true;
        for (const Finding &f : h.findings) {
            const Finding *g = r.find(f.conditionId);
            same = same && g && g->state == f.state;
        }
        check(same, "the verdicts are identical with and without σ");
        const Finding *ha = h.find(QStringLiteral("effA"));
        check(ha && ha->pFire == 1.f && !ha->quantified, "no σ ⇒ P(fire) restates the verdict, unquantified");

        FakeSource g;
        g.set("mA", 12.0, 3.0, 0.3);
        const DetectionResult gr = detect(P, g);   // held: find() points into it
        const Finding *ga = gr.find(QStringLiteral("effA"));
        check(ga && near(ga->grossRisk, 0.3, 1e-6) && near(ga->pFire, phi(2.0 / 3.0), 1e-5),
              "gross risk rides beside P(fire), never folded into it");
    }

    // ── D3 ───────────────────────────────────────────────────────────────────────────────────
    std::printf("=== D3: soft session counts ===\n");
    {
        const auto session = [](float p, bool fired, int n) {
            std::vector<ShotRecord> shots;
            for (int i = 0; i < n; ++i) {
                ShotRecord s; s.shotId = i;
                ConditionRow r; r.conditionId = QStringLiteral("c"); r.state = fired ? ShotState::Fired : ShotState::Clean;
                r.pFire = p; r.quantified = p >= 0.f;
                s.rows.push_back(r);
                shots.push_back(s);
            }
            return shots;
        };
        LedgerOptions hard; hard.softTier = false; hard.warmUpWeight = 1.0;
        LedgerOptions soft = hard; soft.softTier = true;
        const auto noProb = session(-1.f, true, 6);
        const auto L0 = conditionLedgers(noProb, hard), L1 = conditionLedgers(noProb, soft);
        check(L0[0].tier == L1[0].tier && L0[0].wilsonLower == L1[0].wilsonLower && L1[0].pPattern == 1.0,
              "no probability anywhere ⇒ exactly today's tier, and P(Pattern) 1");
        const auto sure = session(0.99f, true, 6);
        check(conditionLedgers(sure, soft)[0].tier == Tier::Pattern, "confident firings are still a Pattern");
        const auto border = session(0.55f, true, 6);
        const auto Lb = conditionLedgers(border, soft);
        check(Lb[0].borderline && Lb[0].pPattern > 0.0 && Lb[0].pPattern < 1.0 && Lb[0].wilsonLower < L0[0].wilsonLower,
              "six borderline firings: a lower bound, a P(Pattern) strictly between 0 and 1, flagged borderline");
        const auto Lb2 = conditionLedgers(border, soft);
        check(Lb2[0].pPattern == Lb[0].pPattern, "P(Pattern) is deterministic");
        check(Lb[0].fired == 6 && Lb[0].assessable == 6 && Lb[0].recurrence == L0[0].recurrence,
              "the captions and integer counts stay hard");
        const QJsonObject j = toJson(border, soft);
        const std::vector<ShotRecord> back = fromJson(j);
        check(!back.empty() && back[0].rows[0].pFire == 0.55f && back[0].rows[0].quantified,
              "pFire and quantified round-trip through diagnostics.json");
    }

    // ── D4 ───────────────────────────────────────────────────────────────────────────────────
    std::printf("=== D4: the noisy-OR posterior ===\n");
    {
        // effA a pattern; effB assessed and ABSENT. causeTwo "usually" produces effB.
        DetectionResult det;
        Finding a; a.conditionId = QStringLiteral("effA"); a.state = FindingState::Fired; a.confidence = 1.f; a.pFire = 0.95f;
        Finding b; b.conditionId = QStringLiteral("effB"); b.state = FindingState::NotFired; b.pFire = 0.02f;
        det.findings = { a, b };
        ExplainOptions on; on.posteriorRank = true;
        ExplainOptions off; off.posteriorRank = false;
        const Explanation ep = explain(P, det, {}, on);
        const Explanation el = explain(P, det, {}, off);
        check(!ep.roots.empty() && ep.roots.front().conditionId == QStringLiteral("causeOne"),
              "negative evidence: the cause whose other usual effect was measured absent ranks below");
        double pOne = 0, pTwo = 0;
        for (const Explanation *e : { &ep })
            for (const RankedCause &rc : e->roots) {
                if (rc.conditionId == QStringLiteral("causeOne")) pOne = rc.posterior;
                if (rc.conditionId == QStringLiteral("causeTwo")) pTwo = rc.posterior;
            }
        check(pOne > pTwo || pTwo == 0.0, "…by its posterior");
        check(!el.roots.empty() && el.roots.front().legacyScore > 0.0 && el.roots.front().score == el.roots.front().legacyScore,
              "posteriorRank off ⇒ the legacy score ranks");

        // Both effects present: causeTwo explains both and its posterior wins.
        DetectionResult both = det;
        both.findings[1].state = FindingState::Fired; both.findings[1].pFire = 0.95f;
        const Explanation eb = explain(P, both, {}, on);
        check(!eb.roots.empty() && eb.roots.front().conditionId == QStringLiteral("causeTwo"),
              "with both effects present the cause explaining both comes first");

        // Weakly evidenced effect ⇒ lower posterior than a certain one.
        DetectionResult weak = det;
        weak.findings[0].pFire = 0.55f;
        const Explanation ew = explain(P, weak, {}, on);
        double pWeak = 0;
        for (const RankedCause &rc : ew.roots) if (rc.conditionId == QStringLiteral("causeOne")) pWeak = rc.posterior;
        check(pWeak > 0.0 && pWeak < pOne, "a borderline pattern lends its cause less than a certain one");

        // A cause that was itself measured is not inferred: its own P(present) stands, whatever its
        // absent children say.
        DetectionResult seen = det;
        Finding self; self.conditionId = QStringLiteral("causeTwo"); self.state = FindingState::NotFired; self.pFire = 0.3f;
        seen.findings.push_back(self);
        const Explanation es = explain(P, seen, {}, on);
        double pSelf = -1;
        for (const RankedCause &rc : es.roots) if (rc.conditionId == QStringLiteral("causeTwo")) pSelf = rc.posterior;
        check(pSelf < 0 || near(pSelf, 0.3, 1e-6), "a measured cause keeps its own P(present), un-updated by its children");

        // The score keeps coverage: with both effects present and both causes equally probable, the
        // cause explaining two outranks the one explaining one.
        check(!eb.roots.empty() && eb.roots.front().coverage == 2, "the expected explained mass keeps coverage");

                // Stability: every draw names causeOne ⇒ firm; none ⇒ fragile.
        Explanation st = ep;
        stampStability(st, std::vector<Explanation>(10, ep));
        check(st.roots.front().stabilityWord == QStringLiteral("firm") && st.roots.front().stability == 1.0,
              "a root named in every draw is firm");
        stampStability(st, std::vector<Explanation>(10, Explanation()));
        check(st.roots.front().stabilityWord == QStringLiteral("fragile"), "a root named in no draw is fragile");
    }

    std::printf("%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
