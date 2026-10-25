/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// THE CHARTS PANEL'S SESSION MATHS (src/Gui/charts/session_{curves,warp,traces,relations,sequence}).
//
// Three kinds of case:
//   golden*   — the C++ against the MOCKS' OWN numbers on the 4 July fixture. goldens_0704.json is made
//               by build/charts-build/goldens/make_goldens.js, which runs variant_a/lib/traces.js and
//               variant_b/lib/Stats.js verbatim plus the TraceModel / Bench / SeqSession logic. These run
//               with WarpOptions::gates = false and the Linear warp: the mocks' ladder exactly.
//   fixture*  — the build's own rules on the same data (gates on, PCHIP): which anchors the gates drop,
//               the warp's properties, the landmarks against the real P-times.
//   synth*    — every gate and edge on hand-built sessions.

#include "charts/session_bridge.h"
#include "charts/session_curves.h"
#include "charts/session_relations.h"
#include "charts/session_sequence.h"
#include "charts/session_traces.h"
#include "charts/session_warp.h"
#include "charts_maths_fixture.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <cmath>
#include <random>

using namespace pinpoint::charts;
using charts_test::Fixture;

namespace {

const QString kDataDir = QStringLiteral(CHARTS_MATHS_DATA);

// Golden agreement: the same arithmetic in another language, so equal to the last few bits.
constexpr double kTol = 1e-9;

bool same(double got, const QJsonValue &want, double tol = kTol)
{
    if (want.isNull() || want.isUndefined()) return std::isnan(got);
    const double w = want.toDouble();
    if (std::isnan(got)) return false;
    return std::abs(got - w) <= tol * std::max(1.0, std::abs(w));
}

QString fmt(double got, const QJsonValue &want)
{
    return QStringLiteral("got %1 want %2").arg(got, 0, 'g', 17).arg(want.isNull() ? QStringLiteral("null")
                                                                                   : QString::number(want.toDouble(), 'g', 17));
}

#define CHECK_SAME(got, want, what) \
    QVERIFY2(same((got), (want)), qPrintable(QStringLiteral("%1: %2").arg(what, fmt((got), (want)))))

std::vector<double> doubles(const QJsonValue &v)
{
    std::vector<double> out;
    for (const QJsonValue &x : v.toArray()) out.push_back(x.isDouble() ? x.toDouble() : kNaN);
    return out;
}
std::vector<int> ints(const QJsonValue &v)
{
    std::vector<int> out;
    for (const QJsonValue &x : v.toArray()) out.push_back(x.toInt());
    return out;
}

void checkSeries(const std::vector<double> &got, const QJsonValue &want, const QString &what)
{
    const QJsonArray w = want.toArray();
    QCOMPARE(int(got.size()), int(w.size()));
    for (int i = 0; i < w.size(); ++i)
        QVERIFY2(same(got[size_t(i)], w.at(i)), qPrintable(QStringLiteral("%1[%2]: %3").arg(what).arg(i).arg(fmt(got[size_t(i)], w.at(i)))));
}
void checkInts(const std::vector<int> &got, const QJsonValue &want, const QString &what)
{
    const std::vector<int> w = ints(want);
    QVERIFY2(got == w, qPrintable(what));
}

// ── synthetic sessions ───────────────────────────────────────────────────────────────────────

constexpr qint64 kImpactUs = 2'000'000;

// A canonical ladder, ms from impact (P1 P2 P3 P4 P5 P6 P7 P8 P10).
const std::vector<std::pair<int, double>> kLadderMs { { 0, -950 }, { 12, -700 }, { 8, -550 }, { 2, -280 }, { 13, -100 },
                                                      { 9, -45 }, { 5, 0 }, { 14, 85 }, { 7, 270 } };

std::shared_ptr<ShotSeries> makeShot(int ordinal, double tempo = 1.0, bool phases = true)
{
    auto s = std::make_shared<ShotSeries>();
    s->ordinal = ordinal; s->shotId = ordinal; s->club = QStringLiteral("7 IRON"); s->impactUs = kImpactUs;
    for (const auto &[ph, ms] : kLadderMs)
        if (phases || ph == 5)
            s->phases.push_back(PhaseMark{ ph, kImpactUs + qint64(std::llround(ms * tempo * 1000.0)), 0.7 });
    return s;
}

void setPhase(ShotSeries &s, int ph, double ms, double conf = 0.7)
{
    for (PhaseMark &p : s.phases) if (p.phase == ph) { p.tUs = kImpactUs + qint64(std::llround(ms * 1000.0)); p.conf = conf; return; }
    s.phases.push_back(PhaseMark{ ph, kImpactUs + qint64(std::llround(ms * 1000.0)), conf });
}
void removePhase(ShotSeries &s, int ph)
{
    s.phases.erase(std::remove_if(s.phases.begin(), s.phases.end(), [ph](const PhaseMark &p) { return p.phase == ph; }), s.phases.end());
}

// A curve from a function of shot ms, sampled every `stepMs` over [t0, t1].
MetricSeries curveOf(const QString &key, const std::function<double(double)> &f, double t0, double t1, double stepMs = 4.0)
{
    MetricSeries m;
    m.key = key; m.label = key; m.unit = QStringLiteral("mph");
    for (double t = t0; t <= t1 + 1e-9; t += stepMs) {
        m.tUs.push_back(kImpactUs + qint64(std::llround(t * 1000.0)));
        m.value.push_back(f(t));
    }
    return m;
}

double gauss(double t, double c, double w) { return std::exp(-(t - c) * (t - c) / (2 * w * w)); }
// A hand-speed-like curve with a backswing hump, a dip at the top and a downswing peak; `k` stretches time.
double handLike(double t, double k)
{
    const double x = t / k;
    return 8.0 * gauss(x, -600, 120) + 20.0 * gauss(x, -60, 45) + 0.2;
}

void addReading(ShotSeries &s, const QString &key, int ph, double v, const QString &unit = QStringLiteral("mph"))
{
    MetricSeries &m = s.metrics[key];
    m.key = key; m.label = key; m.unit = unit;
    PhaseSample p; p.phase = ph; p.value = v;
    m.phaseSamples.push_back(p);
    if (!s.metricOrder.contains(key)) s.metricOrder << key;
}

SessionSnapshot snapshotOf(const std::vector<std::shared_ptr<ShotSeries>> &shots)
{
    SessionSnapshot s;
    for (const auto &sh : shots) s.shots.push_back(sh);
    return s;
}

const GatedAnchor *considered(const ShotWarp &w, int ph)
{
    for (const GatedAnchor &g : w.considered) if (g.phase == ph) return &g;
    return nullptr;
}

} // namespace

class ChartsSessionMathsTest : public QObject
{
    Q_OBJECT

    Fixture     m_fx;
    QJsonObject m_g;
    SessionAlignment m_mock;     // gates off, Linear: the mocks' ladder
    QStringList m_ks { QStringLiteral("pelvisAngularSpeed"), QStringLiteral("thoraxAngularSpeed"),
                       QStringLiteral("leadArmAngularSpeed"), QStringLiteral("clubAngularSpeed") };

