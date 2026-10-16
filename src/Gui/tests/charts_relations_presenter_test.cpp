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

// The Charts panel's relations presenter (src/Gui/charts/charts_relations_presenter.h) on the trimmed
// 4 July fixture — the session variant B's mock was drawn from — read at the metric catalogue's card
// instants, and on the real scratch session through SessionSeriesModel (QSKIP when the share is not
// mounted). The mock's own sentences and counts are the expectations: shot 8's seven readings apart,
// clubhead speed's ledger with 4 of 67 clearing chance, shaft lean at P7 drifting at r −.88, the kinds
// with no structure (silhouette .03) and an 8 mph split of 6 and 9.

#include "charts/charts_relations_presenter.h"
#include "charts/charts_words.h"
#include "charts/session_relations.h"
#include "charts/session_series_model.h"
#include "charts_maths_fixture.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

using namespace pinpoint::charts;

namespace {

const QString kDataDir = QStringLiteral(CHARTS_MATHS_DATA);

QString dirOf(int ordinal) { return QStringLiteral("/fixture/2026-07-04/swing_%1").arg(ordinal, 4, 10, QLatin1Char('0')); }

std::shared_ptr<SessionSnapshot> withDirs(const SessionSnapshot &in)
{
    auto s = std::make_shared<SessionSnapshot>(in);
    s->revision = 7;
    for (auto &sp : s->shots) {
        auto c = std::make_shared<ShotSeries>(*sp);
        c->swingDir = dirOf(c->ordinal);
        sp = c;
    }
    return s;
}

QString text(const QVariant &sentence) { return sentence.toMap().value(QStringLiteral("text")).toString(); }

QVariantMap rowOf(const QVariantMap &thisShot, const QString &id)
{
    for (const QVariant &g : thisShot.value(QStringLiteral("groups")).toList())
        for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList())
            if (r.toMap().value(QStringLiteral("readingId")).toString() == id) return r.toMap();
    return {};
}

} // namespace

class ChartsRelationsPresenterTest : public QObject
{
    Q_OBJECT

    charts_test::Fixture m_fx;
    std::shared_ptr<SessionSnapshot> m_snap;
    QJsonObject m_g;
    struct Live { QVariantMap status, thisShot, relations, kinds; };
    QHash<QString, Live> m_live;

