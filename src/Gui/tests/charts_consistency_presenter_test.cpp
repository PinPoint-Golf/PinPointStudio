// The Consistency tab's presenter (src/Gui/charts/charts_consistency_presenter.h) on the trimmed 5 Oct fixture, with a
// launch monitor, against that session's golden; on 4 July, with none; and on cut copies of them for what the data
// can leave out. The words are pinned verbatim: they are what the golfer reads. The variability words are not here:
// the presenter hands grades and templates, and QML takes the words from the vocabularies, so these pin the grades
// (against the grader run on its own) and the templates' placeholders.

#include "charts/charts_consistency_presenter.h"
#include "charts/consistency_env.h"
#include "charts/consistency_grades.h"
#include "charts/variability_env.h"
#include "charts/session_consistency.h"
#include "charts/session_series_model.h"
#include "charts/session_warp.h"
#include "charts_maths_fixture.h"
#include "Diagnostics/norms/norm_provider.h"
#include "variability/variability_facade.h"

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
namespace pv = pinpoint::variability;

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

// A copy of a session with `key` a curve and nothing else: no phase samples, so no reading.
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

// The session's grades worked out here, as the presenter must: its input on the production env, graded on the
// production scales.
struct Graded {
    ConsistencyInput in;
    ConsistencyGrades grades;
    std::unique_ptr<IVariabilityScaleProvider> provider;
};
Graded gradedOf(const SessionSnapshot &s)
{
    Graded g;
    g.provider = makeVariabilityScaleProvider();
    g.in = consistencyInput(s, alignSession(s), consistencyEnvFromPacks());
    g.grades = consistencyGrades(g.in, consistencyGradeScales(*g.provider));
    return g;
}

// A pooled cell's grade, by hand, for the head's rule.
pv::CellGrade cellGradeOf(int grade, double rho, bool limited = false)
{
    pv::CellGrade c;
    c.graded = true;
    c.grade = grade;
    c.rho = rho;
    c.measurementLimited = limited;
    c.n = grade > 0 ? 1 : 0;
    c.setting = grade > 0 ? 0 : -1;
    return c;
}

