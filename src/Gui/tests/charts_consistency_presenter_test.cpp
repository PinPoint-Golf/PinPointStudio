// The Consistency tab's presenter (src/Gui/charts/charts_consistency_presenter.h) on the trimmed 5 Oct fixture, with a
// launch monitor, against that session's golden; on 4 July, with none; and on cut copies of them for what the data
// can leave out. The words are pinned verbatim: they are what the golfer reads.

#include "charts/charts_consistency_presenter.h"
#include "charts/consistency_env.h"
#include "charts/session_consistency.h"
#include "charts/session_series_model.h"
#include "charts/session_warp.h"
#include "charts_maths_fixture.h"
#include "Diagnostics/norms/norm_provider.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace pinpoint::charts;

namespace {

const QString kDataDir = QStringLiteral(CHARTS_MATHS_DATA);
const QChar kNbsp(0x00A0);

QString text(const QVariant &sentence) { return sentence.toMap().value(QStringLiteral("text")).toString(); }

// A fixture has no folder of its own, and the presenter tells snapshots apart by pointer and revision.
std::shared_ptr<SessionSnapshot> sessionOf(const QString &file)
{
    const charts_test::Fixture fx = charts_test::loadFixture(kDataDir + file);
    auto s = std::make_shared<SessionSnapshot>(fx.session);
    s->sessionDir = QStringLiteral("/fixture") + file;
    s->revision = 1;
    return s;
}

// A copy of a session with a metric gone from every shot.
std::shared_ptr<SessionSnapshot> without(const SessionSnapshot &in, const QString &key)
{
    auto s = std::make_shared<SessionSnapshot>(in);
    for (auto &sp : s->shots) {
        auto c = std::make_shared<ShotSeries>(*sp);
        c->metrics.remove(key);
        c->metricOrder.removeAll(key);
        sp = c;
    }
    return s;
}

// A copy of a session keeping only its launch-monitor readings.
std::shared_ptr<SessionSnapshot> launchMonitorOnly(const SessionSnapshot &in)
{
    auto s = std::make_shared<SessionSnapshot>(in);
    for (auto &sp : s->shots) {
        auto c = std::make_shared<ShotSeries>(*sp);
        for (const QString &k : sp->metricOrder)
            if (!k.startsWith(QLatin1String("lm."))) { c->metrics.remove(k); c->metricOrder.removeAll(k); }
        sp = c;
    }
    return s;
}

// A copy of a session with `key` a curve and nothing else, as swingPlaneCurve is: no phase samples, so no reading.
// `outlier`'s curve is far above the rest; `without` has none. Swing folders, for a relations presenter to pick by.
std::shared_ptr<SessionSnapshot> withCurveOnly(const SessionSnapshot &in, const QString &key, int outlier, int absent)
{
    auto s = std::make_shared<SessionSnapshot>(in);
    ++s->revision;
    for (auto &sp : s->shots) {
        auto c = std::make_shared<ShotSeries>(*sp);
        c->swingDir = QStringLiteral("/fixture/swing_%1").arg(c->ordinal);
        c->metrics.remove(key);
        c->metricOrder.removeAll(key);
        if (c->ordinal != absent) {
            MetricSeries m;
            m.key = key; m.label = QStringLiteral("Swing plane curve"); m.unit = QStringLiteral("°");
            for (qint64 t = c->impactUs - 3000000; t <= c->impactUs + 500000; t += 5000) {
                m.tUs.push_back(t);
                m.value.push_back(c->ordinal / 10.0 + (c->ordinal == outlier ? 100.0 : 0.0));
            }
            c->metrics.insert(key, m);
            c->metricOrder << key;
        }
        sp = c;
    }
    return s;
}

std::unique_ptr<ChartsConsistencyPresenter> make(std::shared_ptr<const SessionSnapshot> s, int picked = 0,
                                                 const QString &openCell = QString())
{
    auto p = std::make_unique<ChartsConsistencyPresenter>();
    p->setSynchronous(true);
    p->setPickedOrdinal(picked);
    p->setOpenCell(openCell);
    p->setSnapshot(std::move(s));
    return p;
}

QVariantMap groupOf(const QVariantMap &shot, const QString &name)
{
    for (const QVariant &g : shot.value(QStringLiteral("groups")).toList())
        if (g.toMap().value(QStringLiteral("name")).toString() == name) return g.toMap();
    return {};
}

QVariantMap rowOf(const QVariantMap &group, const QString &key)
{
    for (const QVariant &r : group.value(QStringLiteral("rows")).toList())
        if (r.toMap().value(QStringLiteral("key")).toString() == key) return r.toMap();
    return {};
}

QVariantMap cellAt(const QVariantMap &matrix, const QString &area, int column)
{
    for (const QVariant &r : matrix.value(QStringLiteral("rows")).toList())
        if (r.toMap().value(QStringLiteral("id")).toString() == area)
            return r.toMap().value(QStringLiteral("cells")).toList().at(column).toMap();
    return {};
}

} // namespace

class ChartsConsistencyPresenterTest : public QObject
{
    Q_OBJECT