    std::unique_ptr<ChartsRelationsPresenter> make(std::shared_ptr<const SessionSnapshot> s, int picked,
                                                   const QString &reg = QStringLiteral("analyse"))
    {
        auto p = std::make_unique<ChartsRelationsPresenter>();
        p->setSynchronous(true);
        p->setRegisterMode(reg);
        p->setPickedSwingDir(picked > 0 ? dirOf(picked) : QString());
        p->setSnapshot(std::move(s));
        return p;
    }

private slots:
    void initTestCase()
    {
        m_fx = charts_test::loadFixture(kDataDir + QStringLiteral("/session_0704.json"));
        QVERIFY2(m_fx.ok, "the 4 July fixture did not load");
        m_snap = withDirs(m_fx.session);
        QFile f(kDataDir + QStringLiteral("/goldens_0704.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        m_g = QJsonDocument::fromJson(f.readAll()).object();
    }

    // ── This shot ──────────────────────────────────────────────────────────────────────────

    void shotEightIsSevenReadingsApart()
    {
        auto p = make(m_snap, 8);
        QVERIFY(p->ready());
        const QVariantMap ts = p->thisShot();
        QVERIFY(ts.value(QStringLiteral("available")).toBool());
        QCOMPARE(ts.value(QStringLiteral("apartCount")).toInt(), 7);
        QCOMPARE(text(ts.value(QStringLiteral("head"))), QStringLiteral("Seven readings apart from the session"));
        QStringList apart, want;
        for (const QVariant &r : ts.value(QStringLiteral("apart")).toList()) apart << r.toMap().value(QStringLiteral("readingId")).toString();
        for (const QJsonValue &v : m_g.value(QStringLiteral("apart")).toArray()) want << v.toString();
        QCOMPARE(apart, want);                                     // the mock's order, most extreme first
        const QString st = text(ts.value(QStringLiteral("standfirst")));
        QVERIFY2(st.contains(QStringLiteral("seven sit past the session's whiskers")), qPrintable(st));
        QVERIFY2(st.contains(QStringLiteral("the most secondary axis tilt at P4")), qPrintable(st));
        // D5: every reading, grouped by family
        int rows = 0;
        for (const QVariant &g : ts.value(QStringLiteral("groups")).toList()) rows += g.toMap().value(QStringLiteral("rows")).toList().size();
        QCOMPARE(rows, ts.value(QStringLiteral("total")).toInt());
        QCOMPARE(rows, 68);
        QCOMPARE(p->status().value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 8 of 15  ·  7 iron"));
    }

    void keyFiguresSayWhereTheNumberSits()
    {
        auto p = make(m_snap, 8, QStringLiteral("replay"));
        QCOMPARE(p->status().value(QStringLiteral("family")).toString(), QStringLiteral("Club delivery"));
        const QVariantMap kf = p->thisShot().value(QStringLiteral("keyFigures")).toMap();
        const QVariantList items = kf.value(QStringLiteral("items")).toList();
        QVERIFY(!items.isEmpty());
        QVariantMap chs;
        for (const QVariant &i : items) if (i.toMap().value(QStringLiteral("readingId")) == QStringLiteral("clubheadSpeed@5")) chs = i.toMap();
        QVERIFY(!chs.isEmpty());
        // the place, from the session itself
        const std::vector<SessionReading> rs = sessionReadings(*m_snap, [](const QString &) { return std::vector<int>{ 5 }; }, { QStringLiteral("clubheadSpeed") });
        const ReadingPlace pl = placeOf(rs.front(), 7);
        const QString want = words::figUnit(pl.value, QStringLiteral("mph")) + QStringLiteral(", median ") + words::fig(rs.front().box.med)
                             + (pl.rankHi == 3 ? QStringLiteral(", 3rd highest") : QString());
        QCOMPARE(text(chs.value(QStringLiteral("sentence"))), want);
        qInfo().noquote() << "key figure:" << text(chs.value(QStringLiteral("sentence")));
        QCOMPARE(chs.value(QStringLiteral("figure")).toString(), words::fig(pl.value));
        QCOMPARE(chs.value(QStringLiteral("unit")).toString(), QStringLiteral("mph"));
        // shot 14 is the third fastest: "85 mph, median 72, 3rd highest"
        auto q = make(m_snap, 14, QStringLiteral("replay"));
        for (const QVariant &i : q->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList())
            if (i.toMap().value(QStringLiteral("readingId")) == QStringLiteral("clubheadSpeed@5")) {
                QCOMPARE(text(i.toMap().value(QStringLiteral("sentence"))), words::figUnit(84.82, QStringLiteral("mph")) + QStringLiteral(", median 72, 3rd highest"));
                QCOMPARE(i.toMap().value(QStringLiteral("foot")).toString(), QStringLiteral("median 72  ·  3rd highest"));
            }
        // the lead reading's trace, on this shot's clock
        const QVariantMap lead = p->thisShot().value(QStringLiteral("lead")).toMap();
        QCOMPARE(lead.value(QStringLiteral("readingId")).toString(), QStringLiteral("clubheadSpeed@5"));
        const QVariantMap tr = lead.value(QStringLiteral("trace")).toMap();
        QVERIFY(tr.value(QStringLiteral("curve")).toMap().value(QStringLiteral("t")).toList().size() > 50);
        QVERIFY(!tr.value(QStringLiteral("instants")).toList().isEmpty());
    }

    void keyFiguresForTheSequenceArePeakTimes()
    {
        auto p = make(m_snap, 8);          // Analyse defaults to the Kinematic sequence
        const QVariantMap kf = p->thisShot().value(QStringLiteral("keyFigures")).toMap();
        QVERIFY(kf.value(QStringLiteral("sequence")).toBool());
        const QVariantList items = kf.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Pelvis peak"));
        // D4: on shot 8 the pelvis peaked late
        QVERIFY(items[0].toMap().value(QStringLiteral("warn")).toBool());
        QCOMPARE(items[0].toMap().value(QStringLiteral("warnWord")).toString(), QStringLiteral("late"));
        const QVariantMap sq = p->sequence();
        QVERIFY(text(sq.value(QStringLiteral("chainSentence"))).endsWith(QStringLiteral(", late")));
        QVERIFY2(text(sq.value(QStringLiteral("note"))).contains(QStringLiteral("cannot yet tell them apart")), qPrintable(text(sq.value(QStringLiteral("note")))));
        QVERIFY(text(sq.value(QStringLiteral("verdict"))).startsWith(QStringLiteral("Out of sequence: the pelvis peaked late")));
    }

    void wrappedToeLineIsMarked()
    {
        auto p = make(m_snap, 8);
        const QVariantMap row = rowOf(p->thisShot(), QStringLiteral("toeLineAngle@0"));
        QVERIFY(!row.isEmpty());
        QVERIFY(row.value(QStringLiteral("wrapped")).toBool());
        QCOMPARE(row.value(QStringLiteral("wrappedMark")).toString(), QStringLiteral("†"));
        QVERIFY(p->thisShot().value(QStringLiteral("wrappedNote")).toString().contains(QStringLiteral("±180°")));
        QVERIFY(p->thisShot().value(QStringLiteral("notes")).toString().contains(QStringLiteral("†")));
    }

    void openRowIsATraceOrARun()
    {
        auto p = make(m_snap, 8);
        p->setOpenId(QStringLiteral("clubheadSpeed@5"));
        QVariantMap o = p->openRow();
        QCOMPARE(o.value(QStringLiteral("opens")).toString(), QStringLiteral("trace"));
        const QVariantMap tr = o.value(QStringLiteral("trace")).toMap();
        bool own = false;
        for (const QVariant &i : tr.value(QStringLiteral("instants")).toList())
            if (i.toMap().value(QStringLiteral("own")).toBool()) { own = true; QCOMPARE(i.toMap().value(QStringLiteral("box")).toMap().value(QStringLiteral("n")).toInt(), 15); }
        QVERIFY(own);
        // a reading with no curve on this shot opens its run in shot order
        p->setOpenId(QStringLiteral("tempoRatio@5"));
        o = p->openRow();
        QCOMPARE(o.value(QStringLiteral("opens")).toString(), QStringLiteral("run"));
        QCOMPARE(o.value(QStringLiteral("run")).toMap().value(QStringLiteral("points")).toList().size(), 15);
        // seeking: the picked shot's own clock
        const qint64 impact = p->status().value(QStringLiteral("pickedImpactUs")).toLongLong();
        QCOMPARE(p->seekUsAtShotMs(-12.5), impact - 12500);
        QCOMPARE(p->seekUsFor(5), impact);
    }

    void firedComesFromTheConditionLink()
    {
        auto s = withDirs(m_fx.session);
        // the session-level link: over the top grades the hand-path loop at P4 (Top = 2)
        s->conditions[QStringLiteral("over_the_top")].metricKey = QStringLiteral("handPathLoop");
        s->conditions[QStringLiteral("over_the_top")].phase = 2;
        // shot 3's row names another reading as the one that drove it: drivenBy wins over the condition's
        auto c3 = std::make_shared<ShotSeries>(*s->shots[2]);
        c3->diagnostics.drivenBy.insert(QStringLiteral("over_the_top"), ShotDiagnostics::Reading{ QStringLiteral("m_x"), QStringLiteral("leadHandWidth"), 2 });
        s->shots[2] = c3;
        auto p = make(s, 8);
        const QVariantMap row = rowOf(p->thisShot(), QStringLiteral("handPathLoop@2"));
        QVERIFY(!row.isEmpty());
        const QVariantList fired = row.value(QStringLiteral("firedShots")).toList();
        int n = 0;
        for (int i = 0; i < 15; ++i) {
            const bool want = i != 2 && s->shots[size_t(i)]->diagnostics.fired.contains(QStringLiteral("over_the_top"));
            QCOMPARE(fired[i].toBool(), want);
            n += want;
        }
        QVERIFY(n > 3);
        QCOMPARE(row.value(QStringLiteral("fired")).toBool(), s->shots[7]->diagnostics.fired.contains(QStringLiteral("over_the_top")));
        if (row.value(QStringLiteral("fired")).toBool()) {
            QCOMPARE(row.value(QStringLiteral("conditions")).toList().value(0).toString(), QStringLiteral("Over the top"));
            QCOMPARE(row.value(QStringLiteral("firedWord")).toString(), QStringLiteral("out of its corridor"));
        }
        const QVariantMap other = rowOf(p->thisShot(), QStringLiteral("leadHandWidth@2"));
        QVERIFY(other.value(QStringLiteral("firedShots")).toList()[2].toBool());   // drivenBy put it here
        // with no link at all, nothing is fired (never a join by value)
        auto plain = make(m_snap, 8);
        for (const QVariant &g : plain->thisShot().value(QStringLiteral("groups")).toList())
            for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList())
                for (const QVariant &f : r.toMap().value(QStringLiteral("firedShots")).toList()) QVERIFY(!f.toBool());
    }

    void firedConditionsSayHowTheyRead()
    {
        // the shipped pack's links on 4 July (build/charts-build/condition_links.md): early extension grades
        // the PEAK of pelvis thrust over a window ending at P7 (extremum); returns steep grades the CHANGE in
        // shaft lie to P7 (delta). Both mark the key@P7 reading; the words say peak / change, not "at".
        auto s = withDirs(m_fx.session);
        ConditionInfo &ee = s->conditions[QStringLiteral("early_extension")];
        ee.metricKey = QStringLiteral("pelvisThrust"); ee.phase = 5; ee.reducer = 3;
        ConditionInfo &ls = s->conditions[QStringLiteral("lie_steep_at_impact")];
        ls.metricKey = QStringLiteral("shaftLie"); ls.phase = 5; ls.reducer = 1;
        int eeShot = 0, lsShot = 0;
        for (const auto &sh : s->shots) {
            if (!eeShot && sh->diagnostics.fired.contains(QStringLiteral("early_extension"))) eeShot = sh->ordinal;
            if (!lsShot && sh->diagnostics.fired.contains(QStringLiteral("lie_steep_at_impact"))) lsShot = sh->ordinal;
        }
        QVERIFY(eeShot && lsShot);
        auto p = make(s, eeShot);
        QVariantMap row = rowOf(p->thisShot(), QStringLiteral("pelvisThrust@5"));
        QVERIFY(row.value(QStringLiteral("fired")).toBool());
        QCOMPARE(row.value(QStringLiteral("conditions")).toList().value(0).toString(), QStringLiteral("Early extension, peak before P7"));
        QCOMPARE(row.value(QStringLiteral("firedWord")).toString(), QStringLiteral("the peak before P7 out of its corridor"));
        const QVariantMap fc = row.value(QStringLiteral("firedConditions")).toList().value(0).toMap();
        QCOMPARE(fc.value(QStringLiteral("reducer")).toInt(), 3);
        QCOMPARE(fc.value(QStringLiteral("instant")).toString(), QStringLiteral("peak before P7"));
        auto q = make(s, lsShot);
        row = rowOf(q->thisShot(), QStringLiteral("shaftLie@5"));
        QVERIFY(row.value(QStringLiteral("fired")).toBool());
        QCOMPARE(row.value(QStringLiteral("conditions")).toList().value(0).toString(),
                 s->conditions.value(QStringLiteral("lie_steep_at_impact")).name + QStringLiteral(", change to P7"));
        QCOMPARE(row.value(QStringLiteral("firedWord")).toString(), QStringLiteral("the change to P7 out of its corridor"));
        // an At link says "at"; a Rate link "rate of change to"
        ee.reducer = 0;
        QCOMPARE(rowOf(make(s, eeShot)->thisShot(), QStringLiteral("pelvisThrust@5")).value(QStringLiteral("conditions")).toList().value(0).toString(),
                 QStringLiteral("Early extension, at P7"));
        QCOMPARE(rowOf(make(s, eeShot)->thisShot(), QStringLiteral("pelvisThrust@5")).value(QStringLiteral("firedWord")).toString(), QStringLiteral("out of its corridor"));
        ee.reducer = 2;
        QCOMPARE(rowOf(make(s, eeShot)->thisShot(), QStringLiteral("pelvisThrust@5")).value(QStringLiteral("conditions")).toList().value(0).toString(),
                 QStringLiteral("Early extension, rate of change to P7"));
    }

    // ── Relations ──────────────────────────────────────────────────────────────────────────

    void clubheadSpeedLedgerAndTheChanceLine()
    {
        // The mock's ledger, exactly, at the mock's read-at instants (the fixture's table, extracted from the
        // manifest when the mocks were drawn) …
        {
            auto g = std::make_unique<ChartsRelationsPresenter>();
            g->setSynchronous(true);
            g->setReadAt([this](const QString &k) { return m_fx.readAtOf(k); });
            g->setPickedSwingDir(dirOf(8));
            g->setSnapshot(m_snap);
            const QVariantList ledger = g->relations().value(QStringLiteral("ledger")).toList();
            const QJsonArray want = m_g.value(QStringLiteral("corr")).toObject().value(QStringLiteral("rows")).toArray();
            QCOMPARE(ledger.size(), want.size());
            for (int i = 0; i < ledger.size(); ++i) {
                QCOMPARE(ledger[i].toMap().value(QStringLiteral("readingId")).toString(), want.at(i).toObject().value(QStringLiteral("id")).toString());
                QVERIFY(std::abs(ledger[i].toMap().value(QStringLiteral("r")).toDouble() - want.at(i).toObject().value(QStringLiteral("r")).toDouble()) < 1e-9);
            }
        }
        // … and the build's: the catalogue's card instants today.
        auto p = make(m_snap, 8);
        const QVariantMap rel = p->relations();
        QVERIFY(rel.value(QStringLiteral("available")).toBool());
        QCOMPARE(rel.value(QStringLiteral("upId")).toString(), QStringLiteral("clubheadSpeed@5"));
        const QVariantList ledger = rel.value(QStringLiteral("ledger")).toList();
        const QVariantMap ch = rel.value(QStringLiteral("chance")).toMap();
        QCOMPARE(ch.value(QStringLiteral("tests")).toInt(), 67);
        QCOMPARE(ch.value(QStringLiteral("clearCount")).toInt(), 4);
        QCOMPARE(ch.value(QStringLiteral("chanceCount")).toInt(), 3);
        QCOMPARE(text(ch.value(QStringLiteral("line"))), QStringLiteral("|r| under .51 is chance at n = 15"));
        QCOMPARE(text(ch.value(QStringLiteral("sentence"))),
                 QStringLiteral("4 of 67 readings clear chance; chance alone would clear about 3. ") + words::coOccurrenceNote());
        QVERIFY(text(rel.value(QStringLiteral("standfirst"))).endsWith(words::coOccurrenceNote()));
        QVERIFY(text(rel.value(QStringLiteral("ledgerNote"))).endsWith(words::coOccurrenceNote()));
        // the scatter: the strongest partner that is not the same quantity, across
        const QVariantMap sc = rel.value(QStringLiteral("scatter")).toMap();
        QCOMPARE(sc.value(QStringLiteral("points")).toList().size(), sc.value(QStringLiteral("n")).toInt());   // shots both were read on
        QVERIFY(sc.value(QStringLiteral("n")).toInt() >= 14);
        QCOMPARE(rel.value(QStringLiteral("acrossId")).toString(), sc.value(QStringLiteral("xId")).toString());
        QVERIFY(!sc.value(QStringLiteral("line")).isNull());
        QVERIFY(ledger[0].toMap().value(QStringLiteral("slopeText")).toString().contains(QString(QChar(0x00A0)) + QStringLiteral("mph per ")));
    }

    void sameQuantityIsLabelledNotRanked()
    {
        auto p = make(m_snap, 8);
        int expectRank = 0;
        bool sawSame = false;
        for (const QVariant &v : p->relations().value(QStringLiteral("ledger")).toList()) {
            const QVariantMap r = v.toMap();
            if (r.value(QStringLiteral("readingId")).toString() == QStringLiteral("clubAngularSpeed@5")) {
                sawSame = true;
                QVERIFY(r.value(QStringLiteral("same")).toBool());
                QCOMPARE(r.value(QStringLiteral("rank")).toInt(), 0);
                QCOMPARE(r.value(QStringLiteral("sameMark")).toString(), QStringLiteral("‡"));
                QVERIFY(!r.value(QStringLiteral("finding")).toBool());
                continue;
            }
            QVERIFY(!r.value(QStringLiteral("same")).toBool());
            QCOMPARE(r.value(QStringLiteral("rank")).toInt(), ++expectRank);
        }
        QVERIFY(sawSame);
        QVERIFY(p->relations().value(QStringLiteral("acrossId")).toString() != QStringLiteral("clubAngularSpeed@5"));
    }

    void shaftLeanDriftsThroughTheSession()
    {
        auto p = make(m_snap, 8);
        const QVariantMap dr = p->relations().value(QStringLiteral("drifts")).toMap();
        const QVariantList items = dr.value(QStringLiteral("items")).toList();
        QVERIFY(!items.isEmpty());
        const QVariantMap d = items[0].toMap();
        QCOMPARE(d.value(QStringLiteral("readingId")).toString(), QStringLiteral("impactShaftLean@5"));
        QVERIFY(std::abs(d.value(QStringLiteral("r")).toDouble() + 0.88) < 0.005);
        QCOMPARE(d.value(QStringLiteral("direction")).toString(), QStringLiteral("falling"));
        QCOMPARE(text(d.value(QStringLiteral("sentence"))), QStringLiteral("Shaft lean at P7, r −.88, falling through the session"));
        QVERIFY(!d.value(QStringLiteral("run")).toMap().value(QStringLiteral("drift")).toMap().value(QStringLiteral("line")).isNull());
        QVERIFY(text(dr.value(QStringLiteral("sentence"))).endsWith(words::coOccurrenceNote()));
    }

    void facetSplitsThePair()
    {
        auto p = make(m_snap, 8);
        const QVariantList facets = p->relations().value(QStringLiteral("facets")).toList();
        QVERIFY(facets.size() >= 2);
        const QVariantMap f0 = facets[0].toMap();
        QCOMPARE(f0.value(QStringLiteral("kind")).toString(), QStringLiteral("condition"));
        p->setFacetId(f0.value(QStringLiteral("id")).toString());
        const QVariantMap fc = p->relations().value(QStringLiteral("facet")).toMap();
        const QVariantList panels = fc.value(QStringLiteral("panels")).toList();
        QCOMPARE(panels.size(), 2);
        const int n0 = panels[0].toMap().value(QStringLiteral("n")).toInt(), n1 = panels[1].toMap().value(QStringLiteral("n")).toInt();
        QCOMPARE(n0 + n1, 15);
        QCOMPARE(n0, f0.value(QStringLiteral("n")).toInt());
        const QString s = text(fc.value(QStringLiteral("sentence")));
        QVERIFY2(s.contains(QStringLiteral(": fired on %1 / clean on %2.").arg(n0).arg(n1)), qPrintable(s));
        QVERIFY(s.endsWith(words::coOccurrenceNote()));
        p->setFacetId(QStringLiteral("half"));
        QCOMPARE(p->relations().value(QStringLiteral("facet")).toMap().value(QStringLiteral("panels")).toList().size(), 2);
        // the matrix is the Up reading's family, lower triangle
        const QVariantMap mx = p->relations().value(QStringLiteral("matrix")).toMap();
        const QVariantList cells = mx.value(QStringLiteral("cells")).toList();
        for (int i = 0; i < cells.size(); ++i) QCOMPARE(cells[i].toList().size(), i);
    }

    // ── Kinds ──────────────────────────────────────────────────────────────────────────────

    void kindsSayNoStructureThenTheSplit()
    {
        auto p = make(m_snap, 8);
        const QVariantMap k = p->kinds();
        QVERIFY(k.value(QStringLiteral("available")).toBool());
        QCOMPARE(k.value(QStringLiteral("method")).toString(), QStringLiteral("split"));
        QVERIFY(!k.value(QStringLiteral("structure")).toBool());
        QCOMPARE(k.value(QStringLiteral("silhouetteText")).toString(), QStringLiteral(".03"));
        const QString head = text(k.value(QStringLiteral("head")));
        QVERIFY2(head.startsWith(QStringLiteral("No structure")), qPrintable(head));
        const QString split = text(k.value(QStringLiteral("splitLine")));
        QVERIFY2(split.startsWith(QStringLiteral("An ") + words::figUnit(8.0, QStringLiteral("mph")) + QStringLiteral(" gap in clubhead speed")) && split.endsWith(QStringLiteral(": 6 faster, 9 slower")), qPrintable(split));
        const QString st = text(k.value(QStringLiteral("standfirst")));
        QVERIFY2(st.contains(QStringLiteral("do not fall into kinds (silhouette .03")), qPrintable(st));
        const QVariantList g = k.value(QStringLiteral("groups")).toList();
        QCOMPARE(g[0].toMap().value(QStringLiteral("n")).toInt(), 6);
        QCOMPARE(g[1].toMap().value(QStringLiteral("n")).toInt(), 9);
        QCOMPARE(k.value(QStringLiteral("pickedKind")).toString(), QStringLiteral("Faster"));
        // the kinds' curves: the split reading first, each kind fitted
        const QVariantList curves = k.value(QStringLiteral("curves")).toList();
        QVERIFY(!curves.isEmpty());
        QCOMPARE(curves[0].toMap().value(QStringLiteral("key")).toString(), QStringLiteral("clubheadSpeed"));
        const QVariantList fits = curves[0].toMap().value(QStringLiteral("fits")).toList();
        QCOMPARE(fits[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Faster, 6 shots"));
        QVERIFY(!fits[0].toMap().value(QStringLiteral("refused")).toBool());
        QCOMPARE(fits[0].toMap().value(QStringLiteral("cover")).toList().size(), 6);
        QVERIFY(!k.value(QStringLiteral("separation")).toMap().value(QStringLiteral("rows")).toList().isEmpty());
        qInfo().noquote() << "kinds:" << head << "|" << split << "| shapes:" << text(k.value(QStringLiteral("shapes")).toMap().value(QStringLiteral("sentence")));
    }

    void kindCurveWindowFollowsTheFiniteValues()
    {
        auto p = make(m_snap, 8);
        const QVariantList curves = p->kinds().value(QStringLiteral("curves")).toList();
        QVERIFY(!curves.isEmpty());
        for (const QVariant &cv : curves) {
            const QVariantMap c = cv.toMap();
            QVERIFY(!c.value(QStringLiteral("window")).isNull());
            const double t0 = c.value(QStringLiteral("window")).toMap().value(QStringLiteral("t0")).toDouble();
            const double t1 = c.value(QStringLiteral("window")).toMap().value(QStringLiteral("t1")).toDouble();
            double first = std::numeric_limits<double>::infinity(), last = -first;
            for (const QVariant &fv : c.value(QStringLiteral("fits")).toList()) {
                const QVariantList t = fv.toMap().value(QStringLiteral("t")).toList(), v = fv.toMap().value(QStringLiteral("v")).toList();
                QCOMPARE(t.size(), v.size());
                for (int i = 0; i < t.size(); ++i) {
                    QVERIFY(t[i].toDouble() >= t0 - 1e-9 && t[i].toDouble() <= t1 + 1e-9);     // trimmed to the window
                    if (!v[i].isNull()) { first = std::min(first, t[i].toDouble()); last = std::max(last, t[i].toDouble()); }
                }
            }
            QVERIFY(std::isfinite(last));
            // the margin: 5 % of the finite span (≥ 2 ms), or out to a P-position within 20 ms
            const double margin = std::max({ 2.0, 0.05 * (last - first), 20.0 }) + 1e-6;
            QVERIFY2(t1 >= last - 1e-9 && t1 <= last + margin, qPrintable(QStringLiteral("%1: t1 %2, last %3").arg(c.value(QStringLiteral("key")).toString()).arg(t1).arg(last)));
            QVERIFY2(t0 <= first + 1e-9 && t0 >= first - margin, qPrintable(QStringLiteral("%1: t0 %2, first %3").arg(c.value(QStringLiteral("key")).toString()).arg(t0).arg(first)));
            for (const QVariant &ph : c.value(QStringLiteral("phases")).toList())
                QVERIFY(ph.toMap().value(QStringLiteral("tMs")).toDouble() >= t0 - 1e-9 && ph.toMap().value(QStringLiteral("tMs")).toDouble() <= t1 + 1e-9);
            qInfo().noquote() << c.value(QStringLiteral("key")).toString() << "window" << t0 << "to" << t1 << "ms; fits finite" << first << "to" << last;
        }
        // no finite value anywhere: the refusals stand and there is no window
        auto s = withDirs(m_fx.session);
        const QVariantList g = p->kinds().value(QStringLiteral("groups")).toList();
        for (int k = 0; k < 2; ++k) {
            const QVariantList mem = g[k].toMap().value(QStringLiteral("members")).toList();
            for (int i = 2; i < mem.size(); ++i) {
                const int o = mem[i].toMap().value(QStringLiteral("ordinal")).toInt();
                auto c = std::make_shared<ShotSeries>(*s->shots[size_t(o - 1)]);
                MetricSeries &m = c->metrics[QStringLiteral("clubheadSpeed")];
                m.tUs.clear(); m.value.clear(); m.valid.clear();
                s->shots[size_t(o - 1)] = c;
            }
        }
        QVariantMap none;
        for (const QVariant &cv : make(s, 8)->kinds().value(QStringLiteral("curves")).toList())
            if (cv.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("clubheadSpeed")) none = cv.toMap();
        QVERIFY(!none.isEmpty());
        QVERIFY(none.value(QStringLiteral("window")).isNull());
        QVERIFY(none.value(QStringLiteral("phases")).toList().isEmpty());
        for (const QVariant &fv : none.value(QStringLiteral("fits")).toList()) {
            QVERIFY(fv.toMap().value(QStringLiteral("refused")).toBool());
            QVERIFY(!fv.toMap().value(QStringLiteral("refusal")).toString().isEmpty());
        }
    }

    void kindCurveRefusedForUnderThreeShots()
    {
        auto p0 = make(m_snap, 8);
        QVariantList a = p0->kinds().value(QStringLiteral("groups")).toList()[0].toMap().value(QStringLiteral("members")).toList();
        QCOMPARE(a.size(), 6);
        // strip the clubhead-speed CURVE (not its readings) from four of the six faster shots
        auto s = withDirs(m_fx.session);
        for (int k = 0; k < 4; ++k) {
            const int o = a[k].toMap().value(QStringLiteral("ordinal")).toInt();
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(o - 1)]);
            MetricSeries &m = c->metrics[QStringLiteral("clubheadSpeed")];
            m.tUs.clear(); m.value.clear(); m.valid.clear();
            s->shots[size_t(o - 1)] = c;
        }
        auto p = make(s, 8);
        QVariantMap curve;
        for (const QVariant &c : p->kinds().value(QStringLiteral("curves")).toList())
            if (c.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("clubheadSpeed")) curve = c.toMap();
        QVERIFY(!curve.isEmpty());
        const QVariantList fits = curve.value(QStringLiteral("fits")).toList();
        QVERIFY(fits[0].toMap().value(QStringLiteral("refused")).toBool());
        QCOMPARE(fits[0].toMap().value(QStringLiteral("refusal")).toString(),
                 QStringLiteral("Three shots are needed for a kind's curve; two of the six faster shots have this curve."));
        QVERIFY(fits[0].toMap().value(QStringLiteral("t")).toList().isEmpty());
        QVERIFY(!fits[1].toMap().value(QStringLiteral("refused")).toBool());
    }

    void kindsListTheShotsTheyCouldNotPlace()
    {
        // clubhead speed at P7 (the split reading) unmeasured on two slower shots: they sit in neither kind,
        // and the head, the standfirst and a third neutral member set say so
        auto s = withDirs(m_fx.session);
        for (int o : { 1, 4 }) {
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(o - 1)]);
            auto &ps = c->metrics[QStringLiteral("clubheadSpeed")].phaseSamples;
            ps.erase(std::remove_if(ps.begin(), ps.end(), [](const PhaseSample &x) { return x.phase == 5; }), ps.end());
            s->shots[size_t(o - 1)] = c;
        }
        auto p = make(s, 4);
        const QVariantMap k = p->kinds();
        QCOMPARE(k.value(QStringLiteral("method")).toString(), QStringLiteral("split"));
        const QVariantList g = k.value(QStringLiteral("groups")).toList();
        QCOMPARE(g.size(), 3);
        int total = 0;
        for (const QVariant &v : g) total += v.toMap().value(QStringLiteral("n")).toInt();
        QCOMPARE(total, 15);                                       // no shot silently disappears
        const QVariantMap nm = g[2].toMap();
        QVERIFY(nm.value(QStringLiteral("neutral")).toBool());
        QCOMPARE(nm.value(QStringLiteral("name")).toString(), QStringLiteral("Not measured"));
        QCOMPARE(nm.value(QStringLiteral("text")).toString(), QStringLiteral("not measured: shots 1 and 4"));
        QCOMPARE(k.value(QStringLiteral("unplacedCount")).toInt(), 2);
        const QString head = text(k.value(QStringLiteral("head")));
        QVERIFY2(head.endsWith(QStringLiteral("; two not measured")), qPrintable(head));
        const QString st = text(k.value(QStringLiteral("standfirst")));
        QVERIFY2(st.contains(QStringLiteral("Two shots have no clubhead speed at P7 and sit in neither: shots 1 and 4.")), qPrintable(st));
        QVERIFY2(st.contains(QStringLiteral("Shot 4 has no clubhead speed at P7, so it is in neither.")), qPrintable(st));
        QCOMPARE(k.value(QStringLiteral("pickedKind")).toString(), QString());
        qInfo().noquote() << "kinds with unmeasured shots:" << head;
        // the full 4 July session places every shot: no third set
        QCOMPARE(make(m_snap, 8)->kinds().value(QStringLiteral("groups")).toList().size(), 2);
    }

