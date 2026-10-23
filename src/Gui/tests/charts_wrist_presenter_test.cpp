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
#include "report/report_wrist.h"

#include <QRegularExpression>

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

// Every string in a map, with its path ("rows/0/caption"), for the noun scan.
void stringsOf(const QVariant &v, const QString &path, QStringList *out)
{
    if (v.typeId() == QMetaType::QString) { *out << path + QStringLiteral(" = ") + v.toString(); return; }
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        for (auto it = m.cbegin(); it != m.cend(); ++it) stringsOf(it.value(), path + QLatin1Char('/') + it.key(), out);
    } else if (v.typeId() == QMetaType::QVariantList) {
        const QVariantList l = v.toList();
        for (int i = 0; i < l.size(); ++i) stringsOf(l.at(i), path + QLatin1Char('/') + QString::number(i), out);
    } else if (v.typeId() == QMetaType::QStringList) {
        const QStringList l = v.toStringList();
        for (int i = 0; i < l.size(); ++i) *out << path + QLatin1Char('/') + QString::number(i) + QStringLiteral(" = ") + l.at(i);
    }
}
QStringList trustLines(const QVariantMap &m) { return m.value(QStringLiteral("trust")).toMap().value(QStringLiteral("lines")).toStringList(); }
bool anyLine(const QStringList &l, const QString &needle) { for (const QString &x : l) if (x.contains(needle)) return true; return false; }

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


    // ── the session report's Wrist page (wristSessionReportPayload, report/report_wrist.h) ──────────
    // the screen's Session map is unchanged by the report's noun: with "shots" every key the report does not
    // re-make for paper (rows' wash, caption, sub) and the new trust is the screen's, byte for byte
    void reportPayloadScreenUnchanged()
    {
        ChartsWristPresenter p;
        setUp(p, 0);
        p.setScope(QStringLiteral("session"));
        const QVariantMap scr = p.session();
        const QVariantMap rep = wristSessionReportPayload(*m_snap, -1, QStringLiteral("shots"));
        for (auto it = scr.cbegin(); it != scr.cend(); ++it) {
            if (it.key() == QLatin1String("trust") || it.key() == QLatin1String("rows") || it.key() == QLatin1String("caption")
                || it.key() == QLatin1String("sub")) continue;
            if (it.key() == QLatin1String("scoreRun")) {    // paper adds its value axis (ticks): the rest is the screen's
                QVariantMap r = rep.value(it.key()).toMap();
                QVERIFY(r.contains(QStringLiteral("ticks")));
                r.remove(QStringLiteral("ticks"));
                QVERIFY2(QVariant(r) == it.value(), "scoreRun");
                continue;
            }
            QVERIFY2(rep.value(it.key()) == it.value(), qPrintable(it.key()));
        }
        // the screen's own words, as they were
        QCOMPARE(scr.value(QStringLiteral("kicker")).toString(), QStringLiteral("3 shots  ·  7 iron  ·  Wrist"));
        QCOMPARE(scr.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("caption")).toString(), QStringLiteral("Wrist score, shot by shot (0–100)"));
        QCOMPARE(scr.value(QStringLiteral("styles")).toMap().value(QStringLiteral("columns")).toList().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Shot 1"));
        QVERIFY(scr.value(QStringLiteral("sub")).toString().contains(QStringLiteral("this shot ringed")));
        QVERIFY(text(scr.value(QStringLiteral("standfirst"))).contains(QStringLiteral("Three shots is a small session: each box is drawn from 3 values.")));
        // every shot disagrees with its index: "on every shot", and the screen names its Auto control
        QVERIFY2(text(scr.value(QStringLiteral("standfirst"))).contains(QStringLiteral("most resemble Neutral (3); on every shot that is not the style Auto graded against.")),
                 qPrintable(text(scr.value(QStringLiteral("standfirst")))));
        QVERIFY(!rep.value(QStringLiteral("styleLine")).toString().contains(QStringLiteral("Auto")));
        // the standfirst is its four parts, run for run
        const QString sf = text(rep.value(QStringLiteral("standfirst")));
        QVERIFY2(sf.startsWith(rep.value(QStringLiteral("scoreLine")).toString()), qPrintable(sf));
        QVERIFY(sf.endsWith(rep.value(QStringLiteral("sizeLine")).toString()));
        QVERIFY(rep.value(QStringLiteral("repeat")).toString().isEmpty() || sf.contains(rep.value(QStringLiteral("repeat")).toString()));
        // the screen carries its trust: This shot and Session
        p.setPickedSwingDir(dirOf(1));
        const QStringList ts = trustLines(p.thisShot());
        QVERIFY2(anyLine(ts, QStringLiteral("Lead wrist bow / cup: from the wrist sensor, confidence ")), qPrintable(ts.join(QLatin1Char('\n'))));
        QVERIFY(anyLine(ts, QStringLiteral("Lead forearm roll: not measured on this swing")));
        QVERIFY(anyLine(ts, QStringLiteral("the style Auto resolved this swing to")));
        QVERIFY(anyLine(ts, QStringLiteral("coaching starting points")));
        QVERIFY(!anyLine(ts, QStringLiteral("not graded")));
        QVERIFY(!anyLine(ts, QStringLiteral("resemblance index")));
        QVERIFY(!trustLines(p.session()).isEmpty());
    }

    // the report's words count swings, the wash is the dominant style's band, a median outside it says its side
    void reportPayloadSingleStyle()
    {
        const QVariantMap rep = wristSessionReportPayload(*m_snap, -1, QStringLiteral("swings"));
        QVERIFY(rep.value(QStringLiteral("available")).toBool());
        QCOMPARE(rep.value(QStringLiteral("kicker")).toString(), QStringLiteral("3 swings  ·  7 iron  ·  Wrist"));
        QStringList all;
        for (auto it = rep.cbegin(); it != rep.cend(); ++it)
            if (it.key() != QLatin1String("movesWith")) stringsOf(it.value(), it.key(), &all);
        stringsOf(rep.value(QStringLiteral("movesWith")).toMap().value(QStringLiteral("refusal")), QStringLiteral("movesWith/refusal"), &all);
        static const QRegularExpression shotWord(QStringLiteral("\\b[Ss]hots?\\b"));
        for (const QString &x : all) QVERIFY2(!shotWord.match(x).hasMatch(), qPrintable(x));
        QVERIFY(text(rep.value(QStringLiteral("movesWith")).toMap().value(QStringLiteral("refusal"))).contains(QStringLiteral("swings")));
        // all three resolve to Cupped (the top is 28–60° cupped): one style
        const QVariantMap dom = rep.value(QStringLiteral("dominant")).toMap();
        QCOMPARE(dom.value(QStringLiteral("name")).toString(), QStringLiteral("Cupped"));
        QCOMPARE(dom.value(QStringLiteral("n")).toInt(), 3);
        QCOMPARE(rep.value(QStringLiteral("caption")).toString(), QStringLiteral("Shaded: the Cupped band, the style every swing was graded as."));
        QVERIFY(!rep.value(QStringLiteral("sub")).toString().contains(QStringLiteral("ringed")));
        const QVariantMap fe = findIn(rep.value(QStringLiteral("rows")).toList(), QStringLiteral("dof"), QStringLiteral("leadWristFlexExt"));
        QCOMPARE(fe.value(QStringLiteral("label")).toString(), QStringLiteral("Lead wrist bow / cup"));
        QCOMPARE(fe.value(QStringLiteral("camera")).toString(), QStringLiteral("wrist sensor"));
        // the wash at the top is the Cupped band shot 1 was graded against
        const QVariantMap own = m_snap->shots[0]->wrist.value(QStringLiteral("dofs")).toList().at(1).toMap().value(QStringLiteral("cells")).toList().at(3).toMap();
        const QVariantMap top = fe.value(QStringLiteral("points")).toList().at(3).toMap();
        QCOMPARE(top.value(QStringLiteral("wash")).toString(), QStringLiteral("band"));
        QCOMPARE(top.value(QStringLiteral("bandLo")).toDouble(), own.value(QStringLiteral("bandLo")).toDouble());
        QCOMPARE(top.value(QStringLiteral("bandHi")).toDouble(), own.value(QStringLiteral("bandHi")).toDouble());
        // the top's median (about −29°) sits below the Cupped band: outside, on the open side, in words
        const QVariantMap tb = fe.value(QStringLiteral("boxes")).toList().at(3).toMap();
        QVERIFY(tb.value(QStringLiteral("outside")).toBool());
        QCOMPARE(tb.value(QStringLiteral("ringWord")).toString(), QStringLiteral("open"));
        for (int i = 0; i < 8; ++i) {
            const QVariantMap b = fe.value(QStringLiteral("boxes")).toList().at(i).toMap(), pt = fe.value(QStringLiteral("points")).toList().at(i).toMap();
            const QVariant mv = b.value(QStringLiteral("median"));
            const bool out = i > 0 && pt.value(QStringLiteral("banded")).toBool() && !mv.isNull()
                             && (mv.toDouble() > pt.value(QStringLiteral("bandHi")).toDouble() || mv.toDouble() < pt.value(QStringLiteral("bandLo")).toDouble());
            QCOMPARE(b.value(QStringLiteral("outside")).toBool(), out);
            QCOMPARE(b.value(QStringLiteral("ringWord")).toString().isEmpty(), !out);
        }
        // the split lines
        QVERIFY(rep.value(QStringLiteral("scoreLine")).toString().startsWith(QStringLiteral("Wrist score median ")));
        QVERIFY(!rep.value(QStringLiteral("scoreLine")).toString().endsWith(QLatin1Char('.')));
        // the style line is the resemblance index's clause alone: the head says the style counts, paper says it once
        QVERIFY2(rep.value(QStringLiteral("styleLine")).toString().startsWith(QStringLiteral("By the resemblance index")),
                 qPrintable(rep.value(QStringLiteral("styleLine")).toString()));
        QVERIFY(!rep.value(QStringLiteral("styleLine")).toString().contains(QStringLiteral("Graded")));
        // the score run carries its value axis for paper (no ticker there): 0, 50, 100
        {
            const QVariantList tk = rep.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("ticks")).toList();
            QStringList tt;
            for (const QVariant &v : tk) tt << v.toMap().value(QStringLiteral("text")).toString();
            QCOMPARE(tt.join(QLatin1Char(',')), QStringLiteral("0,50,100"));
        }
        QCOMPARE(rep.value(QStringLiteral("sizeLine")).toString(), QStringLiteral("Three swings is a small session: each box is drawn from 3 values."));
        // trust: true lines only
        const QStringList t = trustLines(rep);
        QVERIFY2(anyLine(t, QStringLiteral("Lead wrist bow / cup: from the wrist sensor on every swing, confidence ")), qPrintable(t.join(QLatin1Char('\n'))));
        // one style: the grading line says it without the Auto control, and no wash line repeats the caption
        QVERIFY(t.contains(QStringLiteral("Every swing with a wrist reading is graded as a Cupped wrist, against the Cupped band.")));
        QVERIFY(!anyLine(t, QStringLiteral("The shaded band")));
        QVERIFY(!anyLine(t, QStringLiteral("tie")));
        for (const QString &x : t) QVERIFY2(!x.contains(QStringLiteral("Auto")), qPrintable(x));
        // every swing carries an index and none agrees with its grade: "on every swing", paper names no control
        QCOMPARE(rep.value(QStringLiteral("styleLine")).toString(),
                 QStringLiteral("By the resemblance index, most resemble Neutral (3); on every swing that is not the style it was graded against"));
        QVERIFY(t.contains(QStringLiteral("The bands are coaching starting points; they have not yet been measured on good players.")));
        QVERIFY(t.contains(QStringLiteral("Three swings is a small session: each box is drawn from 3 values.")));
        QVERIFY(!anyLine(t, QStringLiteral("No wrist reading")) && !anyLine(t, QStringLiteral("No wrist score")) && !anyLine(t, QStringLiteral("resemblance index")));
        for (const QString &x : t) QVERIFY2(!x.contains(QStringLiteral("HackMotion"), Qt::CaseInsensitive) && !x.contains(QStringLiteral("heuristic")), qPrintable(x));
        // the block on paper: the posture row's keys, the report's swing numbers
        QHash<QString, int> swingNo;
        for (int i = 1; i <= 3; ++i) swingNo.insert(QDir::cleanPath(dirOf(i)), 10 + i);
        const QVariantMap b = pinpoint::report::wristReportBlock(rep, swingNo).toMap();
        const QVariantMap row = findIn(b.value(QStringLiteral("profile")).toMap().value(QStringLiteral("rows")).toList(), QStringLiteral("key"), QStringLiteral("leadWristFlexExt"));
        QCOMPARE(row.value(QStringLiteral("metricKey")).toString(), QStringLiteral("hm.leadWristFlexExt"));
        const QVariantMap c3 = row.value(QStringLiteral("cells")).toList().at(3).toMap();
        QVERIFY(c3.value(QStringLiteral("outside")).toBool());
        QCOMPARE(c3.value(QStringLiteral("ringWord")).toString(), QStringLiteral("open"));
        QCOMPARE(c3.value(QStringLiteral("text")).toString(), tb.value(QStringLiteral("medianText")).toString());
        QCOMPARE(b.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("points")).toList().first().toMap().value(QStringLiteral("swing")).toInt(), 11);
        QCOMPARE(b.value(QStringLiteral("trust")).toStringList(), t);
        QVERIFY(pinpoint::report::wristReportBlock(wristSessionReportPayload(SessionSnapshot{}, -1, QStringLiteral("swings")), swingNo).isNull());
    }

    // every swing scores 100: the middle half is one value, said as one (screen and paper)
    void middleHalfOfOneValue()
    {
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = m_snap->sessionDir;
        snap->revision = 1;
        const QStringList ids{ QStringLiteral("cupped_at_top"), QStringLiteral("bowed_at_top"), QStringLiteral("scooping"), QStringLiteral("bowed_lead_wrist") };
        for (int i = 1; i <= 3; ++i) { auto s = makeShot(i, -30); clean(*s, ids); snap->shots.push_back(s); }
        for (const QString &id : ids) snap->tiers.insert(id, QStringLiteral("clean"));
        const QVariantMap rep = wristSessionReportPayload(*snap, -1, QStringLiteral("swings"));
        QCOMPARE(rep.value(QStringLiteral("scoreLine")).toString(), QStringLiteral("Wrist score median 100, middle half all at 100"));
        ChartsWristPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setScope(QStringLiteral("session"));
        const QString sf = text(p.session().value(QStringLiteral("standfirst")));
        QVERIFY2(sf.startsWith(QStringLiteral("Wrist score median 100, middle half all at 100. ")), qPrintable(sf));
        QVERIFY(!sf.contains(QStringLiteral("100–100")));
    }

    // a mixed session: the dominant style shades, the caption says each swing kept its own; a swing with no wrist
    // reading and none graded say so in trust; no index anywhere says what that means
    void reportPayloadMixed()
    {
        auto snap = std::make_shared<SessionSnapshot>();
        snap->sessionDir = QStringLiteral("/wx/2026-08-18_Mark-Liversedge_Wrist_02");
        snap->revision = 1;
        auto bare = std::make_shared<ShotSeries>();
        bare->ordinal = 4; bare->shotId = 4; bare->swingDir = dirOf(4); bare->club = QStringLiteral("7 IRON");
        snap->shots = { makeShot(1, -30, false), makeShot(2, -40, false), makeShot(3, 0, false), bare };
        const QVariantMap rep = wristSessionReportPayload(*snap, -1, QStringLiteral("swings"));
        QCOMPARE(rep.value(QStringLiteral("ready")).toInt(), 3);
        QCOMPARE(rep.value(QStringLiteral("dominant")).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Cupped"));
        QCOMPARE(rep.value(QStringLiteral("caption")).toString(),
                 QStringLiteral("Shaded: the Cupped band, the style 2 of 3 swings were graded as. Each swing was graded against its own style’s band."));
        const QStringList t = trustLines(rep);
        QVERIFY2(t.contains(QStringLiteral("No wrist reading on 1 of 4 swings.")), qPrintable(t.join(QLatin1Char('\n'))));
        QVERIFY(t.contains(QStringLiteral("No wrist score yet on every swing: the diagnostics have not graded them.")));
        QVERIFY(t.contains(QStringLiteral("Each swing is graded against the band of its own style: Neutral on 1 and Cupped on 2.")));
        QVERIFY(t.contains(QStringLiteral("The shaded band is the Cupped style's, the style most swings were graded as.")));   // no tie: no tie rule
        QVERIFY(t.contains(QStringLiteral("No swing in this session carries a resemblance index, so each swing's style is read from its own wrist readings.")));
        for (const QString &x : t) QVERIFY2(!x.contains(QStringLiteral("Auto")), qPrintable(x));
        QVERIFY(rep.value(QStringLiteral("scoreLine")).toString().startsWith(QStringLiteral("No swing's wrist conditions")));
        QCOMPARE(rep.value(QStringLiteral("styleLine")).toString(), QString());   // no index: no style line (the head has the counts)
        // a tie goes to Neutral, and trust says the tie plainly
        snap->shots = { makeShot(1, -30, false), makeShot(2, 0, false) };
        {
            const QVariantMap tie = wristSessionReportPayload(*snap, -1, QStringLiteral("swings"));
            QCOMPARE(tie.value(QStringLiteral("dominant")).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Neutral"));
            const QStringList tt = trustLines(tie);
            QVERIFY2(tt.contains(QStringLiteral("Neutral and Cupped were tied; the Neutral band is shaded.")), qPrintable(tt.join(QLatin1Char('\n'))));
            QVERIFY(!anyLine(tt, QStringLiteral("The shaded band")));
        }
        // a session with no lead wrist bow / cup reading: Auto has nothing to resolve (a camera-only session)
        {
            auto cam = std::make_shared<SessionSnapshot>();
            cam->sessionDir = snap->sessionDir;
            auto t1 = makeShot(1, -30, false);
            t1->metrics.remove(QStringLiteral("hm.leadWristFlexExt"));
            t1->metricOrder.removeAll(QStringLiteral("hm.leadWristFlexExt"));
            t1->wrist = shotWristPayload(*t1, -1, {});
            cam->shots = { t1 };
            const QVariantMap crep = wristSessionReportPayload(*cam, -1, QStringLiteral("swings"));
            const QStringList ct = trustLines(crep);
            QVERIFY2(ct.contains(QStringLiteral("No swing reads the lead wrist's bow / cup, so every swing is graded against the Neutral band.")),
                     qPrintable(ct.join(QLatin1Char('\n'))));
            QVERIFY(ct.contains(QStringLiteral("No swing in this session carries a resemblance index, so there is no style to compare.")));
            QVERIFY(!anyLine(ct, QStringLiteral("Auto")));
            QVERIFY(!anyLine(ct, QStringLiteral("graded as")) && !anyLine(ct, QStringLiteral("The shaded band")));
            QCOMPARE(crep.value(QStringLiteral("caption")).toString(), QStringLiteral("Shaded: the Neutral band."));
            // the screen keeps its words: Auto is a control there
            ChartsWristPresenter cp;
            cp.setSynchronous(true);
            cp.setSnapshot(cam);
            cp.setScope(QStringLiteral("session"));
            QVERIFY(trustLines(cp.session()).contains(
                QStringLiteral("No shot reads the lead wrist's bow / cup, so Auto has no style to resolve: every shot is graded against the Neutral band.")));
        }
        // This shot's trust on an ungraded swing without an index
        ChartsWristPresenter p;
        p.setSynchronous(true);
        p.setSnapshot(snap);
        p.setPickedSwingDir(dirOf(1));
        const QStringList ts = trustLines(p.thisShot());
        QVERIFY(anyLine(ts, QStringLiteral("The diagnostics have not graded this swing yet")));
        QVERIFY(anyLine(ts, QStringLiteral("carries no resemblance index")));
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
