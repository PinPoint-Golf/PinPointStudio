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

// The Charts panel's traces presenter (src/Gui/charts/charts_traces_presenter.h) on the trimmed 4 July
// fixture (synchronous: every job inline) and, when /mnt/swingdata is mounted, on the real scratch
// 4 July session and the 50-shot 7 Oct session through SessionSeriesModel (read-only) — where the
// budgets of build/charts-build/STEP2A.md are MEASURED and reported, not asserted.

#include "charts_maths_fixture.h"

#include "charts/charts_traces_presenter.h"
#include "charts/charts_words.h"
#include "charts/session_series_model.h"
#include "charts/session_warp.h"

#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QStandardPaths>
#include <QTest>

#include <cmath>
#include <cstdio>
#include <functional>

using namespace pinpoint::charts;

namespace {

QString text(const QVariant &sentence) { return sentence.toMap().value(QStringLiteral("text")).toString(); }

QList<double> dlist(const QVariant &v) { return v.value<QList<double>>(); }

// Expected texts are written with "~" for the no-break space (U+00A0) that joins a figure to its unit.
QString nb(QString s) { return s.replace(QLatin1Char('~'), QChar(0x00A0)); }

// Measurements and notes: printed, and appended to $PP_CHARTS_MEASURE_OUT when it is set (ctest prints
// a passing test's output only on failure).
template <typename... A>
void report(const char *fmt, A... a)
{
    const QString line = QString::asprintf(fmt, a...);
    std::fputs(qPrintable(line), stdout);
    const QString out = qEnvironmentVariable("PP_CHARTS_MEASURE_OUT");
    if (out.isEmpty()) return;
    QFile f(out);
    if (f.open(QIODevice::Append | QIODevice::Text)) f.write(line.toUtf8());
}

// The fixture with every shot given a swing folder (the carousel identity the panel picks by).
std::shared_ptr<SessionSnapshot> fixtureSession(const charts_test::Fixture &fx)
{
    auto s = std::make_shared<SessionSnapshot>();
    s->sessionDir = QStringLiteral("/fx/2026-07-04_Mark-Liversedge_Wrist_01");
    s->conditions = fx.session.conditions;
    s->revision = 1;
    for (const auto &sh : fx.session.shots) {
        auto c = std::make_shared<ShotSeries>(*sh);
        c->swingDir = s->sessionDir + QStringLiteral("/swing_%1").arg(c->ordinal, 4, 10, QLatin1Char('0'));
        c->shotId = c->ordinal;
        s->shots.push_back(c);
    }
    return s;
}

QString dirOf(int ordinal) { return QStringLiteral("/fx/2026-07-04_Mark-Liversedge_Wrist_01/swing_%1").arg(ordinal, 4, 10, QLatin1Char('0')); }

// A shot as shotReplay.analysisDetail carries it (disk_replay_source.cpp's shape).
QVariantMap detailOf(const ShotSeries &s)
{
    QVariantList series, phases;
    for (const QString &k : s.metricOrder) {
        const MetricSeries *m = s.metric(k);
        if (!m) continue;
        QVariantList ts, vs, vd, ps;
        for (qint64 t : m->tUs) ts << qlonglong(t);
        for (double v : m->value) vs << v;
        for (uint8_t b : m->valid) vd << int(b);
        for (const PhaseSample &p : m->phaseSamples)
            ps << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) },
                               { QStringLiteral("value"), p.value }, { QStringLiteral("band"), p.band } };
        QVariantMap sm{ { QStringLiteral("key"), m->key }, { QStringLiteral("label"), m->label }, { QStringLiteral("unit"), m->unit },
                        { QStringLiteral("t_us"), ts }, { QStringLiteral("value"), vs }, { QStringLiteral("phaseSamples"), ps } };
        if (!m->valid.empty()) sm.insert(QStringLiteral("valid"), vd);
        if (std::isfinite(m->sigma)) sm.insert(QStringLiteral("sigma"), m->sigma);
        series << sm;
    }
    for (const PhaseMark &p : s.phases)
        phases << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), qlonglong(p.tUs) }, { QStringLiteral("conf"), p.conf } };
    return { { QStringLiteral("series"), series }, { QStringLiteral("phases"), phases },
             { QStringLiteral("kinematicSequence"), s.kinematicSequence } };
}

QVariantMap rowOf(const QVariantList &rows, const QString &key)
{
    for (const QVariant &r : rows) if (r.toMap().value(QStringLiteral("key")).toString() == key) return r.toMap();
    return {};
}

qint64 phaseUs(const ShotSeries &s, int ph)
{
    for (const PhaseMark &p : s.phases) if (p.phase == ph) return p.tUs;
    return -1;
}

} // namespace

class ChartsTracesPresenterTest : public QObject
{
    Q_OBJECT

    charts_test::Fixture m_fx;
    std::shared_ptr<SessionSnapshot> m_snap;

    // A presenter on the fixture, picked on shot 8, synchronous.
    void setUp(ChartsTracesPresenter &p, std::shared_ptr<const SessionSnapshot> snap = {}, int picked = 8)
    {
        p.setSynchronous(true);
        p.setSnapshot(snap ? snap : m_snap);
        p.setPickedSwingDir(dirOf(picked));
        p.setRegisterName(QStringLiteral("analyse"));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_fx = charts_test::loadFixture(QStringLiteral(CHARTS_MATHS_DATA "/session_0704.json"));
        QVERIFY(m_fx.ok);
        QCOMPARE(int(m_fx.session.shots.size()), 15);
        m_snap = fixtureSession(m_fx);
    }