    std::shared_ptr<SessionSnapshot> m_oct, m_july;
    QJsonObject m_golden;

private slots:
    void initTestCase()
    {
        m_oct = sessionOf(QStringLiteral("/session_1005.json"));
        m_july = sessionOf(QStringLiteral("/session_0704.json"));
        QVERIFY(m_oct->count() == 31);
        QVERIFY(m_july->count() == 15);
        QFile f(kDataDir + QStringLiteral("/goldens_1005_consistency.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        m_golden = QJsonDocument::fromJson(f.readAll()).object();
    }

    void theHead()
    {
        const auto p = make(m_oct);
        const QVariantMap head = p->head();
        QCOMPARE(head.value(QStringLiteral("kicker")).toString(), QStringLiteral("Consistency  ·  31 shots  ·  Gap wedge"));
        QCOMPARE(text(head.value(QStringLiteral("headline"))),
                 QStringLiteral("Your swing holds about the same all the way through, and your sequence moves most."));
        QCOMPARE(text(head.value(QStringLiteral("ball"))).replace(kNbsp, QLatin1Char(' ')),
                 QStringLiteral("Your ball: carry 100 yd, middle half 97 to 103 yd. It finishes 5 yd left, middle half 2 to 10 yd left."));
        QVERIFY(head.value(QStringLiteral("notes")).toList().isEmpty());
        QVERIFY(p->empty().isEmpty());
        QVERIFY(p->key().contains(QStringLiteral("P7-P8 is after the ball has gone")));
        QVERIFY(p->key().contains(QStringLiteral("after impact on every shot, and is shown lighter")));
        QVERIFY(!p->key().contains(QStringLiteral("spread")));
        QVERIFY(p->keyMarks().contains(QStringLiteral("◐ some")));
    }

    // Every branch of the rule, on tables made for it.
    void theHeadlineRule_data()
    {
        QTest::addColumn<QString>("cells");     // area:column:word ...
        QTest::addColumn<QString>("want");
        const QString wayDown = QStringLiteral("You repeat your swing best in the backswing. It loosens on the way down, and ");
        const QString wayBack = QStringLiteral("You repeat your swing best on the way down. It is looser in the backswing, and ");
        const QString same = QStringLiteral("Your swing holds about the same all the way through, and ");
        QTest::newRow("backswing lower") << "posture:1:steady posture:4:loose impact:6:loose" << wayDown + "your impact position moves most.";
        QTest::newRow("downswing lower") << "armsAndClub:2:loose armsAndClub:5:steady setup:0:steady" << wayBack + "your arms and club move most.";
        QTest::newRow("within .1") << "posture:2:some posture:5:some setup:0:loose" << same + "your setup moves most.";
        // the downswing's mean is one some in five, .1 over the backswing's 0: that is apart. One in six is not
        const QString steadyBack = QStringLiteral("posture:1:steady posture:2:steady posture:3:steady lateral:1:steady lateral:2:steady ");
        QTest::newRow("exactly .1") << steadyBack + "posture:4:steady posture:5:steady posture:6:steady lateral:4:steady lateral:5:some"
                                    << wayDown + "your lateral motion moves most.";
        QTest::newRow("just under .1") << steadyBack + "posture:4:steady posture:5:steady posture:6:steady lateral:4:steady lateral:5:some lateral:6:steady"
                                       << same + "your lateral motion moves most.";
        QTest::newRow("nothing above steady") << "posture:1:steady posture:5:nearError setup:0:steady" << "Your swing repeats from shot to shot.";
        QTest::newRow("after the ball is left out") << "posture:1:steady setup:0:steady posture:7:loose" << "Your swing repeats from shot to shot.";
        QTest::newRow("except for Finish") << "posture:1:steady posture:5:steady finish:7:loose" << same + "your finish moves most.";
        QTest::newRow("a dash has no word") << "posture:1:dash posture:5:steady setup:0:steady" << "Your swing repeats from shot to shot.";
        QTest::newRow("no word at all") << "posture:1:dash" << QString();
        QTest::newRow("one area is not the swing") << "impact:6:loose posture:1:dash" << QString();
        QTest::newRow("nor is one area and its span after the ball") << "lateral:3:loose posture:7:loose" << QString();
    }
    void theHeadlineRule()
    {
        QFETCH(QString, cells);
        QFETCH(QString, want);
        ConsistencyResult res;
        for (const QString &c : cells.split(QLatin1Char(' '))) {
            const QStringList f = c.split(QLatin1Char(':'));
            res.cells.push_back(ConsistencyCell{ f[0], f[1].toInt(), consistencyWordFromToken(f[2]) });
        }
        QCOMPARE(ChartsConsistencyPresenter::headline(res), want);
    }

    // The table has the golden's words, and a shot marks only the cells it stood out in.
    void theMatrix()
    {
        const auto p = make(m_oct, 8, QStringLiteral("lateral:7"));
        const QVariantMap matrix = p->matrix();
        const QVariantList columns = matrix.value(QStringLiteral("columns")).toList();
        QCOMPARE(columns.size(), kConsistencyColumns);
        QCOMPARE(columns.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("P1"));
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("P3-P4"));
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("short")).toString(), QStringLiteral("3-4"));
        const QVariantList rows = matrix.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), consistencyAreas().size());
        QCOMPARE(rows.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Arms and club"));

        const QHash<QString, QString> wordOf{ { QStringLiteral("steady"), QStringLiteral("steady") }, { QStringLiteral("some"), QStringLiteral("some") },
                                              { QStringLiteral("loose"), QStringLiteral("loose") }, { QStringLiteral("nearError"), QStringLiteral("⚠ error") },
                                              { QStringLiteral("dash"), QStringLiteral("-") } };
        const QHash<QString, QString> markOf{ { QStringLiteral("steady"), QStringLiteral("·") }, { QStringLiteral("some"), QStringLiteral("◐") },
                                              { QStringLiteral("loose"), QStringLiteral("●") }, { QStringLiteral("nearError"), QStringLiteral("⚠") },
                                              { QStringLiteral("dash"), QStringLiteral("-") } };
        int judged = 0;
        for (const QJsonValue &v : m_golden.value(QStringLiteral("cells")).toArray()) {
            const QJsonObject c = v.toObject();
            const QString area = c.value(QStringLiteral("area")).toString(), word = c.value(QStringLiteral("word")).toString();
            const QVariantMap cell = cellAt(matrix, area, c.value(QStringLiteral("column")).toInt());
            QCOMPARE(cell.value(QStringLiteral("text")).toString(), wordOf.value(word));
            QCOMPARE(cell.value(QStringLiteral("short")).toString(), markOf.value(word));
            QCOMPARE(cell.value(QStringLiteral("heat")).toInt(), word == QLatin1String("loose") ? 2 : word == QLatin1String("some") ? 1 : 0);
            QCOMPARE(cell.value(QStringLiteral("strong")).toBool(), word == QLatin1String("loose") || word == QLatin1String("some"));
            QCOMPARE(cell.value(QStringLiteral("heavy")).toBool(), word == QLatin1String("loose"));
            QVERIFY(cell.value(QStringLiteral("tap")).toBool());
            ++judged;
        }
        QCOMPARE(judged, 33);
        // a column that does not apply is blank, and not pressable
        const QVariantMap blank = cellAt(matrix, QStringLiteral("setup"), 3);
        QCOMPARE(blank.value(QStringLiteral("text")).toString(), QString());
        QCOMPARE(blank.value(QStringLiteral("short")).toString(), QString());
        QVERIFY(!blank.value(QStringLiteral("tap")).toBool());
        // the key says how long P7-P8 is, the same on every session
        QVERIFY2(p->key().contains(QStringLiteral("read over the same 100\u00A0ms after impact on every shot")), qPrintable(p->key()));
        // after the ball has gone: lighter, except Finish
        QVERIFY(cellAt(matrix, QStringLiteral("lateral"), 7).value(QStringLiteral("soft")).toBool());
        QVERIFY(!cellAt(matrix, QStringLiteral("lateral"), 6).value(QStringLiteral("soft")).toBool());
        QVERIFY(!cellAt(matrix, QStringLiteral("finish"), 7).value(QStringLiteral("soft")).toBool());
        // the open cell, and no other
        QVERIFY(cellAt(matrix, QStringLiteral("lateral"), 7).value(QStringLiteral("on")).toBool());
        QVERIFY(!cellAt(matrix, QStringLiteral("lateral"), 6).value(QStringLiteral("on")).toBool());

        const ConsistencyEnv env = consistencyEnvFromPacks();
        QSet<QString> want;
        for (const QJsonValue &v : m_golden.value(QStringLiteral("standouts")).toArray())
            if (v.toObject().value(QStringLiteral("ordinal")).toInt() == 8)
                want << env.areaOf(v.toObject().value(QStringLiteral("key")).toString()) + QLatin1Char(':')
                            + QString::number(v.toObject().value(QStringLiteral("column")).toInt());
        QCOMPARE(want.size(), 4);
        QSet<QString> got;
        for (const QVariant &r : rows)
            for (int c = 0; c < kConsistencyColumns; ++c) {
                const QVariantMap cell = r.toMap().value(QStringLiteral("cells")).toList().at(c).toMap();
                if (!cell.value(QStringLiteral("mark")).toString().isEmpty()) {
                    QCOMPARE(cell.value(QStringLiteral("mark")).toString(), QStringLiteral("◆"));
                    got << r.toMap().value(QStringLiteral("id")).toString() + QLatin1Char(':') + QString::number(c);
                }
            }
        QCOMPARE(got, want);

        p->setMedium(QStringLiteral("print"));      // paper has nothing to press
        QVERIFY(!cellAt(p->matrix(), QStringLiteral("lateral"), 7).value(QStringLiteral("tap")).toBool());
    }

    void theShotsList()
    {
        const auto p = make(m_oct);
        const QVariantList shots = p->shots();
        QCOMPARE(shots.size(), 31);
        QStringList flagged, guarded;
        for (const QVariant &s : shots) {
            const QVariantMap m = s.toMap();
            const QString o = QString::number(m.value(QStringLiteral("ordinal")).toInt());
            const QStringList f = m.value(QStringLiteral("flags")).toStringList();
            if (!f.isEmpty()) flagged << o + QLatin1Char(':') + f.join(QLatin1Char('+'));
            if (m.value(QStringLiteral("guarded")).toBool()) guarded << o;
        }
        QCOMPARE(flagged, QStringList({ QStringLiteral("7:short"), QStringLiteral("8:short+offline") }));
        QCOMPARE(guarded, QStringList({ QStringLiteral("2"), QStringLiteral("3") }));
    }

    void shotEight()
    {
        const auto p = make(m_oct, 8);
        const QVariantMap shot = p->shot();
        QVERIFY(shot.value(QStringLiteral("available")).toBool());
        QCOMPARE(shot.value(QStringLiteral("kicker")).toString(), QStringLiteral("Shot 8 of 31  ·  gap wedge"));
        QCOMPARE(shot.value(QStringLiteral("lm")).toString(), QStringLiteral("launch monitor: short and offline"));
        QCOMPARE(text(shot.value(QStringLiteral("sentence"))).replace(kNbsp, QLatin1Char(' ')),
                 QStringLiteral("Carry 11 yd against your usual 100 yd. It finished 30 yd right. 4 swing readings stood out, "
                                "more than most shots this session, but chance can reach that."));
        QStringList names;
        for (const QVariant &g : shot.value(QStringLiteral("groups")).toList()) names << g.toMap().value(QStringLiteral("name")).toString();
        QCOMPARE(names, QStringList({ QStringLiteral("The ball"), QStringLiteral("Your swing") }));
        const QVariantMap ball = groupOf(shot, QStringLiteral("The ball"));
        QCOMPARE(rowOf(ball, QStringLiteral("lm.launchAngle")).value(QStringLiteral("label")).toString(), QStringLiteral("Launch angle"));
        const QVariantMap back = rowOf(groupOf(shot, QStringLiteral("Your swing")), QStringLiteral("spineBackLine"));
        QCOMPARE(back.value(QStringLiteral("tag")).toString(), QStringLiteral("P4-P5"));
        QCOMPARE(shot.value(QStringLiteral("note")).toString(), QStringLiteral("These happened on the same shot. That does not show one caused another."));
        QVERIFY(!shot.value(QStringLiteral("guarded")).toBool());
        QCOMPARE(shot.value(QStringLiteral("guardNote")).toString(), QString());
    }

    void shotSeven()
    {
        const auto p = make(m_oct, 7);
        const QVariantMap shot = p->shot();
        QCOMPARE(shot.value(QStringLiteral("lm")).toString(), QStringLiteral("launch monitor: short"));
        QCOMPARE(text(shot.value(QStringLiteral("sentence"))).replace(kNbsp, QLatin1Char(' ')),
                 QStringLiteral("Carry 86 yd against your usual 100 yd. Nothing in your swing stood out. The ball and club readings below did."));
        QVERIFY(groupOf(shot, QStringLiteral("Your swing")).isEmpty());
        // a smash factor to its two places, with no unit word: the shared row, so Charts' Apart reads the same
        const QVariantMap smash = rowOf(groupOf(shot, QStringLiteral("The club")), QStringLiteral("lm.smashFactor"));
        QCOMPARE(smash.value(QStringLiteral("valueText")).toString(), QStringLiteral("0.99"));
        QCOMPARE(smash.value(QStringLiteral("medianText")).toString(), QStringLiteral("1.09"));
        QCOMPARE(smash.value(QStringLiteral("valueFigure")).toString(), QStringLiteral("0.99"));
        QCOMPARE(smash.value(QStringLiteral("unitText")).toString(), QString());
        QCOMPARE(smash.value(QStringLiteral("boxText")).toMap().value(QStringLiteral("med")).toString(), QStringLiteral("1.09"));
    }

    void anOrdinaryShotAndAMoreThanChanceShot()
    {
        // shot 12: one stand-out, carry inside its club's fences
        const auto p = make(m_oct, 12);
        const QString s = text(p->shot().value(QStringLiteral("sentence"))).replace(kNbsp, QLatin1Char(' '));
        QVERIFY2(s.contains(QStringLiteral(", close to your usual 100 yd. 1 swing reading stood out. Most shots this session show one or two, so this may be chance.")), qPrintable(s));
        QCOMPARE(p->shot().value(QStringLiteral("lm")).toString(), QStringLiteral("launch monitor: not short or offline"));
        QCOMPARE(p->shot().value(QStringLiteral("note")).toString(), QString());   // one row: nothing to go together
    }

    void guardedShots()
    {
        const QString note = QStringLiteral("⚠ The timing of this swing's positions looks unusual, so comparisons across its spans are not counted.");
        for (int ordinal : { 2, 3 }) {
            const auto p = make(m_oct, ordinal);
            QVERIFY(p->shot().value(QStringLiteral("guarded")).toBool());
            QCOMPARE(p->shot().value(QStringLiteral("guardNote")).toString(), note);
        }
        // its spans were not counted, so it cannot be said that nothing in the swing stood out
        const auto three = make(m_oct, 3);
        QCOMPARE(text(three->shot().value(QStringLiteral("sentence"))),
                 QStringLiteral("The spans of this swing can't be compared with your other shots. Nothing at its positions stood out."));
        QCOMPARE(three->shot().value(QStringLiteral("guardNote")).toString(), note);

        // the rows of an open cell carry the ⚠ on the values that rest on the ladder, and only on a guarded shot
        const QString rowWords = QStringLiteral("⚠ the timing of this swing's positions looks unusual");
        const auto cautions = [&](int ordinal) {
            int n = 0;
            for (const QVariant &v : make(m_oct, ordinal, QStringLiteral("lateral:3"))->cell().value(QStringLiteral("rows")).toList())
                n += v.toMap().value(QStringLiteral("cautionText")).toString().contains(rowWords.mid(2)) ? 1 : 0;
            return n;
        };
        QVERIFY(cautions(2) + cautions(3) > 0);
        QCOMPARE(cautions(8), 0);
        for (const QVariant &v : make(m_oct, 3, QStringLiteral("lateral:3"))->cell().value(QStringLiteral("rows")).toList()) {
            const QVariantMap r = v.toMap();
            if (r.value(QStringLiteral("measured")).toBool()) QVERIFY(r.value(QStringLiteral("caution")).toBool());
        }
    }

    void theOpenCell()
    {
        const auto p = make(m_oct, 8, QStringLiteral("sequence:6"));
        const QVariantMap cell = p->cell();
        QCOMPARE(cell.value(QStringLiteral("area")).toString(), QStringLiteral("Sequence"));
        QCOMPARE(cell.value(QStringLiteral("tag")).toString(), QStringLiteral("P6-P7"));
        QCOMPARE(cell.value(QStringLiteral("title")).toString(), QStringLiteral("Sequence, P6-P7"));
        QCOMPARE(cell.value(QStringLiteral("word")).toString(), QStringLiteral("loose"));
        QCOMPARE(text(cell.value(QStringLiteral("sub"))), QStringLiteral("2 of 5 readings loose; a row opens its curve"));
        const QVariantList subRuns = cell.value(QStringLiteral("sub")).toMap().value(QStringLiteral("runs")).toList();
        QCOMPARE(subRuns.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("2"));
        QVERIFY(subRuns.at(0).toMap().value(QStringLiteral("fig")).toBool());
        QCOMPARE(cell.value(QStringLiteral("unjudged")).toInt(), 15);
        QCOMPARE(cell.value(QStringLiteral("unjudgedNote")).toString(), QStringLiteral("15 readings have no range to be judged against, and are listed as not judged."));
        QCOMPARE(cell.value(QStringLiteral("shotLabel")).toString(), QStringLiteral("Shot 8"));

        // loosest first, by spread over the norm's σ, the readings not judged after them all
        const ConsistencyInput in = consistencyInput(*m_oct, alignSession(*m_oct), consistencyEnvFromPacks());
        const QVariantList rows = cell.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), 20);
        QHash<QString, double> ratio;
        for (const QJsonValue &v : m_golden.value(QStringLiteral("readings")).toArray()) {
            const QJsonObject r = v.toObject();
            if (r.value(QStringLiteral("column")).toInt() != 6) continue;
            for (const ConsistencyReading &g : in.readings)
                if (g.column == 6 && g.key == r.value(QStringLiteral("key")).toString())
                    ratio[g.key] = r.value(QStringLiteral("spread")).toDouble() / g.normSigma;
        }
        double last = 1e9;
        bool notJudged = false;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            const QString place = r.value(QStringLiteral("place")).toString();
            QCOMPARE(r.value(QStringLiteral("tag")).toString(), QStringLiteral("P6-P7"));
            if (place == QLatin1String("not judged")) { notJudged = true; QVERIFY(!r.value(QStringLiteral("judged")).toBool()); continue; }
            QVERIFY(!notJudged);
            QVERIFY(place == QLatin1String("loose") || place == QLatin1String("steady") || place == QLatin1String("⚠ near error"));
            const double here = ratio.value(r.value(QStringLiteral("key")).toString());
            QVERIFY(here <= last);
            last = here;
        }
        QVERIFY(notJudged);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("place")).toString(), QStringLiteral("loose"));

        // Impact reads P7 itself. No such cell, or no pick, is not an error
        const auto impact = make(m_oct, 0, QStringLiteral("impact:6"));
        QCOMPARE(impact->cell().value(QStringLiteral("tag")).toString(), QStringLiteral("P7"));
        QCOMPARE(impact->cell().value(QStringLiteral("shotLabel")).toString(), QString());
        QVERIFY(!impact->cell().value(QStringLiteral("rows")).toList().isEmpty());
        QCOMPARE(impact->shot().value(QStringLiteral("available")).toBool(), false);
        QCOMPARE(impact->shot().value(QStringLiteral("note")).toString(), QStringLiteral("Pick a shot to see what stood out on it."));
        QVERIFY(make(m_oct, 0, QStringLiteral("setup:3"))->cell().isEmpty());
        QVERIFY(make(m_oct, 0, QStringLiteral("nonsense"))->cell().isEmpty());
    }

    void noLaunchMonitor()
    {
        const auto p = make(m_july, 8);
        const QVariantMap head = p->head();
        QCOMPARE(head.value(QStringLiteral("kicker")).toString(), QStringLiteral("Consistency  ·  15 shots  ·  7 iron"));
        QCOMPARE(text(head.value(QStringLiteral("headline"))),
                 QStringLiteral("Your swing holds about the same all the way through, and your impact position moves most."));
        QVERIFY(head.value(QStringLiteral("ball")).toMap().isEmpty());
        QVERIFY(head.value(QStringLiteral("notes")).toList().isEmpty());
        for (const QVariant &s : p->shots())
            QVERIFY(s.toMap().value(QStringLiteral("flags")).toStringList().isEmpty());
        const QVariantMap shot = p->shot();
        QVERIFY(shot.value(QStringLiteral("available")).toBool());
        QCOMPARE(shot.value(QStringLiteral("lm")).toString(), QString());
        QCOMPARE(text(shot.value(QStringLiteral("sentence"))),
                 QStringLiteral("6 swing readings stood out, more than most shots this session, but chance can reach that."));
        QStringList names;
        for (const QVariant &g : shot.value(QStringLiteral("groups")).toList()) names << g.toMap().value(QStringLiteral("name")).toString();
        QCOMPARE(names, QStringList({ QStringLiteral("Your swing") }));
    }

    void carryWithNoOffline()
    {
        const auto p = make(without(*m_oct, QStringLiteral("lm.offline")), 8);
        const QVariantMap head = p->head();
        QCOMPARE(text(head.value(QStringLiteral("ball"))).replace(kNbsp, QLatin1Char(' ')),
                 QStringLiteral("Your ball: carry 100 yd, middle half 97 to 103 yd."));
        QCOMPARE(text(head.value(QStringLiteral("notes")).toList().value(0)),
                 QStringLiteral("This launch monitor gives carry but no offline, so only short shots are flagged."));
        QStringList flagged;
        for (const QVariant &s : p->shots())
            for (const QString &f : s.toMap().value(QStringLiteral("flags")).toStringList())
                flagged << QString::number(s.toMap().value(QStringLiteral("ordinal")).toInt()) + f;
        QCOMPARE(flagged, QStringList({ QStringLiteral("7short"), QStringLiteral("8short") }));
        QCOMPARE(p->shot().value(QStringLiteral("lm")).toString(), QStringLiteral("launch monitor: short"));
        p->setPickedOrdinal(12);
        QCOMPARE(p->shot().value(QStringLiteral("lm")).toString(), QStringLiteral("launch monitor: not short"));
    }

    void aClubUnderThreeShotsIsLeftOut()
    {
        auto s = std::make_shared<SessionSnapshot>(*m_oct);
        for (int i : { 4, 5 }) {
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(i)]);
            c->club = QStringLiteral("DRIVER");
            s->shots[size_t(i)] = c;
        }
        const auto p = make(s, 5);
        const QVariantMap head = p->head();
        QCOMPARE(head.value(QStringLiteral("kicker")).toString(), QStringLiteral("Consistency  ·  31 shots  ·  mixed clubs"));
        QCOMPARE(text(head.value(QStringLiteral("notes")).toList().value(0)),
                 QStringLiteral("Left out of the table: the driver (2 shots). A club needs three shots to be set against itself."));
        bool countIsAFigure = false;
        for (const QVariant &r : head.value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("runs")).toList())
            countIsAFigure = countIsAFigure || (r.toMap().value(QStringLiteral("fig")).toBool() && r.toMap().value(QStringLiteral("text")).toString() == QLatin1String("2"));
        QVERIFY(countIsAFigure);
        QVERIFY(text(head.value(QStringLiteral("ball"))).startsWith(QStringLiteral("Your ball with the gap wedge: carry ")));
        QVERIFY(text(p->shot().value(QStringLiteral("sentence"))).contains(QStringLiteral("There are too few shots with this club to set shot 5 against them.")));
    }

    // The rows Charts builds carry the same promise here: a curve-only reading opens its curve, or offers nothing.
    void aCurveOnlyReadingOpensItsCurveOrNothing()
    {
        const QString key = QStringLiteral("swingPlaneCurve");
        const auto s = withCurveOnly(*m_oct, key, 8, 12);
        const QVariantMap row = rowOf(groupOf(make(s, 8)->shot(), QStringLiteral("Your swing")), key);
        QVERIFY2(!row.isEmpty(), "the curve stands out");
        QCOMPARE(row.value(QStringLiteral("opens")).toString(), QStringLiteral("trace"));
        const QString id = row.value(QStringLiteral("readingId")).toString();

        ChartsRelationsPresenter charts;
        charts.setSynchronous(true);
        charts.setPickedSwingDir(QStringLiteral("/fixture/swing_8"));
        charts.setOpenId(id);
        charts.setSnapshot(s);
        QCOMPARE(charts.openRow().value(QStringLiteral("readingId")).toString(), id);
        QCOMPARE(charts.openRow().value(QStringLiteral("opens")).toString(), QStringLiteral("trace"));

        // an open cell: a shot with the curve, a shot without it, and no shot at all
        const QString area = consistencyEnvFromPacks().areaOf(key);
        QVERIFY(!area.isEmpty());
        const QString cell = area + QStringLiteral(":%1").arg(consistencyColumnsOf(area).back() - 1);
        const auto cellRow = [&](int picked) {
            for (const QVariant &v : make(s, picked, cell)->cell().value(QStringLiteral("rows")).toList())
                if (v.toMap().value(QStringLiteral("key")).toString() == key) return v.toMap();
            return QVariantMap{};
        };
        QCOMPARE(cellRow(8).value(QStringLiteral("opens")).toString(), QStringLiteral("trace"));
        for (int picked : { 12, 0 }) {
            const QVariantMap none = cellRow(picked);
            QVERIFY(!none.isEmpty());
            QCOMPARE(none.value(QStringLiteral("opens")).toString(), QString());
            QCOMPARE(none.value(QStringLiteral("readingId")).toString(), QString());
            QVERIFY(!none.value(QStringLiteral("hasCurve")).toBool());
        }
    }

    // The same reasons as Apart, about the swing: nothing is said to be unremarkable that was never compared.
    void noTestCouldRun()
    {
        // five clubs and no camera
        auto lm = launchMonitorOnly(*m_oct);
        for (auto &sp : lm->shots) {
            auto c = std::make_shared<ShotSeries>(*sp);
            c->club = QStringLiteral("Club %1").arg(c->ordinal % 5);
            sp = c;
        }
        const QString noCamera = text(make(lm, 3)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(noCamera.endsWith(QStringLiteral("This shot has no camera readings, so there is no swing to set against the others.")), qPrintable(noCamera));

        // five of one club and three singles: readings, and only four others to set them against
        auto few = std::make_shared<SessionSnapshot>(*m_oct);
        few->shots.resize(8);
        for (int i = 0; i < 8; ++i) {
            auto c = std::make_shared<ShotSeries>(*few->shots[size_t(i)]);
            c->club = i < 5 ? QStringLiteral("7i") : QStringLiteral("Single %1").arg(i);
            few->shots[size_t(i)] = c;
        }
        const QString others = text(make(few, 5)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(others.contains(QStringLiteral("There are too few shots in the session to set shot 5 against them.")), qPrintable(others));
        QVERIFY2(!others.contains(QStringLiteral("stood out")), qPrintable(others));
        const QString single = text(make(few, 7)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(single.contains(QStringLiteral("There are too few shots with this club to set shot 7 against them.")), qPrintable(single));
    }

    void tooFewShots()
    {
        auto s = std::make_shared<SessionSnapshot>(*m_oct);
        s->shots.resize(4);
        const auto p = make(s, 2);
        QCOMPARE(p->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("tooFew"));
        const QString words = QStringLiteral("Most of this table is \"-\" because a reading needs five shots with one club to show how much it moves.");
        QCOMPARE(text(p->empty().value(QStringLiteral("sentence"))), words);
        QCOMPARE(text(p->head().value(QStringLiteral("headline"))), words);
        QCOMPARE(cellAt(p->matrix(), QStringLiteral("posture"), 3).value(QStringLiteral("text")).toString(), QStringLiteral("-"));
        QCOMPARE(cellAt(p->matrix(), QStringLiteral("posture"), 3).value(QStringLiteral("short")).toString(), QStringLiteral("-"));
        QCOMPARE(cellAt(p->matrix(), QStringLiteral("setup"), 3).value(QStringLiteral("text")).toString(), QString());
        QVERIFY(p->shot().value(QStringLiteral("available")).toBool());

        // down to one shot, it still speaks
        s = std::make_shared<SessionSnapshot>(*m_oct);
        s->shots.resize(1);
        const auto one = make(s, 1);
        QCOMPARE(one->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("tooFew"));
        QVERIFY(one->shot().value(QStringLiteral("available")).toBool());
    }

    void noCameraShots()
    {
        const auto p = make(launchMonitorOnly(*m_oct), 8);
        const QString words = QStringLiteral("This session has no camera shots, so only launch monitor readings can be judged.");
        QCOMPARE(p->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noCamera"));
        QCOMPARE(text(p->empty().value(QStringLiteral("sentence"))), words);
        QCOMPARE(text(p->head().value(QStringLiteral("headline"))), words);
        QVERIFY(!p->head().value(QStringLiteral("ball")).toMap().isEmpty());
        QCOMPARE(cellAt(p->matrix(), QStringLiteral("posture"), 3).value(QStringLiteral("text")).toString(), QStringLiteral("-"));
        QVERIFY(!cellAt(p->matrix(), QStringLiteral("posture"), 3).value(QStringLiteral("tap")).toBool());
        QVERIFY(text(p->shot().value(QStringLiteral("sentence"))).endsWith(QStringLiteral("This shot has no camera readings, so there is no swing to set against the others.")));
        QVERIFY(!p->shot().value(QStringLiteral("guarded")).toBool());
    }

    // Carry, no offline, no camera: a launch monitor's figures yield a swing reading, and that is not a swing.
    void aReadingFromTheLaunchMonitorIsNotACamera()
    {
        const QString derived = QStringLiteral("lowPointFromAttack");
        auto s = launchMonitorOnly(*without(*m_oct, QStringLiteral("lm.offline")));
        for (size_t i = 0; i < s->shots.size(); ++i) {
            const MetricSeries *read = m_oct->shots[i]->metric(derived);
            if (!read) continue;
            auto c = std::make_shared<ShotSeries>(*s->shots[i]);
            MetricSeries m = *read;
            m.tUs.clear();
            m.value.clear();
            m.valid.clear();
            c->metrics.insert(derived, m);
            c->metricOrder << derived;
            s->shots[i] = c;
        }
        const auto p = make(s, 8);
        const QString words = QStringLiteral("This session has no camera shots, so only launch monitor readings can be judged.");
        QCOMPARE(p->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noCamera"));
        QCOMPARE(text(p->head().value(QStringLiteral("headline"))), words);
        QCOMPARE(text(p->head().value(QStringLiteral("notes")).toList().value(0)),
                 QStringLiteral("This launch monitor gives carry but no offline, so only short shots are flagged."));
        // the reading keeps its word in the table
        QVERIFY(cellAt(p->matrix(), QStringLiteral("impact"), 6).value(QStringLiteral("tap")).toBool());
        QVERIFY2(text(p->shot().value(QStringLiteral("sentence"))).contains(QStringLiteral("This shot has no camera readings")),
                 qPrintable(text(p->shot().value(QStringLiteral("sentence")))));
    }

    // Three ends of the count: what most shots show, past that, and past what chance gives.
    void whatTheCountMeans()
    {
        // 5 Oct: most shots show one, the shuffled 95th percentile is 7.55. Shot 24 has six, shot 12 one
        const QString twelve = text(make(m_oct, 12)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(twelve.endsWith(QStringLiteral("1 swing reading stood out. Most shots this session show one or two, so this may be chance.")), qPrintable(twelve));
        const QString twentyFour = text(make(m_oct, 24)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(twentyFour.endsWith(QStringLiteral("6 swing readings stood out, more than most shots this session, but chance can reach that.")), qPrintable(twentyFour));

        // past it: one shot of a copy of the session with its readings moved well off the rest
        auto s = std::make_shared<SessionSnapshot>(*m_oct);
        auto c = std::make_shared<ShotSeries>(*s->shots[9]);
        for (auto it = c->metrics.begin(); it != c->metrics.end(); ++it) {
            if (it.key().startsWith(QLatin1String("lm."))) continue;
            for (PhaseSample &ps : it.value().phaseSamples) ps.value += 1000.0;
        }
        s->shots[9] = c;
        const QString ten = text(make(s, 10)->shot().value(QStringLiteral("sentence")));
        QVERIFY2(ten.contains(QStringLiteral(" swing readings stood out, more than chance would give.")), qPrintable(ten));
    }

    void noSession()
    {
        const auto p = make(nullptr);
        QCOMPARE(p->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noSession"));
        QCOMPARE(text(p->head().value(QStringLiteral("headline"))), QStringLiteral("No session is loaded."));
        QVERIFY(p->shots().isEmpty());
        QVERIFY(!p->shot().value(QStringLiteral("available")).toBool());

        auto none = std::make_shared<SessionSnapshot>();
        none->sessionDir = QStringLiteral("/fixture/none");
        const auto q = make(none);
        QCOMPARE(q->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noShots"));
    }

    // While the model reads a session, nothing is said of it: "not loaded yet" is not "nothing here".
    void aSessionStillLoadingSaysSo()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("swing_0001")));
        QFile doc(dir.path() + QStringLiteral("/swing_0001/swing.json"));       // the model reads only what is there
        QVERIFY(doc.open(QIODevice::WriteOnly));
        doc.write("{}");
        doc.close();
        QSemaphore gate;
        SessionSeriesModel model;
        model.setDocLoader([&gate](const QString &, QString *error) {
            gate.acquire();
            if (error) *error = QStringLiteral("held by the test");
            return QJsonObject{};
        });
        ChartsConsistencyPresenter p;
        QCOMPARE(text(p.status().value(QStringLiteral("settling"))), QStringLiteral("Reading this session…"));  // before any job
        p.setModel(&model);
        QVERIFY(p.waitForIdle());
        QCOMPARE(p.empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noSession"));
        QVERIFY(!p.status().value(QStringLiteral("loading")).toBool());

        bool saidTooSoon = false;
        connect(&p, &ChartsConsistencyPresenter::resultsChanged, this, [&]() {
            if (model.busy() && !p.empty().isEmpty()) saidTooSoon = true;
        });
        model.activateSession(dir.path());
        QVERIFY(model.busy());
        QTRY_VERIFY(p.status().value(QStringLiteral("loading")).toBool());
        QVERIFY(p.waitForIdle());
        QCOMPARE(text(p.status().value(QStringLiteral("settling"))), QStringLiteral("Reading this session…"));
        QVERIFY(p.empty().isEmpty());
        QCOMPARE(p.head().value(QStringLiteral("kicker")).toString(), QStringLiteral("Consistency"));
        QCOMPARE(text(p.head().value(QStringLiteral("headline"))), QStringLiteral("Reading this session…"));
        QCOMPARE(p.key(), QString());
        QCOMPARE(p.shot().value(QStringLiteral("note")).toString(), QString());

        gate.release();
        QVERIFY(model.waitForIdle());
        QTRY_VERIFY(!p.status().value(QStringLiteral("loading")).toBool());
        QVERIFY(p.waitForIdle());
        QCOMPARE(p.empty().value(QStringLiteral("kind")).toString(), QStringLiteral("noShots"));
        QCOMPARE(text(p.head().value(QStringLiteral("headline"))), QStringLiteral("No shot in this session has been analysed yet."));
        QVERIFY(!saidTooSoon);
    }

    // The worker: results come off the GUI thread, and picking a shot or a cell never redoes the session.
    void pickingCostsOnlyItsRows()
    {
        ChartsConsistencyPresenter p;
        QSignalSpy results(&p, &ChartsConsistencyPresenter::resultsChanged);
        p.setSnapshot(m_oct);
        QVERIFY(p.busy());
        QVERIFY(results.isEmpty());         // queued: nothing arrives until the loop turns
        QVERIFY(p.waitForIdle());
        QVERIFY(!p.busy());
        QVERIFY(p.ready());
        QVERIFY(results.count() >= 1);
        QCOMPARE(p.sessionRuns(), 1);
        QVERIFY(p.lastJobThread() != nullptr);
        QVERIFY(p.lastJobThread() != QThread::currentThread());
        QVERIFY(p.status().value(QStringLiteral("timings")).toMap().value(QStringLiteral("consistencyMs")).toDouble() > 0.0);

        for (int ordinal = 1; ordinal <= 31; ++ordinal) {
            p.setPickedOrdinal(ordinal);
            QVERIFY(p.waitForIdle());
            QCOMPARE(p.shot().value(QStringLiteral("ordinal")).toInt(), ordinal);
        }
        p.setOpenCell(QStringLiteral("lateral:3"));
        QVERIFY(p.waitForIdle());
        QCOMPARE(p.cell().value(QStringLiteral("areaId")).toString(), QStringLiteral("lateral"));
        QCOMPARE(p.sessionRuns(), 1);
        QCOMPARE(p.status().value(QStringLiteral("timings")).toMap().value(QStringLiteral("consistencyMs")).toDouble(), 0.0);

        // a burst coalesces, and the last pick wins
        for (int ordinal = 1; ordinal <= 5; ++ordinal) p.setPickedOrdinal(ordinal);
        QVERIFY(p.waitForIdle());
        QCOMPARE(p.shot().value(QStringLiteral("ordinal")).toInt(), 5);
        QCOMPARE(p.sessionRuns(), 1);

        // a new revision does
        auto next = std::make_shared<SessionSnapshot>(*m_oct);
        next->revision = 2;
        p.setSnapshot(next);
        QVERIFY(p.waitForIdle());
        QCOMPARE(p.sessionRuns(), 2);
    }

    void pickedBySwingFolder()
    {
        const auto p = make(m_oct, 3);
        p->setPickedSwingDir(m_oct->shots[7]->swingDir);
        QCOMPARE(p->shot().value(QStringLiteral("ordinal")).toInt(), 8);
        p->setPickedSwingDir(QStringLiteral("/somewhere/else"));        // not in the session: no pick, and not the ordinal
        QVERIFY(!p->shot().value(QStringLiteral("available")).toBool());
        QCOMPARE(p->status().value(QStringLiteral("pickedOrdinal")).toInt(), 0);
        p->setPickedSwingDir(QString());
        QCOMPARE(p->shot().value(QStringLiteral("ordinal")).toInt(), 3);
    }

    // A pick and an open cell belong to their session: the next one must not publish its own shot 8 as picked.
    void aNewSessionDropsThePick()
    {
        const auto p = make(m_oct, 8, QStringLiteral("lateral:3"));
        QVERIFY(p->shot().value(QStringLiteral("available")).toBool());
        auto again = std::make_shared<SessionSnapshot>(*m_oct);     // a new revision of the same session keeps them
        again->revision = 2;
        p->setSnapshot(again);
        QCOMPARE(p->pickedOrdinal(), 8);
        QCOMPARE(p->openCell(), QStringLiteral("lateral:3"));

        p->setSnapshot(m_july);
        QCOMPARE(p->pickedOrdinal(), 0);
        QCOMPARE(p->openCell(), QString());
        QVERIFY(!p->shot().value(QStringLiteral("available")).toBool());
        QVERIFY(p->cell().isEmpty());
        QCOMPARE(p->status().value(QStringLiteral("pickedOrdinal")).toInt(), 0);
        for (const QVariant &r : p->matrix().value(QStringLiteral("rows")).toList())
            for (const QVariant &c : r.toMap().value(QStringLiteral("cells")).toList()) {
                QVERIFY(c.toMap().value(QStringLiteral("mark")).toString().isEmpty());
                QVERIFY(!c.toMap().value(QStringLiteral("on")).toBool());
            }
    }

    // The middle half near the line never reads "0 to on line".
    void offlineNearTheLine()
    {
        const auto scaled = [&](double k) {
            auto s = std::make_shared<SessionSnapshot>(*m_oct);
            for (auto &sp : s->shots) {
                auto c = std::make_shared<ShotSeries>(*sp);
                const auto it = c->metrics.find(QStringLiteral("lm.offline"));
                if (it != c->metrics.end())
                    for (PhaseSample &ps : it.value().phaseSamples) ps.value *= k;
                sp = c;
            }
            return s;
        };
        const auto ballOf = [&](double k) { return text(make(scaled(k))->head().value(QStringLiteral("ball"))).replace(kNbsp, QLatin1Char(' ')); };
        QVERIFY2(ballOf(0.01).endsWith(QStringLiteral("It finishes on line, middle half on line.")), qPrintable(ballOf(0.01)));
        QVERIFY2(ballOf(0.1).endsWith(QStringLiteral("It finishes 1 yd left, middle half on line to 1 yd left.")), qPrintable(ballOf(0.1)));
    }

    void paperHasNothingToPick()
    {
        const auto p = make(m_oct);
        QCOMPARE(p->shot().value(QStringLiteral("note")).toString(), QStringLiteral("Pick a shot to see what stood out on it."));
        p->setMedium(QStringLiteral("print"));
        QCOMPARE(p->shot().value(QStringLiteral("note")).toString(), QString());
    }

    // The session report's reading: the tab's words, nothing to press, nothing picked, every flagged shot.
    void theReportReadsTheTabsWords()
    {
        const auto p = make(m_oct);
        const QVariantMap r = consistencySessionReportPayload(m_oct, consistencyEnvFromPacks(), QStringLiteral("standard"),
                                                              pinpoint::analysis::sharedNormProvider(), QStringLiteral("swings"));
        QVERIFY(r.value(QStringLiteral("available")).toBool());
        QVERIFY(r.value(QStringLiteral("table")).toBool());
        QCOMPARE(r.value(QStringLiteral("headline")).toMap().value(QStringLiteral("text")).toString(),
                 p->head().value(QStringLiteral("headline")).toMap().value(QStringLiteral("text")).toString());
        QCOMPARE(r.value(QStringLiteral("ball")).toMap().value(QStringLiteral("text")).toString(),
                 p->head().value(QStringLiteral("ball")).toMap().value(QStringLiteral("text")).toString());
        QVERIFY(r.value(QStringLiteral("kicker")).toString().contains(QStringLiteral(" swings")));

        const QString diamond = QStringLiteral("◆");
        QVERIFY(p->key().contains(diamond) && p->keyMarks().contains(diamond));
        QVERIFY(!r.value(QStringLiteral("key")).toString().contains(diamond));
        QVERIFY(!r.value(QStringLiteral("keyMarks")).toString().contains(diamond));
        for (const QVariant &row : r.value(QStringLiteral("matrix")).toMap().value(QStringLiteral("rows")).toList())
            for (const QVariant &c : row.toMap().value(QStringLiteral("cells")).toList()) {
                const QVariantMap cell = c.toMap();
                QVERIFY(!cell.value(QStringLiteral("tap")).toBool());
                QVERIFY(!cell.value(QStringLiteral("on")).toBool());
                QVERIFY(cell.value(QStringLiteral("mark")).toString().isEmpty());
            }

        QList<int> want;
        for (const QVariant &s : p->shots())
            if (!s.toMap().value(QStringLiteral("flags")).toStringList().isEmpty()) want << s.toMap().value(QStringLiteral("ordinal")).toInt();
        QList<int> got;
        for (const QVariant &s : r.value(QStringLiteral("flagged")).toList()) {
            const QVariantMap shot = s.toMap();
            got << shot.value(QStringLiteral("ordinal")).toInt();
            QVERIFY(!shot.value(QStringLiteral("swingDir")).toString().isEmpty());
            p->setPickedOrdinal(got.last());
            QCOMPARE(shot.value(QStringLiteral("sentence")).toMap().value(QStringLiteral("text")).toString(),
                     p->shot().value(QStringLiteral("sentence")).toMap().value(QStringLiteral("text")).toString());
        }
        QCOMPARE(got, want);
    }
};

QTEST_GUILESS_MAIN(ChartsConsistencyPresenterTest)
#include "charts_consistency_presenter_test.moc"