    // ── Replay Session follows the playhead (D6) ──────────────────────────────────────────

    void replaySessionFollowsThePlayhead()
    {
        auto p = make(m_snap, 8, QStringLiteral("replay"));
        QSignalSpy spy(p.get(), &ChartsRelationsPresenter::playheadChanged);
        const ShotSeries &s8 = *m_snap->shots[7];
        qint64 top = -1, impact = s8.impactUs, addr = -1;
        for (const PhaseMark &m : s8.phases) { if (m.phase == 2) top = m.tUs; if (m.phase == 0) addr = m.tUs; }
        QVERIFY(top > 0 && impact > top);
        p->setPlayheadUs(addr - 100000);            // before every P-position: the first
        const QString first = p->replaySession().value(QStringLiteral("tag")).toString();
        QCOMPARE(p->replaySession().value(QStringLiteral("key")).toString(), QStringLiteral("clubheadSpeed"));
        p->setPlayheadUs(top + 2000);
        QCOMPARE(p->replaySession().value(QStringLiteral("tag")).toString(), QStringLiteral("P4"));
        QCOMPARE(p->replaySession().value(QStringLiteral("readingId")).toString(), QStringLiteral("clubheadSpeed@2"));
        const int afterTop = spy.count();
        p->setPlayheadUs(top + 3000);               // same P-position, value unchanged: no signal unless the readout moved
        p->setPlayheadUs(impact);
        QCOMPARE(p->replaySession().value(QStringLiteral("tag")).toString(), QStringLiteral("P7"));
        QVERIFY(spy.count() > afterTop);
        const QVariantMap rs = p->replaySession();
        QCOMPARE(text(rs.value(QStringLiteral("head"))), QStringLiteral("Clubhead speed at P7, impact, every shot"));
        QCOMPARE(rs.value(QStringLiteral("movesWith")).toList().size(), 3);
        QVERIFY(text(rs.value(QStringLiteral("note"))).endsWith(words::coOccurrenceNote()));
        QVERIFY(text(rs.value(QStringLiteral("now")).toMap().value(QStringLiteral("sentence"))).startsWith(QStringLiteral("This shot ")));
        bool cur = false;
        for (const QVariant &ph : rs.value(QStringLiteral("phases")).toList())
            if (ph.toMap().value(QStringLiteral("current")).toBool()) { cur = true; QCOMPARE(ph.toMap().value(QStringLiteral("seekUs")).toLongLong(), impact); }
        QVERIFY(cur);
        QVERIFY(first != QStringLiteral("P7"));
        // the lead's value at the playhead, as text (clubhead speed is bridged after impact: read it before)
        p->setPlayheadUs(impact - 30000);
        QVERIFY(p->playhead().value(QStringLiteral("leadValueText")).toString().endsWith(QStringLiteral("mph")));
    }

