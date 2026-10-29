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
//   · the golfer's day (session_delivery.h decision 13): a temp golfer folder with a sibling session of the same date,
//     its swings as swing_phasegrid.json files only (written by the app's own savePhaseGrid), is pooled into the fit
//     and the scatter, another date is not; a shot read from its sidecar agrees with the same shot read from the
//     snapshot; the day is cached until its folders change.

#include "charts/charts_delivery_presenter.h"
#include "charts/session_delivery.h"
#include "charts/session_series_types.h"
#include "Diagnostics/measures/measure_sample.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

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
        instant(QStringLiteral("lm.faceAngle"), 5, -2.0);
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

void hasKeys(const QVariantMap &m, const QStringList &keys, const char *what)
{
    for (const QString &k : keys) QVERIFY2(m.contains(k), qPrintable(QStringLiteral("%1 lacks %2").arg(QString::fromLatin1(what), k)));
}

QVariantList layersOf(ChartsDeliveryPresenter &p) { return p.plotLayers().value(QStringLiteral("swingPlaneCurve")).toList(); }
QVariantMap layerOf(const QVariantList &ls, const QString &role)
{
    for (const QVariant &l : ls) if (l.toMap().value(QStringLiteral("role")).toString() == role) return l.toMap();
    return {};
}

} // namespace

class ChartsDeliveryPresenterTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyModel()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(std::make_shared<SessionSnapshot>());
        QVERIFY(!p.available());
        QVERIFY(!p.thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(p.thisShot().value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("Select a swing to review.")));
        QVERIFY(!p.session().value(QStringLiteral("available")).toBool());
        QVERIFY(p.plotLayers().isEmpty());
        QCOMPARE(p.seekUsFor(QStringLiteral("P4")), qint64(-1));
        QCOMPARE(p.choices().value(QStringLiteral("onShowKeys")).toStringList(), deliveryOnShowKeys());
    }

    void thisShotPayload()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(14));
        QVERIFY(p.available());
        const QVariantMap t = p.thisShot();
        hasKeys(t, { "available", "empty", "ordinal", "shotId", "swingDir", "inSession", "kicker", "mode", "hasPlane", "hasMonitor",
                     "head", "headText", "headRule", "story", "slot", "flight", "impact", "shape", "trust", "replay", "standfirsts" }, "thisShot");
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QCOMPARE(t.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 14 of 14  ·  7-iron  ·  Club delivery"));
        QCOMPARE(t.value(QStringLiteral("mode")).toString(), QStringLiteral("full"));
        QCOMPARE(t.value(QStringLiteral("headRule")).toInt(), 1);
        QCOMPARE(t.value(QStringLiteral("headText")).toString(),
                 QStringLiteral("At P6 your plane was 65°, in your steepest third, and the path went 4.1° left."));
        QCOMPARE(t.value(QStringLiteral("head")).toMap().value(QStringLiteral("text")).toString(), t.value(QStringLiteral("headText")).toString());

        const QVariantMap story = t.value(QStringLiteral("story")).toMap();
        hasKeys(story, { "available", "text", "parts", "set", "back", "top", "delivered", "hands", "shapeRead", "shapeWord", "shapeLine", "positions" }, "story");
        QCOMPARE(story.value(QStringLiteral("text")).toString(),
                 QStringLiteral("set on 58° · 4° steeper at P3 · peaked at the top · delivered at 59°, where you set it. Hands 9° steeper coming down."));
        const QVariantList pos = story.value(QStringLiteral("positions")).toList();
        QCOMPARE(pos.size(), 7);
        QCOMPARE(pos[3].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("the top"));
        QCOMPARE(pos[3].toMap().value(QStringLiteral("value")).toDouble(), 65.0);
        QCOMPARE(pos[3].toMap().value(QStringLiteral("seekUs")).toLongLong(), kImpactUs - 600000);

        const QVariantMap slot = t.value(QStringLiteral("slot")).toMap();
        QCOMPARE(slot.value(QStringLiteral("text")).toString(), QStringLiteral("inside the slot"));
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

        const QVariantList impact = t.value(QStringLiteral("impact")).toMap().value(QStringLiteral("lines")).toList();
        QCOMPARE(impact.size(), 3);   // lie (the monitor's), the monitor's loft (no shaft lean on these shots) and attack
        QCOMPARE(impact[0].toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Toe 2.0° up at impact → face about 1.0° left on this 7-iron."));
        QCOMPARE(impact[1].toMap().value(QStringLiteral("text")).toString(), QStringLiteral("The monitor saw 30.0° of loft."));
        QCOMPARE(impact[2].toMap().value(QStringLiteral("source")).toString(), QStringLiteral("monitor"));
        const QVariantMap shape = t.value(QStringLiteral("shape")).toMap();
        QCOMPARE(shape.value(QStringLiteral("word")).toString(), QStringLiteral("Pull, slight fade"));
        QCOMPARE(t.value(QStringLiteral("replay")).toMap().value(QStringLiteral("shape")).toString(), QStringLiteral("Pull, slight fade"));
        QVERIFY(!t.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList().isEmpty());

        // the standfirsts and the playhead
        QCOMPARE(t.value(QStringLiteral("standfirsts")).toList().size(), 7);
        QCOMPARE(p.seekUsFor(QStringLiteral("P6")), kImpactUs - 120000);
        QCOMPARE(p.seekUsFor(QStringLiteral("P8")), qint64(-1));
        p.setPlayheadUs(kImpactUs - 120000 + 1000);
        QCOMPARE(p.selected(), 5);
        QCOMPARE(p.standfirst().value(QStringLiteral("text")).toString(),
                 QStringLiteral("At P6 the plane was 65°, 7° above where you set it (the hands carry it here)."));
        p.setPlayheadUs(kImpactUs - 1200000);
        QCOMPARE(p.selected(), 0);
        QCOMPARE(p.standfirst().value(QStringLiteral("text")).toString(),
                 QStringLiteral("At address the plane is 58°, your shoulders' 68°: a slot 10° wide."));
    }

    void plotLayersAndTheScopeSwitch()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(3));
        QCOMPARE(p.plotLayers().keys(), QStringList{ QStringLiteral("swingPlaneCurve") });
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
        QCOMPARE(p.choices().value(QStringLiteral("scope")).toString(), QStringLiteral("session"));
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
        QVERIFY(p.plotLayers().isEmpty());
    }

    void sessionPayload()
    {
        ChartsDeliveryPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(2));
        p.setScope(QStringLiteral("session"));
        const QVariantMap s = p.session();
        hasKeys(s, { "available", "n", "shots", "planeShots", "monitorShots", "small", "kicker", "head", "headText", "band", "varies",
                     "usual", "flight", "shapes", "trust" }, "session");
        QCOMPARE(s.value(QStringLiteral("n")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("monitorShots")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("kicker")).toString(), QStringLiteral("14 shots  ·  7-iron  ·  Club delivery"));
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
        QCOMPARE(marks.value(QStringLiteral("line")).toString(), QStringLiteral("Pull, slight fade on 14 of 14 shots"));
        const QVariantMap sc = s.value(QStringLiteral("flight")).toMap().value(QStringLiteral("scatter")).toMap();
        QCOMPARE(sc.value(QStringLiteral("points")).toList().size(), 14);

        // the report's page: the same shape, no picked shot, its own noun
        const QVariantMap r = deliverySessionReportPayload(*makeSession(14), QStringLiteral("swings"));
        QCOMPARE(r.keys(), s.keys());
        QCOMPARE(r.value(QStringLiteral("kicker")).toString(), QStringLiteral("14 swings  ·  7-iron  ·  Club delivery"));
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
        const QVariantMap t = p.thisShot();
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
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString golfer = tmp.filePath(QStringLiteral("Golfer"));
        const QString cur = golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_01");
        const QString sib = golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_02");
        const QString other = golfer + QStringLiteral("/2026-10-21_Golfer_Wrist_01");
        // a sibling's swing as its sidecar alone (no swing document; one gets a stand-in document of another size)
        const auto writeGrid = [](const QString &session, int folder, int i, bool staleDoc) {
            const QString dir = session + QStringLiteral("/swing_%1").arg(folder, 4, 10, QLatin1Char('0'));
            QDir().mkpath(dir);
            pinpoint::analysis::SwingPhaseGrid g = deliveryPhaseGridOf(*makeShot(i));
            g.sessionId = QFileInfo(session).fileName();
            g.ordinal = folder;
            QFile f(dir + QStringLiteral("/swing_phasegrid.json"));
            if (!f.open(QIODevice::WriteOnly)) return false;
            f.write(QJsonDocument(pinpoint::analysis::savePhaseGrid(g, 1234, 5678)).toJson(QJsonDocument::Compact));
            if (staleDoc) { QFile d(dir + QStringLiteral("/swing.ppsw")); if (d.open(QIODevice::WriteOnly)) d.write("x"); }
            return true;
        };
        QVERIFY(QDir().mkpath(cur));
        for (int k = 1; k <= 8; ++k) QVERIFY(writeGrid(sib, k, 6 + k, k == 1));     // shots 7..14 of the fit's line
        for (int k = 1; k <= 8; ++k) QVERIFY(writeGrid(other, k, k, false));        // another date: never pooled

        // a shot read from its sidecar agrees with the same shot read from the snapshot
        {
            QFile f(sib + QStringLiteral("/swing_0003/swing_phasegrid.json"));
            QVERIFY(f.open(QIODevice::ReadOnly));
            bool ok = false;
            const DeliveryShotInput a = deliveryInputFromPhaseGrid(QJsonDocument::fromJson(f.readAll()).object(), QStringLiteral("x"), &ok);
            const DeliveryShotInput b = deliveryInputFromShot(*makeShot(9));
            QVERIFY(ok);
            for (int p = 0; p < kDeliveryPositions; ++p) {
                QCOMPARE(a.plane[size_t(p)], b.plane[size_t(p)]);
                QCOMPARE(a.hands[size_t(p)], b.hands[size_t(p)]);
            }
            QCOMPARE(a.steepening, b.steepening);
            QCOMPARE(a.shoulderPlane, b.shoulderPlane);
            QCOMPARE(a.lmPath, b.lmPath);
            QCOMPARE(a.lmAttack, b.lmAttack);
            QCOMPARE(a.planeSigma, b.planeSigma);
            QCOMPARE(a.session, QStringLiteral("2026-10-20_Golfer_Wrist_02"));
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
        const QVariantMap flight = p.thisShot().value(QStringLiteral("flight")).toMap();
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
        QCOMPARE(pool.value(QStringLiteral("staleGrids")).toInt(), 1);
        const QVariantList pts = flight.value(QStringLiteral("scatter")).toMap().value(QStringLiteral("points")).toList();
        QCOMPARE(pts.size(), 14);
        int mine = 0, theirs = 0, picked = 0;
        for (const QVariant &v : pts) {
            const QVariantMap m = v.toMap();
            QVERIFY(!m.value(QStringLiteral("session")).toString().startsWith(QStringLiteral("2026-10-21")));
            if (m.value(QStringLiteral("thisSession")).toBool()) ++mine;
            else { ++theirs; QCOMPARE(m.value(QStringLiteral("session")).toString(), QStringLiteral("2026-10-20_Golfer_Wrist_02")); }
            if (m.value(QStringLiteral("picked")).toBool()) { ++picked; QVERIFY(m.value(QStringLiteral("thisSession")).toBool()); }
        }
        QCOMPARE(mine, 6);
        QCOMPARE(theirs, 8);
        QCOMPARE(picked, 1);
        // the session view's flight is the same pool; its band stays the session's
        const QVariantMap s = p.session();
        QCOMPARE(s.value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(), 14);
        QCOMPARE(s.value(QStringLiteral("headText")).toString(),
                 QStringLiteral("Your path follows your plane: steeper at P6 by a degree, 0.9° further left (14 shots on 20 Oct, 2 sessions)."));
        QCOMPARE(s.value(QStringLiteral("band")).toMap().value(QStringLiteral("n")).toInt(), 6);
        // the report's page pools the same day
        QCOMPARE(deliverySessionReportPayload(*snap).value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap()
                     .value(QStringLiteral("n")).toInt(), 14);

        // cached per day: a sidecar gone from inside a swing folder is not re-read while the day's folders stand …
        QVERIFY(QFile::remove(sib + QStringLiteral("/swing_0002/swing_phasegrid.json")));
        p.setPickedSwingDir(snap->shots[0]->swingDir);
        QCOMPARE(p.thisShot().value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(), 14);
        // … and a new session folder on the day re-reads it
        QVERIFY(QDir().mkpath(golfer + QStringLiteral("/2026-10-20_Golfer_Wrist_03")));
        p.setPickedSwingDir(snap->shots[1]->swingDir);
        QCOMPARE(p.thisShot().value(QStringLiteral("flight")).toMap().value(QStringLiteral("fit")).toMap().value(QStringLiteral("n")).toInt(), 13);
    }

    // The asynchronous path: one coalesced job, delivered by a queued call.
    void asynchronous()
    {
        ChartsDeliveryPresenter p;
        p.setSnapshot(makeSession(14));
        p.setPickedSwingDir(dirOf(1));
        QVERIFY(p.waitForIdle(20000));
        QVERIFY(p.available());
        QCOMPARE(p.thisShot().value(QStringLiteral("ordinal")).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(ChartsDeliveryPresenterTest)
#include "charts_delivery_presenter_test.moc"
