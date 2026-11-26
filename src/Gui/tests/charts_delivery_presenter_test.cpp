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

// The Club delivery section's presenter (charts_delivery_presenter.h; club_delivery_chart_design.md stage 2) on an
// in-memory session of fourteen synthetic shots — the swing plane curve and the hand plane on the 5 ms grid the fusion
// stage publishes, the shoulder plane and the steepening as phase samples, the monitor's path, attack, face and lie at
// impact — whose path follows the plane at P6:
//
//   · every documented key of choices, thisShot and session, with the values the core gives;
//   · plotLayers: the picked shot's address line, shoulder plane, slot and the hands' stretch (This shot), the
//     session's medians and no stretch (Session) — the scope switch;
//   · seekUsFor, the playhead's standfirst, a shot from its own detail, an empty model, the report's payload;
//   · the golfer's day (session_delivery.h decision 13), FROM THE OTHER SESSIONS' SUMMARIES (refactor 5.5d; DEC-74): a
//     sibling session of the same day, its day readings made by the one rule off its shots' grids, handed in through
//     the neighbours door, is pooled into the fit and the scatter, another day is not; a shot's day reading agrees
//     with the same shot read from the snapshot; the presenter reads the day again when the athlete level changes;
//   · a session of the day not summed up yet, or a newer build's, is left out and said in the trust note.

#include "charts/charts_delivery_presenter.h"
#include "charts/charts_gadget.h"   // the typed results as maps, for the assertions (refactor 5.5b)
#include "../../Export/rule_data_install.h"
#include "charts/session_delivery.h"
#include "../../Diagnostics/derived/series/session_series_types.h"
#include "Diagnostics/measures/measure_sample.h"
#include "Diagnostics/derived/session_day_readings_rules.h"   // the day readings' one rule (refactor 5.5d)
#include "charts/session_neighbours.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <cmath>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint::charts;

namespace {

const QString kSession = QStringLiteral("/fixture/2026-10-29_Synthetic_Wrist_01");
QString dirOf(int i) { return kSession + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')); }

constexpr qint64 kImpactUs = 2'000'000;
constexpr int    kPh[7]  = { 0, 12, 8, 2, 13, 9, 5 };
constexpr double kMs[7]  = { -1200, -1000, -850, -600, -250, -120, 0 };
// The hands carry the curve from 50 ms before the top to 25 ms after P6: the hand plane sits 4° under it elsewhere.
constexpr double kHandsFromMs = -650, kHandsToMs = -95;

// Shot i (1..14): the plane at P6 is 58 + 0.5 (i − 1); the path follows it, −0.92 a degree, with a ±0.3 wobble.
double p6Of(int i) { return 58.0 + 0.5 * (i - 1); }
double pathOf(int i) { return 55.5 - 0.92 * p6Of(i) + (i % 2 ? 0.3 : -0.3); }

std::shared_ptr<ShotSeries> makeShot(int i, bool monitor = true)
{
    auto s = std::make_shared<ShotSeries>();
    s->ordinal = i; s->shotId = i; s->swingDir = dirOf(i); s->club = QStringLiteral("7 IRON"); s->impactUs = kImpactUs;
    const double val[7] = { 58, 54, 62, 65, 64, p6Of(i), 59 };
    for (int p = 0; p < 7; ++p) s->phases.push_back({ kPh[p], kImpactUs + qint64(kMs[p] * 1000), 1.0 });
    MetricSeries c, h;
    c.key = QStringLiteral("swingPlaneCurve"); c.unit = QStringLiteral("°"); c.sigma = 0.8;
    h.key = QStringLiteral("handPlaneCurve");  h.unit = QStringLiteral("°");
    for (int k = 0; k <= 248; ++k) {
        const double m = -1200.0 + 5.0 * k;   // −1200 … +40 ms
        // flat about each position (so the ±15 ms sampler reads it exactly), linear between
        double v = val[6];
        int p = 0;
        while (p < 6 && m > kMs[p + 1]) ++p;
        if (p >= 6) v = val[6];
        else {
            const double a = kMs[p] + 25, b = kMs[p + 1] - 25;
            v = m <= a ? val[p] : m >= b ? val[p + 1] : val[p] + (val[p + 1] - val[p]) * (m - a) / (b - a);
        }
        c.tUs.push_back(kImpactUs + qint64(m * 1000)); c.value.push_back(v);
        h.tUs.push_back(kImpactUs + qint64(m * 1000)); h.value.push_back(m >= kHandsFromMs && m <= kHandsToMs ? v : v - 4.0);
    }
    s->metrics.insert(c.key, c); s->metricOrder << c.key;
    s->metrics.insert(h.key, h); s->metricOrder << h.key;
    const auto instant = [&](const QString &key, int phase, double v) {
        MetricSeries m;
        m.key = key; m.unit = QStringLiteral("°");
        PhaseSample ps; ps.phase = phase; ps.value = v;
        for (const PhaseMark &pm : s->phases) if (pm.phase == phase) ps.tUs = pm.tUs;
        m.phaseSamples.push_back(ps);
        s->metrics.insert(key, m); s->metricOrder << key;
    };
    instant(QStringLiteral("shoulderPlane"), 0, 68.0);
    instant(QStringLiteral("handPlaneSteepening"), 2, 5.0 + 0.3 * (i - 1) + (i % 3 == 0 ? 1.5 : 0.0));   // a looser fit than P6's
    if (monitor) {
        instant(QStringLiteral("lm.clubPath"), 5, pathOf(i));
        instant(QStringLiteral("lm.attackAngle"), 5, 28.0 - 0.5 * p6Of(i) + (i % 2 ? 0.2 : -0.2));
        instant(QStringLiteral("lm.faceAngle"), 5, -2.0);   // inside the pack's 4° start corridor: a straight start (refactor 6.3b)
        instant(QStringLiteral("lm.faceToPath"), 5, 1.5);
        instant(QStringLiteral("lm.lieAngle"), 5, 2.0);
        instant(QStringLiteral("lm.dynamicLoft"), 5, 30.0);
    }
    return s;
}

std::shared_ptr<SessionSnapshot> makeSession(int n)
{
    auto snap = std::make_shared<SessionSnapshot>();
    snap->sessionDir = kSession;
    for (int i = 1; i <= n; ++i) snap->shots.push_back(makeShot(i));
    return snap;
}

// shotReplay.analysisDetail's shape, from a shot: phases and every series.
QVariantMap detailOf(const ShotSeries &s)
{
    QVariantList phases, series;
    for (const PhaseMark &p : s.phases)
        phases.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) } });
    for (const QString &k : s.metricOrder) {
        const MetricSeries *m = s.metric(k);
        QVariantList t, v, ps;
        for (size_t i = 0; i < m->tUs.size(); ++i) { t.append(qlonglong(m->tUs[i])); v.append(m->value[i]); }
        for (const PhaseSample &p : m->phaseSamples)
            ps.append(QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                                   { QStringLiteral("value"), p.value } });
        series.append(QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("unit"), m->unit }, { QStringLiteral("t_us"), t },
                                   { QStringLiteral("value"), v }, { QStringLiteral("phaseSamples"), ps } });
    }
    return { { QStringLiteral("club"), s.club }, { QStringLiteral("phases"), phases }, { QStringLiteral("series"), series },
             { QStringLiteral("impactUs"), qlonglong(s.impactUs) } };
}