    void portGapsAreFilled()
    {
        auto p = make(m_snap, 8, QStringLiteral("replay"));
        const QChar nb(0x00A0);
        // 1. every shot, for an ordinal → carousel request
        const QVariantList shots = p->status().value(QStringLiteral("shots")).toList();
        QCOMPARE(shots.size(), 15);
        QCOMPARE(shots[7].toMap().value(QStringLiteral("ordinal")).toInt(), 8);
        QCOMPARE(shots[7].toMap().value(QStringLiteral("swingDir")).toString(), dirOf(8));
        QCOMPARE(shots[7].toMap().value(QStringLiteral("shotId")).toInt(), 8);
        QCOMPARE(shots[7].toMap().value(QStringLiteral("club")).toString(), QStringLiteral("7 IRON"));
        // 5. the profile's column heads
        QCOMPARE(p->status().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("Shot 8"));
        QCOMPARE(p->status().value(QStringLiteral("countLabel")).toString(), QStringLiteral("15 shots, low to high"));
        QCOMPARE(p->status().value(QStringLiteral("countLabelShort")).toString(), QStringLiteral("15 shots"));
        // 2. each kind's curve carries its ±sd hairlines and the scale that holds them
        const QVariantList curves = p->kinds().value(QStringLiteral("curves")).toList();
        QVERIFY(!curves.isEmpty());
        for (const QVariant &cv : curves) {
            const QVariantMap c = cv.toMap();
            const double ylo = c.value(QStringLiteral("yRange")).toMap().value(QStringLiteral("lo")).toDouble();
            const double yhi = c.value(QStringLiteral("yRange")).toMap().value(QStringLiteral("hi")).toDouble();
            QVERIFY(yhi > ylo);
            for (const QVariant &fv : c.value(QStringLiteral("fits")).toList()) {
                const QVariantMap f = fv.toMap();
                if (f.value(QStringLiteral("refused")).toBool()) continue;
                const QVariantList v = f.value(QStringLiteral("v")).toList(), sd = f.value(QStringLiteral("sd")).toList();
                const QVariantList lo = f.value(QStringLiteral("lo")).toList(), hi = f.value(QStringLiteral("hi")).toList();
                QCOMPARE(lo.size(), v.size());
                QCOMPARE(hi.size(), v.size());
                int drawn = 0;
                for (int i = 0; i < v.size(); ++i) {
                    if (v[i].isNull() || sd[i].isNull()) { QVERIFY(lo[i].isNull()); continue; }
                    ++drawn;
                    QCOMPARE(lo[i].toDouble(), v[i].toDouble() - sd[i].toDouble());
                    QCOMPARE(hi[i].toDouble(), v[i].toDouble() + sd[i].toDouble());
                    QVERIFY(lo[i].toDouble() >= ylo && hi[i].toDouble() <= yhi);
                }
                QVERIFY(drawn > 0);
            }
        }
        // 3. the ledger's sparkline is scaled from data
        const QVariantMap rel = p->relations();
        for (const QVariant &rv : rel.value(QStringLiteral("ledger")).toList()) {
            const QVariantMap pair = rv.toMap().value(QStringLiteral("pair")).toMap();
            const double xlo = pair.value(QStringLiteral("xRange")).toMap().value(QStringLiteral("lo")).toDouble();
            const double xhi = pair.value(QStringLiteral("xRange")).toMap().value(QStringLiteral("hi")).toDouble();
            const double ylo = pair.value(QStringLiteral("yRange")).toMap().value(QStringLiteral("lo")).toDouble();
            const double yhi = pair.value(QStringLiteral("yRange")).toMap().value(QStringLiteral("hi")).toDouble();
            for (const QVariant &x : pair.value(QStringLiteral("xs")).toList()) if (!x.isNull()) QVERIFY(x.toDouble() >= xlo && x.toDouble() <= xhi);
            for (const QVariant &y : pair.value(QStringLiteral("ys")).toList()) if (!y.isNull()) QVERIFY(y.toDouble() >= ylo && y.toDouble() <= yhi);
        }
        // 4. metric keys for hue, never split out of an id
        for (const QVariant &i : p->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList())
            QCOMPARE(i.toMap().value(QStringLiteral("key")).toString(), i.toMap().value(QStringLiteral("readingId")).toString().section(QLatin1Char('@'), 0, 0));
        for (const QVariant &r : p->kinds().value(QStringLiteral("separation")).toMap().value(QStringLiteral("rows")).toList())
            QCOMPARE(r.toMap().value(QStringLiteral("key")).toString(), r.toMap().value(QStringLiteral("readingId")).toString().section(QLatin1Char('@'), 0, 0));
        const QVariantMap sc = rel.value(QStringLiteral("scatter")).toMap();
        QCOMPARE(sc.value(QStringLiteral("yKey")).toString(), QStringLiteral("clubheadSpeed"));
        QCOMPARE(sc.value(QStringLiteral("xKey")).toString(), sc.value(QStringLiteral("xId")).toString().section(QLatin1Char('@'), 0, 0));
        p->setFacetId(QStringLiteral("half"));
        for (const QVariant &pl : p->relations().value(QStringLiteral("facet")).toMap().value(QStringLiteral("panels")).toList())
            QCOMPARE(pl.toMap().value(QStringLiteral("scatter")).toMap().value(QStringLiteral("yKey")).toString(), QStringLiteral("clubheadSpeed"));
        p->setPlayheadUs(m_snap->shots[7]->impactUs);
        QCOMPARE(p->replaySession().value(QStringLiteral("partner")).toMap().value(QStringLiteral("scatter")).toMap().value(QStringLiteral("yKey")).toString(),
                 QStringLiteral("clubheadSpeed"));
        // 6. a number meets its unit across U+00A0 in the joins made here
        QVERIFY(text(rel.value(QStringLiteral("standfirst"))).contains(QStringLiteral("95") + nb + QStringLiteral("%")));
        QVERIFY(text(rel.value(QStringLiteral("ledgerNote"))).contains(QStringLiteral("95") + nb + QStringLiteral("%")));
        const QString slope = rel.value(QStringLiteral("ledger")).toList()[0].toMap().value(QStringLiteral("slopeText")).toString();
        QVERIFY2(slope.contains(nb + QStringLiteral("mph per ")) && !slope.contains(QStringLiteral(" mph")), qPrintable(slope));
        // the sequence's bare figures drop the unit whichever space charts_words.h joins it with
        for (const QVariant &r : p->sequence().value(QStringLiteral("rows")).toList())
            QVERIFY(!r.toMap().value(QStringLiteral("valueText")).toString().contains(QStringLiteral("ms")));
    }

