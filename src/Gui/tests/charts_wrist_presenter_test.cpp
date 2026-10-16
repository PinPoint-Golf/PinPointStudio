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

// ChartsWristPresenter (build/wrist-tab/CONTRACT.md I4) on a three-shot synthetic session built in
// memory: the wrist unit's two lead-wrist series, a phase at every checkpoint, each shot's wrist profile
// graded by the real engine against the shipped norms (session_wrist.h), and ledger rows for the wrist
// conditions of the shipped pack (Condition::domain == "wrist"). Synchronous; no files but the pack and
// the norms (PINPOINT_CORE_PACK / PINPOINT_CORE_NORMS).
//
//   shot 1  cupped at the top FIRED (a session pattern); its axis partner and the impact axis clean
//   shot 2  cupped at the top FIRED
//   shot 3  nothing fired, every wrist row clean — but bowed/cupped −60° at the top, outside any band (W-D6)

#include "charts/charts_wrist_presenter.h"
#include "charts/session_series_types.h"
#include "charts/session_wrist.h"

#include <QStandardPaths>
#include <QTest>

#include <cmath>
#include <utility>
#include <vector>

using namespace pinpoint::charts;

namespace {

QString text(const QVariant &sentence) { return sentence.toMap().value(QStringLiteral("text")).toString(); }
QString dirOf(int i) { return QStringLiteral("/wx/2026-08-18_Mark-Liversedge_Wrist_02/swing_%1").arg(i, 4, 10, QLatin1Char('0')); }

double curve(double ms, const std::vector<std::pair<double, double>> &k)
{
    for (size_t i = 1; i < k.size(); ++i)
        if (ms <= k[i].first) return k[i - 1].second + (k[i].second - k[i - 1].second) * (ms - k[i - 1].first) / (k[i].first - k[i - 1].first);
    return k.back().second;
}

// (phase, ms) — Address, Takeaway, P2 (shaft-parallel back), P3, Top, P5, P6, Impact, P8, Finish. P2 at
// both its candidate phases, so the case holds whichever the engine samples.
const std::vector<std::pair<int, int>> kPhases{ { 0, 0 }, { 1, 100 }, { 12, 300 }, { 8, 500 }, { 2, 800 },
                                                { 13, 1000 }, { 9, 1080 }, { 5, 1150 }, { 14, 1250 }, { 7, 1600 } };

std::shared_ptr<ShotSeries> makeShot(int ordinal, double topFlex, bool withIndex = true, bool withForearmRotation = false)
{
    auto s = std::make_shared<ShotSeries>();
    s->ordinal = ordinal;
    s->shotId = ordinal;
    s->swingDir = dirOf(ordinal);
    s->club = QStringLiteral("7 IRON");
    s->impactUs = 1150000;
    for (const auto &p : kPhases) s->phases.push_back(PhaseMark{ p.first, qint64(p.second) * 1000, 1.0 });
    MetricSeries fe, ru;
    fe.key = QStringLiteral("hm.leadWristFlexExt"); fe.unit = QStringLiteral("°"); fe.label = QStringLiteral("Lead wrist flex/ext");
    ru.key = QStringLiteral("hm.leadWristRadUln");  ru.unit = QStringLiteral("°"); ru.label = QStringLiteral("Lead wrist rad/uln");
    for (int ms = 0; ms <= 1600; ms += 10) {
        fe.tUs.push_back(qint64(ms) * 1000);
        ru.tUs.push_back(qint64(ms) * 1000);
        fe.value.push_back(curve(ms, { { 0, 0 }, { 800, topFlex }, { 1150, 10 }, { 1600, 20 } }) + 0.5 * ordinal);
        ru.value.push_back(curve(ms, { { 0, 0 }, { 800, 30 }, { 1150, 5 }, { 1600, 0 } }));
    }
    s->metricOrder << fe.key << ru.key;
    s->metrics.insert(fe.key, fe);
    s->metrics.insert(ru.key, ru);
    if (withForearmRotation) {                   // the forearm's rotation from address, not the roll strip
        MetricSeries fr;
        fr.key = QStringLiteral("hm.forearmRotation"); fr.unit = QStringLiteral("°"); fr.label = QStringLiteral("Lead forearm rotation");
        for (int ms = 0; ms <= 1600; ms += 10) { fr.tUs.push_back(qint64(ms) * 1000); fr.value.push_back(curve(ms, { { 0, 0 }, { 800, 40 }, { 1600, -20 } })); }
        s->metricOrder << fr.key;
        s->metrics.insert(fr.key, fr);
    }
    s->wrist = shotWristPayload(*s, -1, withIndex ? QVariantMap{ { QStringLiteral("neutral"), 84 }, { QStringLiteral("bowed"), 7 },
                                                                 { QStringLiteral("cupped"), 50 } }
                                                  : QVariantMap{});
    return s;
}

void fire(ShotSeries &s, const QString &id, int phase)
{
    s.diagnostics.fired << id;
    s.diagnostics.fired.sort();
    ShotDiagnostics::Value v;
    v.value = -30; v.lo = -20; v.hi = 10; v.z = -2; v.dir = -1; v.confidence = 0.75;
    s.diagnostics.values.insert(id, v);
    ShotDiagnostics::Reading r;
    r.metricKey = QStringLiteral("hm.leadWristFlexExt");
    r.phase = phase;
    s.diagnostics.drivenBy.insert(id, r);
}
void clean(ShotSeries &s, const QStringList &ids)
{
    for (const QString &id : ids) {
        s.diagnostics.clean << id;
        ShotDiagnostics::Value v;
        v.value = 0; v.lo = -20; v.hi = 10; v.z = 0.1;
        s.diagnostics.values.insert(id, v);
    }
    s.diagnostics.clean.sort();
}

QVariantMap findIn(const QVariantList &l, const QString &key, const QString &value)
{
    for (const QVariant &v : l) if (v.toMap().value(key).toString() == value) return v.toMap();
    return {};
}

} // namespace