// THE OTHER SESSIONS AS THEIR SUMMARIES HOLD THEM (refactor 5.5d): a sibling's day readings, by the one rule over each
// shot's grid (swing number, fixture shot), and the neighbours of this session.
NeighbourSession summed(const QString &folder, const QDate &day, int nOfDay, const std::vector<std::pair<int, int>> &shots)
{
    NeighbourSession o;
    o.place = { folder, day, nOfDay };
    o.readings.has = true;
    for (const auto &[swing, i] : shots) {
        pinpoint::analysis::SwingPhaseGrid g = deliveryPhaseGridOf(*makeShot(i));
        g.ordinal = swing;
        if (auto r = pinpoint::analysis::shotDeliveryReadingOf(pinpoint::analysis::deliveryGridNumbersOf(g), swing, g.ordinal))
            o.readings.delivery.push_back(*r);
    }
    return o;
}
SessionNeighbours neighbours(const QString &sessionDir, const QDate &day, int nOfDay, std::vector<NeighbourSession> others)
{
    SessionNeighbours n;
    n.known = true;
    n.self = { QFileInfo(sessionDir).fileName(), day, nOfDay };
    n.others = std::move(others);
    return n;
}
// The process's door, for one test.
struct DoorScope {
    explicit DoorScope(SessionNeighbours n) { setNeighboursDoor({ [n](const QString &) { return n; }, {} }); }
    ~DoorScope() { setNeighboursDoor({}); }
};

void hasKeys(const QVariantMap &m, const QStringList &keys, const char *what)
{
    for (const QString &k : keys) QVERIFY2(m.contains(k), qPrintable(QStringLiteral("%1 lacks %2").arg(QString::fromLatin1(what), k)));
}

QVariantList layersOf(ChartsDeliveryPresenter &p) { return chartsGadgetMap(p.plotLayers()).value(QStringLiteral("swingPlaneCurve")).toList(); }
QVariantMap layerOf(const QVariantList &ls, const QString &role)
{
    for (const QVariant &l : ls) if (l.toMap().value(QStringLiteral("role")).toString() == role) return l.toMap();
    return {};
}

} // namespace