    void ticksAreNice()
    {
        ChartsRelationsPresenter p;
        QVariantList t = p.ticks(63.0, 86.2, 4, QStringLiteral("mph"));
        QStringList got;
        for (const QVariant &v : t) got << v.toMap().value(QStringLiteral("text")).toString();
        QCOMPARE(got, (QStringList{ QStringLiteral("65"), QStringLiteral("70"), QStringLiteral("75"), QStringLiteral("80"), QStringLiteral("85") }));
        t = p.ticks(-1.4, 0.6, 4, QStringLiteral("in"));
        QCOMPARE(t.first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("\u22121.0"));
        QVERIFY(p.ticks(kNaN, 1, 4).isEmpty());
    }

    // ── Edge states (STEP2A) ──────────────────────────────────────────────────────────────

    void edgeNoSessionNoPick()
    {
        auto p = make(std::make_shared<SessionSnapshot>(), 0);
        QVERIFY(p->ready());
        QCOMPARE(p->status().value(QStringLiteral("shotCount")).toInt(), 0);
        QVERIFY(!p->thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(!p->relations().value(QStringLiteral("available")).toBool());
        QVERIFY(!p->kinds().value(QStringLiteral("available")).toBool());
        QVERIFY(p->replaySession().isEmpty());
        QCOMPARE(p->seekUsFor(5), qint64(-1));
        // no session is said as that, never as "N shots are needed; this session has none"
        const QString none = QStringLiteral("No session is loaded.");
        QCOMPARE(text(p->status().value(QStringLiteral("refusal"))), none);
        QCOMPARE(text(p->relations().value(QStringLiteral("refusal"))), none);
        QCOMPARE(text(p->kinds().value(QStringLiteral("refusal"))), none);
        QCOMPARE(text(p->kinds().value(QStringLiteral("head"))), none);
        QCOMPARE(text(p->thisShot().value(QStringLiteral("head"))), none);
        auto emptyDir = std::make_shared<SessionSnapshot>();
        emptyDir->sessionDir = QStringLiteral("/fixture/empty");
        auto e = make(emptyDir, 0);
        QCOMPARE(text(e->relations().value(QStringLiteral("refusal"))), QStringLiteral("No shot in this session has been analysed yet."));
        QCOMPARE(text(e->kinds().value(QStringLiteral("refusal"))), QStringLiteral("No shot in this session has been analysed yet."));
        auto q = make(m_snap, 0);                   // a session, nothing picked
        QVERIFY(!q->thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(q->relations().value(QStringLiteral("available")).toBool());
    }

    void edgeTwoShots()
    {
        auto s = withDirs(m_fx.session);
        s->shots.resize(2);
        auto p = make(s, 2);
        QVERIFY(p->status().value(QStringLiteral("refused")).toBool());
        QCOMPARE(text(p->status().value(QStringLiteral("refusal"))), QStringLiteral("Three shots are needed to draw the session; this session has two."));
        QCOMPARE(text(p->thisShot().value(QStringLiteral("head"))), QStringLiteral("Three shots are needed to set a shot against its session; this session has two."));
        QVERIFY(!p->relations().value(QStringLiteral("available")).toBool());
        QCOMPARE(text(p->kinds().value(QStringLiteral("head"))), QStringLiteral("Six shots are needed to look for kinds of swing; this session has two."));
        QCOMPARE(text(p->kinds().value(QStringLiteral("refusal"))), QStringLiteral("Six shots are needed to look for kinds of swing; this session has two."));
        QCOMPARE(text(p->relations().value(QStringLiteral("refusal"))), QStringLiteral("Five shots are needed to relate readings across a session; this session has two."));
        // this shot's own figures are still published: the value, no median or place, and why
        p->setRegisterMode(QStringLiteral("replay"));
        const QVariantList kf = p->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList();
        QVERIFY(!kf.isEmpty());
        QCOMPARE(kf[0].toMap().value(QStringLiteral("key")).toString(), QStringLiteral("clubheadSpeed"));
        QCOMPARE(text(kf[0].toMap().value(QStringLiteral("sentence"))), words::figUnit(71.26, QStringLiteral("mph")));
        QCOMPARE(kf[0].toMap().value(QStringLiteral("foot")).toString(), QStringLiteral("Two shots: too few to place it"));
        QVERIFY(kf[0].toMap().value(QStringLiteral("place")).toString().isEmpty());
        QCOMPARE(p->thisShot().value(QStringLiteral("lead")).toMap().value(QStringLiteral("readingId")).toString(), QStringLiteral("clubheadSpeed@5"));
        QVERIFY(!p->thisShot().value(QStringLiteral("lead")).toMap().value(QStringLiteral("trace")).isNull());
        // one shot: "the session's only shot"
        auto one = withDirs(m_fx.session);
        one->shots.resize(1);
        auto q = make(one, 1, QStringLiteral("replay"));
        QCOMPARE(q->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList().value(0).toMap().value(QStringLiteral("foot")).toString(),
                 QStringLiteral("the session's only shot"));
        // four shots: placed against the session, but too few to relate
        auto four = withDirs(m_fx.session);
        four->shots.resize(4);
        auto f = make(four, 2, QStringLiteral("replay"));
        QVERIFY(f->thisShot().value(QStringLiteral("available")).toBool());
        QVERIFY(text(f->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList().value(0).toMap().value(QStringLiteral("sentence"))).contains(QStringLiteral(", median ")));
        QCOMPARE(text(f->relations().value(QStringLiteral("refusal"))), QStringLiteral("Five shots are needed to relate readings across a session; this session has four."));
    }

    void kindsThatAreTheClubsAreSaidToBe()
    {
        // label the six faster shots DRIVER: the 8 mph split is then the club, not a kind of swing
        auto p0 = make(m_snap, 8);
        const QVariantList faster = p0->kinds().value(QStringLiteral("groups")).toList()[0].toMap().value(QStringLiteral("members")).toList();
        QCOMPARE(faster.size(), 6);
        auto s = withDirs(m_fx.session);
        for (const QVariant &mm : faster) {
            const int o = mm.toMap().value(QStringLiteral("ordinal")).toInt();
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(o - 1)]);
            c->club = QStringLiteral("DRIVER");
            s->shots[size_t(o - 1)] = c;
        }
        auto p = make(s, 8);
        QVariantMap k = p->kinds();
        QCOMPARE(k.value(QStringLiteral("method")).toString(), QStringLiteral("clubs"));
        QVERIFY(!k.value(QStringLiteral("structure")).toBool());
        QCOMPARE(text(k.value(QStringLiteral("head"))), QStringLiteral("The two kinds are the two clubs: driver and 7 iron"));
        QCOMPARE(k.value(QStringLiteral("kicker")).toString(), QStringLiteral("Kinds of swing  ·  fifteen shots  ·  mixed clubs"));
        QCOMPARE(p->status().value(QStringLiteral("club")).toString(), QStringLiteral("mixed clubs"));
        const QString st = text(k.value(QStringLiteral("standfirst")));
        QVERIFY2(st.contains(QStringLiteral("15 of the 15 grouped shots sit with their own club")) && st.contains(QStringLiteral("Kinds across clubs are not meaningful.")), qPrintable(st));
        // within the largest club: the nine 7 irons alone
        QVERIFY2(st.contains(QStringLiteral("Within the nine 7 iron shots alone: ")), qPrintable(st));
        const QVariantMap within = k.value(QStringLiteral("withinClub")).toMap();
        QCOMPARE(within.value(QStringLiteral("club")).toString(), QStringLiteral("7 iron"));
        QCOMPARE(within.value(QStringLiteral("n")).toInt(), 9);
        const QVariantMap inner = within.value(QStringLiteral("kinds")).toMap();
        QVERIFY(inner.value(QStringLiteral("available")).toBool());
        int innerN = 0;
        for (const QVariant &g : inner.value(QStringLiteral("groups")).toList())
            for (const QVariant &mm : g.toMap().value(QStringLiteral("members")).toList()) {
                ++innerN;
                QCOMPARE(s->shots[size_t(mm.toMap().value(QStringLiteral("ordinal")).toInt() - 1)]->club, QStringLiteral("7 IRON"));   // the session's own ordinals
            }
        QCOMPARE(innerN, 9);
        // the groups shown ARE the clubs, and shot 8 (faster) is a driver
        const QVariantList g = k.value(QStringLiteral("groups")).toList();
        QCOMPARE(g.size(), 2);
        QCOMPARE(k.value(QStringLiteral("pickedKind")).toString(), QStringLiteral("driver"));
        // two shots relabelled against the split: 13 of 15 agree, "mostly"
        auto s2 = std::make_shared<SessionSnapshot>(*s);
        int flipped = 0;
        for (auto &sp : s2->shots) {
            if (flipped == 2 || sp->club != QStringLiteral("7 IRON")) continue;
            auto c = std::make_shared<ShotSeries>(*sp); c->club = QStringLiteral("DRIVER"); sp = c; ++flipped;
        }
        k = make(s2, 8)->kinds();
        QVERIFY2(text(k.value(QStringLiteral("head"))).startsWith(QStringLiteral("The two kinds are mostly the two clubs")), qPrintable(text(k.value(QStringLiteral("head")))));
        // clubs that do NOT line up with the kinds: kinds as before, and the mix is said
        auto s3 = withDirs(m_fx.session);
        for (int o : { 1, 2, 3 }) { auto c = std::make_shared<ShotSeries>(*s3->shots[size_t(o - 1)]); c->club = QStringLiteral("DRIVER"); s3->shots[size_t(o - 1)] = c; }
        k = make(s3, 8)->kinds();
        QCOMPARE(k.value(QStringLiteral("method")).toString(), QStringLiteral("split"));
        QVERIFY(text(k.value(QStringLiteral("standfirst"))).endsWith(QStringLiteral("they are not the clubs.")));
        QVERIFY(k.value(QStringLiteral("kicker")).toString().endsWith(QStringLiteral("mixed clubs")));
    }