class ChartsWristPresenterTest : public QObject
{
    Q_OBJECT

    std::shared_ptr<SessionSnapshot> m_snap;

    void setUp(ChartsWristPresenter &p, int picked = 1)
    {
        p.setSynchronous(true);
        p.setSnapshot(m_snap);
        if (picked > 0) p.setPickedSwingDir(dirOf(picked));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        warmWristBands();
        m_snap = std::make_shared<SessionSnapshot>();
        m_snap->sessionDir = QStringLiteral("/wx/2026-08-18_Mark-Liversedge_Wrist_02");
        m_snap->revision = 1;
        auto s1 = makeShot(1, -30), s2 = makeShot(2, -28), s3 = makeShot(3, -60);
        const QStringList others{ QStringLiteral("bowed_at_top"), QStringLiteral("scooping"), QStringLiteral("bowed_lead_wrist") };
        fire(*s1, QStringLiteral("cupped_at_top"), 2);
        clean(*s1, others);
        fire(*s2, QStringLiteral("cupped_at_top"), 2);
        clean(*s2, others);
        clean(*s3, others + QStringList{ QStringLiteral("cupped_at_top") });
        m_snap->shots = { s1, s2, s3 };
        m_snap->tiers.insert(QStringLiteral("cupped_at_top"), QStringLiteral("pattern"));
        for (const QString &id : others) m_snap->tiers.insert(id, QStringLiteral("clean"));
        QVERIFY(!s1->wrist.isEmpty());
    }

    // ── the payload (I3) ──────────────────────────────────────────────────────────────────────
    void payloadShape()
    {
        const QVariantMap w = m_snap->shots[0]->wrist;
        QCOMPARE(w.value(QStringLiteral("checkpoints")).toList().size(), 8);
        const QVariantList dofs = w.value(QStringLiteral("dofs")).toList();
        QCOMPARE(dofs.size(), 2);
        QCOMPARE(dofs.at(1).toMap().value(QStringLiteral("dof")).toString(), QStringLiteral("leadWristFlexExt"));
        QCOMPARE(dofs.at(1).toMap().value(QStringLiteral("metricKey")).toString(), QStringLiteral("hm.leadWristFlexExt"));
        // regrading by hand moves the mode; Auto again is the same payload
        const QVariantMap c = regradeWristPayload(*m_snap->shots[0], w, 2);
        QCOMPARE(c.value(QStringLiteral("archetype")).toInt(), 2);
        QCOMPARE(c.value(QStringLiteral("archetypeMode")).toInt(), 2);
        QCOMPARE(regradeWristPayload(*m_snap->shots[0], w, -1), w);
    }