    // ── This shot: the answer head, D4, the neutral note, the chain ────────────────────────────
    void sequenceHeadShot8()
    {
        ChartsTracesPresenter p;
        setUp(p);
        const QVariantMap c = p.choices();
        QCOMPARE(c.value(QStringLiteral("preset")).toString(), QStringLiteral("Kinematic sequence"));
        const QVariantMap s = p.thisShot();
        QVERIFY(s.value(QStringLiteral("available")).toBool());
        QCOMPARE(s.value(QStringLiteral("ordinal")).toInt(), 8);
        QCOMPARE(s.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 8 of 15  ·  7 iron"));
        QCOMPARE(text(s.value(QStringLiteral("head"))), QStringLiteral("Out of sequence: the pelvis peaked late"));
        QCOMPARE(text(s.value(QStringLiteral("note"))), words::latePelvisNote());
        // the pelvis' own time from impact (+20 ms), not the club→pelvis gap (69 ms)
        QCOMPARE(text(s.value(QStringLiteral("standfirst"))),
                 nb(QStringLiteral("The club peaked 49~ms before impact, the pelvis 20~ms after impact. "
                                   "On fourteen of the fifteen shots the pelvis peaks after impact.")));
        const QVariantMap seq = s.value(QStringLiteral("sequence")).toMap();
        QCOMPARE(text(seq.value(QStringLiteral("chain"))), nb(QStringLiteral("Lead arm — 58~ms → Club — 69~ms → Pelvis, late")));
        // the chain's figures are runs set in the figure face
        int figRuns = 0;
        for (const QVariant &r : seq.value(QStringLiteral("chain")).toMap().value(QStringLiteral("runs")).toList())
            figRuns += r.toMap().value(QStringLiteral("fig")).toBool() ? 1 : 0;
        QCOMPARE(figRuns, 2);
        // D4: ONE late segment on the tile, the pelvis
        int late = 0;
        QString lateSeg;
        for (const QVariant &r : seq.value(QStringLiteral("table")).toList())
            if (r.toMap().value(QStringLiteral("late")).toBool()) { ++late; lateSeg = r.toMap().value(QStringLiteral("segment")).toString(); }
        QCOMPARE(late, 1);
        QCOMPARE(lateSeg, QStringLiteral("pelvis"));
        // SAID ONCE (Mark, 17 Oct 2026): the strip gives only the order it reads against; the unplaced chest is a
        // plain "-" in the table, no guessed rank and no gate words (the headline and the panel's tail say why)
        QCOMPARE(text(seq.value(QStringLiteral("note"))), QStringLiteral("Expected pelvis, chest, lead arm, club."));
        QVERIFY(seq.value(QStringLiteral("whyText")).toString().isEmpty());
        QVERIFY(!seq.value(QStringLiteral("verdictText")).toString().contains(QLatin1Char('?')));
        for (const QVariant &r : seq.value(QStringLiteral("table")).toList())
            if (r.toMap().value(QStringLiteral("segment")).toString() == QLatin1String("thorax")) {
                QCOMPARE(r.toMap().value(QStringLiteral("actualRank")).toString(), QStringLiteral("-"));
                QCOMPARE(r.toMap().value(QStringLiteral("peakText")).toString(), QStringLiteral("-"));
            }
        // the late mark is a flag and a word on the curve's peak, never a colour
        const QVariantMap pel = rowOf(s.value(QStringLiteral("rows")).toList(), QStringLiteral("pelvisAngularSpeed"));
        QVERIFY(!pel.isEmpty());
        const QVariantMap pk = pel.value(QStringLiteral("peaks")).toList().value(0).toMap();
        QVERIFY(pk.value(QStringLiteral("late")).toBool());
        QCOMPARE(pk.value(QStringLiteral("lateWord")).toString(), QStringLiteral("late"));
        QCOMPARE(pk.value(QStringLiteral("text")).toString(), nb(QStringLiteral("+20~ms")));
        const QVariantMap arm = rowOf(s.value(QStringLiteral("rows")).toList(), QStringLiteral("leadArmAngularSpeed"));
        QVERIFY(!arm.value(QStringLiteral("peaks")).toList().value(0).toMap().value(QStringLiteral("late")).toBool());
        // the tile's % of pro and the window that opens on the data (P4→P8)
        QVERIFY(arm.value(QStringLiteral("sequence")).toMap().value(QStringLiteral("pctText")).toString().contains(QLatin1Char('%')));
        const QVariantMap win = s.value(QStringLiteral("window")).toMap();
        QCOMPARE(win.value(QStringLiteral("key")).toString(), QStringLiteral("data"));
        QCOMPARE(win.value(QStringLiteral("tagsText")).toString(), QStringLiteral("P4→P8"));
        QCOMPARE(win.value(QStringLiteral("header")).toString(), QStringLiteral("Top → Shaft-parallel through"));
        // Replay: the key figures, the late pelvis flagged with its word
        p.setRegisterName(QStringLiteral("replay"));
        p.setPreset(QStringLiteral("Kinematic sequence"));
        const QVariantList figs = p.thisShot().value(QStringLiteral("figures")).toList();
        QCOMPARE(figs.size(), 3);
        QCOMPARE(figs.at(1).toMap().value(QStringLiteral("text")).toString(), nb(QStringLiteral("58~ms")));
        QVERIFY(figs.at(2).toMap().value(QStringLiteral("warn")).toBool());
    }

    void replayHeadClubAndSpeed()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setRegisterName(QStringLiteral("replay"));
        QCOMPARE(p.choices().value(QStringLiteral("preset")).toString(), QStringLiteral("Club delivery"));
        QCOMPARE(text(p.thisShot().value(QStringLiteral("head"))), nb(QStringLiteral("Clubhead speed 82~mph at impact")));
        QVERIFY(text(p.thisShot().value(QStringLiteral("note"))).isEmpty());
        // Replay is the glance: of the instant readings only path, face, attack angle, low point and dynamic
        // loft (with their launch-monitor twins) are cards (16 Oct)
        const QStringList glance{ QStringLiteral("clubPath"), QStringLiteral("lm.clubPath"), QStringLiteral("lm.faceAngle"),
                                  QStringLiteral("attackAngle"), QStringLiteral("lm.attackAngle"), QStringLiteral("lowPointAhead"),
                                  QStringLiteral("lm.lowPointAhead"), QStringLiteral("lm.dynamicLoft") };
        for (const QVariant &cv : p.thisShot().value(QStringLiteral("cards")).toList())
            QVERIFY2(glance.contains(cv.toMap().value(QStringLiteral("key")).toString()), qPrintable(cv.toMap().value(QStringLiteral("key")).toString()));
        // Analyse shows them all: instant-only members are cards, not traces; shaft lie rides on shaft lean's card
        p.setRegisterName(QStringLiteral("analyse"));
        p.setPreset(QStringLiteral("Club delivery"));
        const QVariantList cards = p.thisShot().value(QStringLiteral("cards")).toList();
        bool lean = false, lieAlone = false;
        for (const QVariant &cv : cards) {
            const QVariantMap card = cv.toMap();
            if (card.value(QStringLiteral("key")).toString() == QLatin1String("impactShaftLean")) {
                lean = true;
                bool hasLie = false;
                for (const QVariant &t : card.value(QStringLiteral("tiles")).toList())
                    hasLie = hasLie || t.toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("lie"));
                QVERIFY2(hasLie, "shaft lie's tiles ride on shaft lean's card");
            }
            lieAlone = lieAlone || card.value(QStringLiteral("key")).toString() == QLatin1String("shaftLie");
        }
        QVERIFY(lean);
        QVERIFY(!lieAlone);
    }

    // ── Against: the session median on shot 8's own clock, the largest gap said ───────────────
    void againstMedianShot8()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setAgainst(QStringLiteral("median"));
        p.setDifference(true);
        const QVariantMap r = p.reference();
        QVERIFY2(r.value(QStringLiteral("active")).toBool(), qPrintable(text(r.value(QStringLiteral("why")))));
        QCOMPARE(r.value(QStringLiteral("label")).toString(), QStringLiteral("session median"));
        const ShotSeries &s8 = *m_snap->shots[7];
        // the reference lands on shot 8's OWN P-times: the window's ends (P4, P8 canonical) unwarp onto
        // shot 8's P4 and P8
        const QVariantMap row = rowOf(r.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
        const QList<double> t = dlist(row.value(QStringLiteral("t")));
        QVERIFY(t.size() > 10);
        QVERIFY(std::abs(t.first() - double(phaseUs(s8, phase::Top) - s8.impactUs) / 1000.0) < 0.05);
        for (int i = 1; i < t.size(); ++i) QVERIFY(t[i] > t[i - 1]);
        // the canonical P-times unwarp onto shot 8's own: P4, P6 and P8 land on its P4, P6 and P8
        const SessionAlignment al = alignSession(*m_snap);
        for (int ph : { int(phase::Top), int(phase::P6), int(phase::P8) }) {
            const CanonicalPhase *cp = al.canonicalOf(ph);
            QVERIFY(cp);
            QVERIFY(std::llabs(p.seekUsFor(8, cp->tMs) - phaseUs(s8, ph)) <= 1);
        }
        // the grid's last instant is within one grid step of shot 8's P8 (the grid stops at or before it)
        const double p8 = double(phaseUs(s8, phase::P8) - s8.impactUs) / 1000.0;
        QVERIFY2(p8 - t.last() < 2.5 && p8 - t.last() >= -0.05, qPrintable(QStringLiteral("P8 %1, last %2").arg(p8).arg(t.last())));
        QCOMPARE(dlist(row.value(QStringLiteral("diff"))).size(), t.size());
        {   // the strip's scale comes from the data: symmetric, and covering every difference
            const double lo = row.value(QStringLiteral("diffLo")).toDouble(), hi = row.value(QStringLiteral("diffHi")).toDouble();
            QVERIFY(std::isfinite(hi) && hi > 0);
            QCOMPARE(lo, -hi);
            double mx = 0;
            for (double d : dlist(row.value(QStringLiteral("diff")))) if (std::isfinite(d)) mx = std::max(mx, std::abs(d));
            QCOMPARE(hi, mx);
            QVERIFY(row.value(QStringLiteral("bridges")).toList().isEmpty());     // a median's blank stays blank
        }
        const QString head = text(r.value(QStringLiteral("head")));
        QVERIFY2(head.startsWith(QStringLiteral("Shot 8 left your usual swing furthest ")), qPrintable(head));
        QVERIFY2(head.contains(QStringLiteral("impact")), qPrintable(head));
        // the playhead reads "now" against the reference
        p.setPlayheadUs(s8.impactUs - 40000);
        const QVariantMap now = p.now();
        QVERIFY(now.value(QStringLiteral("valid")).toBool());
        const QVariantMap nr = rowOf(now.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
        QVERIFY(!nr.value(QStringLiteral("refText")).toString().isEmpty());
        QVERIFY(nr.value(QStringLiteral("addrText")).toString().startsWith(QStringLiteral("Δ ")));
        report("    against median: %s\n", qPrintable(head));
    }

    // ── Against the ★ reference (build/wrist-tab): any session's swing, listed first, no session needed ──
    void againstReferenceStar()
    {
        const auto findA = [](const QVariantMap &choices, const QString &id) {
            for (const QVariant &v : choices.value(QStringLiteral("against")).toList())
                if (v.toMap().value(QStringLiteral("id")).toString() == id) return v.toMap();
            return QVariantMap{};
        };
        ChartsTracesPresenter p;
        setUp(p);
        // unset: listed straight after "none", disabled, with why
        const QVariantList ag = p.choices().value(QStringLiteral("against")).toList();
        QVERIFY(ag.size() >= 2);
        QCOMPARE(ag.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("reference"));
        QVERIFY(!ag.at(1).toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(!ag.at(1).toMap().value(QStringLiteral("why")).toString().isEmpty());
        p.setAgainst(QStringLiteral("reference"));
        QVERIFY(p.reference().value(QStringLiteral("refused")).toBool());
        QVERIFY(text(p.reference().value(QStringLiteral("why"))).startsWith(QStringLiteral("No reference swing is set")));

        // a session shot as the reference: lined up on shot 8's own clock
        p.setReferenceSwingDir(dirOf(3));
        QVERIFY(findA(p.choices(), QStringLiteral("reference")).value(QStringLiteral("enabled")).toBool());
        const QVariantMap r = p.reference();
        QVERIFY(r.value(QStringLiteral("active")).toBool());
        QCOMPARE(r.value(QStringLiteral("label")).toString(), QStringLiteral("the reference ★"));
        QCOMPARE(r.value(QStringLiteral("refLabel")).toString(), QStringLiteral("shot 3, 4 Jul 2026"));
        QCOMPARE(r.value(QStringLiteral("ordinal")).toInt(), 3);
        QVERIFY(!r.value(QStringLiteral("rows")).toList().isEmpty());
        bool anyValue = false;
        for (const QVariant &row : r.value(QStringLiteral("rows")).toList())
            for (double v : dlist(row.toMap().value(QStringLiteral("v")))) anyValue = anyValue || std::isfinite(v);
        QVERIFY(anyValue);

        // the picked swing IS the reference: refused, said so
        p.setReferenceSwingDir(dirOf(8));
        QVERIFY(p.reference().value(QStringLiteral("refused")).toBool());
        QCOMPARE(text(p.reference().value(QStringLiteral("why"))), QStringLiteral("This swing is your reference ★."));

        // a session of two: the median is refused, the ★ is not (it needs no session)
        auto two = std::make_shared<SessionSnapshot>(*m_snap);
        two->shots.clear();
        for (int o : { 3, 8 }) {                      // renumbered 1, 2: a session of its own
            auto c = std::make_shared<ShotSeries>(*m_snap->shots[size_t(o - 1)]);
            c->ordinal = int(two->shots.size()) + 1;
            two->shots.push_back(c);
        }
        two->revision = 99;
        ChartsTracesPresenter q;
        setUp(q, two, 8);
        q.setReferenceSwingDir(dirOf(3));
        QVERIFY(!findA(q.choices(), QStringLiteral("median")).value(QStringLiteral("enabled")).toBool());
        q.setAgainst(QStringLiteral("reference"));
        QVERIFY(q.reference().value(QStringLiteral("active")).toBool());

        // a swing that is not on disk: refused with the reason
        q.setReferenceSwingDir(QStringLiteral("/nonexistent/2026-01-01_X/swing_0001"));
        QVERIFY(q.reference().value(QStringLiteral("refused")).toBool());
        QCOMPARE(text(q.reference().value(QStringLiteral("why"))), QStringLiteral("The reference swing is no longer on disk."));
    }

    void againstRefusals()
    {
        ChartsTracesPresenter p;
        setUp(p, {}, 1);
        p.setAgainst(QStringLiteral("previous"));
        QVERIFY(p.reference().value(QStringLiteral("refused")).toBool());
        QCOMPARE(text(p.reference().value(QStringLiteral("why"))), QStringLiteral("Shot 1 has no shot before it."));
        p.setAgainst(QStringLiteral("next"));
        QVERIFY(p.reference().value(QStringLiteral("active")).toBool());
        QCOMPARE(p.reference().value(QStringLiteral("ordinal")).toInt(), 2);
        p.setAgainst(QStringLiteral("typical"));
        QVERIFY(p.reference().value(QStringLiteral("label")).toString().endsWith(QStringLiteral("the most typical")));
        p.setAgainst(QStringLiteral("like"));
        const QVariantMap like = p.reference();
        if (like.value(QStringLiteral("active")).toBool()) {
            QVERIFY(like.value(QStringLiteral("n")).toInt() >= 3);
            QVERIFY(like.value(QStringLiteral("label")).toString().endsWith(QStringLiteral("shots shaped like this one")));
        }
    }

    // ── Session: the envelope, where the curves part, the alignment note ──────────────────────
    void sessionEnvelopeAndPart()
    {
        ChartsTracesPresenter p;
        setUp(p);
        const QVariantMap s = p.session();
        QVERIFY2(s.value(QStringLiteral("available")).toBool(), qPrintable(text(s.value(QStringLiteral("why")))));
        QCOMPARE(s.value(QStringLiteral("n")).toInt(), 15);
        const QVariantList rows = s.value(QStringLiteral("rows")).toList();
        QVERIFY(rows.size() >= 3);
        const QVariantMap club = rowOf(rows, QStringLiteral("clubAngularSpeed"));
        const QVariantMap panel = club.value(QStringLiteral("panels")).toList().value(0).toMap();
        const QList<double> med = dlist(panel.value(QStringLiteral("med"))), q1 = dlist(panel.value(QStringLiteral("q1"))),
                            q3 = dlist(panel.value(QStringLiteral("q3")));
        QCOMPARE(med.size(), dlist(panel.value(QStringLiteral("t"))).size());
        int finite = 0;
        for (int i = 0; i < med.size(); ++i)
            if (std::isfinite(med[i])) { ++finite; QVERIFY(q1[i] <= med[i] + 1e-9 && med[i] <= q3[i] + 1e-9); }
        QVERIFY(finite > med.size() / 2);
        const QString head = text(s.value(QStringLiteral("head")));
        QVERIFY2(head.contains(QStringLiteral(" parts most ")), qPrintable(head));
        QVERIFY(text(s.value(QStringLiteral("standfirst"))).startsWith(QStringLiteral("There, half the fifteen shots lie within ")));
        QVERIFY(!text(s.value(QStringLiteral("alignNote"))).isEmpty());
        QVERIFY(!club.value(QStringLiteral("picked")).toMap().isEmpty());
        // the P ticks carry the middle half of when each P happened, on the shots' own clocks
        bool p6 = false;
        for (const QVariant &t : s.value(QStringLiteral("ticks")).toList())
            if (t.toMap().value(QStringLiteral("tag")).toString() == QLatin1String("P6")) { p6 = true; QVERIFY(t.toMap().value(QStringLiteral("when")).toString().contains(QStringLiteral(" to "))); }
        QVERIFY(p6);
        // the sequence across the session, the pelvis said neutrally
        const QVariantMap sq = s.value(QStringLiteral("sequence")).toMap();
        QVERIFY(sq.value(QStringLiteral("present")).toBool());
        QVERIFY(text(sq.value(QStringLiteral("stand"))).contains(words::latePelvisNote()));
        QCOMPARE(sq.value(QStringLiteral("segments")).toList().size(), 4);
        // questions, answered from the data
        const QVariantList qs = s.value(QStringLiteral("questions")).toList();
        QVERIFY(!qs.isEmpty() && qs.size() <= 3);
        QCOMPARE(qs.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("part"));
        QVERIFY(text(qs.first().toMap().value(QStringLiteral("answer"))).contains(QStringLiteral("outside the middle half")));
        report("    session head: %s\n    %s\n", qPrintable(head), qPrintable(text(qs.first().toMap().value(QStringLiteral("answer")))));
        // Replay (D6): "now" against "typical" at the playhead
        const ShotSeries &s8 = *m_snap->shots[7];
        p.setPlayheadUs(s8.impactUs - 30000);
        const QVariantMap nr = rowOf(p.now().value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
        QVERIFY(!nr.value(QStringLiteral("typicalText")).toString().isEmpty());
        QVERIFY(std::isfinite(nr.value(QStringLiteral("typical")).toDouble()));
    }

    // ── the group fit: refused under three, blank where fewer than three contribute ────────────
    void groupFitHonesty()
    {
        // two shots on a different club: the club facet's small group is refused
        auto mixed = std::make_shared<SessionSnapshot>(*m_snap);
        mixed->revision = 2;
        for (int i : { 2, 5 }) {
            auto c = std::make_shared<ShotSeries>(*mixed->shots[size_t(i)]);
            c->club = QStringLiteral("DRIVER");
            mixed->shots[size_t(i)] = c;
        }
        ChartsTracesPresenter p;
        setUp(p, mixed);
        QVERIFY(!text(p.session().value(QStringLiteral("clubNote"))).isEmpty());   // mixed clubs said, all drawn by default
        bool clubOffered = false;
        for (const QVariant &f : p.choices().value(QStringLiteral("facets")).toList()) clubOffered = clubOffered || f.toMap().value(QStringLiteral("id")) == QStringLiteral("club");
        QVERIFY(clubOffered);
        p.setBand(QStringLiteral("group fit"));
        p.setFacet(QStringLiteral("club"));
        const QVariantMap s = p.session();
        QVERIFY(s.value(QStringLiteral("available")).toBool());
        const QVariantList panels = rowOf(s.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed")).value(QStringLiteral("panels")).toList();
        QCOMPARE(panels.size(), 2);
        bool refused = false, fitted = false;
        for (const QVariant &pv : panels) {
            const QVariantMap fit = pv.toMap().value(QStringLiteral("fit")).toMap();
            if (fit.value(QStringLiteral("refused")).toBool()) {
                refused = true;
                QCOMPARE(fit.value(QStringLiteral("why")).toString(), QStringLiteral("fewer than three shots: no group fit"));
                QVERIFY(!fit.contains(QStringLiteral("v")));
            } else {
                fitted = true;
                QVERIFY(fit.value(QStringLiteral("label")).toString().contains(QStringLiteral("13 shots")));
                const QList<double> v = dlist(fit.value(QStringLiteral("v"))), n = dlist(fit.value(QStringLiteral("nShots")));
                for (int i = 0; i < v.size(); ++i)
                    if (std::isfinite(v[i])) QVERIFY2(n[i] >= 3, "a fitted instant rests on three shots or more");
            }
        }
        QVERIFY(refused && fitted);
    }

    // ── facets: a fault that fires on every shot is never offered ─────────────────────────────
    void facetsOffered()
    {
        ChartsTracesPresenter p;
        setUp(p);
        QHash<QString, int> fired;
        for (const auto &s : m_snap->shots) for (const QString &id : s->diagnostics.fired) fired[id] += 1;
        int conditions = 0;
        for (const QVariant &fv : p.choices().value(QStringLiteral("facets")).toList()) {
            const QVariantMap f = fv.toMap();
            if (f.value(QStringLiteral("kind")).toString() != QLatin1String("condition")) continue;
            ++conditions;
            const int n = fired.value(f.value(QStringLiteral("id")).toString());
            QVERIFY2(n >= 3 && n <= 12, qPrintable(f.value(QStringLiteral("id")).toString()));
        }
        QVERIFY(conditions > 0);
        // a condition facet carries the co-occurrence caveat
        const QString id = [&] {
            for (const QVariant &fv : p.choices().value(QStringLiteral("facets")).toList())
                if (fv.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("condition")) return fv.toMap().value(QStringLiteral("id")).toString();
            return QString();
        }();
        p.setFacet(id);
        QCOMPARE(text(p.session().value(QStringLiteral("caveat"))), words::coOccurrenceNote());
        QCOMPARE(p.session().value(QStringLiteral("groups")).toList().size(), 2);
        // the default is no facet (D3)
        p.setFacet(QString());
        QCOMPARE(p.session().value(QStringLiteral("groups")).toList().size(), 1);
    }

    // ── alignment: impact-only shots say their route (§4.1) ──────────────────────────────────
    void impactOnlyAlignment()
    {
        auto bare = std::make_shared<SessionSnapshot>(*m_snap);
        bare->revision = 3;
        for (auto &sh : bare->shots) {
            auto c = std::make_shared<ShotSeries>(*sh);
            std::vector<PhaseMark> keep;
            for (const PhaseMark &m : c->phases) if (m.phase == phase::Impact) keep.push_back(m);
            c->phases = keep;
            sh = c;
        }
        ChartsTracesPresenter p;
        setUp(p, bare);
        const QString note = text(p.session().value(QStringLiteral("alignNote")));
        QVERIFY2(note.contains(QStringLiteral("only impact"), Qt::CaseInsensitive), qPrintable(note));
        report("    impact-only note: %s\n", qPrintable(note));
        // and with landmarks and the tempo fit off: the design's own words
        const SessionAlignment al = alignSession(*bare, [] { WarpOptions o; o.useLandmarks = false; o.useTempo = false; return o; }());
        QCOMPARE(al.countRoute(AlignRoute::ImpactShift), 15);
    }

    // ── coverage: the thorax's bridged hole, and the group fit across it ─────────────────────
    void coverageThorax()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setBand(QStringLiteral("fit"));
        const QVariantMap s = p.session();
        const QVariantMap th = rowOf(s.value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed"));
        QVERIFY(!th.isEmpty());
        const QVariantList cov = th.value(QStringLiteral("coverage")).toList();
        QCOMPARE(cov.size(), 15);
        QCOMPARE(cov.first().toMap().value(QStringLiteral("ordinal")).toInt(), 8);         // this shot first
        QVERIFY(cov.first().toMap().value(QStringLiteral("picked")).toBool());
        QVERIFY2(cov.first().toMap().value(QStringLiteral("runs")).toList().size() >= 2, "shot 8's thorax has a bridged hole");
        // figures: pooled samples, shot 8's own peak, shot 8's gap
        const QVariantList figs = th.value(QStringLiteral("figures")).toList();
        QCOMPARE(figs.value(2).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("shot 8 gap"));
        QVERIFY(figs.value(2).toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral(" to ")));
        const QString head = text(s.value(QStringLiteral("head")));
        QVERIFY2(head.startsWith(QStringLiteral("The group fit spans shot 8’s gap")) || head.startsWith(QStringLiteral("No shot measured")), qPrintable(head));
        QVERIFY(text(s.value(QStringLiteral("standfirst"))).contains(QStringLiteral("no figure for shot 8 is read from it")));
        report("    fit head: %s; shot 8 gap %s\n", qPrintable(head), qPrintable(figs.value(2).toMap().value(QStringLiteral("text")).toString()));
    }

    // ── seeking: canonical → the shot's own µs, and back ────────────────────────────────────
    void seekRoundTrip()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.thisShot();
        const SessionAlignment al = alignSession(*m_snap);
        const CanonicalPhase *p6 = al.canonicalOf(phase::P6);
        QVERIFY(p6);
        for (int ord : { 3, 8, 12 }) {
            const ShotSeries &s = *m_snap->shots[size_t(ord - 1)];
            const ShotWarp &w = al.shots[size_t(ord - 1)];
            bool kept = false;
            for (const WarpAnchor &a : w.anchors) kept = kept || a.phase == phase::P6;
            if (!kept) continue;
            QVERIFY(std::llabs(p.seekUsFor(ord, p6->tMs) - phaseUs(s, phase::P6)) <= 1);
        }
        const ShotSeries &s8 = *m_snap->shots[7];
        for (double ms : { -150.0, -40.0, 0.0, 25.0 }) {
            const double canon = p.canonMsForShotMs(ms);
            QVERIFY(std::isfinite(canon));
            QVERIFY(std::llabs(p.seekUsFor(8, canon) - (s8.impactUs + qint64(ms * 1000))) <= 1);
            QCOMPARE(p.seekUsForShotMs(ms), s8.impactUs + qint64(ms * 1000));
        }
        QCOMPARE(p.seekUsFor(99, 0.0), qint64(-1));
        // brushing: the nearest curve at an instant is a shot of the session
        const QVariantMap hit = p.shotAt(QStringLiteral("clubAngularSpeed"), -40.0, 1500.0);
        QVERIFY(hit.value(QStringLiteral("found")).toBool());
        QVERIFY(hit.value(QStringLiteral("swingDir")).toString().startsWith(QStringLiteral("/fx/")));
        // hover: the mean and the raw sample beside it, "-" where not measured
        const QVariantMap hv = p.hoverAt(-40.0);
        QCOMPARE(hv.value(QStringLiteral("when")).toString(), nb(QStringLiteral("−40~ms · impact")));
        const QVariantMap club = rowOf(hv.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
        QVERIFY(club.value(QStringLiteral("measured")).toBool());
        QVERIFY(club.value(QStringLiteral("raw")).toString().startsWith(QStringLiteral("raw ")));
    }

    // ── the window: today's semantics, and the figures recomputed for it ─────────────────────
    void windowSemantics()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setPreset(QStringLiteral("Club delivery"));
        const ShotSeries &s8 = *m_snap->shots[7];
        auto win = [&] { return p.thisShot().value(QStringLiteral("window")).toMap(); };
        auto peakText = [&] {
            const QVariantMap r = rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("clubheadSpeed"));
            for (const QVariant &t : r.value(QStringLiteral("tiles")).toList())
                if (t.toMap().value(QStringLiteral("label")).toString() == QLatin1String("peak")) return t.toMap().value(QStringLiteral("text")).toString();
            return QString();
        };
        QCOMPARE(win().value(QStringLiteral("key")).toString(), QStringLiteral("swing"));      // the swing, not the recording
        QCOMPARE(win().value(QStringLiteral("phaseA")).toInt(), 0);
        QCOMPARE(win().value(QStringLiteral("phaseB")).toInt(), 7);
        p.setWindow(QStringLiteral("full"));
        QCOMPARE(win().value(QStringLiteral("startUs")).toLongLong(), 0);
        QCOMPARE(win().value(QStringLiteral("header")).toString(), QStringLiteral("Full recording"));
        p.setWindow(QStringLiteral("back"));
        const QString backPeak = peakText();
        p.setWindow(QStringLiteral("down"));
        const QString downPeak = peakText();
        QVERIFY2(backPeak != downPeak, "PEAK is recomputed for the window");
        p.setWindow(QStringLiteral("seg:2-13"));
        QCOMPARE(win().value(QStringLiteral("tagsText")).toString(), QStringLiteral("P4→P5"));
        p.setCustomStartUs(s8.impactUs - 100000);
        p.setCustomEndUs(s8.impactUs);
        p.setWindow(QStringLiteral("custom"));
        QCOMPARE(win().value(QStringLiteral("key")).toString(), QStringLiteral("custom"));
        bool chip = false;
        for (const QVariant &c : p.choices().value(QStringLiteral("windows")).toList())
            chip = chip || (c.toMap().value(QStringLiteral("key")).toString() == QLatin1String("custom") && c.toMap().value(QStringLiteral("current")).toBool());
        QVERIFY(chip);
        // a window wholly outside the data: "-" figures, never 0
        p.setCustomStartUs(0);
        p.setCustomEndUs(1000);
        const QVariantMap r = rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("clubheadSpeed"));
        for (const QVariant &t : r.value(QStringLiteral("tiles")).toList())
            if (t.toMap().value(QStringLiteral("window")).toBool())
                QCOMPARE(t.toMap().value(QStringLiteral("text")).toString(), QStringLiteral("-"));
        // Custom: a member switched off is a chip left on the list, off, and no trace
        p.setWindow(QStringLiteral("auto"));
        p.setMetricsOff({ QStringLiteral("handSpeed") });
        QVERIFY(p.choices().value(QStringLiteral("custom")).toBool());
        QVERIFY(rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("handSpeed")).isEmpty());
    }

    // ── the edge states (STEP2A) ────────────────────────────────────────────────────────────
    void edgeStates()
    {
        {   // no session, no shot
            ChartsTracesPresenter p;
            p.setSynchronous(true);
            QVERIFY(!p.thisShot().value(QStringLiteral("available")).toBool());
            QVERIFY(!text(p.thisShot().value(QStringLiteral("empty"))).isEmpty());
            QCOMPARE(text(p.session().value(QStringLiteral("why"))), QStringLiteral("No session is loaded."));
            QVERIFY(!p.now().value(QStringLiteral("valid")).toBool());
        }
        {   // n < 3: refused, saying why
            auto two = std::make_shared<SessionSnapshot>(*m_snap);
            two->shots.resize(2);
            two->revision = 4;
            ChartsTracesPresenter p;
            setUp(p, two, 2);
            QCOMPARE(text(p.session().value(QStringLiteral("why"))), QStringLiteral("Three shots are needed to draw the session; this session has two."));
            QVERIFY(p.thisShot().value(QStringLiteral("available")).toBool());
            p.setAgainst(QStringLiteral("median"));
            QVERIFY(p.reference().value(QStringLiteral("refused")).toBool());
        }
        {   // the picked shot not in the snapshot (a live shot arriving): This shot from its own detail
            ChartsTracesPresenter p;
            setUp(p);
            p.setPickedSwingDir(QStringLiteral("/elsewhere/swing_0099"));
            p.setPickedDetail(detailOf(*m_snap->shots[7]));
            const QVariantMap s = p.thisShot();
            QVERIFY(s.value(QStringLiteral("available")).toBool());
            QVERIFY(!s.value(QStringLiteral("inSession")).toBool());
            QCOMPARE(s.value(QStringLiteral("shotId")).toInt(), 99);
            QCOMPARE(text(s.value(QStringLiteral("head"))), QStringLiteral("Out of sequence: the pelvis peaked late"));
            QVERIFY(p.session().value(QStringLiteral("available")).toBool());      // the session still draws, with this shot on it
            QVERIFY(!rowOf(p.session().value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed")).value(QStringLiteral("picked")).toMap().isEmpty());
            p.setAgainst(QStringLiteral("median"));
            QVERIFY(p.reference().value(QStringLiteral("active")).toBool());
        }
        {   // IMU-only / one camera: a metric absent from every shot has no row and no empty figure
            auto noThorax = std::make_shared<SessionSnapshot>(*m_snap);
            noThorax->revision = 5;
            for (auto &sh : noThorax->shots) {
                auto c = std::make_shared<ShotSeries>(*sh);
                c->metrics.remove(QStringLiteral("thoraxAngularSpeed"));
                c->metricOrder.removeAll(QStringLiteral("thoraxAngularSpeed"));
                sh = c;
            }
            ChartsTracesPresenter p;
            setUp(p, noThorax);
            QVERIFY(rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed")).isEmpty());
            QVERIFY(rowOf(p.session().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed")).isEmpty());
            QVERIFY(!rowOf(p.session().value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed")).isEmpty());
        }
        {   // a shot with no curve for a key: absent on This shot, the session still draws it
            auto gap = std::make_shared<SessionSnapshot>(*m_snap);
            gap->revision = 6;
            auto c = std::make_shared<ShotSeries>(*gap->shots[7]);
            c->metrics.remove(QStringLiteral("clubAngularSpeed"));
            c->metricOrder.removeAll(QStringLiteral("clubAngularSpeed"));
            gap->shots[7] = c;
            ChartsTracesPresenter p;
            setUp(p, gap);
            QVERIFY(rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed")).isEmpty());
            const QVariantMap sr = rowOf(p.session().value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
            QVERIFY(!sr.isEmpty());
            QVERIFY(sr.value(QStringLiteral("picked")).toMap().isEmpty());
        }
        {   // a preset with no drawable curve on this shot: cards only, said (Club delivery was one until it took
            // in Club & speed's curves on 16 Oct, so every preset is tried)
            ChartsTracesPresenter p;
            setUp(p);
            QStringList names;
            for (const QVariant &g : p.choices().value(QStringLiteral("presets")).toList()) names << g.toMap().value(QStringLiteral("id")).toString();
            for (const QString &n : names) {
                p.setPreset(n);
                if (p.thisShot().value(QStringLiteral("rows")).toList().isEmpty())
                    QVERIFY2(!text(p.thisShot().value(QStringLiteral("noTraces"))).isEmpty(), qPrintable(n));
            }
        }
    }

    // ── bridged runs: never drawn through their placeholder values ──────────────────────────
    void bridgedRunsAreBridged()
    {
        ChartsTracesPresenter p;
        setUp(p);
        const QVariantMap row = rowOf(p.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed"));
        QVERIFY(!row.isEmpty());
        const QList<double> t = dlist(row.value(QStringLiteral("t"))), mean = dlist(row.value(QStringLiteral("mean"))),
                            raw = dlist(row.value(QStringLiteral("raw")));
        // shot 8's thorax is bridged from −6.8 to +13.4 ms (valid 0, value 0): ONE bridge, from the last
        // measured sample before (−13.5 ms) to the first after (+20.1 ms)
        QVariantMap br;
        for (const QVariant &b : row.value(QStringLiteral("bridges")).toList())
            if (b.toMap().value(QStringLiteral("t0")).toDouble() < 0 && b.toMap().value(QStringLiteral("t1")).toDouble() > 0) br = b.toMap();
        QVERIFY2(!br.isEmpty(), "a bridge across impact");
        QVERIFY2(std::abs(br.value(QStringLiteral("t0")).toDouble() + 13.5) < 0.15, qPrintable(QString::number(br.value(QStringLiteral("t0")).toDouble())));
        QVERIFY2(std::abs(br.value(QStringLiteral("t1")).toDouble() - 20.1) < 0.15, qPrintable(QString::number(br.value(QStringLiteral("t1")).toDouble())));
        QVERIFY(br.value(QStringLiteral("v0")).toDouble() > 100 && br.value(QStringLiteral("v1")).toDouble() > 100);
        for (int i = 0; i < t.size(); ++i) {
            if (t[i] > br.value(QStringLiteral("t0")).toDouble() + 0.01 && t[i] < br.value(QStringLiteral("t1")).toDouble() - 0.01)
                QVERIFY2(std::isnan(mean[i]) && std::isnan(raw[i]), "nothing is drawn inside the bridged run");
            if (std::isfinite(mean[i])) QVERIFY2(mean[i] != 0.0, "no placeholder 0 is drawn");
            if (std::isfinite(raw[i])) QVERIFY2(raw[i] != 0.0, "no placeholder 0 is drawn");
        }
        // the windowed mean never reads a placeholder: make them absurd and nothing measured moves
        auto loud = std::make_shared<SessionSnapshot>(*m_snap);
        loud->revision = 7;
        {
            auto c = std::make_shared<ShotSeries>(*loud->shots[7]);
            MetricSeries &m = c->metrics[QStringLiteral("thoraxAngularSpeed")];
            int changed = 0;
            for (size_t i = 0; i < m.value.size(); ++i) if (!m.isValid(i)) { m.value[i] = 99999.0; ++changed; }
            QVERIFY(changed > 0);
            loud->shots[7] = c;
        }
        ChartsTracesPresenter q;
        setUp(q, loud);
        const QList<double> mean2 = dlist(rowOf(q.thisShot().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed")).value(QStringLiteral("mean")));
        QCOMPARE(mean2.size(), mean.size());
        for (int i = 0; i < mean.size(); ++i)
            if (std::isfinite(mean[i])) QCOMPARE(mean2[i], mean[i]);
        for (double edge : { -13.5, 20.1 }) {
            int k = 0;
            for (int i = 1; i < t.size(); ++i) if (std::abs(t[i] - edge) < std::abs(t[k] - edge)) k = i;
            QVERIFY(std::isfinite(mean[k]) && mean[k] > 100);
            QCOMPARE(mean2[k], mean[k]);
        }
        // the session's picked curve and every other shot: no placeholder drawn, the bridge published
        p.setBand(QStringLiteral("every"));
        const QVariantMap sr = rowOf(p.session().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed"));
        const QVariantMap pk = sr.value(QStringLiteral("picked")).toMap();
        QVERIFY(!pk.value(QStringLiteral("bridges")).toList().isEmpty());
        for (double v : dlist(pk.value(QStringLiteral("v")))) if (std::isfinite(v)) QVERIFY(v != 0.0);
        int othersBridged = 0;
        for (const QVariant &o : sr.value(QStringLiteral("panels")).toList().value(0).toMap().value(QStringLiteral("others")).toList()) {
            for (double v : dlist(o.toMap().value(QStringLiteral("v")))) if (std::isfinite(v)) QVERIFY(v != 0.0);
            othersBridged += o.toMap().value(QStringLiteral("bridges")).toList().isEmpty() ? 0 : 1;
        }
        QVERIFY(othersBridged > 0);
        // Against a reference SHOT: its bridged runs are bridges too
        p.setAgainst(QStringLiteral("previous"));
        const QVariantMap rr = rowOf(p.reference().value(QStringLiteral("rows")).toList(), QStringLiteral("thoraxAngularSpeed"));
        QVERIFY(!rr.value(QStringLiteral("bridges")).toList().isEmpty());
        for (double v : dlist(rr.value(QStringLiteral("v")))) if (std::isfinite(v)) QVERIFY(v != 0.0);
    }

    // ── tick lists carry their text ─────────────────────────────────────────────────────────
    void tickTexts()
    {
        ChartsTracesPresenter p;
        setUp(p);
        const QVariantMap s = p.thisShot();
        const auto checkTicks = [](const QVariantList &ticks, const char *what) {
            QVERIFY2(ticks.size() >= 2, what);
            for (const QVariant &tv : ticks) {
                const QVariantMap t = tv.toMap();
                QVERIFY2(t.contains(QStringLiteral("v")) && !t.value(QStringLiteral("text")).toString().isEmpty(), what);
                QVERIFY2(!t.value(QStringLiteral("text")).toString().contains(QLatin1Char('-')), what);   // U+2212, never a hyphen
            }
        };
        const QVariantMap club = rowOf(s.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed"));
        checkTicks(club.value(QStringLiteral("yTicks")).toList(), "row yTicks");
        for (const QVariant &tv : club.value(QStringLiteral("yTicks")).toList())
            QCOMPARE(tv.toMap().value(QStringLiteral("text")).toString(), words::fig(tv.toMap().value(QStringLiteral("v")).toDouble()));
        checkTicks(s.value(QStringLiteral("msTicks")).toList(), "msTicks");
        bool neg = false;
        for (const QVariant &tv : s.value(QStringLiteral("msTicks")).toList())
            if (tv.toMap().value(QStringLiteral("v")).toDouble() < 0) { neg = true; QVERIFY(tv.toMap().value(QStringLiteral("text")).toString().startsWith(QChar(0x2212))); }
        QVERIFY(neg);
        checkTicks(s.value(QStringLiteral("overlay")).toMap().value(QStringLiteral("yTicks")).toList(), "overlay yTicks");
        const QVariantMap ses = p.session();
        checkTicks(rowOf(ses.value(QStringLiteral("rows")).toList(), QStringLiteral("clubAngularSpeed")).value(QStringLiteral("yTicks")).toList(), "session yTicks");
        checkTicks(ses.value(QStringLiteral("msTicks")).toList(), "session msTicks");
        checkTicks(ses.value(QStringLiteral("sequence")).toMap().value(QStringLiteral("ticks")).toList(), "sequence ticks");
        for (const QVariant &tv : ses.value(QStringLiteral("sequence")).toMap().value(QStringLiteral("ticks")).toList()) {
            const QString t = tv.toMap().value(QStringLiteral("text")).toString();
            QVERIFY(t.startsWith(QLatin1Char('+')) || t.startsWith(QChar(0x2212)));
        }
        // a sub-unit step keeps its decimals ("0.5", never two "1"s)
        p.setPreset(QStringLiteral("Club delivery"));
        QStringList seen;
        for (const QVariant &r : p.thisShot().value(QStringLiteral("rows")).toList())
            for (const QVariant &tv : r.toMap().value(QStringLiteral("yTicks")).toList()) {
                const QString key = r.toMap().value(QStringLiteral("key")).toString() + tv.toMap().value(QStringLiteral("text")).toString();
                QVERIFY2(!seen.contains(key), qPrintable(key));
                seen << key;
            }
    }

    // ── the window brush: P-positions on the recording, decimated overview sparklines ────────
    void windowBrush()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setPreset(QStringLiteral("Club delivery"));
        const QVariantMap s = p.thisShot();
        const ShotSeries &s8 = *m_snap->shots[7];
        bool top = false, impact = false;
        for (const QVariant &tv : s.value(QStringLiteral("recordingTicks")).toList()) {
            const QVariantMap t = tv.toMap();
            if (t.value(QStringLiteral("landmark")).toString() == QLatin1String("top")) {
                top = true;
                QCOMPARE(t.value(QStringLiteral("us")).toLongLong(), phaseUs(s8, phase::Top));
                QCOMPARE(t.value(QStringLiteral("tag")).toString(), QStringLiteral("P4"));
            }
            if (t.value(QStringLiteral("landmark")).toString() == QLatin1String("impact")) {
                impact = true;
                QCOMPARE(t.value(QStringLiteral("us")).toLongLong(), s8.impactUs);
            }
        }
        QVERIFY(top && impact);
        const QVariantList ov = s.value(QStringLiteral("overview")).toList();
        QCOMPARE(ov.size(), s.value(QStringLiteral("rows")).toList().size());
        bool decimated = false;
        for (const QVariant &o : ov) {
            const QVariantMap m = o.toMap();
            const QList<double> t = dlist(m.value(QStringLiteral("t"))), v = dlist(m.value(QStringLiteral("v")));
            QCOMPARE(t.size(), v.size());
            QVERIFY(t.size() <= 200 && t.size() >= 2);
            for (int i = 1; i < t.size(); ++i) QVERIFY(t[i] >= t[i - 1]);
            const MetricSeries *ms = s8.metric(m.value(QStringLiteral("key")).toString());
            QVERIFY(ms);
            QVERIFY(t.first() >= double(ms->tUs.front()) && t.last() <= double(ms->tUs.back()));   // the whole recording's µs
            decimated = decimated || ms->tUs.size() > 200;
            // the brush draws the same series the rows do: its extremes are the drawn line's
            QVERIFY(m.value(QStringLiteral("vHi")).toDouble() >= m.value(QStringLiteral("vLo")).toDouble());
        }
        QVERIFY2(decimated, "a 308-sample curve is decimated");
    }

    // ── the live-state renders' findings (STEP2C) ────────────────────────────────────────────
    void noPickNoPickedLine()
    {
        ChartsTracesPresenter p;
        setUp(p);
        p.setPickedSwingDir(QString());
        for (const QString &band : { QStringLiteral("middle"), QStringLiteral("fit") }) {
            p.setBand(band);
            const QVariantList rows = p.session().value(QStringLiteral("rows")).toList();
            QVERIFY(!rows.isEmpty());
            for (const QVariant &r : rows)
                for (const QVariant &f : r.toMap().value(QStringLiteral("figures")).toList())
                    QVERIFY2(!f.toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("shot ")),
                             qPrintable(f.toMap().value(QStringLiteral("label")).toString()));
        }
    }

    void smallSessionStandfirst()
    {
        for (int n : { 1, 2 }) {
            auto small = std::make_shared<SessionSnapshot>(*m_snap);
            small->shots.erase(small->shots.begin(), small->shots.begin() + 7);   // shot 8 first
            small->shots.resize(size_t(n));
            for (size_t i = 0; i < small->shots.size(); ++i) {
                auto c = std::make_shared<ShotSeries>(*small->shots[i]);
                c->ordinal = int(i) + 1;
                small->shots[i] = c;
            }
            small->revision = 10 + quint64(n);
            ChartsTracesPresenter p;
            p.setSynchronous(true);
            p.setSnapshot(small);
            p.setPickedSwingDir(dirOf(8));
            const QString st = text(p.thisShot().value(QStringLiteral("standfirst")));
            QVERIFY2(!st.contains(QStringLiteral(" of the ")), qPrintable(st));       // no "one of the one shots"
            QCOMPARE(st, nb(QStringLiteral("The club peaked 49~ms before impact, the pelvis 20~ms after impact.")));
        }
    }

    void mixedClubsKicker()
    {
        auto mixed = std::make_shared<SessionSnapshot>(*m_snap);
        mixed->revision = 20;
        for (int i : { 2, 5 }) {
            auto c = std::make_shared<ShotSeries>(*mixed->shots[size_t(i)]);
            c->club = QStringLiteral("GAP WEDGE");
            mixed->shots[size_t(i)] = c;
        }
        ChartsTracesPresenter p;
        setUp(p, mixed);
        QCOMPARE(p.session().value(QStringLiteral("kicker")).toString(), QStringLiteral("15 shots  ·  7 iron and gap wedge"));
        ChartsTracesPresenter q;
        setUp(q);
        QCOMPARE(q.session().value(QStringLiteral("kicker")).toString(), QStringLiteral("15 shots  ·  7 iron"));
    }

    void oneCameraSequenceHead()
    {
        // one camera: only the lead arm and the club placed, in order; the pelvis' and chest's peaks
        // unplaced (ChartMetrics' verdict reads "Out of sequence (? = uncertain)" off them; the panel does not)
        auto one = std::make_shared<SessionSnapshot>(*m_snap);
        one->revision = 30;
        auto c = std::make_shared<ShotSeries>(*one->shots[7]);
        QVariantMap ks = c->kinematicSequence;
        QVariantList nodes = ks.value(QStringLiteral("nodes")).toList();
        for (QVariant &nv : nodes) {
            QVariantMap nm = nv.toMap();
            if (nm.value(QStringLiteral("segment")).toString() == QLatin1String("pelvis")) nm.insert(QStringLiteral("placed"), false);
            nv = nm;
        }
        ks.insert(QStringLiteral("nodes"), nodes);
        ks.insert(QStringLiteral("order"), QVariantList{ QStringLiteral("leadArm"), QStringLiteral("club") });
        c->kinematicSequence = ks;
        one->shots[7] = c;
        ChartsTracesPresenter p;
        setUp(p, one);
        const QVariantMap s = p.thisShot();
        QCOMPARE(text(s.value(QStringLiteral("head"))), QStringLiteral("Lead arm then club, in order so far; pelvis and chest not placed"));
        QVERIFY(text(s.value(QStringLiteral("note"))).isEmpty());          // the pelvis is not said late
        // the sequence fold agrees with the head: ChartMetrics' "Out of sequence (? = uncertain)" read only against
        // the unplaced peaks' guesses (Mark, 17 Oct 2026)
        QCOMPARE(s.value(QStringLiteral("sequence")).toMap().value(QStringLiteral("verdictText")).toString(), QStringLiteral("In order so far"));
    }

    void lmOnlyPresetByData()
    {
        // LM-only: the picked shot carries only lm.* readings; other shots still carry a clubhead
        // speed curve, so "Club delivery" exists in the session but not on this shot
        auto lm = std::make_shared<SessionSnapshot>(*m_snap);
        lm->revision = 40;
        auto c = std::make_shared<ShotSeries>(*lm->shots[7]);
        c->metrics.clear();
        c->metricOrder.clear();
        const auto reading = [&](const char *key, const char *label, const char *unit, double v) {
            MetricSeries m;
            m.key = QString::fromLatin1(key); m.label = QString::fromUtf8(label); m.unit = QString::fromUtf8(unit);
            m.phaseSamples.push_back(PhaseSample{ phase::Impact, c->impactUs, v, kNaN, QString() });
            c->metrics.insert(m.key, m);
            c->metricOrder << m.key;
        };
        reading("lm.ballSpeed", "Ball speed (measured)", "mph", 112.3);
        reading("lm.launchAngle", "Launch angle (measured)", "°", 18.2);
        c->kinematicSequence.clear();
        lm->shots[7] = c;
        ChartsTracesPresenter p;
        setUp(p, lm);
        p.setRegisterName(QStringLiteral("replay"));
        QCOMPARE(p.choices().value(QStringLiteral("preset")).toString(), QStringLiteral("Ball flight"));
        QCOMPARE(text(p.thisShot().value(QStringLiteral("head"))), nb(QStringLiteral("Ball speed 112~mph, launch angle 18°")));
        QCOMPARE(p.thisShot().value(QStringLiteral("figures")).toList().size(), 1);
        // a saved choice the shot has no data for does not win; one it has data for does
        p.setPreset(QStringLiteral("Club delivery"));
        QCOMPARE(p.choices().value(QStringLiteral("preset")).toString(), QStringLiteral("Ball flight"));
        ChartsTracesPresenter q;
        setUp(q);
        q.setRegisterName(QStringLiteral("replay"));
        q.setPreset(QStringLiteral("Kinematic sequence"));
        QCOMPARE(q.choices().value(QStringLiteral("preset")).toString(), QStringLiteral("Kinematic sequence"));
    }

    void axisCaptionFollowsTheRoutes()
    {
        ChartsTracesPresenter full;
        setUp(full);
        QVERIFY(full.session().value(QStringLiteral("axisCaption")).toString().contains(QStringLiteral("P-positions")));
        auto bare = std::make_shared<SessionSnapshot>(*m_snap);
        bare->revision = 50;
        for (auto &sh : bare->shots) {
            auto c = std::make_shared<ShotSeries>(*sh);
            std::vector<PhaseMark> keep;
            for (const PhaseMark &m : c->phases) if (m.phase == phase::Impact) keep.push_back(m);
            c->phases = keep;
            sh = c;
        }
        ChartsTracesPresenter p;
        setUp(p, bare);
        const QString cap = p.session().value(QStringLiteral("axisCaption")).toString();
        QVERIFY2(cap.contains(QStringLiteral("landmarks")) && !cap.contains(QStringLiteral("P-positions")), qPrintable(cap));
        // a mix is named with its counts: strip the P's of three shots only
        auto some = std::make_shared<SessionSnapshot>(*m_snap);
        some->revision = 51;
        for (int i : { 0, 1, 2 }) some->shots[size_t(i)] = bare->shots[size_t(i)];
        ChartsTracesPresenter q;
        setUp(q, some);
        const QString mix = q.session().value(QStringLiteral("axisCaption")).toString();
        QVERIFY2(mix.contains(QStringLiteral("12 on P-positions")) && mix.contains(QStringLiteral("3 ")), qPrintable(mix));
        report("    axis captions: \"%s\" / \"%s\"\n", qPrintable(cap), qPrintable(mix));
    }

    void liveStateSessions()
    {
        const QString root = QStringLiteral("/mnt/swingdata/scratch/charts-live/");
        if (!QFileInfo(root).isDir()) QSKIP("charts-live states not reachable (is /mnt/swingdata mounted?)");
        for (const QString &state : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QStringList ses = QDir(root + state).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (ses.isEmpty()) continue;
            SessionSeriesModel model;
            model.setSynchronous(true);
            model.activateSession(root + state + QLatin1Char('/') + ses.first());
            const auto snap = model.snapshot();
            if (!snap || snap->count() == 0) continue;
            for (const QString &reg : { QStringLiteral("analyse"), QStringLiteral("replay") }) {
                ChartsTracesPresenter p;
                p.setSynchronous(true);
                p.setModel(&model);
                p.setRegisterName(reg);
                p.setPickedSwingDir(snap->shots.back()->swingDir);
                const QVariantMap s = p.thisShot(), se = p.session();
                const QString head = text(s.value(QStringLiteral("head")));
                report("    %-7s %-8s n=%d preset \"%s\" head \"%s\" | figures %d | session \"%s\" %s | %s\n", qPrintable(state), qPrintable(reg),
                       snap->count(), qPrintable(p.choices().value(QStringLiteral("preset")).toString()), qPrintable(head),
                       int(s.value(QStringLiteral("figures")).toList().size()),
                       qPrintable(se.value(QStringLiteral("kicker")).toString()), qPrintable(text(se.value(QStringLiteral("why")))),
                       qPrintable(se.value(QStringLiteral("axisCaption")).toString()));
                QVERIFY(!head.isEmpty());
                QVERIFY2(head != p.choices().value(QStringLiteral("preset")).toString(), "the head is built from the data, not the preset's name");
                QVERIFY(!text(s.value(QStringLiteral("standfirst"))).contains(QStringLiteral(" of the one ")));
                QVERIFY(!text(s.value(QStringLiteral("standfirst"))).contains(QStringLiteral(" of the two ")));
                if (head.startsWith(QStringLiteral("Out of sequence")))
                    QVERIFY2(head.contains(QStringLiteral("peaked late")), "out of sequence is said only of a placed segment that peaked late");
                for (const QVariant &r : se.value(QStringLiteral("rows")).toList())
                    for (const QVariant &f : r.toMap().value(QStringLiteral("figures")).toList())
                        QVERIFY(!f.toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("shot 0")));
            }
        }
    }

    // ── asynchronous: one coalesced job for a burst of writes, delivered on this thread ─────
    void asynchronousDelivery()
    {
        ChartsTracesPresenter p;
        p.setSnapshot(m_snap);
        int results = 0;
        connect(&p, &ChartsTracesPresenter::resultsChanged, this, [&results] { ++results; });
        p.setPickedSwingDir(dirOf(8));
        p.setBand(QStringLiteral("every"));
        p.setAgainst(QStringLiteral("median"));
        QVERIFY(p.waitForIdle(30000));
        QVERIFY(results >= 1 && results <= 2);
        QCOMPARE(text(p.thisShot().value(QStringLiteral("head"))), QStringLiteral("Out of sequence: the pelvis peaked late"));
        QVERIFY(!p.busy());
    }

    // ── real data and the budgets (QSKIP without the share) ──────────────────────────────────
    void realSessions()
    {
        const QStringList dirs{ QStringLiteral("/mnt/swingdata/scratch/typeset-harness/slots/try/session/2026-07-04_Mark-Liversedge_Wrist_01"),
                                QStringLiteral("/mnt/swingdata/Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01") };
        int measured = 0;
        for (const QString &dir : dirs) {
            if (!QFileInfo(dir).isDir()) continue;
            SessionSeriesModel model;
            model.setSynchronous(true);
            model.activateSession(dir);
            const auto snap = model.snapshot();
            if (!snap || snap->count() < 3) continue;
            ++measured;
            const int n = snap->count();
            const QString picked = snap->shots[size_t(std::min(7, n - 1))]->swingDir;
            report("  %s: %d shots, picked %s\n", qPrintable(QFileInfo(dir).fileName()), n, qPrintable(QFileInfo(picked).fileName()));
            for (const QString &reg : { QStringLiteral("analyse"), QStringLiteral("replay") }) {
                ChartsTracesPresenter p;
                p.setSynchronous(true);
                p.setModel(&model);
                p.setRegisterName(reg);
                p.setPickedSwingDir(picked);
                QElapsedTimer t;
                t.start();
                const QVariantMap s = p.session();
                const double full = t.nsecsElapsed() / 1e6;
                const QVariantMap tm = p.lastTimings();
                QVERIFY(p.thisShot().value(QStringLiteral("available")).toBool());
                report("    [%s] full recompute %.1f ms (this shot %.1f, against %.1f, session %.1f); head \"%s\" / \"%s\"\n",
                            qPrintable(reg), full, tm.value(QStringLiteral("thisShotMs")).toDouble(), tm.value(QStringLiteral("referenceMs")).toDouble(),
                            tm.value(QStringLiteral("sessionMs")).toDouble(), qPrintable(text(p.thisShot().value(QStringLiteral("head")))),
                            qPrintable(text(s.value(QStringLiteral("head")))));
                report("      align: %s\n", qPrintable(text(s.value(QStringLiteral("alignNote")))));
                // choice changes on the cached session
                for (const auto &[label, apply] : std::vector<std::pair<const char *, std::function<void()>>>{
                         { "band every", [&] { p.setBand(QStringLiteral("every")); } },
                         { "band fit", [&] { p.setBand(QStringLiteral("fit")); } },
                         { "facet half", [&] { p.setFacet(QStringLiteral("half")); } },
                         { "facet shape", [&] { p.setFacet(QStringLiteral("shape")); } },
                         { "against median", [&] { p.setAgainst(QStringLiteral("median")); } },
                         { "against like", [&] { p.setAgainst(QStringLiteral("like")); } },
                         { "window swing", [&] { p.setWindow(QStringLiteral("swing")); } } }) {
                    apply();
                    t.restart();
                    p.session();
                    report("      %-15s %.1f ms (job %.1f)\n", label, t.nsecsElapsed() / 1e6, p.lastTimings().value(QStringLiteral("jobMs")).toDouble());
                }
                // GUI-thread calls: the playhead, the hover, a seek, a brush lookup
                const QVariantMap win = p.thisShot().value(QStringLiteral("window")).toMap();
                const qint64 a = win.value(QStringLiteral("startUs")).toLongLong(), b = win.value(QStringLiteral("endUs")).toLongLong();
                t.restart();
                const int frames = 2000;
                double worst = 0;
                for (int i = 0; i < frames; ++i) {
                    QElapsedTimer f;
                    f.start();
                    p.setPlayheadUs(a + (b - a) * i / frames);
                    worst = std::max(worst, f.nsecsElapsed() / 1e6);
                }
                const double perFrame = t.nsecsElapsed() / 1e6 / frames;
                t.restart();
                for (int i = 0; i < 200; ++i) p.hoverAt(-200.0 + i);
                const double hover = t.nsecsElapsed() / 1e6 / 200;
                t.restart();
                for (int i = 0; i < 200; ++i) p.seekUsFor(1 + i % n, -100.0 + i);
                const double seek = t.nsecsElapsed() / 1e6 / 200;
                t.restart();
                for (int i = 0; i < 200; ++i) p.shotAt(p.thisShot().value(QStringLiteral("rows")).toList().value(0).toMap().value(QStringLiteral("key")).toString(), -50.0, 100.0);
                const double brush = t.nsecsElapsed() / 1e6 / 200;
                t.restart();
                for (int i = 0; i < 50; ++i) { const QVariantMap x = p.session(); Q_UNUSED(x); }
                const double read = t.nsecsElapsed() / 1e6 / 50;
                report("      GUI thread: playhead %.3f ms/frame (worst %.3f), hoverAt %.3f, seekUsFor %.4f, shotAt+thisShot %.3f, session() read %.4f ms\n",
                            perFrame, worst, hover, seek, brush, read);
                // what a QML read of each map costs (QVariantMap → JS converts the whole tree): bind once
                {
                    QJSEngine js;
                    p.setBand(QStringLiteral("every"));
                    p.setFacet(QString());
                    const QVariantMap ses = p.session(), shot = p.thisShot(), ref = p.reference(), ch = p.choices();
                    double ms[4] = { 0, 0, 0, 0 };
                    const QVariantMap *maps[4] = { &ses, &shot, &ref, &ch };
                    for (int k = 0; k < 4; ++k) {
                        QElapsedTimer j;
                        j.start();
                        for (int i = 0; i < 5; ++i) { const QJSValue v = js.toScriptValue(*maps[k]); Q_UNUSED(v); }
                        ms[k] = j.nsecsElapsed() / 1e6 / 5;
                    }
                    report("      one QML read (QVariantMap → JS): session[every] %.2f ms, thisShot %.2f, reference %.2f, choices %.2f\n",
                           ms[0], ms[1], ms[2], ms[3]);
                }
                QVERIFY(worst < 16.0);
            }
        }
        if (!measured) QSKIP("no real session reachable (is /mnt/swingdata mounted?)");
    }
};

QTEST_GUILESS_MAIN(ChartsTracesPresenterTest)
#include "charts_traces_presenter_test.moc"