    double canonT(int ph) const { const CanonicalPhase *c = m_mock.canonicalOf(ph); return c ? c->tMs : kNaN; }
    std::vector<int> allOrds() const { std::vector<int> o; for (const auto &s : m_fx.session.shots) o.push_back(s->ordinal); return o; }

private slots:
    void initTestCase()
    {
        m_fx = charts_test::loadFixture(kDataDir + QStringLiteral("/session_0704.json"));
        QVERIFY2(m_fx.ok, "the 4 July test fixture did not load");
        QFile f(kDataDir + QStringLiteral("/goldens_0704.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        m_g = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!m_g.isEmpty());
        WarpOptions o; o.gates = false; o.interp = WarpInterp::Linear;
        m_mock = alignSession(m_fx.session, o);
    }

    // ═════════════════════════════ GOLDEN: the mocks' numbers ═════════════════════════════

    void goldenCanonical()
    {
        const QJsonArray want = m_g.value(QStringLiteral("canonical")).toArray();
        QCOMPARE(int(m_mock.canonical.size()), want.size());
        for (int i = 0; i < want.size(); ++i) {
            const QJsonObject w = want.at(i).toObject();
            const CanonicalPhase *c = m_mock.canonicalOf(w.value(QStringLiteral("phase")).toInt());
            QVERIFY(c);
            CHECK_SAME(c->tMs, w.value(QStringLiteral("t")), QStringLiteral("t"));
            CHECK_SAME(c->q1Ms, w.value(QStringLiteral("q1")), QStringLiteral("q1"));
            CHECK_SAME(c->q3Ms, w.value(QStringLiteral("q3")), QStringLiteral("q3"));
            CHECK_SAME(c->loMs, w.value(QStringLiteral("lo")), QStringLiteral("lo"));
            CHECK_SAME(c->hiMs, w.value(QStringLiteral("hi")), QStringLiteral("hi"));
            QCOMPARE(c->n, w.value(QStringLiteral("n")).toInt());
        }
    }

    void goldenLinearWarp()
    {
        for (const QJsonValue &v : m_g.value(QStringLiteral("anchors")).toArray()) {
            const QJsonObject w = v.toObject();
            const ShotWarp *sw = m_mock.warpFor(w.value(QStringLiteral("ordinal")).toInt());
            QVERIFY(sw);
            const QJsonArray an = w.value(QStringLiteral("anchors")).toArray();
            QCOMPARE(int(sw->anchors.size()), an.size());
            for (int i = 0; i < an.size(); ++i) {
                CHECK_SAME(sw->anchors[size_t(i)].shotMs, an.at(i).toArray().at(0), QStringLiteral("anchor shot"));
                CHECK_SAME(sw->anchors[size_t(i)].canonMs, an.at(i).toArray().at(1), QStringLiteral("anchor canon"));
            }
        }
        for (const QJsonValue &v : m_g.value(QStringLiteral("warp")).toArray()) {
            const QJsonObject w = v.toObject();
            const ShotWarp *sw = m_mock.warpFor(w.value(QStringLiteral("ordinal")).toInt());
            const std::vector<double> t = doubles(w.value(QStringLiteral("t")));
            const QJsonArray wa = w.value(QStringLiteral("warp")).toArray(), ua = w.value(QStringLiteral("unwarp")).toArray();
            for (size_t i = 0; i < t.size(); ++i) {
                CHECK_SAME(sw->warpMs(t[i]), wa.at(int(i)), QStringLiteral("warp"));
                CHECK_SAME(sw->unwarpMs(t[i]), ua.at(int(i)), QStringLiteral("unwarp"));
            }
        }
    }

    void goldenEnvelopeAndPart()
    {
        for (const QJsonValue &v : m_g.value(QStringLiteral("envelopes")).toArray()) {
            const QJsonObject w = v.toObject();
            const QString key = w.value(QStringLiteral("key")).toString();
            const double t0 = w.value(QStringLiteral("t0")).toDouble(), t1 = w.value(QStringLiteral("t1")).toDouble();
            const std::vector<TraceCurve> cs = alignedCurves(m_fx.session, m_mock, key);
            const SessionEnvelope e = envelope(curvesOf(cs), sessionGrid(t0, t1));
            checkSeries(e.t, w.value(QStringLiteral("t")), key + QStringLiteral(" t"));
            checkSeries(e.q1, w.value(QStringLiteral("q1")), key + QStringLiteral(" q1"));
            checkSeries(e.med, w.value(QStringLiteral("med")), key + QStringLiteral(" med"));
            checkSeries(e.q3, w.value(QStringLiteral("q3")), key + QStringLiteral(" q3"));
            checkSeries(e.lo, w.value(QStringLiteral("lo")), key + QStringLiteral(" lo"));
            checkSeries(e.hi, w.value(QStringLiteral("hi")), key + QStringLiteral(" hi"));
            checkInts(e.n, w.value(QStringLiteral("n")), key + QStringLiteral(" n"));
            QCOMPARE(e.count, w.value(QStringLiteral("count")).toInt());
            const PartingPoint p = partAt(e, e.count);
            const QJsonValue wp = w.value(QStringLiteral("part"));
            QCOMPARE(p.found, !wp.isNull());
            if (p.found) {
                const QJsonObject o = wp.toObject();
                CHECK_SAME(p.tCanonMs, o.value(QStringLiteral("t")), key + QStringLiteral(" part t"));
                CHECK_SAME(p.iqr, o.value(QStringLiteral("iqr")), key + QStringLiteral(" part iqr"));
                CHECK_SAME(p.med, o.value(QStringLiteral("med")), key + QStringLiteral(" part med"));
                QCOMPARE(p.n, o.value(QStringLiteral("n")).toInt());
            }
        }
    }

    void goldenPooledFit()
    {
        for (const QJsonValue &v : m_g.value(QStringLiteral("fits")).toArray()) {
            const QJsonObject w = v.toObject();
            const QString key = w.value(QStringLiteral("key")).toString();
            const std::vector<int> ords = ints(w.value(QStringLiteral("ordinals")));
            const double t0 = w.value(QStringLiteral("t0")).toDouble(), t1 = w.value(QStringLiteral("t1")).toDouble();
            FitOptions mocks; mocks.requireCoverage = false;          // the mocks had no coverage rule
            const GroupFit f = pooledFit(alignedCurves(m_fx.session, m_mock, key), ords, sessionGrid(t0, t1), mocks);
            const QString what = key + QLatin1Char('/') + w.value(QStringLiteral("group")).toString();
            QVERIFY(!f.refused());
            QCOMPARE(f.groupSize, w.value(QStringLiteral("count")).toInt());
            QCOMPARE(f.samplesPooled, w.value(QStringLiteral("samples")).toInt());
            checkSeries(f.t, w.value(QStringLiteral("t")), what + QStringLiteral(" t"));
            checkSeries(f.v, w.value(QStringLiteral("v")), what + QStringLiteral(" v"));
            checkSeries(f.sd, w.value(QStringLiteral("sd")), what + QStringLiteral(" sd"));
            checkSeries(f.se, w.value(QStringLiteral("se")), what + QStringLiteral(" se"));
            checkInts(f.nShots, w.value(QStringLiteral("nShots")), what + QStringLiteral(" nShots"));
            checkInts(f.nSamples, w.value(QStringLiteral("nSamples")), what + QStringLiteral(" nSamples"));
        }
    }

    void goldenAdaptiveFitIsBsLoess()
    {
        // B's 12–60 ms adaptive window, as an option of the one engine, against Stats.loess.
        for (const QJsonValue &v : m_g.value(QStringLiteral("loess")).toArray()) {
            const QJsonObject w = v.toObject();
            const QString key = w.value(QStringLiteral("key")).toString();
            const std::vector<int> ords = ints(w.value(QStringLiteral("ordinals")));
            FitOptions o; o.adaptive = true; o.minSamples = 0; o.requireCoverage = false;
            const GroupFit f = pooledFit(alignedCurves(m_fx.session, m_mock, key), ords, doubles(w.value(QStringLiteral("grid"))), o);
            checkSeries(f.v, w.value(QStringLiteral("m")), key + QStringLiteral(" loess m"));
            checkSeries(f.sd, w.value(QStringLiteral("sd")), key + QStringLiteral(" loess sd"));
            checkInts(f.nSamples, w.value(QStringLiteral("n")), key + QStringLiteral(" loess n"));
            checkInts(f.nShots, w.value(QStringLiteral("shots")), key + QStringLiteral(" loess shots"));
        }
    }

    void goldenCoverage()
    {
        const std::vector<TraceCurve> cs = alignedCurves(m_fx.session, m_mock, QStringLiteral("thoraxAngularSpeed"));
        const double t0 = canonT(2), t1 = canonT(14);
        for (const QJsonValue &v : m_g.value(QStringLiteral("coverage")).toArray()) {
            const QJsonObject w = v.toObject();
            const int ord = w.value(QStringLiteral("ordinal")).toInt();
            const std::vector<TimeSpan> runs = coverageRuns(cs[size_t(ord - 1)]);
            const QJsonArray wr = w.value(QStringLiteral("runs")).toArray();
            QCOMPARE(int(runs.size()), wr.size());
            for (int i = 0; i < wr.size(); ++i) {
                CHECK_SAME(runs[size_t(i)].t0, wr.at(i).toArray().at(0), QStringLiteral("run t0"));
                CHECK_SAME(runs[size_t(i)].t1, wr.at(i).toArray().at(1), QStringLiteral("run t1"));
            }
            const std::vector<TimeSpan> holes = coverageHoles(runs, t0, t1);
            const QJsonArray wh = w.value(QStringLiteral("holes")).toArray();
            QCOMPARE(int(holes.size()), wh.size());
            for (int i = 0; i < wh.size(); ++i) {
                CHECK_SAME(holes[size_t(i)].t0, wh.at(i).toArray().at(0), QStringLiteral("hole t0"));
                CHECK_SAME(holes[size_t(i)].t1, wh.at(i).toArray().at(1), QStringLiteral("hole t1"));
            }
        }
    }

    void goldenShapesAndTypical()
    {
        std::vector<std::vector<TraceCurve>> per;
        for (const QString &k : m_ks) per.push_back(alignedCurves(m_fx.session, m_mock, k));
        const double t0 = canonT(2), t1 = canonT(14);
        const ShapeSplit sp = shapeSplit(per, t0, t1);
        const QJsonObject w = m_g.value(QStringLiteral("shapeSplit")).toObject();
        QVERIFY(sp.found);
        QCOMPARE(sp.medoidA, w.value(QStringLiteral("a")).toInt());     // "shaped like shot 3"
        QCOMPARE(sp.medoidB, w.value(QStringLiteral("b")).toInt());     // "like shot 12"
        checkInts(sp.groupA, w.value(QStringLiteral("ga")), QStringLiteral("shape ga"));
        checkInts(sp.groupB, w.value(QStringLiteral("gb")), QStringLiteral("shape gb"));

        const int picked = m_g.value(QStringLiteral("picked")).toInt();
        const ShotScore ty = typicalShot(per, t0, t1, picked);
        const ShotScore fu = leastTypicalShot(per, t0, t1, picked);
        const QJsonObject wt = m_g.value(QStringLiteral("typical")).toObject(), wf = m_g.value(QStringLiteral("furthest")).toObject();
        QCOMPARE(ty.ordinal, wt.value(QStringLiteral("shot")).toInt());
        QCOMPARE(fu.ordinal, wf.value(QStringLiteral("shot")).toInt());
        CHECK_SAME(ty.score, wt.value(QStringLiteral("scores")).toObject().value(QString::number(ty.ordinal)), QStringLiteral("typical score"));
        CHECK_SAME(fu.score, wf.value(QStringLiteral("scores")).toObject().value(QString::number(fu.ordinal)), QStringLiteral("furthest score"));
    }

    void goldenGapAgainstTheMedian()
    {
        const QJsonObject w = m_g.value(QStringLiteral("compare")).toObject();
        const std::vector<TraceCurve> cs = alignedCurves(m_fx.session, m_mock, w.value(QStringLiteral("key")).toString());
        const double t0 = canonT(2), t1 = canonT(14), sp = t1 - t0;
        const SessionEnvelope e = envelope(curvesOf(cs), sessionGrid(t0, t1));
        const TraceCurve &c = cs[size_t(w.value(QStringLiteral("shot")).toInt() - 1)];
        const TraceGap g = gapAt(c, e.t, e.med, t0 + 0.1 * sp, t1 - 0.1 * sp);
        const QJsonObject wg = w.value(QStringLiteral("gap")).toObject();
        QVERIFY(g.found);
        CHECK_SAME(g.t, wg.value(QStringLiteral("t")), QStringLiteral("gap t"));
        CHECK_SAME(g.diff, wg.value(QStringLiteral("d")), QStringLiteral("gap d"));
        CHECK_SAME(g.value, wg.value(QStringLiteral("v")), QStringLiteral("gap v"));
        CHECK_SAME(g.ref, wg.value(QStringLiteral("ref")), QStringLiteral("gap ref"));
        checkSeries(diffFrom(c, e.t, e.med), w.value(QStringLiteral("diff")), QStringLiteral("diff"));
        CHECK_SAME(meanGap(c, e.t, e.med, t0, t1), w.value(QStringLiteral("meanGap")), QStringLiteral("meanGap"));
    }

    void goldenReadings()
    {
        const auto readAt = [this](const QString &k) { return m_fx.readAtOf(k); };
        const int pick = m_g.value(QStringLiteral("picked")).toInt() - 1;
        const std::vector<SessionReading> rs = sessionReadings(m_fx.session, readAt, m_fx.session.shots[size_t(pick)]->metricOrder);
        const QJsonArray want = m_g.value(QStringLiteral("readings")).toArray();
        QCOMPARE(int(rs.size()), want.size());                       // 68 on 4 July
        for (int i = 0; i < want.size(); ++i) {
            const QJsonObject w = want.at(i).toObject();
            const SessionReading &r = rs[size_t(i)];
            QCOMPARE(r.id, w.value(QStringLiteral("id")).toString());
            QCOMPARE(r.multi, w.value(QStringLiteral("multi")).toBool());
            checkSeries(r.values, w.value(QStringLiteral("values")), r.id);
            const QJsonObject b = w.value(QStringLiteral("box")).toObject();
            QCOMPARE(r.box.n, b.value(QStringLiteral("n")).toInt());
            CHECK_SAME(r.box.min, b.value(QStringLiteral("min")), r.id + QStringLiteral(" min"));
            CHECK_SAME(r.box.max, b.value(QStringLiteral("max")), r.id + QStringLiteral(" max"));
            CHECK_SAME(r.box.q1, b.value(QStringLiteral("q1")), r.id + QStringLiteral(" q1"));
            CHECK_SAME(r.box.med, b.value(QStringLiteral("med")), r.id + QStringLiteral(" med"));
            CHECK_SAME(r.box.q3, b.value(QStringLiteral("q3")), r.id + QStringLiteral(" q3"));
            CHECK_SAME(r.box.lo, b.value(QStringLiteral("lo")), r.id + QStringLiteral(" lo"));
            CHECK_SAME(r.box.hi, b.value(QStringLiteral("hi")), r.id + QStringLiteral(" hi"));
            checkInts(r.box.outliers, b.value(QStringLiteral("outliers")), r.id + QStringLiteral(" outliers"));
            const ReadingPlace p = placeOf(r, pick);
            CHECK_SAME(p.rz, w.value(QStringLiteral("rz")), r.id + QStringLiteral(" rz"));
            QCOMPARE(p.rankHi, w.value(QStringLiteral("rankHi")).toInt());
            QCOMPARE(p.rankLo, w.value(QStringLiteral("rankLo")).toInt());
            QCOMPARE(p.side, w.value(QStringLiteral("side")).toInt());
        }
        // the "apart" ledger, most extreme first (seven readings on shot 8)
        QStringList apart;
        for (int i : apartFrom(rs, pick)) apart << rs[size_t(i)].id;
        QStringList wantApart;
        for (const QJsonValue &v : m_g.value(QStringLiteral("apart")).toArray()) wantApart << v.toString();
        QCOMPARE(apart, wantApart);
    }

    void goldenCorrelationAndDrift()
    {
        const auto readAt = [this](const QString &k) { return m_fx.readAtOf(k); };
        const int pick = m_g.value(QStringLiteral("picked")).toInt() - 1;
        const std::vector<SessionReading> rs = sessionReadings(m_fx.session, readAt, m_fx.session.shots[size_t(pick)]->metricOrder);
        int y = -1;
        for (size_t i = 0; i < rs.size(); ++i) if (rs[i].id == QStringLiteral("clubheadSpeed@5")) y = int(i);
        QVERIFY(y >= 0);
        const auto check = [&](const std::vector<CorrRow> &got, const QJsonArray &want, bool withSame) {
            QCOMPARE(int(got.size()), want.size());
            for (int i = 0; i < want.size(); ++i) {
                const QJsonObject w = want.at(i).toObject();
                const CorrRow &c = got[size_t(i)];
                QCOMPARE(rs[size_t(c.reading)].id, w.value(QStringLiteral("id")).toString());
                QCOMPARE(c.c.n, w.value(QStringLiteral("n")).toInt());
                CHECK_SAME(c.c.r, w.value(QStringLiteral("r")), QStringLiteral("r"));
                CHECK_SAME(c.c.lo, w.value(QStringLiteral("lo")), QStringLiteral("lo"));
                CHECK_SAME(c.c.hi, w.value(QStringLiteral("hi")), QStringLiteral("hi"));
                CHECK_SAME(c.c.slope, w.value(QStringLiteral("slope")), QStringLiteral("slope"));
                CHECK_SAME(c.c.intercept, w.value(QStringLiteral("icpt")), QStringLiteral("icpt"));
                QCOMPARE(c.c.clear, w.value(QStringLiteral("clear")).toBool());
                if (withSame) QCOMPARE(c.same, w.value(QStringLiteral("same")).toBool());
            }
        };
        check(correlate(rs, y), m_g.value(QStringLiteral("corr")).toObject().value(QStringLiteral("rows")).toArray(), true);
        check(drifts(rs), m_g.value(QStringLiteral("drifts")).toArray(), false);
        for (const QJsonValue &v : m_g.value(QStringLiteral("rCritical")).toArray())
            CHECK_SAME(rCritical(v.toObject().value(QStringLiteral("n")).toInt()), v.toObject().value(QStringLiteral("r")), QStringLiteral("rCritical"));
        QVERIFY(std::abs(rCritical(15) - 0.51) < 0.005);              // "|r| under .51 is chance at n = 15"
        QCOMPARE(chanceCount(67), 3);                                 // "chance alone would clear about 3"
        for (const QJsonValue &v : m_g.value(QStringLiteral("dCritical")).toArray()) {
            const QJsonObject o = v.toObject();
            CHECK_SAME(dCritical(o.value(QStringLiteral("nA")).toInt(), o.value(QStringLiteral("nB")).toInt()), o.value(QStringLiteral("d")), QStringLiteral("dCritical"));
        }
    }

    void goldenKinds()
    {
        const auto readAt = [this](const QString &k) { return m_fx.readAtOf(k); };
        const int pick = m_g.value(QStringLiteral("picked")).toInt() - 1;
        const std::vector<SessionReading> rs = sessionReadings(m_fx.session, readAt, m_fx.session.shots[size_t(pick)]->metricOrder);
        const SessionKinds k = findKinds(rs);
        const QJsonObject w = m_g.value(QStringLiteral("kinds")).toObject();
        CHECK_SAME(k.silhouette, w.value(QStringLiteral("silhouette")), QStringLiteral("silhouette"));
        QVERIFY(std::abs(k.silhouette - 0.03) < 0.005);               // "no structure (silhouette .02 as shown)"
        QCOMPARE(k.method, KindsMethod::GapSplit);
        QCOMPARE(k.readingsUsed, w.value(QStringLiteral("readingsUsed")).toInt());
        QCOMPARE(k.minSize, w.value(QStringLiteral("minSize")).toInt());
        QCOMPARE(rs[size_t(k.splitReading)].id, w.value(QStringLiteral("by")).toString());   // clubheadSpeed@5
        const QJsonObject sp = w.value(QStringLiteral("split")).toObject();
        CHECK_SAME(k.splitGap, sp.value(QStringLiteral("gap")), QStringLiteral("gap"));
        CHECK_SAME(k.splitScore, sp.value(QStringLiteral("score")), QStringLiteral("score"));
        CHECK_SAME(k.splitCut, sp.value(QStringLiteral("cut")), QStringLiteral("cut"));
        CHECK_SAME(k.splitLow, sp.value(QStringLiteral("low")), QStringLiteral("low"));
        CHECK_SAME(k.splitHigh, sp.value(QStringLiteral("high")), QStringLiteral("high"));
        QVERIFY(k.splitGap > 7.5 && k.splitGap < 8.5);                // "an 8 mph clubhead-speed gap"
        checkInts(k.groupA, w.value(QStringLiteral("a")), QStringLiteral("kind a"));
        checkInts(k.groupB, w.value(QStringLiteral("b")), QStringLiteral("kind b"));
        QCOMPARE(int(k.groupA.size()), 6);                            // 6 faster
        QCOMPARE(int(k.groupB.size()), 9);                            // 9 slower
        QCOMPARE(k.medoidA, w.value(QStringLiteral("medoidA")).toInt());
        QCOMPARE(k.medoidB, w.value(QStringLiteral("medoidB")).toInt());
        checkInts(k.kmA.empty() ? std::vector<int>{} : std::vector<int>{ k.kmMedoidA, k.kmMedoidB },
                  w.value(QStringLiteral("kmMedoids")), QStringLiteral("k-medoids medoids"));
        CHECK_SAME(k.dCritical, w.value(QStringLiteral("dCritical")), QStringLiteral("dCritical"));
        const QJsonArray seps = w.value(QStringLiteral("seps")).toArray();
        QCOMPARE(int(k.separations.size()), seps.size());
        for (int i = 0; i < seps.size(); ++i) {
            const QJsonObject s = seps.at(i).toObject();
            const KindSeparation &g = k.separations[size_t(i)];
            QCOMPARE(rs[size_t(g.reading)].id, s.value(QStringLiteral("id")).toString());
            CHECK_SAME(g.d, s.value(QStringLiteral("d")), QStringLiteral("d"));
            CHECK_SAME(g.medA, s.value(QStringLiteral("medA")), QStringLiteral("medA"));
            CHECK_SAME(g.medB, s.value(QStringLiteral("medB")), QStringLiteral("medB"));
            QCOMPARE(g.nA, s.value(QStringLiteral("nA")).toInt());
            QCOMPARE(g.nB, s.value(QStringLiteral("nB")).toInt());
        }
    }

    void goldenFacets()
    {
        const std::vector<SessionFacet> fs = conditionFacets(m_fx.session);
        const QJsonArray want = m_g.value(QStringLiteral("facetsA")).toArray();
        QCOMPARE(int(fs.size()), want.size());
        for (int i = 0; i < want.size(); ++i) {
            QCOMPARE(fs[size_t(i)].id, want.at(i).toObject().value(QStringLiteral("id")).toString());
            QCOMPARE(fs[size_t(i)].firedCount, want.at(i).toObject().value(QStringLiteral("n")).toInt());
        }
        // the five that fire on (nearly) every shot are never offered
        const QJsonObject n = m_g.value(QStringLiteral("firedCounts")).toObject();
        for (auto it = n.begin(); it != n.end(); ++it)
            if (it.value().toInt() > m_fx.session.count() - 3)
                for (const SessionFacet &f : fs) QVERIFY(f.id != it.key());
        QVERIFY(!clubFacet(m_fx.session).has_value());                // one club: no club facet
        const SessionFacet h = halfFacet(m_fx.session);
        QCOMPARE(int(h.groups[0].ordinals.size()), 7);
        QCOMPARE(int(h.groups[1].ordinals.size()), 8);
    }

    void goldenSequence()
    {
        const SessionSequence sq = sessionSequence(m_fx.session);
        const QJsonObject w = m_g.value(QStringLiteral("sequence")).toObject();
        const QJsonArray shots = w.value(QStringLiteral("shots")).toArray();
        QCOMPARE(int(sq.shots.size()), shots.size());
        for (int i = 0; i < shots.size(); ++i) {
            const QJsonArray segs = shots.at(i).toObject().value(QStringLiteral("segments")).toArray();
            for (int j = 0; j < kSequenceSegments; ++j) {
                const QJsonObject s = segs.at(j).toObject();
                const SegmentPeak &p = sq.shots[size_t(i)].segments[size_t(j)];
                QCOMPARE(p.placed, s.value(QStringLiteral("placed")).toBool());
                if (p.placed) CHECK_SAME(p.shotMs, s.value(QStringLiteral("ms")), QStringLiteral("peak ms"));
                QCOMPARE(p.rank, s.value(QStringLiteral("rank")).toInt());
                QCOMPARE(p.late, s.value(QStringLiteral("late")).toBool());
            }
        }
        const QJsonArray segs = w.value(QStringLiteral("segments")).toArray();
        for (int j = 0; j < kSequenceSegments; ++j) {
            const QJsonObject s = segs.at(j).toObject();
            const SegmentSummary &m = sq.segments[size_t(j)];
            QCOMPARE(m.placed, s.value(QStringLiteral("placed")).toInt());
            QCOMPARE(m.afterImpact, s.value(QStringLiteral("after")).toInt());
            CHECK_SAME(m.medianMs, s.value(QStringLiteral("median")), QStringLiteral("median"));
            CHECK_SAME(m.q1Ms, s.value(QStringLiteral("q1")), QStringLiteral("q1"));
            CHECK_SAME(m.q3Ms, s.value(QStringLiteral("q3")), QStringLiteral("q3"));
        }
        QCOMPARE(sq.commonOrder, w.value(QStringLiteral("commonOrder")).toString());
        QCOMPARE(sq.commonOrderCount, w.value(QStringLiteral("commonOrderN")).toInt());
        // D4 on shot 8: only the pelvis is late, not all three placed segments
        const ShotSequence &s8 = sq.shots[7];
        QVERIFY(s8.segments[0].late);
        QVERIFY(!s8.segments[2].late && !s8.segments[3].late);
        QCOMPARE(sq.segments[0].afterImpact, 14);                     // the pelvis after impact on 14 of 14 placed
    }

    // ═════════════════════════════ FIXTURE: the build's rules ═════════════════════════════

    void fixtureGatesDropShot9FinishAndFallbackP8()
    {
        const SessionAlignment al = alignSession(m_fx.session);       // gates on, PCHIP
        QCOMPARE(al.countRoute(AlignRoute::PAnchors), 15);
        for (const ShotWarp &w : al.shots) {
            if (w.ordinal == 9) {
                QCOMPARE(w.droppedCount(), 2);
                QCOMPARE(considered(w, phase::P8)->gate, AnchorGate::LowConfidence);     // +99.6 ms, conf 0.30
                const GatedAnchor *fin = considered(w, phase::Finish);
                QCOMPARE(fin->gate, AnchorGate::OutlierSegment);                         // +964.7 ms
                QVERIFY(fin->segmentMs / fin->sessionSegmentMs > 3.0);
            } else {
                QVERIFY2(w.droppedCount() == 0, qPrintable(QStringLiteral("shot %1 dropped an anchor").arg(w.ordinal)));
            }
        }
        // every ladder P is canonical; P8 and P10 rest on 14 shots
        QCOMPARE(int(al.canonical.size()), 9);
        QCOMPARE(al.canonicalOf(phase::P8)->n, 14);
        QCOMPARE(al.canonicalOf(phase::Finish)->n, 14);
        QVERIFY(al.canonicalOf(phase::Finish)->hiMs < 300.0);        // shot 9's Finish no longer widens it
        QVERIFY(m_mock.canonicalOf(phase::Finish)->hiMs > 900.0);    // where the mock's did
    }

    void fixturePchipProperties()
    {
        const SessionAlignment al = alignSession(m_fx.session);
        for (const ShotWarp &w : al.shots) {
            for (const WarpAnchor &a : w.anchors) QCOMPARE(w.warpMs(a.shotMs), a.canonMs);   // lands every anchor exactly
            double prev = -std::numeric_limits<double>::infinity();
            for (double t = -1500; t <= 1200; t += 0.25) {
                const double c = w.warpMs(t);
                QVERIFY2(c > prev, qPrintable(QStringLiteral("shot %1 not strictly increasing at %2").arg(w.ordinal).arg(t)));
                prev = c;
                QVERIFY(std::abs(w.unwarpMs(c) - t) <= 1e-9);           // unwarp is exact
            }
            // the µs conversions round-trip through the shot's clock
            const qint64 us = w.impactUs - 123456;
            QCOMPARE(w.usAtCanonMs(w.canonMsAt(us)), us);
        }
    }

    void fixtureLandmarksTrackThePositions()
    {
        int dips = 0, peaks = 0;
        for (const auto &sh : m_fx.session.shots) {
            const std::vector<LandmarkFind> f = findLandmarks(shotCurve(*sh, QStringLiteral("handSpeed")));
            double top = kNaN;
            for (const PhaseMark &p : sh->phases) if (p.phase == phase::Top) top = double(p.tUs - sh->impactUs) / 1000.0;
            if (f[1].found) { ++dips; QVERIFY2(std::abs(f[1].shotMs - top) <= 15.0, qPrintable(QStringLiteral("shot %1 dip %2 vs top %3").arg(sh->ordinal).arg(f[1].shotMs).arg(top))); }
            if (f[2].found) { ++peaks; QVERIFY(f[2].shotMs > -120 && f[2].shotMs < 0); }
        }
        QVERIFY(dips >= 13);
        QVERIFY(peaks >= 10);
        // clubhead speed is bridged after impact, so its peak (≈ impact) has no measured right flank
        int clubPeaks = 0;
        for (const auto &sh : m_fx.session.shots)
            if (findLandmarks(shotCurve(*sh, QStringLiteral("clubheadSpeed")))[2].found) ++clubPeaks;
        QVERIFY(clubPeaks <= 2);
    }

    void fixtureImpactOnlyShotsGoByLandmarks()
    {
        // Strip every P from shots 3 and 11 (impact kept): the landmarks must carry them, close to
        // where their real P-positions would have put them.
        SessionSnapshot s = m_fx.session;
        const SessionAlignment full = alignSession(m_fx.session);
        for (int o : { 3, 11 }) {
            auto sh = std::make_shared<ShotSeries>(*s.shots[size_t(o - 1)]);
            sh->phases.erase(std::remove_if(sh->phases.begin(), sh->phases.end(), [](const PhaseMark &p) { return p.phase != phase::Impact; }),
                             sh->phases.end());
            s.shots[size_t(o - 1)] = sh;
        }
        const SessionAlignment al = alignSession(s);
        QCOMPARE(al.countRoute(AlignRoute::Landmarks), 2);
        QCOMPARE(al.countRoute(AlignRoute::PAnchors), 13);
        QVERIFY(!al.landmarks.empty());
        for (int o : { 3, 11 }) {
            const ShotWarp *w = al.warpFor(o), *ref = full.warpFor(o);
            QVERIFY(w->anchors.size() >= 3);
            const double top = ref->anchors[3].shotMs;                // P4 on the real ladder
            QVERIFY2(std::abs(w->warpMs(top) - ref->warpMs(top)) < 20.0, qPrintable(QStringLiteral("shot %1").arg(o)));
        }
    }

    void fixtureKindsClubheadFitStopsBeforeImpact()
    {
        // 4 July, the faster kind's clubhead-speed curve (NOTES-2c): every run ends −4.1 … −0.3 ms
        // (bridged after impact). The fit used to carry the pre-impact slope on to 103 mph at +13 ms.
        const SessionAlignment al = alignSession(m_fx.session);
        const auto readAt = [this](const QString &k) { return m_fx.readAtOf(k); };
        const SessionKinds k = findKinds(sessionReadings(m_fx.session, readAt));
        QCOMPARE(int(k.groupA.size()), 6);
        const std::vector<TraceCurve> cs = alignedCurves(m_fx.session, al, QStringLiteral("clubheadSpeed"));
        // the relations presenter's grid: the curves' extent held to Address − 150 … Finish + 150
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const TraceCurve &t : cs) if (!t.empty()) { lo = std::min(lo, t.t.front()); hi = std::max(hi, t.t.back()); }
        lo = std::max(lo, al.canonicalOf(phase::Address)->tMs - 150.0);
        hi = std::min(hi, al.canonicalOf(phase::Finish)->tMs + 150.0);
        const std::vector<double> grid = sessionGrid(lo, hi);
        const GroupFit f = pooledFit(cs, k.groupA, grid);
        FitOptions mocks; mocks.requireCoverage = false;
        const GroupFit old = pooledFit(cs, k.groupA, grid, mocks);
        // the third-latest run end among the kind: past it (and its half sample) the fit is blank
        std::vector<double> ends;
        double lastEnd = -1e9;
        for (int o : k.groupA) {
            const ShotCoverage c = shotCoverage(cs[size_t(o - 1)]);
            ends.push_back(c.runs.back().t1 + c.edgeMs);
            lastEnd = std::max(lastEnd, c.runs.back().t1);
        }
        QVERIFY(lastEnd < 0.0);                                       // every run ends before impact
        std::sort(ends.begin(), ends.end(), std::greater<double>());
        bool sawOldExtrapolation = false;
        for (size_t i = 0; i < grid.size(); ++i) {
            if (grid[i] > ends[2]) QVERIFY2(std::isnan(f.v[i]), qPrintable(QStringLiteral("value at %1").arg(grid[i])));
            if (grid[i] > 0.0) QVERIFY(!(f.v[i] > 86.0));
            if (grid[i] > 10.0 && old.v[i] > 100.0) sawOldExtrapolation = true;
        }
        QVERIFY(sawOldExtrapolation);                                 // the defect, reproduced with the rule off
        QVERIFY(f.drawnPoints() > int(grid.size()) / 2);              // the measured stretch is still drawn
    }

    void fixtureGroupFitSpansTheChestHoles()
    {
        // The chest (thorax) rate is bridged around impact on every shot; pooled, the group still has
        // ≥ 3 shots wherever any one shot has a hole of under ~30 ms.
        const SessionAlignment al = alignSession(m_fx.session);
        const std::vector<TraceCurve> cs = alignedCurves(m_fx.session, al, QStringLiteral("thoraxAngularSpeed"));
        const double t0 = al.canonicalOf(phase::Top)->tMs, t1 = al.canonicalOf(phase::P8)->tMs;
        const GroupFit f = pooledFit(cs, allOrds(), sessionGrid(t0, t1));
        QVERIFY(!f.refused());
        QVERIFY(f.drawnPoints() > int(f.t.size()) / 2);
        const GroupFit tiny = pooledFit(cs, { 1, 2 }, sessionGrid(t0, t1));
        QCOMPARE(tiny.refusal, FitRefusal::GroupTooSmall);           // a group under 3 is refused
    }

    // ═════════════════════════════ SYNTHETIC: every gate and edge ═════════════════════════════

    void synthTooFewShots()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots { makeShot(1), makeShot(2) };
        for (auto &s : shots) s->metrics.insert(QStringLiteral("x"), curveOf(QStringLiteral("x"), [](double t) { return t; }, -300, 100));
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionAlignment al = alignSession(ss);
        const std::vector<TraceCurve> cs = alignedCurves(ss, al, QStringLiteral("x"));
        const SessionEnvelope e = envelope(curvesOf(cs), sessionGrid(-280, 85));
        for (double v : e.med) QVERIFY(std::isnan(v));               // < 3 shots: blank
        QVERIFY(!partAt(e, 2).found);
        const GroupFit f = pooledFit(cs, { 1, 2 }, sessionGrid(-280, 85));
        QVERIFY(f.refused());
        QVERIFY(f.v.empty());
        QVERIFY(!pearson({ 1, 2, 3, 4 }, { 2, 4, 6, 8 }).defined);   // n < 5
        QVERIFY(!shapeSplit({ cs }, -280, 85).found);
        // a reading measured on 2 shots is not a reading
        addReading(*shots[0], QStringLiteral("r"), 5, 1.0);
        addReading(*shots[1], QStringLiteral("r"), 5, 2.0);
        QVERIFY(sessionReadings(snapshotOf(shots), {}).empty());
    }