    void familyDefaultsByData()
    {
        // shot 8 without any Club delivery reading: Replay falls to a family this shot measured
        auto p0 = make(m_snap, 8, QStringLiteral("replay"));
        QStringList keys;
        for (const QVariant &g : p0->thisShot().value(QStringLiteral("groups")).toList())
            if (g.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Club delivery"))
                for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList()) keys << r.toMap().value(QStringLiteral("key")).toString();
        QVERIFY(!keys.isEmpty());
        auto s = withDirs(m_fx.session);
        auto c = std::make_shared<ShotSeries>(*s->shots[7]);
        for (const QString &k : keys) { c->metrics.remove(k); c->metricOrder.removeAll(k); }
        s->shots[7] = c;
        auto p = make(s, 8, QStringLiteral("replay"));
        const QString fam = p->status().value(QStringLiteral("family")).toString();
        QVERIFY2(fam != QStringLiteral("Club delivery") && !fam.isEmpty(), qPrintable(fam));
        QVERIFY(!p->thisShot().value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList().isEmpty());
        QVERIFY(!p->thisShot().value(QStringLiteral("lead")).toMap().value(QStringLiteral("readingId")).toString().isEmpty());
        // the reader's own choice stands when the session has it
        p->setFamily(QStringLiteral("Club delivery"));
        QCOMPARE(p->status().value(QStringLiteral("family")).toString(), QStringLiteral("Club delivery"));
    }