class ChartsDeliveryPresenterTest : public QObject
{
    Q_OBJECT
    pinpoint::test::IsolatedStores m_stores;   // #187: the first member, so every store is scratch before any other

private slots:
    void initTestCase() { pinpoint::installProcessRuleData(); }   // this process's packs and norms (refactor step 3.2)
    void emptyModel()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(std::make_shared<SessionSnapshot>());
        QVERIFY(!p.available());
        QVERIFY(!chartsGadgetMap(p.thisShot()).value(QStringLiteral("available")).toBool());
        QVERIFY(chartsGadgetMap(p.thisShot()).value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("Select a swing to review.")));
        QVERIFY(!chartsGadgetMap(p.session()).value(QStringLiteral("available")).toBool());
        QVERIFY(p.plotLayers().swingPlaneCurve.isEmpty());   // no lines: the row's list is empty (a typed value always has its key)
        QCOMPARE(p.seekUsFor(QStringLiteral("P4")), qint64(-1));
        QCOMPARE(chartsGadgetMap(p.choices()).value(QStringLiteral("onShowKeys")).toStringList(), deliveryOnShowKeys());
    }

    void thisShotPayload()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(14));
        QVERIFY(p.available());
        const QVariantMap t = chartsGadgetMap(p.thisShot());
        hasKeys(t, { "available", "empty", "ordinal", "shotId", "swingDir", "inSession", "kicker", "mode", "hasPlane", "hasMonitor",
                     "head", "headText", "headRule", "story", "slot", "flight", "impact", "shape", "trust", "replay", "standfirsts" }, "thisShot");
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QCOMPARE(t.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 14 of 14  ·  7 iron  ·  Club delivery"));
        QCOMPARE(t.value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        QCOMPARE(t.value(QStringLiteral("headRule")).toInt(), 1);
        QCOMPARE(t.value(QStringLiteral("headText")).toString(),
                 QStringLiteral("At P6 your plane was 65°, in your steepest third, and the path went 4.1° left."));
        QCOMPARE(t.value(QStringLiteral("head")).toMap().value(QStringLiteral("text")).toString(), t.value(QStringLiteral("headText")).toString());

        const QVariantMap story = t.value(QStringLiteral("story")).toMap();
        hasKeys(story, { "available", "text", "parts", "set", "back", "top", "delivered", "hands", "shapeRead", "shapeWord", "shapeLine", "positions" }, "story");
        QCOMPARE(story.value(QStringLiteral("text")).toString(),
                 QStringLiteral("set on 58° · 4° steeper than address at P3 · peaked at the top · delivered at 59°, where you set it. Hands 9° steeper coming down."));
        const QVariantList pos = story.value(QStringLiteral("positions")).toList();
        QCOMPARE(pos.size(), 7);
        QCOMPARE(pos[3].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("the top"));
        QCOMPARE(pos[3].toMap().value(QStringLiteral("value")).toDouble(), 65.0);
        QCOMPARE(pos[3].toMap().value(QStringLiteral("seekUs")).toLongLong(), kImpactUs - 600000);

        const QVariantMap slot = t.value(QStringLiteral("slot")).toMap();
        QCOMPARE(slot.value(QStringLiteral("text")).toString(), QStringLiteral("inside the on-plane window"));
        QCOMPARE(slot.value(QStringLiteral("widthText")).toString(), QStringLiteral("10° wide"));

        const QVariantMap flight = t.value(QStringLiteral("flight")).toMap();
        hasKeys(flight, { "available", "text", "attackText", "thisLine", "fit", "attack", "fits", "place", "scatter" }, "flight");
        const QVariantMap fit = flight.value(QStringLiteral("fit")).toMap();
        hasKeys(fit, { "key", "x", "y", "xLabel", "yLabel", "n", "slope", "intercept", "r", "sd", "slopeText", "rText", "sdText",
                       "holds", "verdict", "why" }, "fit");
        QCOMPARE(fit.value(QStringLiteral("n")).toInt(), 14);
        QVERIFY(fit.value(QStringLiteral("holds")).toBool());
        QCOMPARE(flight.value(QStringLiteral("text")).toString(),
                 QStringLiteral("On your 14 shots with the monitor, each degree steeper at P6 moves your path 0.9° left."));
        QCOMPARE(flight.value(QStringLiteral("thisLine")).toString(), QStringLiteral("This one: 65° at P6, your steepest third → path −4.1°."));
        const QVariantMap sc = flight.value(QStringLiteral("scatter")).toMap();
        QCOMPARE(sc.value(QStringLiteral("points")).toList().size(), 14);
        QVERIFY(sc.value(QStringLiteral("points")).toList().last().toMap().value(QStringLiteral("picked")).toBool());
        QVERIFY(sc.value(QStringLiteral("line")).toMap().contains(QStringLiteral("y1")));
        QCOMPARE(sc.value(QStringLiteral("xLo")).toDouble(), 58.0);
        QCOMPARE(sc.value(QStringLiteral("xHi")).toDouble(), 64.5);
        // rule C3: the x axis is signed only for the hands' steepening, by the fit's series (never its label's words)
        QCOMPARE(sc.value(QStringLiteral("xSigned")).toBool(), fit.value(QStringLiteral("x")).toString() == QLatin1String("steepening"));
        QVERIFY(!sc.value(QStringLiteral("xSigned")).toBool());   // this day's stronger fit is the plane at P6

        const QVariantList impact = t.value(QStringLiteral("impact")).toMap().value(QStringLiteral("lines")).toList();
        QCOMPARE(impact.size(), 3);   // lie (the monitor's), the monitor's loft (no shaft lean on these shots) and attack
        QCOMPARE(impact[0].toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Toe 2.0° up at impact → face about 1.0° left on this 7 iron."));
        QCOMPARE(impact[1].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("The monitor saw 30.0° of loft."));
        QCOMPARE(impact[2].toMap().value(QStringLiteral("source")).toString(), QStringLiteral("monitor"));
        const QVariantMap shape = t.value(QStringLiteral("shape")).toMap();
        QCOMPARE(shape.value(QStringLiteral("word")).toString(), QStringLiteral("Straight start, slight fade"));
        QCOMPARE(t.value(QStringLiteral("replay")).toMap().value(QStringLiteral("shape")).toString(), QStringLiteral("Straight start, slight fade"));
        QVERIFY(!t.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList().isEmpty());

        // the standfirsts and the playhead
        QCOMPARE(t.value(QStringLiteral("standfirsts")).toList().size(), 7);
        QCOMPARE(p.seekUsFor(QStringLiteral("P6")), kImpactUs - 120000);
        QCOMPARE(p.seekUsFor(QStringLiteral("P8")), qint64(-1));
        p.setPlayheadUs(kImpactUs - 120000 + 1000);
        QCOMPARE(p.selected(), 5);
        QCOMPARE(chartsGadgetMap(p.standfirst()).value(QStringLiteral("text")).toString(),
                 QStringLiteral("At P6 the plane was 65°, 7° above where you set it (the hands carry it here)."));
        p.setPlayheadUs(kImpactUs - 1200000);
        QCOMPARE(p.selected(), 0);
        QCOMPARE(chartsGadgetMap(p.standfirst()).value(QStringLiteral("text")).toString(),
                 QStringLiteral("At address the plane is 58°, your shoulders' 68°: an on-plane window 10° wide."));
    }

    void plotLayersAndTheScopeSwitch()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(3));
        QCOMPARE(chartsGadgetMap(p.plotLayers()).keys(), QStringList{ QStringLiteral("swingPlaneCurve") });
        QVariantList ls = layersOf(p);
        QCOMPARE(ls.size(), 4);
        const QVariantMap a = layerOf(ls, QStringLiteral("address")), s = layerOf(ls, QStringLiteral("shoulders"));
        QCOMPARE(a.value(QStringLiteral("k")).toString(), QStringLiteral("hline"));
        QCOMPARE(a.value(QStringLiteral("v")).toDouble(), 58.0);
        QCOMPARE(a.value(QStringLiteral("label")).toString(), QStringLiteral("address"));
        QCOMPARE(s.value(QStringLiteral("v")).toDouble(), 68.0);
        QCOMPARE(s.value(QStringLiteral("label")).toString(), QStringLiteral("shoulders"));
        const QVariantMap slot = layerOf(ls, QStringLiteral("slot"));
        QCOMPARE(slot.value(QStringLiteral("k")).toString(), QStringLiteral("band"));
        QCOMPARE(slot.value(QStringLiteral("t")).toList(), (QVariantList{ -1200.0, 40.0 }));
        QCOMPARE(slot.value(QStringLiteral("lo")).toList(), (QVariantList{ 58.0, 58.0 }));
        QCOMPARE(slot.value(QStringLiteral("hi")).toList(), (QVariantList{ 68.0, 68.0 }));
        const QVariantMap hands = layerOf(ls, QStringLiteral("hands"));
        QCOMPARE(hands.value(QStringLiteral("k")).toString(), QStringLiteral("span"));
        QCOMPARE(hands.value(QStringLiteral("t0")).toDouble(), kHandsFromMs);
        QCOMPARE(hands.value(QStringLiteral("t1")).toDouble(), kHandsToMs);
        QCOMPARE(hands.value(QStringLiteral("label")).toString(), QStringLiteral("hands carry the plane here"));
        QVERIFY(hands.value(QStringLiteral("labelled")).toBool());

        // Session: the session's medians, and no stretch (a stretch is one shot's)
        p.setScope(QStringLiteral("session"));
        QCOMPARE(chartsGadgetMap(p.choices()).value(QStringLiteral("scope")).toString(), QStringLiteral("session"));
        ls = layersOf(p);
        QCOMPARE(ls.size(), 3);
        QVERIFY(layerOf(ls, QStringLiteral("hands")).isEmpty());
        QCOMPARE(layerOf(ls, QStringLiteral("address")).value(QStringLiteral("v")).toDouble(), 58.0);
        QCOMPARE(layerOf(ls, QStringLiteral("shoulders")).value(QStringLiteral("v")).toDouble(), 68.0);
        // and back
        p.setScope(QStringLiteral("shot"));
        QCOMPARE(layersOf(p).size(), 4);
        // no picked shot on This shot: no lines
        p.setPickedSwingDir(QString());
        QVERIFY(p.plotLayers().swingPlaneCurve.isEmpty());   // no lines: the row's list is empty (a typed value always has its key)
    }

    void sessionPayload()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(2));
        p.setScope(QStringLiteral("session"));
        const QVariantMap s = chartsGadgetMap(p.session());
        hasKeys(s, { "available", "n", "shots", "planeShots", "monitorShots", "small", "kicker", "head", "headText", "band", "varies",
                     "usual", "flight", "shapes", "trust" }, "session");
        QCOMPARE(s.value(QStringLiteral("n")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("monitorShots")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("kicker")).toString(), QStringLiteral("14 shots  ·  7 iron  ·  Club delivery"));
        QCOMPARE(s.value(QStringLiteral("headText")).toString(),
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0.9° further left (14 shots)."));
        const QVariantMap band = s.value(QStringLiteral("band")).toMap();
        QVERIFY(band.value(QStringLiteral("shown")).toBool());
        const QVariantList pos = band.value(QStringLiteral("positions")).toList();
        QCOMPARE(pos.size(), 7);
        const QVariantMap p6 = pos[5].toMap();
        hasKeys(p6, { "index", "tag", "name", "box", "median", "medianText", "wide", "half", "halfText", "picked", "values" }, "band position");
        QCOMPARE(p6.value(QStringLiteral("median")).toDouble(), 61.25);
        QCOMPARE(p6.value(QStringLiteral("picked")).toDouble(), 58.5);
        QCOMPARE(p6.value(QStringLiteral("values")).toList().size(), 14);
        const QVariantMap varies = s.value(QStringLiteral("varies")).toMap();
        QVERIFY(varies.value(QStringLiteral("has")).toBool());
        QCOMPARE(varies.value(QStringLiteral("loosest")).toString(), QStringLiteral("P6"));
        QVERIFY(varies.value(QStringLiteral("words")).toString().endsWith(QStringLiteral("— your path follows your plane (r −0.99).")));
        const QVariantMap marks = s.value(QStringLiteral("shapes")).toMap();
        QCOMPARE(marks.value(QStringLiteral("marks")).toList().size(), 14);
        QVERIFY(marks.value(QStringLiteral("marks")).toList()[1].toMap().value(QStringLiteral("picked")).toBool());
        QCOMPARE(marks.value(QStringLiteral("line")).toString(), QStringLiteral("Straight start, slight fade on 14 of 14 shots"));
        QVERIFY(marks.value(QStringLiteral("marks")).toList()[1].toMap().contains(QStringLiteral("fromBall")));   // the report's grid says what it read
        // rule C6: the grid's hover words are the presenter's, by the swing number
        QCOMPARE(marks.value(QStringLiteral("marks")).toList()[1].toMap().value(QStringLiteral("hoverText")).toString(),
                 QStringLiteral("Shot 2  Straight start, slight fade"));
        // the folded section keeps the head's first clause (words::firstClauseOf: up to the first ';' or '.')
        QCOMPARE(s.value(QStringLiteral("head")).toMap().value(QStringLiteral("clause")).toString(),
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0"));
        const QVariantMap sc = s.value(QStringLiteral("flight")).toMap().value(QStringLiteral("scatter")).toMap();
        QCOMPARE(sc.value(QStringLiteral("points")).toList().size(), 14);

        // the report's page: the same shape, no picked shot, its own noun
        const QVariantMap r = reportPayloadOf(deliverySessionReport(*makeSession(14), QStringLiteral("swings")));
        QCOMPARE(r.keys(), s.keys());
        QCOMPARE(r.value(QStringLiteral("kicker")).toString(), QStringLiteral("14 swings  ·  7 iron  ·  Club delivery"));
        QVERIFY(r.value(QStringLiteral("band")).toMap().value(QStringLiteral("positions")).toList()[5].toMap().value(QStringLiteral("picked")).isNull());
    }

    // A shot not in the snapshot reads from its own detail; one with no monitor says the rule 3 head.
    void shotFromDetailAndNoMonitor()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        auto live = makeShot(99, false);
        live->swingDir = QStringLiteral("/elsewhere/2026-10-30_Live/swing_0099");
        p.setPickedDetail(detailOf(*live));
        p.setPickedSwingDir(live->swingDir);
        const QVariantMap t = chartsGadgetMap(p.thisShot());
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QVERIFY(!t.value(QStringLiteral("inSession")).toBool());
        QVERIFY(t.value(QStringLiteral("kicker")).toString().startsWith(QStringLiteral("This swing")));
        QCOMPARE(t.value(QStringLiteral("mode")).toString(), QStringLiteral("noMonitor"));
        QCOMPARE(t.value(QStringLiteral("headRule")).toInt(), 3);
        // the session's fit still places it, said as the fit's, never as a measurement
        QVERIFY(t.value(QStringLiteral("flight")).toMap().value(QStringLiteral("thisLine")).toString()
                    .contains(QStringLiteral("on your fit, a plane like this goes with a path about")));
        QCOMPARE(layerOf(layersOf(p), QStringLiteral("hands")).value(QStringLiteral("t0")).toDouble(), kHandsFromMs);
    }

    void golfersDay()
    {
        // An absolute folder on every platform (#260): the presenter places a neighbour's swing folder beside this one through
        // QFileInfo::absolutePath, which on Windows gives a drive-less "/fixture/..." its drive. Path text only: nothing is
        // written under it.
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString golfer = QDir::cleanPath(root.path() + QStringLiteral("/Golfer"));
        const QString cur = golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_01");
        const QDate day(2026, 10, 20);
        // the sibling: shots 7..14 of the fit's line, as swings 1..8; another day's eight never pooled
        std::vector<std::pair<int, int>> sibShots, otherShots;
        for (int k = 1; k <= 8; ++k) { sibShots.push_back({ k, 6 + k }); otherShots.push_back({ k, k }); }
        SessionNeighbours n = neighbours(cur, day, 1, { summed(QStringLiteral("2026-10-21_Golfer_Wrist_01"), day.addDays(1), 1, otherShots),
                                                        summed(QStringLiteral("2026-10-20_Golfer_Wrist_02"), day, 2, sibShots) });
        DoorScope door(n);

        // a shot's day reading (its summary's) agrees with the same shot read from the snapshot: the one rule
        {
            const DeliveryShotInput b = deliveryInputFromShot(*makeShot(9));
            const pinpoint::analysis::ShotDeliveryReading &a = n.others[1].readings.delivery[2];   // the sibling's swing 3 = shot 9
            QCOMPARE(a.shotId, 3);
            QCOMPARE(a.planeP6, b.plane[5]);
            QCOMPARE(a.steepening, b.steepening);
            QCOMPARE(a.lmPath, b.lmPath);
            QCOMPARE(a.lmAttack, b.lmAttack);
        }

        // this session: shots 1..6 (too few alone)
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = cur;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->swingDir = cur + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
            snap->shots.push_back(s);
        }
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setPickedSwingDir(snap->shots[5]->swingDir);
        const QVariantMap flight = chartsGadgetMap(p.thisShot()).value(QStringLiteral("flight")).toMap();
        const QVariantMap fit = flight.value(QStringLiteral("fit")).toMap();
        QCOMPARE(fit.value(QStringLiteral("n")).toInt(), 14);
        QVERIFY(fit.value(QStringLiteral("holds")).toBool());
        QCOMPARE(flight.value(QStringLiteral("text")).toString(),
                 QStringLiteral("On your 14 shots with the monitor on 20 Oct (2 sessions), each degree steeper at P6 moves your path 0.9° left."));
        const QVariantMap pool = flight.value(QStringLiteral("pool")).toMap();
        QCOMPARE(pool.value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-20"));
        QCOMPARE(pool.value(QStringLiteral("sessions")).toInt(), 2);
        QCOMPARE(pool.value(QStringLiteral("shots")).toInt(), 14);
        QCOMPARE(pool.value(QStringLiteral("otherShots")).toInt(), 8);
        QCOMPARE(pool.value(QStringLiteral("notSummedUp")).toInt(), 0);
        QCOMPARE(pool.value(QStringLiteral("newer")).toInt(), 0);
        const QVariantList pts = flight.value(QStringLiteral("scatter")).toMap().value(QStringLiteral("points")).toList();
        QCOMPARE(pts.size(), 14);
        int mine = 0, theirs = 0, picked = 0;
        for (const QVariant &v : pts) {
            const QVariantMap m = v.toMap();
            QVERIFY(!m.value(QStringLiteral("session")).toString().startsWith(QStringLiteral("2026-10-21")));
            if (m.value(QStringLiteral("thisSession")).toBool()) ++mine;
            else {
                ++theirs;
                QCOMPARE(m.value(QStringLiteral("session")).toString(), QStringLiteral("2026-10-20_Golfer_Wrist_02"));
                // named by its place in the day (SessionIdentity's n-of-day, step 2.7b), numbered by its swing folder
                QCOMPARE(m.value(QStringLiteral("sessionLabel")).toString(), QStringLiteral("session 2"));
                QCOMPARE(m.value(QStringLiteral("shotNo")).toInt(), m.value(QStringLiteral("ordinal")).toInt());
                QVERIFY(m.value(QStringLiteral("swingDir")).toString().startsWith(golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_02/swing_")));
            }
            if (m.value(QStringLiteral("picked")).toBool()) { ++picked; QVERIFY(m.value(QStringLiteral("thisSession")).toBool()); }
        }
        QCOMPARE(mine, 6);
        QCOMPARE(theirs, 8);
        QCOMPARE(picked, 1);
        // the session view's flight is the same pool; its band stays the session's
        const QVariantMap s = chartsGadgetMap(p.session());
        QCOMPARE(s.value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("headText")).toString(),
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0.9° further left (14 shots on 20 Oct, 2 sessions)."));
        QCOMPARE(s.value(QStringLiteral("band")).toMap().value(QStringLiteral("n")).toInt(), 6);
        // the report's page pools the same day from the same neighbours; with none, the session alone
        const auto nOf = [](const QVariantMap &section) {
            return section.value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt();
        };
        QCOMPARE(nOf(reportPayloadOf(deliverySessionReport(*snap, QStringLiteral("shots"), n))), 14);
        QCOMPARE(nOf(reportPayloadOf(deliverySessionReport(*snap))), 6);
    }

    // THE DAY FOLLOWS THE ATHLETE LEVEL (refactor 5.5d): a summary written on the day (the athlete level's Changed, heard
    // through the door's watch) reads the day again — a third session's one shot raises n.
    void dayFollowsTheAthleteLevel()
    {
        const QString cur = QStringLiteral("/fixture/Golfer/2026-10-20_Golfer_Wrist_01");
        const QDate day(2026, 10, 20);
        std::vector<std::pair<int, int>> sibShots;
        for (int k = 1; k <= 8; ++k) sibShots.push_back({ k, 6 + k });
        auto held = std::make_shared<SessionNeighbours>(neighbours(cur, day, 1, { summed(QStringLiteral("2026-10-20_Golfer_Wrist_02"), day, 2, sibShots) }));
        auto changed = std::make_shared<std::function<void()>>();
        NeighboursDoor d;
        d.read = [held](const QString &) { return *held; };
        d.watch = [changed](QObject *, const QString &, std::function<void()> cb) { *changed = std::move(cb); return QList<QMetaObject::Connection>{}; };
        setNeighboursDoor(d);
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = cur;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->swingDir = cur + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
            snap->shots.push_back(s);
        }
        ChartsDeliveryPresenter p;   // asynchronous: its job reads the door on the GUI thread and watches it
        p.setSnapshot(snap);
        QVERIFY(p.waitForIdle(20000));
        const auto nOf = [&p]() { return chartsGadgetMap(p.session()).value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(); };
        QCOMPARE(nOf(), 14);
        QVERIFY(*changed);
        held->others.push_back(summed(QStringLiteral("2026-10-20_Golfer_Wrist_03"), day, 3, { { 1, 8 } }));
        (*changed)();
        QTRY_VERIFY_WITH_TIMEOUT(!p.busy() && nOf() == 15, 20000);
        setNeighboursDoor({});
    }

    // A SESSION OF THE DAY NOT SUMMED UP YET (a summary older than version 4: the catch-up writes it again, minutes after a
    // new build's first launch), or a newer build's, is left out of the pool and said, never left out silently.
    void dayNotSummedUp()
    {
        const QString cur = QStringLiteral("/fixture/Golfer/2026-10-20_Golfer_Wrist_01");
        const QDate day(2026, 10, 20);
        std::vector<std::pair<int, int>> sibShots;
        for (int k = 1; k <= 8; ++k) sibShots.push_back({ k, 6 + k });
        NeighbourSession notYet, newer;
        notYet.place = { QStringLiteral("2026-10-20_Golfer_Wrist_03"), day, 3 };
        notYet.state = NeighbourSession::State::NotYet;
        newer.place = { QStringLiteral("2026-10-20_Golfer_Wrist_04"), day, 4 };
        newer.state = NeighbourSession::State::Newer;
        const SessionNeighbours n = neighbours(cur, day, 1, { summed(QStringLiteral("2026-10-20_Golfer_Wrist_02"), day, 2, sibShots), notYet, newer });
        DoorScope door(n);
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = cur;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->swingDir = cur + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0'));
            snap->shots.push_back(s);
        }
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        const QVariantMap sess = chartsGadgetMap(p.session());
        const QVariantMap pool = sess.value(QStringLiteral("flight")).toMap().value(QStringLiteral("pool")).toMap();
        QCOMPARE(pool.value(QStringLiteral("sessions")).toInt(), 2);
        QCOMPARE(pool.value(QStringLiteral("otherShots")).toInt(), 8);
        QCOMPARE(pool.value(QStringLiteral("notSummedUp")).toInt(), 1);
        QCOMPARE(pool.value(QStringLiteral("newer")).toInt(), 1);
        const QStringList lines = sess.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList();
        QVERIFY2(lines.contains(QStringLiteral("One other session from this day isn't counted in the fits yet: it is still being brought up to date.")),
                 qPrintable(lines.join(QLatin1Char('\n'))));
        QVERIFY(lines.contains(QStringLiteral("One other session from this day isn't counted in the fits: this version of PinPoint Studio can't read it in full yet.")));
        // a day whose only other session is not summed up yet: the session's own wording, and the line
        DoorScope alone(neighbours(cur, day, 1, { notYet, notYet }));
        p.setSnapshot(std::make_shared<SessionSnapshot>(*snap));
        const QVariantMap s2 = chartsGadgetMap(p.session());
        QCOMPARE(s2.value(QStringLiteral("flight")).toMap().value(QStringLiteral("pool")).toMap().value(QStringLiteral("sessions")).toInt(), 1);
        QVERIFY(s2.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList()
                    .contains(QStringLiteral("Two other sessions from this day aren't counted in the fits yet: they are still being brought up to date.")));
    }

    // "SHOT N" AFTER A GAP, AND THE DAY'S PLACE (step 2.7b, audit F-06 and E-06): the day's sessions are placed by the
    // athlete level (SessionIdentity's n-of-day), never by folder names — under `name-date-type` no folder starts with its
    // date. The sibling first in the day is "session 1". This session lost swing 4 to the trash: its six shots are swings
    // 1–3 and 5–7, and the kicker names the picked one by its swing number.
    void golfersDayNameDateType()
    {
        const QString cur = QStringLiteral("/fixture/Golfer/Golfer_2026-10-20_Wrist_02");
        const QDate day(2026, 10, 20);
        std::vector<std::pair<int, int>> sibShots, otherShots;
        for (int k = 1; k <= 8; ++k) { sibShots.push_back({ k, 6 + k }); otherShots.push_back({ k, k }); }
        DoorScope door(neighbours(cur, day, 2, { summed(QStringLiteral("Golfer_2026-10-20_Wrist_01"), day, 1, sibShots),
                                                 summed(QStringLiteral("Golfer_2026-10-21_Wrist_01"), day.addDays(1), 1, otherShots) }));

        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = cur;
        for (int i = 1; i <= 6; ++i) {
            auto s = makeShot(i);
            s->shotId = i < 4 ? i : i + 1;                                  // swing 4 trashed
            s->swingDir = cur + QStringLiteral("/swing_%1").arg(s->shotId, 4, 10, QLatin1Char('0'));
            snap->shots.push_back(s);
        }
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setPickedSwingDir(snap->shots[5]->swingDir);
        const QVariantMap t = chartsGadgetMap(p.thisShot());
        QCOMPARE(t.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 7 · 6 of 6  ·  7 iron  ·  Club delivery"));
        const QVariantMap flight = t.value(QStringLiteral("flight")).toMap();
        QCOMPARE(flight.value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(), 14);
        const QVariantMap pool = flight.value(QStringLiteral("pool")).toMap();
        QCOMPARE(pool.value(QStringLiteral("date")).toString(), QStringLiteral("2026-10-20"));
        QCOMPARE(pool.value(QStringLiteral("sessions")).toInt(), 2);
        int theirs = 0;
        for (const QVariant &v : flight.value(QStringLiteral("scatter")).toMap().value(QStringLiteral("points")).toList()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("thisSession")).toBool()) {
                QVERIFY(m.value(QStringLiteral("sessionLabel")).toString().isEmpty());
                const int ord = m.value(QStringLiteral("ordinal")).toInt();
                QCOMPARE(m.value(QStringLiteral("shotNo")).toInt(), ord < 4 ? ord : ord + 1);   // the swing number
                continue;
            }
            ++theirs;
            QCOMPARE(m.value(QStringLiteral("session")).toString(), QStringLiteral("Golfer_2026-10-20_Wrist_01"));
            QCOMPARE(m.value(QStringLiteral("sessionLabel")).toString(), QStringLiteral("session 1"));   // first in the day
        }
        QCOMPARE(theirs, 8);
        // the shape grid's marks carry the number their label prints
        const QVariantList marks = chartsGadgetMap(p.session()).value(QStringLiteral("shapes")).toMap().value(QStringLiteral("marks")).toList();
        QCOMPARE(marks.size(), 6);
        QCOMPARE(marks[3].toMap().value(QStringLiteral("ordinal")).toInt(), 4);
        QCOMPARE(marks[3].toMap().value(QStringLiteral("shotNo")).toInt(), 5);
    }

    // The asynchronous path: one coalesced job, delivered by a queued call.
    void asynchronous()
    {
        ChartsDeliveryPresenter p;
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(1));
        QVERIFY(p.waitForIdle(20000));
        QVERIFY(p.available());
        QCOMPARE(chartsGadgetMap(p.thisShot()).value(QStringLiteral("ordinal")).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(ChartsDeliveryPresenterTest)
#include "charts_delivery_presenter_test.moc"