    // ── This shot: the condition's verdict, the score over the wrist domain, the profile ─────────
    void thisShotFindingsScoreProfile()
    {
        ChartsWristPresenter p;
        setUp(p, 1);
        QVERIFY(p.available());
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QVERIFY(t.value(QStringLiteral("graded")).toBool());
        QCOMPARE(t.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 1 of 3  ·  7 iron  ·  Wrist"));

        const QVariantList fs = t.value(QStringLiteral("findings")).toList();
        QCOMPARE(fs.size(), 1);
        const QVariantMap f = fs.first().toMap();
        QCOMPARE(f.value(QStringLiteral("id")).toString(), QStringLiteral("cupped_at_top"));
        QCOMPARE(f.value(QStringLiteral("severity")).toString(), QStringLiteral("pattern"));     // the session's tier word
        QCOMPARE(f.value(QStringLiteral("severityWord")).toString(), QStringLiteral("pattern"));
        // golfer-facing words only: never the author's consequence, never a backticked id
        QVERIFY(!f.value(QStringLiteral("explanation")).toString().isEmpty());
        QVERIFY(f.value(QStringLiteral("coaching")).toString().isEmpty());
        for (const QString &k : { QStringLiteral("explanation"), QStringLiteral("coaching"), QStringLiteral("protect") })
            QVERIFY2(!f.value(k).toString().contains(QLatin1Char('`')), qPrintable(k));
        for (const QVariant &m : f.value(QStringLiteral("more")).toList()) {
            QVERIFY(m.toMap().value(QStringLiteral("k")).toString() != QStringLiteral("Coaching"));
            QVERIFY(!m.toMap().value(QStringLiteral("v")).toString().contains(QLatin1Char('`')));
        }
        QCOMPARE(f.value(QStringLiteral("positions")).toStringList(), QStringList{ QStringLiteral("P4") });
        QCOMPARE(f.value(QStringLiteral("seekUs")).toLongLong(), qint64(800000));
        QCOMPARE(f.value(QStringLiteral("magnitude")).toInt(), 10);                               // −30 against −20…10
        QCOMPARE(f.value(QStringLiteral("confidence")).toDouble(), 0.75);
        QVERIFY(f.value(QStringLiteral("pointsLost")).toInt() > 0);
        QVERIFY(text(t.value(QStringLiteral("head"))).startsWith(f.value(QStringLiteral("name")).toString() + QStringLiteral(" at P4")));

        // the score is the domain's: 100 less what the finding cost, with its sum
        const QVariantMap sc = t.value(QStringLiteral("score")).toMap();
        QCOMPARE(sc.value(QStringLiteral("score")).toInt(), 100 - f.value(QStringLiteral("pointsLost")).toInt());
        QCOMPARE(sc.value(QStringLiteral("contributions")).toList().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cupped_at_top"));
        QVERIFY(sc.value(QStringLiteral("sumText")).toString().startsWith(QStringLiteral("100 ")));

        // working well: the impact axis read clean on both tails; the top axis did not (it fired)
        const QVariantList ws = t.value(QStringLiteral("strengths")).toList();
        QVERIFY(!findIn(ws, QStringLiteral("id"), QStringLiteral("scooping")).isEmpty()
                || !findIn(ws, QStringLiteral("id"), QStringLiteral("bowed_lead_wrist")).isEmpty());
        QVERIFY(findIn(ws, QStringLiteral("id"), QStringLiteral("cupped_at_top")).isEmpty());
        QVERIFY(findIn(ws, QStringLiteral("id"), QStringLiteral("bowed_at_top")).isEmpty());
        for (const QVariant &v : ws) {
            QCOMPARE(v.toMap().value(QStringLiteral("severity")).toString(), QStringLiteral("good"));
            QVERIFY(!v.toMap().value(QStringLiteral("protect")).toString().contains(QLatin1Char('`')));
        }

        // the profile: eight positions, two strips in display order, a value at every checkpoint
        const QVariantMap pr = t.value(QStringLiteral("profile")).toMap();
        QCOMPARE(pr.value(QStringLiteral("positions")).toList().size(), 8);
        const QVariantList rows = pr.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("short")).toString(), QStringLiteral("hinge"));
        const QVariantList pts = rows.at(1).toMap().value(QStringLiteral("points")).toList();
        QCOMPARE(pts.size(), 8);
        QCOMPARE(pts.at(0).toMap().value(QStringLiteral("valueText")).toString(), QStringLiteral("0°"));
        QVERIFY(rows.at(1).toMap().value(QStringLiteral("yLo")).toDouble() < rows.at(1).toMap().value(QStringLiteral("yHi")).toDouble());
        QVERIFY(pr.value(QStringLiteral("missing")).toStringList().contains(QStringLiteral("trail wrist")));
        QVERIFY(pr.value(QStringLiteral("missing")).toStringList().contains(QStringLiteral("lead forearm roll")));   // no rotation either
        // the caption says once what a cell's word and a finding's word mean
        QVERIFY(pr.value(QStringLiteral("caption")).toString().contains(QStringLiteral("(fault, watch)")));
        QVERIFY(pr.value(QStringLiteral("caption")).toString().contains(QStringLiteral("pattern")));
        QCOMPARE(t.value(QStringLiteral("standfirsts")).toList().size(), 8);

        // the resemblance line
        const QVariantMap st = t.value(QStringLiteral("style")).toMap();
        QVERIFY(st.value(QStringLiteral("hasIndex")).toBool());
        QVERIFY(text(st.value(QStringLiteral("line"))).startsWith(QStringLiteral("Resembles Neutral")));
    }