    void edgePickedShotNotYetInTheSession()
    {
        // a live shot: the session has 14, the picked 15th arrives as its own analysisDetail
        auto s = withDirs(m_fx.session);
        const ShotSeries last = *s->shots.back();
        s->shots.pop_back();
        QVariantList series, phases;
        for (const QString &k : last.metricOrder) {
            const MetricSeries &m = last.metrics[k];
            QVariantList t, v, ps;
            for (qint64 x : m.tUs) t << x;
            for (double x : m.value) v << x;
            for (const PhaseSample &p : m.phaseSamples)
                ps << QVariantMap{ { QStringLiteral("phase"), p.phase }, { QStringLiteral("t_us"), p.tUs }, { QStringLiteral("value"), p.value } };
            series << QVariantMap{ { QStringLiteral("key"), k }, { QStringLiteral("label"), m.label }, { QStringLiteral("unit"), m.unit },
                                   { QStringLiteral("t_us"), t }, { QStringLiteral("value"), v }, { QStringLiteral("phaseSamples"), ps } };
        }
        for (const PhaseMark &m : last.phases) phases << QVariantMap{ { QStringLiteral("phase"), m.phase }, { QStringLiteral("t_us"), m.tUs }, { QStringLiteral("conf"), m.conf } };
        auto p = std::make_unique<ChartsRelationsPresenter>();
        p->setSynchronous(true);
        p->setPickedDetail({ { QStringLiteral("series"), series }, { QStringLiteral("phases"), phases }, { QStringLiteral("kinematicSequence"), last.kinematicSequence } });
        p->setPickedSwingDir(dirOf(15));
        p->setSnapshot(s);
        QVERIFY(p->status().value(QStringLiteral("pickedFromDetail")).toBool());
        QCOMPARE(p->status().value(QStringLiteral("shotCount")).toInt(), 15);
        QCOMPARE(p->status().value(QStringLiteral("pickedOrdinal")).toInt(), 15);
        QVERIFY(p->thisShot().value(QStringLiteral("available")).toBool());
        QCOMPARE(p->thisShot().value(QStringLiteral("total")).toInt(), 68);
        // once the session has it, the detail is no longer used
        p->setSnapshot(m_snap);
        QVERIFY(p->status().value(QStringLiteral("pickedInSession")).toBool());
        QVERIFY(!p->status().value(QStringLiteral("pickedFromDetail")).toBool());
    }

