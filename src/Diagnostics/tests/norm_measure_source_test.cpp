// The join that lights the pack up: values + norms -> a graded MeasureReading
// (src/Diagnostics/norms/norm_measure_source.h) and the engine's corridor test on top of it.
//
// Three behaviours carry it, and each exists because the failure it prevents is silent:
//
//   1. A signal fires on a DEVIATION (Watch/Action), not merely on leaving the Ideal band. Ideal is
//      |z| <= 1, so firing there would trip about a third of any normal population on every
//      characteristic in the library.
//   2. An unknown context resolves to NOTHING, so the characteristic reports Unavailable. Grading
//      a bunker shot against a full-swing norm produces a confident, plausible, wrong finding.
//   3. An undeclared context resolves to the default but is MARKED, demoting confidence. The
//      deviation is real; what is uncertain is whether the right norm judged it.
//
//   cmake --build build/analyzer-tests --target norm_measure_source_test
//   ctest --test-dir build/analyzer-tests -R norm_measure_source --output-on-failure

#include "../norms/norm_measure_source.h"
#include "../norms/composite_reading.h"
#include "../../Analysis/det_rng.h"   // seeded, platform-identical normals for the null calibration

#include <cmath>
#include <cstdio>
#include <map>

using namespace pinpoint::analysis;

static int  g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

class FakeValues final : public IMeasureValueSource {
public:
    void set(const QString &id, double v, float conf = 1.0f) { m[id] = { v, conf }; }
    void setFull(const QString &id, const Value &v) { m[id] = v; }
    void clear(const QString &id) { m.erase(id); }
    std::optional<Value> value(const QString &id) const override
    {
        const auto it = m.find(id);
        return it == m.end() ? std::nullopt : std::optional<Value>(it->second);
    }
private:
    std::map<QString, Value> m;
};

class FakeNorms final : public INormProvider {
public:
    FakeNorms()
    {
        m_contexts = ContextTree(std::vector<ContextNode>{
            { QStringLiteral("any"),        QStringLiteral("Any"),   QString() },
            { QStringLiteral("full_swing"), QStringLiteral("Full"),  QStringLiteral("any") },
            { QStringLiteral("driver"),     QStringLiteral("Drv"),   QStringLiteral("full_swing") },
            { QStringLiteral("wedge"),      QStringLiteral("Wdg"),   QStringLiteral("full_swing") },
        });
    }
    void add(const char *measure, const char *context, double mu, double sigma,
             Cohort cohort = {})
    {
        Norm n;
        n.measureId = QLatin1String(measure);
        n.contextId = QLatin1String(context);
        n.cohort    = cohort;
        n.mu        = mu;
        n.sigmaLo = n.sigmaHi = sigma;
        m_norms.upsert(n);
    }
    void addNorm(const Norm &n) { m_norms.upsert(n); }
    // A composite's row: always μ 0, σ 1 in "z", plus its measured ρ (or none, to test the refusal).
    void addComposite(const char *measure, std::optional<double> rho)
    {
        Norm n;
        n.measureId = QLatin1String(measure);
        n.contextId = QStringLiteral("full_swing");
        n.mu        = 0.0;
        n.sigmaLo = n.sigmaHi = 1.0;
        n.unit      = QStringLiteral("z");
        n.rho       = rho;
        m_norms.upsert(n);
    }
    const NormPack         &norms() const override { return m_norms; }
    const ContextTree      &contexts() const override { return m_contexts; }
    const ValidationReport &report() const override { return m_report; }
    QString                 label() const override { return QStringLiteral("fake"); }
    PackOrigin              origin() const override { return PackOrigin::Core; }
private:
    NormPack         m_norms;
    ContextTree      m_contexts;
    ValidationReport m_report;
};