// A template holds every placeholder the QML fills, and no variability word of its own.
void checkTemplate(const QString &t)
{
    for (int i = 1; i <= 7; ++i) QVERIFY2(t.contains(QStringLiteral("%%1").arg(i)), qPrintable(t));
    for (const char *w : { "Grooved", "Repeatable", "Settling", "Variable", "Scattered", "Not enough swings", "Too few", "Checking",
                           "steady", "loose", "repeat", "usual" })
        QVERIFY2(!t.contains(QLatin1String(w), Qt::CaseInsensitive), qPrintable(t));
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
        // the headline is the grades' (the rule's own cases are theHeadlineRule's): the session's head, worked out here
        const Graded g = gradedOf(*m_oct);
        const QVariantMap want = ChartsConsistencyPresenter::headline(consistencyHead(g.grades));
        const QVariantMap line = head.value(QStringLiteral("headline")).toMap();
        QCOMPARE(line.value(QStringLiteral("kind")).toString(), want.value(QStringLiteral("kind")).toString());
        QVERIFY2(line.value(QStringLiteral("kind")).toString() == QLatin1String("halves") || line.value(QStringLiteral("kind")).toString() == QLatin1String("most"),
                 qPrintable(line.value(QStringLiteral("kind")).toString()));
        QCOMPARE(text(line), text(want));
        QVERIFY(text(line).endsWith(QStringLiteral(" moves most.")) || text(line).endsWith(QStringLiteral(" move most.")));
        QCOMPARE(text(head.value(QStringLiteral("ball"))).replace(kNbsp, QLatin1Char(' ')),
                 QStringLiteral("Your ball: carry 100 yd, middle half 97 to 103 yd. It finishes 5 yd left, middle half 2 to 10 yd left."));
        QVERIFY(head.value(QStringLiteral("notes")).toList().isEmpty());
        QVERIFY(p->empty().isEmpty());
        QVERIFY(p->key().contains(QStringLiteral("P7-P8 is after the ball has gone")));
        QVERIFY(p->key().contains(QStringLiteral("after impact on every shot, and is shown lighter")));
        QVERIFY(!p->key().contains(QStringLiteral("spread")));
        checkTemplate(p->key());
        checkTemplate(p->keyMarks());
        QVERIFY(p->keyMarks().contains(QStringLiteral("⚠ %7")));
        QVERIFY(!p->keyMarks().contains(QStringLiteral("◐")));
    }

    // Every branch of the head's rule, on graded cells made for it (consistencyHead, then the presenter's words).
    void theHeadlineRule_data()
    {
        QTest::addColumn<QString>("cells");     // area:column:grade:rho[:limited] ..., or area:column:none
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("want");
        QTest::addColumn<QString>("grades");    // back,down
        const QString halves = QStringLiteral("Your backswing is %1, your downswing %2; ");
        QTest::newRow("both halves") << "posture:1:4:0.4 posture:5:2:0.8 setup:0:5:0.2" << "halves"
                                     << halves + "your posture moves most." << "4,2";
        QTest::newRow("a half takes its loosest third") << "posture:1:5:0.2 posture:2:5:0.2 posture:3:3:0.6 impact:6:4:0.3" << "halves"
                                                        << halves + "your posture moves most." << "3,4";
        QTest::newRow("the area named is the loosest") << "armsAndClub:2:3:0.6 armsAndClub:5:4:0.3 setup:0:5:0.1" << "halves"
                                                       << halves + "your arms and club move most." << "3,4";
        QTest::newRow("a missing half") << "impact:6:3:0.6 setup:0:5:0.2" << "most" << "Your impact position moves most." << "";
        QTest::newRow("after the ball is left out") << "posture:7:1:1.5 setup:0:5:0.2" << "grade0" << "" << "";
        QTest::newRow("except for Finish") << "finish:7:2:0.8 posture:1:4:0.4 posture:5:4:0.4" << "halves"
                                           << halves + "your finish moves most." << "4,4";
        QTest::newRow("grade 0 only") << "posture:1:0:nan setup:0:0:nan" << "grade0" << "" << "";
        QTest::newRow("one area is not the swing") << "impact:6:2:0.9 posture:1:none" << "grade0" << "" << "";
        QTest::newRow("nothing graded") << "posture:1:none setup:0:none" << "ungraded"
                                        << "Nothing here has a range to judge it against." << "";
    }
    void theHeadlineRule()
    {
        QFETCH(QString, cells);
        QFETCH(QString, kind);
        QFETCH(QString, want);
        QFETCH(QString, grades);
        ConsistencyGrades g;
        for (const QString &c : cells.split(QLatin1Char(' '))) {
            const QStringList f = c.split(QLatin1Char(':'));
            ConsistencyCellGrade cell;
            cell.area = f[0];
            cell.column = f[1].toInt();
            if (f[2] != QLatin1String("none"))
                cell.grade = cellGradeOf(f[2].toInt(), f[3] == QLatin1String("nan") ? pv::VariabilityResult::kNaN : f[3].toDouble(),
                                         f.size() > 4);
            g.cells.push_back(cell);
        }
        const QVariantMap h = ChartsConsistencyPresenter::headline(consistencyHead(g));
        QCOMPARE(h.value(QStringLiteral("kind")).toString(), kind);
        QCOMPARE(text(h), want);
        QStringList got;
        for (const QVariant &v : h.value(QStringLiteral("grades")).toList()) got << QString::number(v.toMap().value(QStringLiteral("grade")).toInt());
        QCOMPARE(got.join(QLatin1Char(',')), grades);
        if (kind == QLatin1String("halves"))     // a surface that will not say a half's word says only which area
            QCOMPARE(h.value(QStringLiteral("fallback")).toString(), want.mid(want.indexOf(QStringLiteral("; ")) + 2).replace(0, 1, QStringLiteral("Y")));
    }

    // A reading's grade (consistency_grades.h): its spread against its corridor, pooled within club, R3 when the spread is
    // no bigger than its own measurement σ, grade 0 under eight shots, none without a scale.
    void aReadingsGrade()
    {
        std::vector<ConsistencyShot> shots(10);
        for (int i = 0; i < 10; ++i) { shots[size_t(i)].ordinal = i + 1; shots[size_t(i)].club = QStringLiteral("7i"); }
        ConsistencyReading r;
        r.key = QStringLiteral("someAngle"); r.unit = QStringLiteral("°"); r.column = 2; r.samples = 1;
        r.kind = ConsistencyReadingKind::Position;
        r.normSigma = 10.0;
        for (int i = 0; i < 10; ++i) r.values.push_back(40.0 + i);       // SD 3.03, ρ 0.30: grade 4
        const ConsistencyGradeScales none;

        r.ownSigma = 5.0;                                                 // the spread is inside its own σ
        const pv::VariabilityResult limited = consistencyReadingGrade(r, shots, none);
        QVERIFY(limited.graded);
        QCOMPARE(limited.grade, 4);
        QVERIFY(limited.measurementLimited);
        QCOMPARE(limited.n, 10);
        const pv::VariabilityResultView view = pv::VariabilityResultView::fromResult(limited, QStringLiteral("°"), 1);
        QVERIFY2(view.basisText().endsWith(QStringLiteral("· measurement-limited")), qPrintable(view.basisText()));
        QVERIFY2(view.basisText().contains(QStringLiteral("of corridor")), qPrintable(view.basisText()));

        r.ownSigma = 1.0;
        QVERIFY(!consistencyReadingGrade(r, shots, none).measurementLimited);

        // another club's offset is not spread (R2a), and a club under three shots is left out
        std::vector<ConsistencyShot> two = shots;
        ConsistencyReading moved = r;
        for (int i = 5; i < 10; ++i) { two[size_t(i)].club = QStringLiteral("PW"); moved.values[size_t(i)] += 100.0; }
        const pv::VariabilityResult pooled = consistencyReadingGrade(moved, two, none);
        QVERIFY(pooled.graded);
        QVERIFY2(pooled.rho < 0.25, qPrintable(QString::number(pooled.rho)));

        std::vector<ConsistencyShot> seven(shots.begin(), shots.begin() + 7);
        ConsistencyReading few = r;
        few.values.resize(7);
        const pv::VariabilityResult zero = consistencyReadingGrade(few, seven, none);
        QVERIFY(zero.graded);
        QCOMPARE(zero.grade, 0);

        ConsistencyReading noScale = r;
        noScale.normSigma = kNaN;
        QVERIFY(!consistencyReadingGrade(noScale, shots, none).graded);
        ConsistencyGradeScales scale;
        scale.variabilityScale = [](const QString &) { return std::optional<double>(10.0); };
        QVERIFY(consistencyReadingGrade(noScale, shots, scale).graded);   // the descriptor's scale is the next rung

        // a span takes its median point by ρ
        ConsistencyReading span = r;
        span.kind = ConsistencyReadingKind::Span;
        span.samples = 5;
        span.values.clear();
        const double widths[5] = { 0.5, 1.0, 2.0, 4.0, 8.0 };
        for (int i = 0; i < 10; ++i)
            for (double w : widths) span.values.push_back(40.0 + w * i);
        r.values.clear();
        for (int i = 0; i < 10; ++i) r.values.push_back(40.0 + 2.0 * i);
        QCOMPARE(consistencyReadingGrade(span, shots, none).rho, consistencyReadingGrade(r, shots, none).rho);

        // a guarded shot is left out of a reading on its P-ladder (a span), as the stand-outs leave it out, and kept in a
        // position (R13: guarded readings out, nothing else)
        std::vector<ConsistencyShot> guarded = shots;
        guarded[9].guarded = true;
        ConsistencyReading wild = span;
        for (int k = 0; k < 5; ++k) wild.values[size_t(9 * 5 + k)] = 400.0;      // shot 10's span, far off
        std::vector<ConsistencyShot> nine(shots.begin(), shots.begin() + 9);
        ConsistencyReading spanNine = span;
        spanNine.values.resize(9 * 5);
        QCOMPARE(consistencyReadingGrade(wild, guarded, none).rho, consistencyReadingGrade(spanNine, nine, none).rho);
        QCOMPARE(consistencyReadingGrade(wild, guarded, none).n, 9);
        ConsistencyReading position = r;
        QCOMPARE(consistencyReadingGrade(position, guarded, none).n, 10);
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

        // R1: each cell is cellGrade() over its readings, each graded on its own here; the word is QML's, from the grade
        const Graded g = gradedOf(*m_oct);
        int cells = 0, graded = 0;
        for (const QJsonValue &v : m_golden.value(QStringLiteral("cells")).toArray()) {
            const QJsonObject c = v.toObject();
            const QString area = c.value(QStringLiteral("area")).toString();
            const int column = c.value(QStringLiteral("column")).toInt();
            const QVariantMap cell = cellAt(matrix, area, column);
            std::vector<pv::VariabilityResult> readings;
            int at = 0;
            for (size_t r = 0; r < g.in.readings.size(); ++r)
                if (g.in.readings[r].area == area && g.in.readings[r].column == column) {
                    readings.push_back(consistencyReadingGrade(g.in.readings[r], g.in.shots, consistencyGradeScales(*g.provider)));
                    ++at;
                }
            const pv::CellGrade want = pv::cellGrade(readings);
            const QVariantMap result = cell.value(QStringLiteral("result")).toMap();
            QCOMPARE(result.value(QStringLiteral("graded")).toBool(), want.graded);
            QCOMPARE(result.value(QStringLiteral("grade")).toInt(), want.grade);
            QCOMPARE(result.value(QStringLiteral("measurementLimited")).toBool(), want.measurementLimited);
            QCOMPARE(result.value(QStringLiteral("basisText")).toString(), QString());      // a cell shows the word alone
            const bool limited = want.graded && want.measurementLimited;
            const bool moved = want.graded && !limited && want.grade >= 1 && want.grade <= 3;
            QCOMPARE(cell.value(QStringLiteral("text")).toString(), want.graded ? QString() : QStringLiteral("-"));
            QCOMPARE(cell.value(QStringLiteral("short")).toString(),
                     !want.graded ? QStringLiteral("-") : limited ? QStringLiteral("⚠") : QString::number(want.grade));
            QCOMPARE(cell.value(QStringLiteral("heat")).toInt(), moved && want.grade <= 2 ? 2 : moved ? 1 : 0);
            QCOMPARE(cell.value(QStringLiteral("strong")).toBool(), moved);
            QCOMPARE(cell.value(QStringLiteral("heavy")).toBool(), moved && want.grade <= 2);
            QCOMPARE(cell.value(QStringLiteral("tap")).toBool(), at > 0);
            QVERIFY(cell.value(QStringLiteral("mark")).toString().isEmpty() || cell.value(QStringLiteral("mark")).toString() == QStringLiteral("◆"));
            ++cells;
            graded += want.graded && want.grade >= 1 ? 1 : 0;
        }
        QCOMPARE(cells, 33);
        QVERIFY2(graded > 0, "a 31-shot session grades some of its table");
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
        // ball flight has no badge (R4): the carry is set against its club's median in the sentence
        const QString sentence8 = text(shot.value(QStringLiteral("sentence"))).replace(kNbsp, QLatin1Char(' '));
        QVERIFY2(sentence8.startsWith(QStringLiteral("Carry 11 yd against a median of ")), qPrintable(sentence8));
        QVERIFY2(sentence8.endsWith(QStringLiteral(" yd. It finished 30 yd right. 4 swing readings stood out, "
                                                   "more than most shots this session, but chance can reach that.")), qPrintable(sentence8));
        for (const QVariant &r : groupOf(shot, QStringLiteral("The ball")).value(QStringLiteral("rows")).toList())
            QVERIFY2(!r.toMap().contains(QStringLiteral("variability")), qPrintable(r.toMap().value(QStringLiteral("key")).toString()));
        QVERIFY(shot.value(QStringLiteral("carry")).isNull());
        int typical = 0;
        for (const QVariant &r : groupOf(shot, QStringLiteral("Your swing")).value(QStringLiteral("rows")).toList())
            typical += r.toMap().value(QStringLiteral("variability")).toMap().value(QStringLiteral("kind")).toString() == QLatin1String("typicality") ? 1 : 0;
        QVERIFY(typical > 0);
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
        const QString sentence7 = text(shot.value(QStringLiteral("sentence"))).replace(kNbsp, QLatin1Char(' '));
        QVERIFY2(sentence7.startsWith(QStringLiteral("Carry 86 yd against a median of ")), qPrintable(sentence7));
        QVERIFY2(sentence7.endsWith(QStringLiteral(" yd. Nothing in your swing stood out. The ball and club readings below did.")), qPrintable(sentence7));
        // smash factor is an outcome: no typicality on its row (R4)
        QVERIFY(!rowOf(groupOf(shot, QStringLiteral("The club")), QStringLiteral("lm.smashFactor")).contains(QStringLiteral("variability")));
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
        QVERIFY2(s.contains(QStringLiteral(" yd. 1 swing reading stood out. Most shots this session show one or two, so this may be chance.")), qPrintable(s));
        QVERIFY2(!s.contains(QStringLiteral("usual")), qPrintable(s));
        // ball flight has no badge (R4): no typicality beside the sentence, which sets the carry against its median
        QVERIFY(p->shot().value(QStringLiteral("carry")).isNull());
        QVERIFY2(s.contains(QStringLiteral(" against a median of ")), qPrintable(s));
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
        const Graded g = gradedOf(*m_oct);
        const ConsistencyCellGrade *want = g.grades.cellAt(QStringLiteral("sequence"), 6);
        QVERIFY(want);
        QCOMPARE(cell.value(QStringLiteral("result")).toMap().value(QStringLiteral("grade")).toInt(), want->grade.grade);
        QCOMPARE(cell.value(QStringLiteral("headLabel")).toString(), QStringLiteral("Widest spread first"));
        QCOMPARE(cell.value(QStringLiteral("shotLabel")).toString(), QStringLiteral("Shot 8"));

        // widest spread first by ρ, then those under eight shots, then those with no scale; each with its grade
        const QVariantList rows = cell.value(QStringLiteral("rows")).toList();
        QCOMPARE(rows.size(), int(want->readings.size()));
        int stage = 0, gradedN = 0, ungraded = 0;
        double last = 1e9;
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            QCOMPARE(r.value(QStringLiteral("tag")).toString(), QStringLiteral("P6-P7"));
            const QVariantMap vb = r.value(QStringLiteral("variability")).toMap();
            QCOMPARE(vb.value(QStringLiteral("kind")).toString(), QStringLiteral("consistency"));
            const QVariantMap res = vb.value(QStringLiteral("result")).toMap();
            QCOMPARE(r.value(QStringLiteral("graded")).toBool(), res.value(QStringLiteral("graded")).toBool());
            const bool hasRho = res.value(QStringLiteral("hasRho")).toBool() && res.value(QStringLiteral("grade")).toInt() >= 1;
            const int here = hasRho ? 0 : res.value(QStringLiteral("graded")).toBool() ? 1 : 2;
            QVERIFY(here >= stage);
            stage = here;
            if (here == 0) {
                const double rho = res.value(QStringLiteral("rho")).toDouble();
                QVERIFY(rho <= last);
                last = rho;
                ++gradedN;
                QVERIFY2(res.value(QStringLiteral("basisText")).toString().startsWith(QStringLiteral("σ ")), qPrintable(res.value(QStringLiteral("basisText")).toString()));
            }
            if (here == 2) { ++ungraded; QCOMPARE(res.value(QStringLiteral("basisText")).toString(), QString()); }
            QVERIFY(r.value(QStringLiteral("place")).toString() != QLatin1String("loose"));
        }
        QVERIFY(gradedN > 0);
        QCOMPARE(cell.value(QStringLiteral("unjudged")).toInt(), ungraded);
        QVERIFY2(text(cell.value(QStringLiteral("sub"))).startsWith(QStringLiteral("%1 of %2 reading").arg(gradedN).arg(rows.size())),
                 qPrintable(text(cell.value(QStringLiteral("sub")))));
        QVERIFY(text(cell.value(QStringLiteral("sub"))).endsWith(QStringLiteral("; a row opens its curve")));
        if (ungraded > 1)
            QCOMPARE(cell.value(QStringLiteral("unjudgedNote")).toString(),
                     QStringLiteral("%1 readings have no range to be judged against, and no grade.").arg(ungraded));

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
        const QString kind = head.value(QStringLiteral("headline")).toMap().value(QStringLiteral("kind")).toString();
        QVERIFY2(kind == QLatin1String("halves") || kind == QLatin1String("most"), qPrintable(kind));
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
        const QString key = QStringLiteral("thoraxRotationSigned");
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
        const QString words = QStringLiteral("Most of this table can't be graded yet: a reading needs eight shots with one club to show how much it moves.");
        QCOMPARE(text(p->empty().value(QStringLiteral("sentence"))), words);
        QCOMPARE(text(p->head().value(QStringLiteral("headline"))), words);
        QCOMPARE(p->head().value(QStringLiteral("headline")).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("empty"));
        // under eight shots a graded cell is grade 0 ("Not enough swings", QML's word), its short form "0"
        int zero = 0;
        for (const QVariant &r : p->matrix().value(QStringLiteral("rows")).toList())
            for (const QVariant &c : r.toMap().value(QStringLiteral("cells")).toList()) {
                const QVariantMap res = c.toMap().value(QStringLiteral("result")).toMap();
                if (!res.value(QStringLiteral("graded")).toBool()) continue;
                QCOMPARE(res.value(QStringLiteral("grade")).toInt(), 0);
                QCOMPARE(c.toMap().value(QStringLiteral("short")).toString(), QStringLiteral("0"));
                QCOMPARE(c.toMap().value(QStringLiteral("text")).toString(), QString());
                ++zero;
            }
        QVERIFY(zero > 0);
        QCOMPARE(cellAt(p->matrix(), QStringLiteral("setup"), 3).value(QStringLiteral("text")).toString(), QString());
        QVERIFY(p->shot().value(QStringLiteral("available")).toBool());

        // down to one shot, it still speaks
        s = std::make_shared<SessionSnapshot>(*m_oct);
        s->shots.resize(1);
        const auto one = make(s, 1);
        QCOMPARE(one->empty().value(QStringLiteral("kind")).toString(), QStringLiteral("tooFew"));
        QVERIFY(one->shot().value(QStringLiteral("available")).toBool());
    }

    // R8: a row's typicality is this shot against eight or more OTHER shots of its club. A club of eight has seven others:
    // its rows and its carry have none, and are as they were; the rest of the session's still do.
    void typicalityNeedsEightOthers()
    {
        auto s = std::make_shared<SessionSnapshot>(*m_oct);
        for (int i = 0; i < 8; ++i) {
            auto c = std::make_shared<ShotSeries>(*s->shots[size_t(i)]);
            c->club = QStringLiteral("PW");
            s->shots[size_t(i)] = c;
        }
        int rows = 0, medians = 0;
        for (int ordinal = 1; ordinal <= 8; ++ordinal) {
            const QVariantMap shot = make(s, ordinal)->shot();
            QVERIFY(shot.value(QStringLiteral("carry")).isNull() || shot.value(QStringLiteral("carry")).toMap().isEmpty());
            for (const QVariant &g : shot.value(QStringLiteral("groups")).toList())
                for (const QVariant &r : g.toMap().value(QStringLiteral("rows")).toList()) {
                    QVERIFY(!r.toMap().contains(QStringLiteral("variability")));
                    ++rows;
                }
            // with no badge, the carry is set against its club's median, never "your usual"
            const QString sentence = text(shot.value(QStringLiteral("sentence")));
            QVERIFY2(!sentence.contains(QStringLiteral("usual")), qPrintable(sentence));
            medians += sentence.contains(QStringLiteral(" against a median of ")) ? 1 : 0;
        }
        QVERIFY(medians > 0);
        QVERIFY2(rows > 0, "a stand-out or a fenced reading on the eight-shot club");
        QVERIFY(make(s, 12)->shot().value(QStringLiteral("carry")).isNull());     // ball flight has no badge, whatever the club (R4)
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