    void synthMissingP()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 6; ++i) { shots.push_back(makeShot(i, 1.0 + 0.02 * i)); removePhase(*shots.back(), phase::P3); }
        removePhase(*shots[0], phase::P5);
        removePhase(*shots[1], phase::P5);
        removePhase(*shots[2], phase::P5);
        removePhase(*shots[3], phase::P5);                            // P5 on 2 of 6: not canonical
        const SessionAlignment al = alignSession(snapshotOf(shots));
        QVERIFY(!al.canonicalOf(phase::P3));                          // on no shot
        QVERIFY(!al.canonicalOf(phase::P5));                          // on fewer than half
        QVERIFY(al.canonicalOf(phase::Top));
        QCOMPARE(considered(al.shots[5], phase::P5)->gate, AnchorGate::NotCanonical);
        for (const ShotWarp &w : al.shots) {
            QCOMPARE(w.route, AlignRoute::PAnchors);
            for (const WarpAnchor &a : w.anchors) QVERIFY(a.phase != phase::P3 && a.phase != phase::P5);
        }
    }

    void synthBackwardsLowConfidenceAndOutliers()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 8; ++i) shots.push_back(makeShot(i, 0.95 + 0.012 * i));
        setPhase(*shots[0], phase::P5, -30.0);                        // after P6 (−45): runs backwards
        setPhase(*shots[1], phase::P3, -550.0, 0.2);                  // a clamp-grade time: low confidence
        setPhase(*shots[2], phase::P8, 3.8);                          // 3.8 ms after impact: outlier segment
        setPhase(*shots[3], phase::Finish, 965.0);                    // shot 9's Finish: outlier segment
        setPhase(*shots[4], phase::Top, -281.0, kNaN);                // no confidence is not low confidence
        const SessionAlignment al = alignSession(snapshotOf(shots));
        QCOMPARE(considered(al.shots[0], phase::P5)->gate, AnchorGate::RunsBackwards);
        QCOMPARE(considered(al.shots[0], phase::P6)->gate, AnchorGate::Kept);     // the one nearer impact wins
        QCOMPARE(considered(al.shots[1], phase::P3)->gate, AnchorGate::LowConfidence);
        QCOMPARE(considered(al.shots[2], phase::P8)->gate, AnchorGate::OutlierSegment);
        QCOMPARE(considered(al.shots[2], phase::Finish)->gate, AnchorGate::Kept); // judged from impact once P8 is gone
        QCOMPARE(considered(al.shots[3], phase::Finish)->gate, AnchorGate::OutlierSegment);
        QCOMPARE(considered(al.shots[4], phase::Top)->gate, AnchorGate::Kept);
        for (size_t i = 5; i < 8; ++i) QCOMPARE(al.shots[i].droppedCount(), 0);
        // the dropped anchors are bridged: warps stay strictly increasing and exact
        for (const ShotWarp &w : al.shots)
            for (size_t k = 1; k < w.anchors.size(); ++k) {
                QVERIFY(w.anchors[k].shotMs > w.anchors[k - 1].shotMs);
                QVERIFY(w.anchors[k].canonMs > w.anchors[k - 1].canonMs);
            }
        // with the gates off the mocks keep the outliers (and fold the backwards one away forwards)
        WarpOptions off; off.gates = false;
        const SessionAlignment mock = alignSession(snapshotOf(shots), off);
        QVERIFY(std::any_of(mock.shots[3].anchors.begin(), mock.shots[3].anchors.end(), [](const WarpAnchor &a) { return a.phase == phase::Finish; }));
    }

    void synthImpactOnlyByLandmarks()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->metrics.insert(QStringLiteral("handSpeed"), curveOf(QStringLiteral("handSpeed"), [](double t) { return handLike(t, 1.0); }, -1300, 300));
            shots.push_back(s);
        }
        auto lone = makeShot(7, 1.15, false);                         // impact only, a slower tempo
        lone->metrics.insert(QStringLiteral("handSpeed"), curveOf(QStringLiteral("handSpeed"), [](double t) { return handLike(t, 1.15); }, -1500, 300));
        shots.push_back(lone);
        const SessionAlignment al = alignSession(snapshotOf(shots));
        const ShotWarp *w = al.warpFor(7);
        QCOMPARE(w->route, AlignRoute::Landmarks);
        QCOMPARE(int(w->anchors.size()), 4);                          // onset, dip, peak, impact
        for (const WarpAnchor &a : w->anchors) {
            if (a.landmark == Landmark::None) continue;
            QVERIFY2(std::abs(a.shotMs / 1.15 - a.canonMs) < 6.0, qPrintable(QStringLiteral("landmark %1: %2 → %3").arg(int(a.landmark)).arg(a.shotMs).arg(a.canonMs)));
        }
        // the slower shot's curve lands on the others once aligned
        const std::vector<TraceCurve> cs = alignedCurves(snapshotOf(shots), al, QStringLiteral("handSpeed"));
        for (double c : { -800.0, -600.0, -300.0, -60.0 })
            QVERIFY2(std::abs(valueAt(cs[6], c) - valueAt(cs[0], c)) < 0.6, qPrintable(QStringLiteral("at %1").arg(c)));
        // a gated landmark (the dip too shallow) is not an anchor
        const std::vector<LandmarkFind> flat = findLandmarks(shotCurve(*makeShotWith(QStringLiteral("handSpeed"), [](double t) { return 10.0 + 0.001 * t; }), QStringLiteral("handSpeed")));
        QVERIFY(!flat[0].found && !flat[1].found && !flat[2].found);
    }

    void synthImpactOnlyByTempo()
    {
        // A single broad bump: no dip and no sharp peak in the search windows, so no landmark; the tempo
        // scale must be fitted (and said to be fitted).
        auto bump = [](double t, double k) { return 10.0 * gauss(t / k, -300, 150); };
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->metrics.insert(QStringLiteral("handSpeed"), curveOf(QStringLiteral("handSpeed"), [&](double t) { return bump(t, 1.0); }, -1200, 200));
            shots.push_back(s);
        }
        auto lone = makeShot(7, 1.0, false);
        lone->metrics.insert(QStringLiteral("handSpeed"), curveOf(QStringLiteral("handSpeed"), [&](double t) { return bump(t, 1.0 / 1.2); }, -1200, 200));
        shots.push_back(lone);
        const SessionAlignment al = alignSession(snapshotOf(shots));
        const ShotWarp *w = al.warpFor(7);
        QCOMPARE(w->route, AlignRoute::TempoFitted);
        QVERIFY2(std::abs(w->tempoScale - 1.2) < 0.011, qPrintable(QString::number(w->tempoScale)));
        QCOMPARE(w->unwarpMs(w->warpMs(-250.0)), -250.0);
        // landmarks and tempo both off: lined up at impact, nothing dilated
        WarpOptions o; o.useLandmarks = false; o.useTempo = false;
        const SessionAlignment plain = alignSession(snapshotOf(shots), o);
        const ShotWarp *s = plain.warpFor(7);
        QCOMPARE(s->route, AlignRoute::ImpactShift);
        QCOMPARE(s->warpMs(-250.0), -250.0);
        // no impact at all: unaligned, and out of every cross-shot curve
        auto noImpact = makeShot(8); noImpact->impactUs = -1;
        shots.push_back(noImpact);
        const SessionAlignment al2 = alignSession(snapshotOf(shots));
        QCOMPARE(al2.warpFor(8)->route, AlignRoute::Unaligned);
        QVERIFY(alignedCurves(snapshotOf(shots), al2, QStringLiteral("handSpeed"))[7].empty());
    }

    void synthPchipOnRandomAnchors()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> step(0.5, 300.0), stretch(0.2, 4.0);
        for (int trial = 0; trial < 200; ++trial) {
            std::vector<WarpAnchor> a;
            double s = -1000 + step(rng), c = s * stretch(rng);
            const int n = 2 + trial % 8;
            for (int i = 0; i < n; ++i) { a.push_back(WarpAnchor{ i, Landmark::None, s, c }); s += step(rng); c += step(rng) * stretch(rng); }
            ShotWarp w = makeWarp(a, WarpInterp::Pchip);
            w.route = AlignRoute::PAnchors;
            for (const WarpAnchor &x : a) QCOMPARE(w.warpMs(x.shotMs), x.canonMs);
            double prev = -std::numeric_limits<double>::infinity();
            for (double t = a.front().shotMs - 50; t <= a.back().shotMs + 50; t += (a.back().shotMs - a.front().shotMs) / 997.0) {
                const double v = w.warpMs(t);
                QVERIFY(v > prev);
                prev = v;
                QVERIFY(std::abs(w.unwarpMs(v) - t) <= 1e-9 * std::max(1.0, std::abs(t)));
            }
        }
        // one anchor = a pure shift; none = identity
        ShotWarp one = makeWarp({ WarpAnchor{ 5, Landmark::None, 10.0, 0.0 } }, WarpInterp::Pchip);
        one.route = AlignRoute::ImpactShift;
        QCOMPARE(one.warpMs(25.0), 15.0);
        QCOMPARE(one.unwarpMs(15.0), 25.0);
        ShotWarp none = makeWarp({}, WarpInterp::Pchip);
        QCOMPARE(none.warpMs(-3.0), -3.0);
    }

    void synthBridgedHoleSpannedByOthers()
    {
        // Shot 1 is bridged −13 … +20 ms (4 July's chest gap), shot 2 −40 … +40 ms.
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 5; ++i) {
            auto s = makeShot(i);
            MetricSeries m = curveOf(QStringLiteral("chest"), [i](double t) { return 500.0 + 2.0 * t + i; }, -300, 100, 4.0);
            m.valid.assign(m.tUs.size(), 1);
            for (size_t k = 0; k < m.tUs.size(); ++k) {
                const double t = double(m.tUs[k] - kImpactUs) / 1000.0;
                if ((i == 1 && t > -13.0 && t < 20.0) || (i == 2 && t > -40.0 && t < 40.0)) m.valid[k] = 0;
            }
            s->metrics.insert(QStringLiteral("chest"), m);
            shots.push_back(s);
        }
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionAlignment al = alignSession(ss);
        const std::vector<TraceCurve> cs = alignedCurves(ss, al, QStringLiteral("chest"));
        const std::vector<TimeSpan> runs = coverageRuns(cs[0]);
        QCOMPARE(int(runs.size()), 2);
        const std::vector<TimeSpan> holes = coverageHoles(runs, -280.0, 85.0);
        QCOMPARE(int(holes.size()), 1);
        QCOMPARE(holes[0].t0, -16.0);                                 // the last measured sample before it …
        QCOMPARE(holes[0].t1, 20.0);                                  // … and the first after
        const GroupFit f = pooledFit(cs, { 1, 2, 3, 4, 5 }, sessionGrid(-280.0, 85.0));
        int spanned = 0;
        for (size_t i = 0; i < f.t.size(); ++i) {
            if (f.t[i] < -12.0 || f.t[i] > 19.0) continue;
            QVERIFY(std::isfinite(f.v[i]));                           // shot 1's hole is spanned …
            QVERIFY(f.nShots[i] >= 3);
            if (f.t[i] > -13.0 && f.t[i] < 17.0) QCOMPARE(f.nShots[i], 3);   // … by 3, 4, 5 (1 and 2 are bridged)
            QVERIFY(std::abs(f.v[i] - (503.0 + 2.0 * f.t[i])) < 2.0); // a line through the group
            ++spanned;
        }
        QVERIFY(spanned > 5);
        // shots 1, 2, 3: at impact only shot 3 measured → blank there. Shot 1's samples either side of
        // its own hole are within ±20 ms, but a shot does not "measure" its hole by interpolating it.
        const GroupFit g = pooledFit(cs, { 1, 2, 3 }, sessionGrid(-280.0, 85.0));
        for (size_t i = 0; i < g.t.size(); ++i)
            if (g.t[i] > -2.0 && g.t[i] < 2.0) { QCOMPARE(g.nShots[i], 1); QVERIFY(std::isnan(g.v[i])); }
        QVERIFY(!g.refused());                                        // the group is 3: fitted, blank where thin
    }

    void synthFitStopsWhereTheRunsStop()
    {
        // Five shots of one steep line whose measurements end at −10, −6, 0, +6, +10 ms (bridged after)
        // and start at −210 … −190. The fit must end where fewer than three runs reach — at the
        // third-latest end, 0 ms (+ half a sample) — never continue the slope past the data.
        std::vector<std::shared_ptr<ShotSeries>> shots;
        const double ends[] = { -10, -6, 0, 6, 10 }, starts[] = { -210, -206, -200, -196, -190 };   // on the 2 ms samples
        for (int i = 1; i <= 5; ++i) {
            auto s = makeShot(i);
            MetricSeries m = curveOf(QStringLiteral("speed"), [](double t) { return 80.0 + 0.5 * t; }, -300, 100, 2.0);
            m.valid.assign(m.tUs.size(), 1);
            for (size_t k = 0; k < m.tUs.size(); ++k) {
                const double t = double(m.tUs[k] - kImpactUs) / 1000.0;
                if (t > ends[i - 1] || t < starts[i - 1]) m.valid[k] = 0;
            }
            s->metrics.insert(QStringLiteral("speed"), m);
            shots.push_back(s);
        }
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionAlignment al = alignSession(ss);
        const std::vector<TraceCurve> cs = alignedCurves(ss, al, QStringLiteral("speed"));
        const std::vector<double> grid = timeGrid(-260.0, 60.0, 0.5);
        const GroupFit f = pooledFit(cs, { 1, 2, 3, 4, 5 }, grid);
        for (size_t i = 0; i < grid.size(); ++i) {
            const double x = grid[i];
            int runs = 0;
            bool onEdge = false;                                      // exactly on a run's edge: the warp's last bit decides
            for (int k = 0; k < 5; ++k) {
                if (x >= starts[k] - 1.0 && x <= ends[k] + 1.0) ++runs;
                if (std::abs(x - (starts[k] - 1.0)) < 1e-6 || std::abs(x - (ends[k] + 1.0)) < 1e-6) onEdge = true;
            }
            if (onEdge) continue;
            QVERIFY2(f.nShots[i] == runs, qPrintable(QStringLiteral("at %1: nShots %2, runs %3").arg(x).arg(f.nShots[i]).arg(runs)));   // nShots = the strip's count
            if (runs >= 3) {
                QVERIFY2(std::abs(f.v[i] - (80.0 + 0.5 * x)) < 1e-6, qPrintable(QString::number(x)));
            } else {
                QVERIFY2(std::isnan(f.v[i]) && std::isnan(f.sd[i]) && std::isnan(f.se[i]), qPrintable(QString::number(x)));
            }
        }
        QVERIFY(std::isfinite(f.v[size_t(std::lround((0.5 + 260.0) / 0.5))]));   // +0.5 ms: within the edge's half sample
        QVERIFY(std::isnan(f.v[size_t(std::lround((1.5 + 260.0) / 0.5))]));      // +1.5 ms: past it
        // the mocks' rule would carry the line on: five shots' samples are all still within ±20 ms
        FitOptions mocks; mocks.requireCoverage = false;
        const GroupFit m = pooledFit(cs, { 1, 2, 3, 4, 5 }, grid, mocks);
        QVERIFY(std::isfinite(m.v[size_t(std::lround((8.0 + 260.0) / 0.5))]));
        // the pooled-sample overload derives the same coverage from the samples (no bridged gaps here)
        const GroupFit p = pooledFit(pooledSamples(cs, { 1, 2, 3, 4, 5 }), grid, 5);
        for (size_t i = 0; i < grid.size(); ++i) {
            QCOMPARE(p.nShots[i], f.nShots[i]);
            QCOMPARE(std::isnan(p.v[i]), std::isnan(f.v[i]));
        }
    }

    void synthAngleAndConstantReading()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        const double toe[] = { -3.4, 0.5, -2.0, 2.0, -1.0, -2.5 };
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            addReading(*s, QStringLiteral("toeLineAngle"), 0, toe[i - 1], QStringLiteral("°"));
            addReading(*s, QStringLiteral("flat"), 5, 3.0);
            addReading(*s, QStringLiteral("speed"), 5, 70.0 + i);
            shots.push_back(s);
        }
        const std::vector<SessionReading> rs = sessionReadings(snapshotOf(shots), [](const QString &) { return std::vector<int>{ 5 }; });
        QCOMPARE(int(rs.size()), 3);
        const SessionReading &toeR = rs[0];
        QCOMPARE(toeR.id, QStringLiteral("toeLineAngle@0"));          // address-only: falls back to its own phase
        QCOMPARE(toeR.values[1], 0.5);                               // read as stored: no unwrap (#8)
        QCOMPARE(toeR.box.max - toeR.box.min, 5.4);
        QVERIFY(toeR.box.outliers.empty());
        // a constant reading: r undefined, never a NaN in a ranked list, never a crash
        const Correlation c = pairOf(rs[1], rs[2]);
        QVERIFY(!c.defined);
        QVERIFY(!driftOf(rs[1]).defined);
        QVERIFY(driftOf(rs[2]).defined);
        for (const CorrRow &row : correlate(rs, 2)) QVERIFY(row.reading != 1 && std::isfinite(row.c.r));
        const ReadingPlace p = placeOf(rs[1], 0);
        QCOMPARE(p.rz, 0.0);
        QCOMPARE(p.side, 0);
        QCOMPARE(p.rankHi, 1);
        QCOMPARE(p.rankLo, 1);
        const SessionKinds k = findKinds(rs);                         // 6 shots: k-medoids needs 3 a side
        QVERIFY(k.readingsUsed == 2);                                 // the constant one has no spread
    }

    void synthFacetsAndConditionLinks()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 8; ++i) {
            auto s = makeShot(i);
            s->diagnostics.fired << QStringLiteral("everyShot");
            if (i <= 4) s->diagnostics.fired << QStringLiteral("half");
            if (i <= 2) s->diagnostics.fired << QStringLiteral("rare");
            if (i % 3 == 0) s->diagnostics.fired << QStringLiteral("third");
            addReading(*s, QStringLiteral("lean"), 5, double(i));
            s->club = i <= 3 ? QStringLiteral("DRIVER") : QStringLiteral("7 IRON");
            shots.push_back(s);
        }
        SessionSnapshot ss = snapshotOf(shots);
        const std::vector<SessionFacet> fs = conditionFacets(ss);
        QCOMPARE(int(fs.size()), 1);                                  // "third" fires on 2 of 8, "rare" on 2
        QCOMPARE(fs[0].id, QStringLiteral("half"));                   // a fault on every shot is never a facet
        QCOMPARE(fs[0].groups[0].ordinals, (std::vector<int>{ 1, 2, 3, 4 }));
        const auto club = clubFacet(ss);
        QVERIFY(club.has_value());
        QCOMPARE(int(club->groups.size()), 2);
        // the condition link: fired per shot on the reading it grades
        ConditionInfo ci; ci.metricKey = QStringLiteral("lean"); ci.phase = 5;
        ss.conditions.insert(QStringLiteral("half"), ci);
        const std::vector<SessionReading> rs = sessionReadings(ss, [](const QString &) { return std::vector<int>{ 5 }; });
        QCOMPARE(rs[0].conditionIds, QStringList{ QStringLiteral("half") });
        QCOMPARE(rs[0].fired, (std::vector<uint8_t>{ 1, 1, 1, 1, 0, 0, 0, 0 }));
    }

    void synthSequenceLateIsOneSegment()
    {
        auto s = makeShot(1);
        QVariantList nodes;
        const auto node = [](const char *seg, double before, bool placed) {
            return QVariantMap{ { QStringLiteral("segment"), QString::fromLatin1(seg) }, { QStringLiteral("beforeImpactMs"), before },
                                { QStringLiteral("placed"), placed }, { QStringLiteral("tPeakUs"), 1 } };
        };
        nodes << node("pelvis", -20, true) << node("thorax", 60, false) << node("leadArm", 107, true) << node("club", 49, true);
        s->kinematicSequence = QVariantMap{ { QStringLiteral("nodes"), nodes },
                                            { QStringLiteral("order"), QVariantList{ QStringLiteral("leadArm"), QStringLiteral("club"), QStringLiteral("pelvis") } } };
        const ShotSequence q = shotSequence(*s);
        QCOMPARE(q.placedCount, 3);
        QVERIFY(q.segments[0].late);                                   // the pelvis should lead and came last
        QVERIFY(!q.segments[1].placed && !q.segments[1].late);
        QVERIFY(!q.segments[2].late);                                  // the arm before the club: in turn
        QVERIFY(!q.segments[3].late);
        QCOMPARE(q.segments[2].rank, 1);
        QCOMPARE(q.segments[3].rank, 2);
        QCOMPARE(q.segments[0].rank, 3);
        QCOMPARE(q.segments[0].shotMs, 20.0);                          // + after impact, the shot's clock
    }

    // ═════════════ THE CHEST'S PROBABLE PEAK (session_bridge.h; design §2.2.1) ═════════════

    // A chest-like bump peaking just before impact, its top hidden by a ~30 ms hole, on five shots: the bridge
    // finds the peak inside the hole, with a ± and no earlier hump, and draws the same numbers twice.
    void bridgeFindsTheHiddenPeak()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 5; ++i) shots.push_back(chestShot(i, [i](double t) { return 150.0 + 450.0 * gauss(t, -16.0 + i, 45.0); }));
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionSequence seq = sessionSequence(ss);
        const SessionBridges b = bridgeChest(ss, seq);
        QVERIFY(b.kernel.fitted);
        QCOMPARE(b.kernel.shots, 5);
        QCOMPARE(b.bridgedCount(), 5);
        for (int i = 0; i < 5; ++i) {
            const ChestBridge &c = b.shots[size_t(i)];
            QVERIFY(c.bridged());
            QVERIFY(c.holeToMs - c.holeFromMs > 20 && c.holeToMs - c.holeFromMs < 40);
            QVERIFY2(std::abs(c.peakMs - (-15.0 + i)) < 6.0, qPrintable(QStringLiteral("shot %1 peak %2 ms").arg(i + 1).arg(c.peakMs)));
            QVERIFY2(std::abs(c.peakDps - 600.0) < 40.0, qPrintable(QStringLiteral("shot %1 peak %2 °/s").arg(i + 1).arg(c.peakDps)));
            QVERIFY(c.peakSigmaMs > 0.0 && c.peakSigmaMs < 15.0);
            QVERIFY(c.peakSigmaDps > 0.0);
            QVERIFY(std::isnan(c.earlierMs));
            QVERIFY(c.t.size() >= 20 && c.t.size() == c.mean.size() && c.t.size() == c.sd.size());
        }
        const SessionBridges again = bridgeChest(ss, seq);       // seeded: the same estimate every time
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(again.shots[size_t(i)].peakMs, b.shots[size_t(i)].peakMs);
            QCOMPARE(again.shots[size_t(i)].peakDps, b.shots[size_t(i)].peakDps);
        }
    }

    // A curve still rising when the camera loses it and still rising after: no probable peak is said.
    void bridgeSaysStillRising()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 4; ++i) shots.push_back(chestShot(i, [](double t) { return 300.0 + 2.0 * (t + 300.0); }));
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionBridges b = bridgeChest(ss, sessionSequence(ss));
        QCOMPARE(b.bridgedCount(), 0);
        for (const ChestBridge &c : b.shots) QCOMPARE(int(c.outcome), int(BridgeOutcome::StillRising));
    }

    // A taller measured hump earlier in the downswing is recorded, never taken as the peak (option A).
    void bridgeRecordsTheEarlierHump()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 4; ++i)
            shots.push_back(chestShot(i, [](double t) { return 100.0 + 900.0 * gauss(t, -180.0, 12.0) + 450.0 * gauss(t, -12.0, 45.0); }));
        const SessionSnapshot ss = snapshotOf(shots);
        const SessionBridges b = bridgeChest(ss, sessionSequence(ss));
        QCOMPARE(b.bridgedCount(), 4);
        QCOMPARE(b.earlierCount(), 4);
        for (const ChestBridge &c : b.shots) {
            QVERIFY(std::abs(c.peakMs + 12.0) < 8.0);
            QVERIFY(std::abs(c.earlierMs + 180.0) < 8.0);
            QVERIFY(c.earlierDps > c.peakDps);
        }
    }

    // The probable chest joins the sequence: ranked with the placed segments, late when it comes after the
    // arm, the order rebuilt; a chest the analysis placed is never bridged.
    void bridgeRanksTheProbableChest()
    {
        std::vector<std::shared_ptr<ShotSeries>> shots;
        for (int i = 1; i <= 4; ++i) {
            auto s = chestShot(i, [](double t) { return 150.0 + 450.0 * gauss(t, -14.0, 45.0); });
            QVariantList nodes;
            const auto node = [](const char *seg, double before, bool placed) {
                return QVariantMap{ { QStringLiteral("segment"), QString::fromLatin1(seg) }, { QStringLiteral("beforeImpactMs"), before },
                                    { QStringLiteral("placed"), placed }, { QStringLiteral("tPeakUs"), 1 } };
            };
            nodes << node("pelvis", 120, true) << node("thorax", 60, i == 4) << node("leadArm", 100, true) << node("club", 30, true);
            s->kinematicSequence = QVariantMap{ { QStringLiteral("nodes"), nodes } };
            shots.push_back(s);
        }
        const SessionSnapshot ss = snapshotOf(shots);
        SessionSequence seq = sessionSequence(ss);
        QCOMPARE(seq.segments[1].placed, 1);                          // shot 4's chest, placed by the analysis
        const SessionBridges b = bridgeChest(ss, seq);
        QCOMPARE(b.bridgedCount(), 3);
        QCOMPARE(int(b.shots[3].outcome), int(BridgeOutcome::NoHole));
        applyBridges(seq, b);
        for (int i = 0; i < 3; ++i) {
            const ShotSequence &q = seq.shots[size_t(i)];
            const SegmentPeak &ch = q.segments[1];
            QVERIFY(ch.probable && !ch.placed);
            QVERIFY(std::abs(ch.probableMs + 14.0) < 8.0);
            QCOMPARE(ch.rank, 4);
            QVERIFY(ch.late);                                         // the chest should lead the arm and came after it
            QVERIFY(!q.segments[0].late && !q.segments[2].late && !q.segments[3].late);
            QCOMPARE(q.order, (QStringList{ QStringLiteral("pelvis"), QStringLiteral("leadArm"), QStringLiteral("club"), QStringLiteral("thorax") }));
            QCOMPARE(q.timedCount, 4);
            QCOMPARE(q.placedCount, 3);
        }
        QCOMPARE(seq.segments[1].placed, 1);
        QCOMPARE(seq.segments[1].probable, 3);
        QVERIFY(!seq.shots[3].segments[1].probable);
    }

private:
    // A shot with a chest speed curve from f, sampled every 6.7 ms (150 fps), measured except for a hole from
    // −21 to +5.5 ms (the camera losing the chest where the shoulders square to it).
    std::shared_ptr<ShotSeries> chestShot(int ordinal, const std::function<double(double)> &f)
    {
        auto s = makeShot(ordinal);
        MetricSeries m = curveOf(QStringLiteral("thoraxAngularSpeed"), f, -300, 100, 6.7);
        m.unit = QStringLiteral("°/s");
        for (const qint64 tu : m.tUs) {
            const double t = double(tu - kImpactUs) / 1000.0;
            m.valid.push_back(t > -21.0 && t < 5.5 ? 0 : 1);
        }
        s->metrics.insert(m.key, m);
        s->metricOrder << m.key;
        return s;
    }

    template <typename F>
    std::shared_ptr<ShotSeries> makeShotWith(const QString &key, F f)
    {
        auto s = makeShot(99);
        s->metrics.insert(key, curveOf(key, f, -1300, 300));
        return s;
    }
};

QTEST_GUILESS_MAIN(ChartsSessionMathsTest)
#include "charts_session_maths_test.moc"