int main()
{
    auto norms = std::make_shared<FakeNorms>();
    norms->add("m_ballPosition", "full_swing", 50.0, 10.0);   // ideal 40..60
    norms->add("m_ballPosition", "driver",     10.0, 5.0);    // ideal  5..15

    std::printf("=== the corridor finally exists ===\n");
    {
        FakeValues v;
        v.set(QStringLiteral("m_ballPosition"), 50.0);
        const NormMeasureSource src(v, norms, QStringLiteral("full_swing"));

        const auto r = src.read(QStringLiteral("m_ballPosition"));
        check(r.has_value(), "a measure with a value reads");
        check(r && r->hasCorridor, "hasCorridor is TRUE — the thing that was never true before");
        check(r && r->greenLo == 40.0 && r->greenHi == 60.0, "the ideal band is the norm's");
        check(r && r->grade == Grade::Ideal, "a centred value grades Ideal");
        check(r && r->normContextId == QLatin1String("full_swing"), "the resolved context is reported");
        check(r && !r->contextInferred, "a declared context is not marked inferred");
    }

    std::printf("=== grades across the bands ===\n");
    {
        struct Case { double v; Grade want; const char *label; };
        const Case cases[] = {
            { 50.0, Grade::Ideal,  "centre -> Ideal" },
            { 60.0, Grade::Ideal,  "1 sigma -> Ideal" },
            { 65.0, Grade::Good,   "1.5 sigma -> Good" },
            { 75.0, Grade::Watch,  "2.5 sigma -> Watch" },
            { 90.0, Grade::Action, "4 sigma -> Action" },
        };
        bool ok = true;
        for (const Case &c : cases) {
            FakeValues v;
            v.set(QStringLiteral("m_ballPosition"), c.v);
            const NormMeasureSource src(v, norms, QStringLiteral("full_swing"));
            const auto r = src.read(QStringLiteral("m_ballPosition"));
            if (!r || r->grade != c.want) { ok = false; std::printf("      %s\n", c.label); }
        }
        check(ok, "every band boundary grades as expected through the source");
    }

    std::printf("=== resolution walks up the tree ===\n");
    {
        FakeValues v;
        v.set(QStringLiteral("m_ballPosition"), 10.0);

        const NormMeasureSource drv(v, norms, QStringLiteral("driver"));
        const auto rd = drv.read(QStringLiteral("m_ballPosition"));
        check(rd && rd->normContextId == QLatin1String("driver") && rd->grade == Grade::Ideal,
              "a driver shot at 10% grades Ideal against the DRIVER norm");

        // The same value, judged as a wedge: it inherits full swing (no wedge row) and 10% is a
        // long way forward of a wedge's 50% centre. Same swing, different verdict, correctly.
        const NormMeasureSource wdg(v, norms, QStringLiteral("wedge"));
        const auto rw = wdg.read(QStringLiteral("m_ballPosition"));
        check(rw && rw->normContextId == QLatin1String("full_swing"),
              "a wedge inherits the full-swing norm");
        check(rw && rw->grade == Grade::Action,
              "…and the SAME value grades Action there — which is the whole point of contexts");
    }

    std::printf("=== an unknown context is never silently defaulted ===\n");
    {
        FakeValues v;
        v.set(QStringLiteral("m_ballPosition"), 10.0);
        const NormMeasureSource src(v, norms, QStringLiteral("hovercraft"));
        const auto r = src.read(QStringLiteral("m_ballPosition"));
        check(r.has_value(), "the VALUE still reads");
        check(r && !r->hasCorridor && r->grade == Grade::NotMeasured,
              "but no corridor resolves, so it is NotMeasured — not graded against full swing");
    }

    std::printf("=== an undeclared context is marked, not hidden ===\n");
    {
        FakeValues v;
        v.set(QStringLiteral("m_ballPosition"), 90.0);
        const NormMeasureSource src(v, norms, QString());
        const auto r = src.read(QStringLiteral("m_ballPosition"));
        check(r && r->hasCorridor && r->normContextId == QLatin1String("full_swing"),
              "an undeclared context falls back to full swing");
        check(r && r->contextInferred, "…and says so, so the engine can demote confidence");
        check(r && r->grade == Grade::Action, "the deviation itself is still reported");
    }

    std::printf("=== absent value vs absent norm ===\n");
    {
        FakeValues v;                                   // nothing set
        const NormMeasureSource src(v, norms, QStringLiteral("full_swing"));
        check(!src.read(QStringLiteral("m_ballPosition")).has_value(),
              "no value at all -> nullopt, which the engine reports Unavailable");

        FakeValues v2;
        v2.set(QStringLiteral("m_noNorm"), 42.0);
        const NormMeasureSource src2(v2, norms, QStringLiteral("full_swing"));
        const auto r = src2.read(QStringLiteral("m_noNorm"));
        check(r && !r->hasCorridor,
              "a value with no norm reads, but carries no corridor — also Unavailable, never a pass");

        const NormMeasureSource src3(v2, nullptr, QStringLiteral("full_swing"));
        const auto r3 = src3.read(QStringLiteral("m_noNorm"));
        check(r3 && !r3->hasCorridor, "no norm provider at all degrades, it does not crash");
    }

    std::printf("=== the engine fires on a deviation, not on leaving Ideal ===\n");
    {
        // A minimal pack: one axis, two tails, on one measure.
        CharacteristicPack pack;
        Measure m;
        m.id     = QStringLiteral("m_ballPosition");
        m.unit   = QStringLiteral("%");
        m.status = MeasureStatus::Live;
        pack.measures.push_back(m);

        Signal hi;
        hi.id        = QStringLiteral("sig_back");
        hi.test      = SignalTest::OutsideCorridor;
        hi.measures  = { QStringLiteral("m_ballPosition") };
        hi.direction = Direction::High;
        pack.signalDefs.push_back(hi);

        Signal lo = hi;
        lo.id        = QStringLiteral("sig_forward");
        lo.direction = Direction::Low;
        pack.signalDefs.push_back(lo);

        Condition back;
        back.id         = QStringLiteral("ball_back");
        back.label      = QStringLiteral("Ball back");
        back.axis       = QStringLiteral("ball_position");
        back.detectedBy = { QStringLiteral("sig_back") };
        back.state      = ConditionState::Active;
        pack.conditions.push_back(back);

        Condition fwd = back;
        fwd.id         = QStringLiteral("ball_forward");
        fwd.label      = QStringLiteral("Ball forward");
        fwd.detectedBy = { QStringLiteral("sig_forward") };
        pack.conditions.push_back(fwd);

        auto detectAt = [&](double value, const QString &ctx) {
            FakeValues v;
            v.set(QStringLiteral("m_ballPosition"), value);
            const NormMeasureSource src(v, norms, ctx);
            return detect(pack, src);
        };

        const QString FS = QStringLiteral("full_swing");

        // 65 is 1.5 sigma out — outside the Ideal band, but ordinary variation. It must NOT fire.
        const DetectionResult good = detectAt(65.0, FS);
        check(good.fired().isEmpty(),
              "1.5 sigma outside Ideal does NOT fire — Good is ordinary variation");
        const Finding *gf = good.find(QStringLiteral("ball_back"));
        check(gf && gf->state == FindingState::NotFired,
              "…and it is reported NotFired, which is an assessment, not a gap");

        // 75 is 2.5 sigma — a Watch deviation on the high tail.
        const DetectionResult watch = detectAt(75.0, FS);
        check(watch.fired() == QStringList({ QStringLiteral("ball_back") }),
              "2.5 sigma fires the HIGH tail only");

        // 25 is 2.5 sigma the other way — the same axis, the other condition.
        const DetectionResult low = detectAt(25.0, FS);
        check(low.fired() == QStringList({ QStringLiteral("ball_forward") }),
              "2.5 sigma low fires the LOW tail only — one norm, two tails, never both");

        // No norm for this measure at all: Unavailable, and emphatically not NotFired.
        CharacteristicPack orphan = pack;
        orphan.measures[0].id = QStringLiteral("m_orphan");
        orphan.signalDefs[0].measures = { QStringLiteral("m_orphan") };
        orphan.signalDefs[1].measures = { QStringLiteral("m_orphan") };
        FakeValues ov;
        ov.set(QStringLiteral("m_orphan"), 999.0);
        const NormMeasureSource osrc(ov, norms, FS);
        const DetectionResult ores = detect(orphan, osrc);
        const Finding *of = ores.find(QStringLiteral("ball_back"));
        check(of && of->state == FindingState::Unavailable,
              "a measure with no norm is UNAVAILABLE, never a clean bill of health");

        // The inferred-context demotion reaches the finding.
        const DetectionResult inferred = detectAt(75.0, QString());
        const Finding *inf = inferred.find(QStringLiteral("ball_back"));
        const Finding *dec = watch.find(QStringLiteral("ball_back"));
        check(inf && dec && inf->state == FindingState::Fired
                  && inf->confidence < dec->confidence,
              "an inferred context fires the same finding at LOWER confidence");
    }

    // ── Which population graded it, on the reading itself ───────────────────
    //
    // The engine is dormant, so this is the only place the field can be exercised — and a field
    // nothing sets is a field that reads `undefined` at whatever surface eventually shows it.
    std::printf("=== the answering cohort reaches the reading ===\n");
    {
        Cohort women;
        women.sex = Sex::Female;
        Cohort seniorWomen;
        seniorWomen.sex = Sex::Female;
        seniorWomen.age = AgeBand::Adult55_64;

        auto segmented = std::make_shared<FakeNorms>();
        segmented->add("m_ballPosition", "full_swing", 50.0, 10.0);              // universal
        segmented->add("m_ballPosition", "full_swing", 30.0, 10.0, women);       // segmented

        FakeValues v;
        v.set(QStringLiteral("m_ballPosition"), 30.0);

        // An athlete we know nothing about is graded by the universal corridor, and the reading says
        // so by carrying an unqualified cohort — which is an answer, not a blank.
        const NormMeasureSource anon(v, segmented, QStringLiteral("full_swing"));
        const auto ra = anon.read(QStringLiteral("m_ballPosition"));
        check(ra && ra->normCohort.isUnqualified() && ra->greenLo == 40.0,
              "an athlete with no demographics reads the universal corridor, cohort unqualified");
        check(ra && ra->grade == Grade::Good,
              "…and 30 is two tolerances out of it");

        // The same swing, the same number, a golfer the pack has a corridor for.
        const NormMeasureSource hers(v, segmented, QStringLiteral("full_swing"), GradePolicy{},
                                     nullptr, women);
        const auto rh = hers.read(QStringLiteral("m_ballPosition"));
        check(rh && rh->normCohort == women,
              "a segmented corridor names ITS OWN cohort on the reading");
        check(rh && rh->grade == Grade::Ideal,
              "…and the grade improves because the corridor became right for her, which is exactly "
              "what the surface has to be able to explain");

        // Broader than asked for: nothing exists for her band, so the sex row answers — and the
        // reading names the row that GRADED, not the question that was asked.
        const NormMeasureSource senior(v, segmented, QStringLiteral("full_swing"), GradePolicy{},
                                       nullptr, seniorWomen);
        const auto rs = senior.read(QStringLiteral("m_ballPosition"));
        check(rs && rs->normCohort == women,
              "a broader row answering names the broader cohort, not the athlete's");
    }

    // ══ Composite measures (posture_metrics_design.md §4.3, §5.2, §6.3; V6 of §9) ══════════════
    std::printf("=== composite: the special functions ===\n");
    {
        check(std::fabs(stdNormalQuantile(0.975) - 1.959963984540054) < 1e-12, "Φ⁻¹(0.975) = 1.95996…");
        check(std::fabs(stdNormalQuantile(1e-10) + 6.361340902404056) < 1e-9, "Φ⁻¹(1e-10) deep in the tail");
        check(std::fabs(stdNormalQuantile(stdNormalCdf(-3.3)) + 3.3) < 1e-12, "Φ⁻¹(Φ(x)) = x");
        check(std::fabs(regularizedGammaQ(1.0, 2.5) - std::exp(-2.5)) < 1e-14, "Q(1, x) = e^−x");
        check(std::fabs(regularizedGammaQ(0.5, 0.7) - std::erfc(std::sqrt(0.7))) < 1e-14
                  && std::fabs(regularizedGammaQ(0.5, 9.0) - std::erfc(3.0)) < 1e-16,
              "Q(½, x) = erfc(√x), on both sides of the series / fraction split");
        check(std::fabs(chiSquareSurvival(3.841458820694124, 1.0) - 0.05) < 1e-12
                  && std::fabs(chiSquareSurvival(11.070497693516351, 5.0) - 0.05) < 1e-12,
              "χ² survival hits the textbook 5 % points at k = 1 and k = 5");
    }

    // The worked tables of §4.3 and §5.2, to the two decimals the design prints.
    const auto twoDp = [](double v, double want) { return std::fabs(std::round(v * 100.0) / 100.0 - want) < 1e-9; };
    const auto termsOf = [](const std::vector<double> &w, const std::vector<double> &z) {
        std::vector<CompositeTerm> t;
        for (size_t i = 0; i < w.size(); ++i) t.push_back({ z[i], w[i], 1, std::numeric_limits<double>::quiet_NaN() });
        return t;
    };
    const std::vector<double> wCoh  = { 0.9, 0.8, 0.6, 0.4 };   // §4.2
    const std::vector<double> wDist = { 0.9, 0.7, 0.6, 0.6 };   // §5.1, the parts readable today
    const double rhos[] = { 0.0, 0.3, 0.6 };

    std::printf("=== composite: the worked tables (§4.3 coherent, §5.2 distance) ===\n");
    {
        struct Row { std::vector<double> z; double want[3]; const char *label; };
        // z in the LOSS direction (sᵢzᵢ), which is what the table states; the compensation row is
        // bend lost 2 with the pelvis 2 LOWER.
        const Row coherent[] = {
            { { 1.5, 1.5, 1.5, 1.5 }, { 2.89, 2.14, 1.78 }, "all four at 1.5" },
            { { 3, 0, 0, 0 },         { 1.92, 1.43, 1.19 }, "bend alone at 3" },
            { { 3, 3, 2, 1 },         { 4.77, 3.55, 2.95 }, "3, 3, 2, 1 — a whole-body stand-up" },
            { { 2, 0, 0, -2 },        { 0.71, 0.53, 0.44 }, "bend lost 2, pelvis sank 2 — a compensation" },
        };
        bool ok = true;
        for (const Row &r : coherent)
            for (int k = 0; k < 3; ++k) {
                const double z = combineCoherent(termsOf(wCoh, r.z), rhos[k]).z;
                if (!twoDp(z, r.want[k])) { ok = false; std::printf("      %s ρ=%.1f: %.4f want %.2f\n", r.label, rhos[k], z, r.want[k]); }
            }
        check(ok, "coherent: every cell of the §4.3 table, to two decimals");

        const Row distance[] = {
            { { 3, 0, 0, 0 },         { 2.01, 1.84, 1.57 }, "bend alone at 3" },
            { { 3.5, 0, 0, 0 },       { 2.67, 2.43, 2.03 }, "bend alone at 3.5" },
            { { 2.5, 2.5, 0, 0 },     { 2.46, 2.24, 1.88 }, "bend and knees at 2.5" },
            { { 1.8, 1.8, 1.8, 1.8 }, { 2.25, 2.05, 1.73 }, "all four at 1.8" },
        };
        ok = true;
        for (const Row &r : distance)
            for (int k = 0; k < 3; ++k) {
                const double z = combineDistance(termsOf(wDist, r.z), rhos[k]).z;
                if (!twoDp(z, r.want[k])) { ok = false; std::printf("      %s ρ=%.1f: %.4f want %.2f\n", r.label, rhos[k], z, r.want[k]); }
            }
        check(ok, "distance: every cell of the §5.2 table, to two decimals");

        // A distance reading that is negative in every part is as poor as a positive one.
        check(std::fabs(combineDistance(termsOf(wDist, { -3, 0, 0, 0 }), 0.0).z
                        - combineDistance(termsOf(wDist, { 3, 0, 0, 0 }), 0.0).z) < 1e-12,
              "distance squares: too upright and too bent are equally poor");
        const CompositeCombination dead = combineDistance(termsOf(wDist, { 0, 0, 0, 0 }), 0.3);
        check(std::isfinite(dead.z) && dead.z == -kCompositeZClamp,
              "a setup dead on every norm reads at the clamp, finite, never −∞");
        const CompositeCombination cs = combineCoherent(termsOf(wCoh, { 3, 3, 2, 1 }), 0.3);
        double sum = 0.0;
        for (double sh : cs.shares) sum += sh;
        check(cs.shares.size() == 4 && std::fabs(sum - 1.0) < 1e-12 && cs.shares[0] > cs.shares[3],
              "coherent shares sum to one and rank the parts by wsz");
        const CompositeCombination comp = combineCoherent(termsOf(wCoh, { 2, 0, 0, -2 }), 0.3);
        check(comp.shares[3] < 0.0, "a compensating part carries a NEGATIVE share — 'against it'");
    }

    // V6: the null calibration. 10⁵ clean swings per mode per ρ, every part at its norm with mean
    // correlation ρ; the composite must trip within ±0.5 points of the one-reading rate, 2.28 %.
    std::printf("=== composite: null trip rate (10^5 draws per mode per ρ) ===\n");
    {
        constexpr int    kDraws = 100000;
        const double     oneReading = 1.0 - stdNormalCdf(2.0);   // 2.275 %
        bool ok = true;
        for (double rho : rhos) {
            DetRng rng(20261021u + uint64_t(rho * 10));
            int tripsC = 0, tripsD = 0;
            std::vector<double> x(4);
            for (int i = 0; i < kDraws; ++i) {
                const double common = rng.normal();
                for (double &xi : x) xi = std::sqrt(rho) * common + std::sqrt(1.0 - rho) * rng.normal();
                if (combineCoherent(termsOf(wCoh, x), rho).z > 2.0) ++tripsC;
                if (combineDistance(termsOf(wDist, x), rho).z > 2.0) ++tripsD;
            }
            const double rc = double(tripsC) / kDraws, rd = double(tripsD) / kDraws;
            std::printf("      ρ=%.1f  coherent %.3f %%  distance %.3f %%\n", rho, 100 * rc, 100 * rd);
            if (std::fabs(rc - oneReading) > 0.005 || std::fabs(rd - oneReading) > 0.005) ok = false;
        }
        check(ok, "both modes trip a clean swing within ±0.5 points of 2.28 % at ρ = 0, 0.3, 0.6");
    }

    // V6: σ propagation against a Monte Carlo of the measurement errors.
    std::printf("=== composite: σ propagation against a Monte Carlo ===\n");
    {
        const auto mcSigma = [](std::vector<CompositeTerm> t, double rho, bool distance) {
            DetRng rng(7);
            const std::vector<CompositeTerm> base = t;
            constexpr int n = 40000;
            double s = 0.0, s2 = 0.0;
            for (int i = 0; i < n; ++i) {
                for (size_t k = 0; k < t.size(); ++k) t[k].z = base[k].z + base[k].sigmaRatio * rng.normal();
                const double z = distance ? combineDistance(t, rho).z : combineCoherent(t, rho).z;
                s += z; s2 += z * z;
            }
            const double m = s / n;
            return std::sqrt(s2 / n - m * m);
        };
        std::vector<CompositeTerm> coh = { { 1.2, 0.9, -1, 0.20 }, { 0.5, 0.8, 1, 0.30 },
                                           { -0.3, 0.6, 1, 0.10 }, { 2.0, 0.4, 1, 0.25 } };
        const double sc = combineCoherent(coh, 0.3).sigma, mc = mcSigma(coh, 0.3, false);
        std::printf("      coherent σ %.4f, Monte Carlo %.4f\n", sc, mc);
        check(std::fabs(sc / mc - 1.0) < 0.03, "coherent σ_Z matches the Monte Carlo within 3 %");

        std::vector<CompositeTerm> dis = { { 2.5, 0.9, 1, 0.10 }, { 1.5, 0.7, 1, 0.10 },
                                           { -1.0, 0.6, 1, 0.10 }, { 0.8, 0.6, 1, 0.10 } };
        const double sd = combineDistance(dis, 0.3).sigma, md = mcSigma(dis, 0.3, true);
        std::printf("      distance σ %.4f, Monte Carlo %.4f\n", sd, md);
        check(std::fabs(sd / md - 1.0) < 0.10, "distance σ_Z (delta method, numerical dZ/dQ) within 10 % of the Monte Carlo");

        coh[2].sigmaRatio = std::numeric_limits<double>::quiet_NaN();
        check(std::isnan(combineCoherent(coh, 0.3).sigma) && std::isnan(combineDistance(coh, 0.3).sigma),
              "one part with no σ leaves the composite unquantified");
    }

    // ── Through the source and the engine ──────────────────────────────────────────────────────
    //
    // A pack shaped like §6.1's example: four coherent parts, the trunk an alternative ladder with
    // OPPOSITE signs (DTL bend falls as face-on trunk rise grows), and the family's two tails.
    const auto partMeasure = [](const char *id, const char *unit) {
        Measure m;
        m.id     = QLatin1String(id);
        m.kind   = MeasureKind::Provided;
        m.unit   = QLatin1String(unit);
        m.status = MeasureStatus::Live;
        return m;
    };
    CharacteristicPack cpack;
    cpack.measures.push_back(partMeasure("m_bend", "deg"));
    cpack.measures.push_back(partMeasure("m_trunkRise", "%"));
    cpack.measures.push_back(partMeasure("m_thrust", "cm"));
    cpack.measures.push_back(partMeasure("m_head", "cm"));
    cpack.measures.push_back(partMeasure("m_pelvisLift", "%"));
    cpack.measures.push_back(partMeasure("m_knee", "deg"));
    cpack.measures.push_back(partMeasure("m_balance", "%"));
    cpack.measures.push_back(partMeasure("m_arms", "deg"));
    {
        Measure held;
        held.id     = QStringLiteral("m_held");
        held.kind   = MeasureKind::Composite;
        held.unit   = QStringLiteral("z");
        held.status = MeasureStatus::Live;
        held.composite.combine   = CompositeCombine::Coherent;
        held.composite.minWeight = 0.5;
        held.composite.parts = {
            { QStringLiteral("trunk"), { QStringLiteral("m_bend"), QStringLiteral("m_trunkRise") }, { -1.0, 1.0 }, 0.9, false },
            { QStringLiteral("pelvisDepth"),  { QStringLiteral("m_thrust") },     { 1.0 }, 0.8, false },
            { QStringLiteral("head"),         { QStringLiteral("m_head") },       { 1.0 }, 0.6, false },
            { QStringLiteral("pelvisHeight"), { QStringLiteral("m_pelvisLift") }, { 1.0 }, 0.4, false },
        };
        cpack.measures.push_back(held);

        Measure setup;
        setup.id     = QStringLiteral("m_setup");
        setup.kind   = MeasureKind::Composite;
        setup.unit   = QStringLiteral("z");
        setup.status = MeasureStatus::Live;
        setup.shape  = Shape::Ceiling;
        setup.composite.combine   = CompositeCombine::Distance;
        setup.composite.minWeight = 0.5;
        setup.composite.parts = {
            { QStringLiteral("hinge"),   { QStringLiteral("m_bend") },    {}, 0.9, true },
            { QStringLiteral("knee"),    { QStringLiteral("m_knee") },    {}, 0.7, false },
            { QStringLiteral("balance"), { QStringLiteral("m_balance") }, {}, 0.6, false },
            { QStringLiteral("arms"),    { QStringLiteral("m_arms") },    {}, 0.6, false },
        };
        cpack.measures.push_back(setup);

        const auto sig = [&](const char *id, const char *m, Direction d) {
            Signal s;
            s.id = QLatin1String(id); s.test = SignalTest::OutsideCorridor;
            s.measures = { QLatin1String(m) }; s.direction = d;
            cpack.signalDefs.push_back(s);
        };
        sig("sig_loss", "m_held", Direction::High);
        sig("sig_sinks", "m_held", Direction::Low);
        sig("sig_poorSetup", "m_setup", Direction::High);
        const auto cond = [&](const char *id, const char *signal) {
            Condition c;
            c.id = QLatin1String(id); c.label = c.id; c.state = ConditionState::Active;
            c.detectedBy = { QLatin1String(signal) };
            cpack.conditions.push_back(c);
        };
        cond("loss_of_posture", "sig_loss");
        cond("posture_sinks", "sig_sinks");
        cond("poor_setup", "sig_poorSetup");
    }

    auto cnorms = std::make_shared<FakeNorms>();
    cnorms->add("m_bend", "full_swing", 0.0, 2.0);
    {
        Norm bend;                                   // the bend's row again, with a plausibility floor
        bend.measureId = QStringLiteral("m_bend"); bend.contextId = QStringLiteral("full_swing");
        bend.mu = 0.0; bend.sigmaLo = bend.sigmaHi = 2.0; bend.plausibleLo = -40.0; bend.plausibleHi = 40.0;
        cnorms->addNorm(bend);
    }
    cnorms->add("m_trunkRise", "full_swing", 0.0, 2.5);
    cnorms->add("m_thrust", "full_swing", 0.0, 2.0);
    cnorms->add("m_head", "full_swing", 0.0, 1.5);
    cnorms->add("m_pelvisLift", "full_swing", 0.0, 1.0);
    cnorms->add("m_knee", "full_swing", 20.0, 4.0);
    cnorms->add("m_balance", "full_swing", 55.0, 5.0);
    cnorms->add("m_arms", "full_swing", 0.0, 3.0);
    cnorms->addComposite("m_held", 0.3);
    cnorms->addComposite("m_setup", 0.0);
    const QString FS = QStringLiteral("full_swing");

    // Every part 1.5 σ toward a loss: bend −3° (z −1.5, sign −1), thrust 3 cm, head 2.25 cm, pelvis 1.5 %.
    const auto lossAll = [](FakeValues &v) {
        v.set(QStringLiteral("m_bend"), -3.0);
        v.set(QStringLiteral("m_thrust"), 3.0);
        v.set(QStringLiteral("m_head"), 2.25);
        v.set(QStringLiteral("m_pelvisLift"), 1.5);
    };

    std::printf("=== composite: read through NormMeasureSource ===\n");
    {
        FakeValues v;
        lossAll(v);
        const NormMeasureSource src(v, cnorms, FS, GradePolicy{}, &cpack);
        const auto r = src.read(QStringLiteral("m_held"));
        check(r && r->hasCorridor && !r->compositeShort, "a composite with every part read has a corridor");
        check(r && twoDp(r->value, 2.14), "four moderate losses at ρ = 0.3 read 2.14 — the §4.3 cell");
        check(r && r->grade == Grade::Watch, "…graded against its own μ 0 σ 1 norm: Watch");
        check(r && r->parts.size() == 4 && r->partsUnread.empty()
                  && r->partsWeightRead == r->partsWeightTotal && std::fabs(r->partsWeightTotal - 2.7) < 1e-12,
              "four parts recorded, none unread, 2.7 of 2.7 weight");
        check(r && r->parts[0].name == QLatin1String("trunk") && r->parts[0].measureId == QLatin1String("m_bend")
                  && r->parts[0].sign == -1 && r->parts[0].unit == QLatin1String("deg")
                  && r->parts[0].value == -3.0 && std::fabs(r->parts[0].z + 1.5) < 1e-12,
              "a part carries its alternative, sign, unit, raw value and side-σ z");
        double sum = 0.0;
        for (const CompositePart &pt : r ? r->parts : std::vector<CompositePart>{}) sum += pt.share;
        check(r && std::fabs(sum - 1.0) < 1e-12 && std::fabs(r->parts[0].share - 0.9 / 2.7) < 1e-12,
              "shares are wsz over the sum (equal z: each part's weight over 2.7)");

        const DetectionResult d = detect(cpack, src);
        const Finding *f = d.find(QStringLiteral("loss_of_posture"));
        check(f && f->state == FindingState::Fired, "the family FIRES on four parts none of which would alone");
        check(f && f->evidence.parts.size() == 4 && f->evidence.partsWeightTotal > 2.69,
              "…and the finding's evidence carries the parts the detail page draws");
        const Finding *sk = d.find(QStringLiteral("posture_sinks"));
        check(sk && sk->state == FindingState::NotFired, "the other tail is assessed and clean");
    }

    std::printf("=== composite: alternative ladders, opposite signs ===\n");
    {
        FakeValues v;
        lossAll(v);
        v.clear(QStringLiteral("m_bend"));
        v.set(QStringLiteral("m_trunkRise"), 3.75);   // z +1.5: the trunk imaged LONGER, i.e. standing up
        const NormMeasureSource src(v, cnorms, FS, GradePolicy{}, &cpack);
        const auto r = src.read(QStringLiteral("m_held"));
        check(r && twoDp(r->value, 2.14) && r->parts[0].measureId == QLatin1String("m_trunkRise")
                  && r->parts[0].sign == 1,
              "no DTL bend: the face-on trunk rise answers for the part, with ITS sign — same composite");

        FakeValues w;
        lossAll(w);
        w.set(QStringLiteral("m_bend"), -55.0);       // past the bend's plausibility floor
        w.set(QStringLiteral("m_trunkRise"), 3.75);
        const NormMeasureSource src2(w, cnorms, FS, GradePolicy{}, &cpack);
        const auto r2 = src2.read(QStringLiteral("m_held"));
        check(r2 && r2->parts[0].measureId == QLatin1String("m_trunkRise"),
              "an implausible first alternative is a part not read by THAT rung — the ladder moves on");

        FakeValues u;
        lossAll(u);
        u.clear(QStringLiteral("m_bend"));
        const NormMeasureSource src3(u, cnorms, FS, GradePolicy{}, &cpack);
        const auto r3 = src3.read(QStringLiteral("m_held"));
        check(r3 && r3->hasCorridor && r3->partsUnread.size() == 1
                  && r3->partsUnread[0].name == QLatin1String("trunk")
                  && r3->partsUnread[0].measureId == QLatin1String("m_trunkRise")
                  && r3->partsUnread[0].reason == QLatin1String("notMeasured")
                  && std::fabs(r3->partsWeightRead - 1.8) < 1e-12,
              "neither rung read: the part is unread, blamed on the LAST rung, and 1.8 of 2.7 still covers");
        // Calibrated as a three-part reading: the denominator from the three read, never a zero for the fourth.
        std::vector<CompositeTerm> three = { { 1.5, 0.8, 1, std::numeric_limits<double>::quiet_NaN() },
                                             { 1.5, 0.6, 1, std::numeric_limits<double>::quiet_NaN() },
                                             { 1.5, 0.4, 1, std::numeric_limits<double>::quiet_NaN() } };
        check(r3 && std::fabs(r3->value - combineCoherent(three, 0.3).z) < 1e-12,
              "…and is combined over the three parts read, its denominator theirs alone");
    }

    std::printf("=== composite: coverage and required parts ===\n");
    {
        FakeValues v;
        v.set(QStringLiteral("m_head"), 2.25);
        v.set(QStringLiteral("m_pelvisLift"), 1.5);          // 1.0 of 2.7 — 37 %
        const NormMeasureSource src(v, cnorms, FS, GradePolicy{}, &cpack);
        const auto r = src.read(QStringLiteral("m_held"));
        check(r && r->compositeShort && !r->hasCorridor && r->partsUnread.size() == 2,
              "37 % of the weight read: too few parts, never graded with the missing ones as zero");
        const DetectionResult d = detect(cpack, src);
        const Finding *f = d.find(QStringLiteral("loss_of_posture"));
        check(f && f->state == FindingState::Unavailable && f->refusal == SignalRefusal::CompositeCoverage
                  && f->missingMeasures.contains(QStringLiteral("m_held")),
              "the family is UNAVAILABLE, refused as CompositeCoverage");
        check(f && QLatin1String(signalRefusalRuleId(f->refusal)) == QLatin1String("rule.diag.compositeCoverage"),
              "…under the gate rule rule.diag.compositeCoverage");
        check(evaluateSignal(cpack, QStringLiteral("sig_loss"), src).refusal == SignalRefusal::CompositeCoverage,
              "evaluateSignal reports the same refusal");

        FakeValues dtl;                                        // DTL only: bend + thrust, 1.7 of 2.7
        dtl.set(QStringLiteral("m_bend"), -6.0);
        dtl.set(QStringLiteral("m_thrust"), 0.0);
        const NormMeasureSource s2(dtl, cnorms, FS, GradePolicy{}, &cpack);
        const auto r2 = s2.read(QStringLiteral("m_held"));
        check(r2 && !r2->compositeShort && r2->hasCorridor && r2->parts.size() == 2,
              "63 % of the weight read (the DTL-only case of §4.6): the composite reads");

        // Distance mode: the trunk hinge is REQUIRED (§5.3).
        FakeValues noHinge;
        noHinge.set(QStringLiteral("m_knee"), 27.2);
        noHinge.set(QStringLiteral("m_balance"), 64.0);
        noHinge.set(QStringLiteral("m_arms"), 5.4);           // 1.9 of 2.8 — plenty, but no hinge
        const NormMeasureSource s3(noHinge, cnorms, FS, GradePolicy{}, &cpack);
        const auto r3 = s3.read(QStringLiteral("m_setup"));
        check(r3 && r3->compositeShort, "a required part missing refuses the composite whatever else was read");

        FakeValues all;                                        // every part 1.8 σ out
        all.set(QStringLiteral("m_bend"), 3.6);
        all.set(QStringLiteral("m_knee"), 27.2);
        all.set(QStringLiteral("m_balance"), 64.0);
        all.set(QStringLiteral("m_arms"), -5.4);               // the other way: it squares
        const NormMeasureSource s4(all, cnorms, FS, GradePolicy{}, &cpack);
        const auto r4 = s4.read(QStringLiteral("m_setup"));
        check(r4 && twoDp(r4->value, 2.25) && r4->grade == Grade::Watch && r4->highOpen == false && r4->lowOpen,
              "the setup composite reads 2.25 (§5.2's cell), as a ceiling: only the high tail grades");
        check(r4 && r4->parts.size() == 4 && r4->parts[3].sign == 0 && std::fabs(r4->parts[3].share - 0.6 / 2.8) < 1e-12,
              "distance parts carry no sign, and share wz² over Q");
        const DetectionResult d4 = detect(cpack, s4);
        check(d4.find(QStringLiteral("poor_setup")) && d4.find(QStringLiteral("poor_setup"))->state == FindingState::Fired,
              "poor setup fires");
    }

    std::printf("=== composite: no calibration, no corridor ===\n");
    {
        auto noRho = std::make_shared<FakeNorms>();
        noRho->add("m_bend", "full_swing", 0.0, 2.0);
        noRho->add("m_thrust", "full_swing", 0.0, 2.0);
        noRho->add("m_head", "full_swing", 0.0, 1.5);
        noRho->add("m_pelvisLift", "full_swing", 0.0, 1.0);
        noRho->addComposite("m_held", std::nullopt);
        FakeValues v;
        lossAll(v);
        const NormMeasureSource src(v, noRho, FS, GradePolicy{}, &cpack);
        const auto r = src.read(QStringLiteral("m_held"));
        check(r && !r->hasCorridor && !r->compositeShort && std::isnan(r->value) && r->parts.size() == 4,
              "a composite norm with no ρ: the parts read, but no number is formed and no corridor drawn");
        const DetectionResult dr = detect(cpack, src);
        const Finding *f = dr.find(QStringLiteral("loss_of_posture"));
        check(f && f->state == FindingState::Unavailable && f->refusal == SignalRefusal::NoCorridor,
              "…so the family is unavailable for want of a range, never graded under a ρ nobody measured");

        // A part whose own norm is missing is a part not read, with that reason.
        auto partial = std::make_shared<FakeNorms>();
        partial->add("m_bend", "full_swing", 0.0, 2.0);
        partial->add("m_thrust", "full_swing", 0.0, 2.0);
        partial->add("m_head", "full_swing", 0.0, 1.5);
        partial->addComposite("m_held", 0.3);
        const NormMeasureSource s2(v, partial, FS, GradePolicy{}, &cpack);
        const auto r2 = s2.read(QStringLiteral("m_held"));
        check(r2 && r2->partsUnread.size() == 1 && r2->partsUnread[0].reason == QLatin1String("noCorridor"),
              "a part with a value and no norm is unread as noCorridor");
    }

    std::printf("=== composite: σ and the endpoints, through the source ===\n");
    {
        FakeValues v;
        const auto withSigma = [&](const char *id, double value, double sigma) {
            IMeasureValueSource::Value x;
            x.value = value; x.sigma = sigma; x.sigmaSrc = SigmaSource::Series;
            v.setFull(QLatin1String(id), x);
        };
        IMeasureValueSource::Value bend;
        bend.value = -3.0; bend.sigma = 0.6; bend.sigmaSrc = SigmaSource::Reading;
        bend.atStart = 34.0; bend.atEnd = 31.0;
        v.setFull(QStringLiteral("m_bend"), bend);
        withSigma("m_thrust", 3.0, 0.5);
        withSigma("m_head", 2.25, 0.3);
        withSigma("m_pelvisLift", 1.5, 0.2);
        const NormMeasureSource src(v, cnorms, FS, GradePolicy{}, &cpack);
        const auto r = src.read(QStringLiteral("m_held"));
        const std::vector<CompositeTerm> t = { { -1.5, 0.9, -1, 0.6 / 2.0 }, { 1.5, 0.8, 1, 0.5 / 2.0 },
                                               { 1.5, 0.6, 1, 0.3 / 1.5 }, { 1.5, 0.4, 1, 0.2 / 1.0 } };
        check(r && std::isfinite(r->measSigma) && std::fabs(r->measSigma - combineCoherent(t, 0.3).sigma) < 1e-12,
              "the composite's measurement σ is propagated from the parts' σ over their norm σ");
        check(r && r->sigmaSrc == SigmaSource::Series, "…and carries the WEAKEST part's σ source");
        check(r && r->parts[0].atStart == 34.0 && r->parts[0].atEnd == 31.0 && std::isnan(r->parts[1].atStart),
              "a change part carries both ends of the series (address 34° → impact 31°); an instant does not");

        v.set(QStringLiteral("m_head"), 2.25);   // no σ on one part
        const NormMeasureSource s2(v, cnorms, FS, GradePolicy{}, &cpack);
        const auto r2 = s2.read(QStringLiteral("m_held"));
        check(r2 && std::isnan(r2->measSigma) && r2->sigmaSrc == SigmaSource::None,
              "one part with no σ leaves the composite unquantified — the verdict stays hard");
    }

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail == 0 ? 0 : 1;
}