    // ── W-D6: outside a band, no finding fired — said ───────────────────────────────────────────
    void noFindingButOutsideABand()
    {
        ChartsWristPresenter p;
        setUp(p, 3);
        p.setArchetype(0);                                    // Neutral: −60° at the top is out of it
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("findings")).toList().isEmpty());
        QVERIFY(t.value(QStringLiteral("outCells")).toInt() > 0);
        QVERIFY(text(t.value(QStringLiteral("head"))).startsWith(QStringLiteral("No finding fired; ")));
        QVERIFY(text(t.value(QStringLiteral("head"))).endsWith(QStringLiteral(" sat outside a band")));
        QCOMPARE(t.value(QStringLiteral("score")).toMap().value(QStringLiteral("score")).toInt(), 100);
        // the cell says it with a word, not a colour
        bool word = false;
        for (const QVariant &r : t.value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList())
            for (const QVariant &pt : r.toMap().value(QStringLiteral("points")).toList())
                if (pt.toMap().value(QStringLiteral("out")).toBool())
                    word = word || !pt.toMap().value(QStringLiteral("word")).toString().isEmpty();
        QVERIFY(word);
    }

    // ── the archetype is session-wide and by hand says so ───────────────────────────────────────
    void archetypeByHand()
    {
        ChartsWristPresenter p;
        setUp(p, 1);
        QVERIFY(p.choices().value(QStringLiteral("archetypeAfter")).toString().startsWith(QStringLiteral("→ ")));
        p.setArchetype(2);
        const QVariantMap st = p.thisShot().value(QStringLiteral("style")).toMap();
        QCOMPARE(st.value(QStringLiteral("archetypeName")).toString(), QStringLiteral("Cupped"));
        QVERIFY(st.value(QStringLiteral("byHand")).toBool());
        QVERIFY(p.choices().value(QStringLiteral("archetypeAfter")).toString().isEmpty());
        for (const QVariant &c : p.session().value(QStringLiteral("styles")).toMap().value(QStringLiteral("counts")).toList())
            QCOMPARE(c.toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Cupped"));
    }

    // ── Against: previous, median, reference ★ ──────────────────────────────────────────────────
    void againstGhosts()
    {
        ChartsWristPresenter p;
        setUp(p, 1);
        p.setAgainst(QStringLiteral("previous"));
        QVERIFY(!p.thisShot().value(QStringLiteral("profile")).toMap().value(QStringLiteral("ghostRefusal")).toString().isEmpty());
        QVERIFY(!findIn(p.choices().value(QStringLiteral("againstOptions")).toList(), QStringLiteral("id"), QStringLiteral("previous"))
                     .value(QStringLiteral("enabled")).toBool());
        p.setAgainst(QStringLiteral("median"));
        QVariantMap pr = p.thisShot().value(QStringLiteral("profile")).toMap();
        QCOMPARE(pr.value(QStringLiteral("ghostWord")).toString(), QStringLiteral("session median, n 3"));
        QVERIFY(!pr.value(QStringLiteral("rows")).toList().first().toMap().value(QStringLiteral("ghost")).isNull());
        p.setAgainst(QStringLiteral("reference"));
        QVERIFY(!findIn(p.choices().value(QStringLiteral("againstOptions")).toList(), QStringLiteral("id"), QStringLiteral("reference"))
                     .value(QStringLiteral("enabled")).toBool());
        p.setReferenceSwingDir(dirOf(2));
        pr = p.thisShot().value(QStringLiteral("profile")).toMap();
        QCOMPARE(pr.value(QStringLiteral("ghostWord")).toString(), QStringLiteral("the reference ★"));
        QCOMPARE(p.choices().value(QStringLiteral("reference")).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("shot 2, 18 Aug 2026"));
        p.setReferenceSwingDir(dirOf(1));
        QVERIFY(p.choices().value(QStringLiteral("reference")).toMap().value(QStringLiteral("isThisShot")).toBool());
    }

    // ── curves behind, the playhead's checkpoint, the seek ──────────────────────────────────────
    void curvesPlayheadSeek()
    {
        ChartsWristPresenter p;
        setUp(p, 1);
        QVERIFY(p.thisShot().value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList().first().toMap()
                    .value(QStringLiteral("trace")).toList().isEmpty());
        p.setCurvesBehind(true);
        const QVariantList tr = p.thisShot().value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList().first().toMap()
                                    .value(QStringLiteral("trace")).toList();
        QVERIFY(tr.size() > 10);
        QVERIFY(tr.first().toMap().value(QStringLiteral("x")).toDouble() >= -1e-9);
        QVERIFY(tr.last().toMap().value(QStringLiteral("x")).toDouble() <= 7 + 1e-9);
        QCOMPARE(p.seekUsFor(QStringLiteral("P4")), qint64(800000));
        p.setPlayheadUs(850000);
        QCOMPARE(p.selected(), 3);
        QVERIFY(text(p.standfirst()).startsWith(QStringLiteral("At P4, top")));
        p.setPlayheadUs(0);
        QCOMPARE(p.selected(), 0);
    }

    // ── Session: boxes, the score run, the style table, what keeps firing ───────────────────────
    void sessionScope()
    {
        ChartsWristPresenter p;
        setUp(p, 1);
        p.setScope(QStringLiteral("session"));
        const QVariantMap s = p.session();
        QVERIFY(s.value(QStringLiteral("available")).toBool());
        QCOMPARE(s.value(QStringLiteral("ready")).toInt(), 3);
        QVERIFY(text(s.value(QStringLiteral("head"))).startsWith(QStringLiteral("Graded")));
        QVERIFY(text(s.value(QStringLiteral("standfirst"))).contains(QStringLiteral("small session")));
        const QVariantList rows = s.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 2);
        const QVariantList boxes = rows.first().toMap().value(QStringLiteral("boxes")).toList();
        QCOMPARE(boxes.size(), 8);
        QCOMPARE(boxes.at(3).toMap().value(QStringLiteral("n")).toInt(), 3);
        QVERIFY(boxes.at(3).toMap().value(QStringLiteral("wideText")).toString().endsWith(QStringLiteral("° wide")));
        QCOMPARE(s.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("points")).toList().size(), 3);
        const QVariantList rec = s.value(QStringLiteral("recurring")).toList();
        QCOMPARE(rec.size(), 1);
        QCOMPARE(rec.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("cupped_at_top"));
        QCOMPARE(rec.first().toMap().value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(rec.first().toMap().value(QStringLiteral("countText")).toString(), QStringLiteral("2 of 3 shots, at P4"));
        QCOMPARE(rec.first().toMap().value(QStringLiteral("marks")).toList().size(), 3);
        const QVariantMap sty = s.value(QStringLiteral("styles")).toMap();
        QCOMPARE(sty.value(QStringLiteral("rows")).toList().size(), 3);
        QVERIFY(sty.value(QStringLiteral("hasIndex")).toBool());
        QVERIFY(sty.value(QStringLiteral("note")).toString().isEmpty());
        QCOMPARE(sty.value(QStringLiteral("columns")).toList().first().toMap().value(QStringLiteral("short")).toString(), QStringLiteral("1"));
        QCOMPARE(rec.first().toMap().value(QStringLiteral("severity")).toString(), QStringLiteral("pattern"));
        // Relations needs five shots: refused, with the reason
        QVERIFY(!p.movesWith().value(QStringLiteral("available")).toBool());
        QVERIFY(!text(p.movesWith().value(QStringLiteral("refusal"))).isEmpty());
    }

    // ── the empty states and a shot from its own detail (W14) ───────────────────────────────────
    // ── no resemblance index on any shot: one line, not a grid of "-"; the forearm roll said precisely ──
    void noIndexAndForearmRotation()
    {
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = QStringLiteral("/wx/2026-08-18_Mark-Liversedge_Wrist_02");
        snap->revision = 1;
        snap->shots = { makeShot(1, -30, false, true), makeShot(2, -28, false, true), makeShot(3, -40, false, true) };
        ChartsWristPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setPickedSwingDir(dirOf(1));
        const QVariantMap pr = p.thisShot().value(QStringLiteral("profile")).toMap();
        QVERIFY(!pr.value(QStringLiteral("missing")).toStringList().contains(QStringLiteral("lead forearm roll")));
        const QString mt = pr.value(QStringLiteral("missingText")).toString();
        QVERIFY2(mt.contains(QStringLiteral("roll (pronation)")) && mt.contains(QStringLiteral("monitored, not graded")), qPrintable(mt));
        QVERIFY(!mt.contains(QStringLiteral("Not measured on this swing: lead forearm roll")));
        p.setScope(QStringLiteral("session"));
        const QVariantMap sty = p.session().value(QStringLiteral("styles")).toMap();
        QVERIFY(!sty.value(QStringLiteral("hasIndex")).toBool());
        QVERIFY(sty.value(QStringLiteral("rows")).toList().isEmpty());
        QVERIFY(sty.value(QStringLiteral("columns")).toList().isEmpty());
        QVERIFY(!sty.value(QStringLiteral("note")).toString().isEmpty());
    }

    void emptyAndDetail()
    {
        ChartsWristPresenter p;
        setUp(p, 0);
        QVERIFY(p.thisShot().value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("Select a swing to review.")));
        p.setPickedSwingDir(QStringLiteral("/elsewhere/2026-08-19_X_01/swing_0001"));
        QVERIFY(p.thisShot().value(QStringLiteral("empty")).toString().startsWith(QStringLiteral("No wrist metrics for this swing.")));
        // its own detail: the same series as shot 1, not in the session
        const ShotSeries &s1 = *m_snap->shots[0];
        QVariantList series, phases;
        for (const QString &k : s1.metricOrder) {
            const MetricSeries *m = s1.metric(k);
            QVariantList t, v;
            for (qint64 x : m->tUs) t << qlonglong(x);
            for (double x : m->value) v << x;
            series << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v } };
        }
        for (const PhaseMark &pm : s1.phases)
            phases << QVariantMap{ { QStringLiteral("phase"), pm.phase }, { QStringLiteral("t_us"), qlonglong(pm.tUs) }, { QStringLiteral("conf"), 1.0 } };
        p.setPickedDetail(QVariantMap{ { QStringLiteral("series"), series }, { QStringLiteral("phases"), phases } });
        const QVariantMap t = p.thisShot();
        QVERIFY(t.value(QStringLiteral("available")).toBool());
        QVERIFY(!t.value(QStringLiteral("inSession")).toBool());
        QVERIFY(!t.value(QStringLiteral("graded")).toBool());
        QCOMPARE(t.value(QStringLiteral("findingsEmpty")).toString(), QStringLiteral("Diagnostics have not graded this swing yet."));
        QVERIFY(t.value(QStringLiteral("score")).toMap().value(QStringLiteral("score")).isNull());
    }
};

QTEST_GUILESS_MAIN(ChartsWristPresenterTest)
#include "charts_wrist_presenter_test.moc"