    void edgeMixedClubsAndMissingDevices()
    {
        auto s = withDirs(m_fx.session);
        for (int i = 10; i < 15; ++i) {
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(i)]);
            c->club = QStringLiteral("DRIVER");
            c->metrics.remove(QStringLiteral("clubheadSpeed"));    // no club track on these
            c->metricOrder.removeAll(QStringLiteral("clubheadSpeed"));
            s->shots[size_t(i)] = c;
        }
        auto p = make(s, 3);
        QVERIFY(p->status().value(QStringLiteral("clubsMixed")).toBool());
        QVERIFY(p->status().value(QStringLiteral("clubsSentence")).toString().startsWith(QStringLiteral("The clubs are mixed: ")));
        bool club = false;
        for (const QVariant &f : p->relations().value(QStringLiteral("facets")).toList()) club = club || f.toMap().value(QStringLiteral("id")) == QStringLiteral("club");
        QVERIFY(club);
        p->setFacetId(QStringLiteral("club"));
        QCOMPARE(p->relations().value(QStringLiteral("facet")).toMap().value(QStringLiteral("panels")).toList().size(), 2);
        // a key nobody measured is no row at all; one measured on some is a row with "-" for the others
        const QVariantMap row = rowOf(p->thisShot(), QStringLiteral("clubheadSpeed@5"));
        QCOMPARE(row.value(QStringLiteral("box")).toMap().value(QStringLiteral("n")).toInt(), 10);
        QVERIFY(row.value(QStringLiteral("values")).toList()[12].isNull());
        auto q = make(s, 13);
        QCOMPARE(rowOf(q->thisShot(), QStringLiteral("clubheadSpeed@5")).value(QStringLiteral("valueText")).toString(), QStringLiteral("-"));
        auto none = withDirs(m_fx.session);
        for (auto &sp : none->shots) { auto c = std::make_shared<ShotSeries>(*sp); c->metrics.remove(QStringLiteral("clubheadSpeed")); c->metricOrder.removeAll(QStringLiteral("clubheadSpeed")); sp = c; }
        QVERIFY(rowOf(make(none, 3)->thisShot(), QStringLiteral("clubheadSpeed@5")).isEmpty());
    }

    void edgeImpactOnlyShotsSayTheirRoute()
    {
        auto s = withDirs(m_fx.session);
        for (int i : { 1, 4 }) {
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(i)]);
            std::vector<PhaseMark> keep;
            for (const PhaseMark &m : c->phases) if (m.phase == 5) keep.push_back(m);
            c->phases = keep;
            s->shots[size_t(i)] = c;
        }
        auto p = make(s, 8);
        const QString note = text(p->kinds().value(QStringLiteral("alignNote")));
        QVERIFY2(note.contains(QStringLiteral("Only impact is known on shots 2 and 5")) || note.contains(QStringLiteral("only impact is known")), qPrintable(note));
    }

    // ── Real data, and the budget (report, not assert) ─────────────────────────────────────

    // The live-state renders' sessions (build/run-me/charts-build/review-live.pdf): each state's own copy.
    void liveStates_data()
    {
        QTest::addColumn<QString>("state");
        for (const char *st : { "full", "impact", "live", "lm", "mixed", "n1", "n2", "onecam" }) QTest::newRow(st) << QString::fromLatin1(st);
    }
    void liveStates()
    {
        QFETCH(QString, state);
        const QDir root(QStringLiteral("/mnt/swingdata/scratch/charts-live/") + state);
        if (!root.exists()) QSKIP("live-state sessions not reachable (is /mnt/swingdata mounted?)");
        const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY(!dirs.isEmpty());
        SessionSeriesModel model;
        model.setSynchronous(true);
        model.activateSession(root.filePath(dirs.first()));
        const auto snap = model.snapshot();
        ChartsRelationsPresenter p;
        p.setSynchronous(true);
        p.setRegisterMode(QStringLiteral("replay"));
        if (snap->count()) p.setPickedSwingDir(snap->shots[size_t(snap->count() / 2)]->swingDir);
        p.setModel(&model);
        const QVariantMap st = p.status(), ts = p.thisShot(), rel = p.relations(), k = p.kinds();
        const QVariantList kf = ts.value(QStringLiteral("keyFigures")).toMap().value(QStringLiteral("items")).toList();
        QStringList clubs;
        for (const QVariant &s : st.value(QStringLiteral("shots")).toList()) if (!clubs.contains(s.toMap().value(QStringLiteral("club")).toString())) clubs << s.toMap().value(QStringLiteral("club")).toString();
        qInfo().noquote() << state << ": n" << st.value(QStringLiteral("shotCount")).toInt() << "clubs" << clubs.join(QLatin1Char('/'))
                          << "| family" << st.value(QStringLiteral("family")).toString() << "| key figures" << kf.size()
                          << (kf.isEmpty() ? QString() : text(kf[0].toMap().value(QStringLiteral("sentence"))))
                          << "| lead" << ts.value(QStringLiteral("lead")).toMap().value(QStringLiteral("readingId")).toString()
                          << "| relations" << (rel.value(QStringLiteral("available")).toBool() ? QStringLiteral("yes") : text(rel.value(QStringLiteral("refusal"))))
                          << "| kinds" << text(k.value(QStringLiteral("head"))) << "|" << k.value(QStringLiteral("kicker")).toString();
        qInfo().noquote() << "    kinds:" << text(k.value(QStringLiteral("standfirst")));
        // what each state must say (review-live.pdf)
        if (state == QLatin1String("n1") || state == QLatin1String("n2")) {
            QVERIFY(!kf.isEmpty());                                         // the shot's own figures, at any n
            QVERIFY(!text(kf[0].toMap().value(QStringLiteral("sentence"))).contains(QStringLiteral("median")));
            QVERIFY(!ts.value(QStringLiteral("lead")).toMap().value(QStringLiteral("readingId")).toString().isEmpty());
            QVERIFY(!rel.value(QStringLiteral("available")).toBool());
        }
        if (state == QLatin1String("lm")) {
            QVERIFY(!kf.isEmpty());
            QVERIFY(!ts.value(QStringLiteral("lead")).toMap().value(QStringLiteral("readingId")).toString().isEmpty());
            QCOMPARE(k.value(QStringLiteral("method")).toString(), QStringLiteral("clubs"));
            QVERIFY(text(k.value(QStringLiteral("head"))).startsWith(QStringLiteral("The two kinds are")));
            QVERIFY(text(k.value(QStringLiteral("standfirst"))).contains(QStringLiteral("Kinds across clubs are not meaningful.")));
        }
        if (state == QLatin1String("lm") || state == QLatin1String("mixed")) {
            QVERIFY(k.value(QStringLiteral("kicker")).toString().endsWith(QStringLiteral("mixed clubs")));
            QCOMPARE(st.value(QStringLiteral("club")).toString(), QStringLiteral("mixed clubs"));
        }
        m_live.insert(state, { st, ts, rel, k });
    }

    void realScratchSession()
    {
        measureReal(QStringLiteral("/mnt/swingdata/scratch/typeset-harness/slots/try/session/2026-07-04_Mark-Liversedge_Wrist_01"), 8);
    }

    void realFiftyShotSession()
    {
        const QString dir = qEnvironmentVariable("PP_CHARTS_LARGE_SESSION",
                                                 QStringLiteral("/mnt/swingdata/Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01"));
        measureReal(dir, 25);
    }

private:
    void measureReal(const QString &dir, int pickOrdinal)
    {
        if (!QFileInfo(dir).isDir()) QSKIP("session not reachable (is /mnt/swingdata mounted?)");
        SessionSeriesModel model;           // reads only; never writes
        model.setSynchronous(true);
        model.activateSession(dir);
        const auto snap = model.snapshot();
        QVERIFY(snap && snap->count() >= 3);
        const int n = snap->count();
        const QString picked = snap->shots[size_t(std::min(pickOrdinal, n) - 1)]->swingDir;

        ChartsRelationsPresenter p;          // the app's path: asynchronous, off the GUI thread
        QElapsedTimer t;
        t.start();
        p.setRegisterMode(QStringLiteral("replay"));
        p.setPickedSwingDir(picked);
        p.setModel(&model);
        QVERIFY(p.waitForIdle(120000));
        const double wall = double(t.nsecsElapsed()) / 1e6;
        QVERIFY(p.ready());
        const QVariantMap st = p.status();
        const double job = st.value(QStringLiteral("timings")).toMap().value(QStringLiteral("jobMs")).toDouble();
        const double apply = st.value(QStringLiteral("timings")).toMap().value(QStringLiteral("applyMs")).toDouble();
        QCOMPARE(st.value(QStringLiteral("shotCount")).toInt(), n);
        int fired = 0;
        for (const QVariant &g : p.thisShot().value(QStringLiteral("groups")).toList())
            for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList())
                for (const QVariant &f : r.toMap().value(QStringLiteral("firedShots")).toList()) fired += f.toBool();

        // the shipped pack's links carry the reducer: 4 July's early extension is an extremum, said as a peak
        if (QFileInfo(dir).fileName().startsWith(QStringLiteral("2026-07-04"))) {
            QStringList said;
            for (const QVariant &g : p.thisShot().value(QStringLiteral("groups")).toList())
                for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList())
                    for (const QVariant &c : r.toMap().value(QStringLiteral("conditions")).toList()) said << c.toString();
            if (!qEnvironmentVariableIsEmpty("PINPOINT_CORE_PACK")) {
                QVERIFY2(said.contains(QStringLiteral("Early extension, peak before P7")), qPrintable(said.join(QStringLiteral(" | "))));
                qInfo().noquote() << "  fired on shot" << pickOrdinal << ":" << said.join(QStringLiteral(" | "));
            }
        }
        // a choice change (cached session): Up, then open a row
        t.restart();
        p.setUpId(QStringLiteral("handSpeed@5"));
        p.setOpenId(QStringLiteral("clubheadSpeed@5"));
        p.setFacetId(QStringLiteral("half"));
        QVERIFY(p.waitForIdle(60000));
        const double choice = double(t.nsecsElapsed()) / 1e6;
        const double job2 = p.status().value(QStringLiteral("timings")).toMap().value(QStringLiteral("jobMs")).toDouble();

        // the playhead: GUI thread, every frame of a 240 fps second
        const qint64 impact = p.status().value(QStringLiteral("pickedImpactUs")).toLongLong();
        double worst = 0;
        int signals_ = 0;
        QObject::connect(&p, &ChartsRelationsPresenter::playheadChanged, &p, [&] { ++signals_; });
        for (int f = 0; f < 240; ++f) {
            QElapsedTimer ft; ft.start();
            p.setPlayheadUs(impact - 800000 + qint64(f) * 4167);
            worst = std::max(worst, double(ft.nsecsElapsed()) / 1e6);
        }
        qInfo().noquote() << QStringLiteral("[relations budget] %1: %2 shots, %3 readings, %4 apart on shot %5, %6 fired marks; "
                                            "full recompute job %7 ms (wall %8 ms incl. waits), GUI apply %9 ms; choice change job %10 ms "
                                            "(wall %11 ms); playhead worst %12 ms per frame, %13 signals over 240 frames; kinds %14")
                                 .arg(QFileInfo(dir).fileName()).arg(n).arg(st.value(QStringLiteral("readingCount")).toInt())
                                 .arg(p.thisShot().value(QStringLiteral("apartCount")).toInt()).arg(std::min(pickOrdinal, n)).arg(fired)
                                 .arg(job, 0, 'f', 1).arg(wall, 0, 'f', 1).arg(apply, 0, 'f', 2).arg(job2, 0, 'f', 1).arg(choice, 0, 'f', 1)
                                 .arg(worst, 0, 'f', 3).arg(signals_).arg(text(p.kinds().value(QStringLiteral("head"))));
        qInfo().noquote() << "  apart head:" << text(p.thisShot().value(QStringLiteral("head")))
                          << "| chance:" << text(p.relations().value(QStringLiteral("chance")).toMap().value(QStringLiteral("sentence"))).section(QLatin1Char('.'), 0, 0)
                          << "| drifts clearing chance:" << p.relations().value(QStringLiteral("drifts")).toMap().value(QStringLiteral("clearCount")).toInt();
    }
};

QTEST_GUILESS_MAIN(ChartsRelationsPresenterTest)
#include "charts_relations_presenter_test.moc"
