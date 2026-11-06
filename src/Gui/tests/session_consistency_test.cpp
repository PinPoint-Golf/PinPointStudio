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

// THE CONSISTENCY MATHS (src/Gui/charts/session_consistency.h, docs/design/consistency_design.md §4). The
// rules on hand-made readings and a stub env, the sampling on synthetic shots, the guard on the 4 July
// fixture, then the 5 Oct fixture on the production env: its timing and its golden.

#include "charts/consistency_env.h"
#include "charts/session_consistency.h"
#include "charts/session_traces.h"
#include "charts_maths_fixture.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <cmath>
#include <map>
#include <memory>
#include <set>

using namespace pinpoint::charts;

namespace {

const QString kData = QStringLiteral(CHARTS_MATHS_DATA);

bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

ConsistencyInput inputOf(const QStringList &clubs)
{
    ConsistencyInput in;
    for (int i = 0; i < clubs.size(); ++i) {
        ConsistencyShot s;
        s.ordinal = i + 1;
        s.club = clubs.at(i);
        in.shots.push_back(s);
    }
    return in;
}

ConsistencyReading position(const QString &key, const QString &area, int column, const std::vector<double> &values)
{
    ConsistencyReading r;
    r.key = key; r.area = area; r.column = column;
    r.kind = ConsistencyReadingKind::Position;
    r.samples = 1;
    r.values = values;
    return r;
}

// A span whose every point reads the same value for a shot.
ConsistencyReading span(const QString &key, const QString &area, int column, const std::vector<double> &values)
{
    ConsistencyReading r;
    r.key = key; r.area = area; r.column = column;
    r.kind = ConsistencyReadingKind::Span;
    r.samples = kConsistencySamplesPerSpan;
    for (double v : values)
        for (int j = 0; j < r.samples; ++j) r.values.push_back(v);
    return r;
}

void addSorted(ConsistencyInput &in, ConsistencyReading r)
{
    in.readings.push_back(std::move(r));
    std::stable_sort(in.readings.begin(), in.readings.end(), [](const ConsistencyReading &a, const ConsistencyReading &b) {
        return a.key != b.key ? a.key < b.key : a.column < b.column;
    });
}

// Eight shots of one club: the first is `first`, the others -0.3..0.3 in steps of 0.1 (their spread is
// 0.3 / 1.349, their median 0), so z = first / spread however the club's median moves.
std::vector<double> oneOut(double first) { return { first, -0.3, -0.2, -0.1, 0.0, 0.1, 0.2, 0.3 }; }
const double kOthersSpread = 0.3 / 1.349;

const ConsistencyStandOut *standOutOn(const ConsistencyResult &res, int ordinal, const QString &key)
{
    for (const ConsistencyStandOut &s : res.standouts)
        if (s.ordinal == ordinal && s.key == key) return &s;
    return nullptr;
}

const ConsistencyCell *cellOf(const ConsistencyResult &res, const QString &area, int column)
{
    for (const ConsistencyCell &c : res.cells)
        if (c.area == area && c.column == column) return &c;
    return nullptr;
}

const ConsistencyJudged *judgedOf(const ConsistencyResult &res, const QString &key, int column)
{
    for (const ConsistencyJudged &r : res.readings)
        if (r.key == key && r.column == column) return &r;
    return nullptr;
}

const ConsistencyReading *readingOf(const ConsistencyInput &in, const QString &key, int column)
{
    for (const ConsistencyReading &r : in.readings)
        if (r.key == key && r.column == column) return &r;
    return nullptr;
}

ConsistencyEnv stubEnv(QHash<QString, QString> areas, double norm = 1.0)
{
    ConsistencyEnv env;
    env.areaOf = [areas = std::move(areas)](const QString &key) { return areas.value(key); };
    env.normSigma = [norm](const QString &, int, const QString &) { return norm; };
    return env;
}

MetricSeries curveSeries(const QString &key, qint64 impactUs, double fromMs, double toMs, double sigma = kNaN)
{
    MetricSeries m;
    m.key = key;
    m.label = key;
    m.unit = QStringLiteral("deg");
    m.sigma = sigma;
    for (double t = fromMs; t <= toMs + 1e-9; t += 5.0) {
        m.tUs.push_back(impactUs + qint64(std::llround(t * 1000.0)));
        m.value.push_back(t);
    }
    return m;
}

PhaseSample sampleAt(int phase, double value, double sigma = kNaN)
{
    PhaseSample p;
    p.phase = phase; p.value = value; p.sigma = sigma;
    return p;
}

// A shot with P1..P7 at the same confident times and a P8 of the caller's, confidence 0.30 (under the
// warp's gate 1). A negative p8Ms leaves the mark off.
std::shared_ptr<ShotSeries> ladderShot(int ordinal, double p8Ms, const QString &club = QStringLiteral("7i"))
{
    constexpr qint64 impact = 2000000;
    auto sh = std::make_shared<ShotSeries>();
    sh->ordinal = ordinal; sh->shotId = ordinal; sh->club = club; sh->impactUs = impact;
    const double at[] = { -1000, -800, -600, -300, -150, -60, 0 };
    for (int c = 0; c < 7; ++c)
        sh->phases.push_back(PhaseMark{ kConsistencyPhases[c], impact + qint64(at[c] * 1000.0), 0.9 });
    if (p8Ms >= 0.0) sh->phases.push_back(PhaseMark{ phase::P8, impact + qint64(p8Ms * 1000.0), 0.30 });
    return sh;
}

} // namespace

class SessionConsistencyTest : public QObject {
    Q_OBJECT
private slots:
    // ── the rules ───────────────────────────────────────────────────────────────────────────────
    void residualsAreAgainstTheClub();
    void spreadNeedsFiveFinite();
    void tableDropsThinReadings();
    void verdicts();
    void cellRules();
    void resultCells();
    void weakestPointRule();
    void leaveOneOutAndSixOthers();
    void twoSigmaRule();
    void onePerKeyAndSortOrder();
    void guardTakesSpansAndTiming();
    void ballFlags();
    void launchMonitorRows();
    void homeWord();
    void splitmixReferenceVector();
    void shuffleMatchesTheSlowWay();
    // ── the sampling ────────────────────────────────────────────────────────────────────────────
    void readingsByKind();
    void sigmasOfAReading();
    void spanWithNoCanonicalP();
    void columnSevenIgnoresCanonicalP8();
    void columnSevenIgnoresP8();
    void guardOnKeptAnchors();
    void guardOnRunsBackwards();
    void launchMonitorOnlyShotIsNotGuarded();
    void guardOnFixture();
    // ── the fixture ─────────────────────────────────────────────────────────────────────────────
    void timing5Oct();
    void golden5Oct();
    void goldenHome();
};

void SessionConsistencyTest::residualsAreAgainstTheClub()
{
    const QStringList clubs{ "7i", "7i", "7i", "PW", "PW", "", "", "" };
    const std::vector<double> values{ 1, 2, 3, 4, 5, 10, 20, kNaN };
    const std::vector<double> r = consistencyResiduals(values, 1, clubs);
    // the club's median includes the shot
    QVERIFY(near(r[0], -1) && near(r[1], 0) && near(r[2], 1));
    // a club under 3 shots has none
    QVERIFY(std::isnan(r[3]) && std::isnan(r[4]));
    // a blank label is a club of its own, counting a shot with no value, whose own residual stays NaN
    QVERIFY(near(r[5], -5) && near(r[6], 5) && std::isnan(r[7]));

    // sample by sample
    const std::vector<double> two{ 1, 10, 2, 20, 3, 30 };
    const std::vector<double> rt = consistencyResiduals(two, 2, QStringList{ "a", "a", "a" });
    QVERIFY(near(rt[0], -1) && near(rt[1], -10) && near(rt[4], 1) && near(rt[5], 10));
}

void SessionConsistencyTest::spreadNeedsFiveFinite()
{
    QVERIFY(std::isnan(consistencySpread({ 1, 2, 3, 4, kNaN, kNaN })));
    QVERIFY(near(consistencySpread({ 1, 2, 3, 4, 5, kNaN }), 2.0 / 1.349));
}

void SessionConsistencyTest::tableDropsThinReadings()
{
    const QStringList clubs{ "7i", "7i", "7i", "7i", "7i", "7i", "7i", "7i" };
    ConsistencyInput in = inputOf(clubs);

    // A span with a spread at 3 of its points is in; at 2 it is not, nor in a cell, nor a stand-out.
    ConsistencyReading three = span("three", "posture", 1, oneOut(9.0));
    ConsistencyReading two = span("two", "posture", 2, oneOut(9.0));
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 5; ++j) {
            if (j >= 3) three.values[size_t(i * 5 + j)] = kNaN;
            if (j >= 2) two.values[size_t(i * 5 + j)] = kNaN;
        }
    three.normSigma = two.normSigma = 1.0;
    addSorted(in, three);
    addSorted(in, two);

    // A position needs 5 finite raw values.
    ConsistencyReading five = position("five", "posture", 3, { 1, 2, 3, 4, 5, kNaN, kNaN, kNaN });
    ConsistencyReading four = position("four", "posture", 4, { 1, 2, 3, 4, kNaN, kNaN, kNaN, kNaN });
    five.normSigma = four.normSigma = 1.0;
    addSorted(in, five);
    addSorted(in, four);

    const ConsistencyResult res = consistencyResult(in);
    QVERIFY(judgedOf(res, "three", 1));
    QVERIFY(!judgedOf(res, "two", 2));
    QVERIFY(judgedOf(res, "five", 3));
    QVERIFY(!judgedOf(res, "four", 4));
    QVERIFY(standOutOn(res, 1, "three"));
    QVERIFY(!standOutOn(res, 1, "two"));
    QCOMPARE(cellOf(res, "posture", 2)->word, ConsistencyWord::Dash);       // applies, nothing judged
    QCOMPARE(cellOf(res, "posture", 2)->judged + cellOf(res, "posture", 2)->unjudged, 0);
    QCOMPARE(cellOf(res, "posture", 4)->word, ConsistencyWord::Dash);

    // 5 finite raw values on clubs too small for a residual: in the table, no spread, so unjudged: a dash.
    ConsistencyInput small = inputOf({ "a", "a", "b", "b", "c", "c" });
    ConsistencyReading r = position("x", "posture", 3, { 1, 2, 3, 4, 5, 6 });
    r.normSigma = 1.0;
    small.readings.push_back(r);
    const ConsistencyResult sr = consistencyResult(small);
    QVERIFY(judgedOf(sr, "x", 3));
    QCOMPARE(judgedOf(sr, "x", 3)->verdict, ConsistencyVerdict::Unjudged);
    QCOMPARE(cellOf(sr, "posture", 3)->word, ConsistencyWord::Dash);
}

void SessionConsistencyTest::verdicts()
{
    QCOMPARE(consistencyVerdict(kNaN, 1, 1), ConsistencyVerdict::Unjudged);
    QCOMPARE(consistencyVerdict(1, kNaN, 1), ConsistencyVerdict::Unjudged);
    QCOMPARE(consistencyVerdict(0.74, 1, kNaN), ConsistencyVerdict::Steady);
    // noise can only widen a spread: small is steady whatever σ says
    QCOMPARE(consistencyVerdict(0.74, 1, 5), ConsistencyVerdict::Steady);
    QCOMPARE(consistencyVerdict(0.75, 1, kNaN), ConsistencyVerdict::Loose);
    QCOMPARE(consistencyVerdict(0.75, 1, 0.5), ConsistencyVerdict::Loose);
    QCOMPARE(consistencyVerdict(0.75, 1, 0.75), ConsistencyVerdict::NearError);
    QCOMPARE(consistencyVerdict(2, 1, 3), ConsistencyVerdict::NearError);
}

void SessionConsistencyTest::cellRules()
{
    using V = ConsistencyVerdict;
    const auto word = [](const std::vector<V> &v) { return consistencyCell("posture", 1, v).word; };
    QCOMPARE(word({}), ConsistencyWord::Dash);      // applies, no readings: never blank
    QCOMPARE(word({ V::Unjudged, V::Unjudged }), ConsistencyWord::Dash);
    QCOMPARE(word({ V::Steady, V::Unjudged }), ConsistencyWord::Steady);
    QCOMPARE(word({ V::Loose, V::Steady, V::Steady }), ConsistencyWord::Loose);                  // 1 of 3
    QCOMPARE(word({ V::Loose, V::Steady, V::Steady, V::Steady }), ConsistencyWord::Some);        // 1 of 4
    QCOMPARE(word({ V::Loose, V::Loose, V::Steady, V::Steady, V::Steady, V::Steady }), ConsistencyWord::Loose);  // 2 of 6
    QCOMPARE(word({ V::NearError, V::Steady, V::Steady }), ConsistencyWord::NearError);          // 1 of 3
    QCOMPARE(word({ V::NearError, V::Steady, V::Steady, V::Steady }), ConsistencyWord::Steady);  // 1 of 4
    QCOMPARE(word({ V::Loose, V::NearError, V::NearError, V::Steady }), ConsistencyWord::Some);  // loose comes first
    const ConsistencyCell c = consistencyCell("posture", 1, { V::Loose, V::NearError, V::Steady, V::Unjudged });
    QCOMPARE(c.judged, 3); QCOMPARE(c.loose, 1); QCOMPARE(c.nearError, 1); QCOMPARE(c.unjudged, 1);
}

void SessionConsistencyTest::resultCells()
{
    ConsistencyInput in = inputOf({ "7i", "7i", "7i", "7i", "7i", "7i", "7i", "7i" });
    ConsistencyReading judged = position("a", "setup", 0, oneOut(0.0));
    judged.normSigma = 1.0;
    ConsistencyReading dash = position("b", "impact", 6, oneOut(0.0));          // no norm
    ConsistencyReading elsewhere = position("c", "sequence", 1, oneOut(0.0));   // sequence is not read at P1-P2
    elsewhere.normSigma = 1.0;
    ConsistencyReading unplaced = position("d", "", 3, oneOut(0.0));
    unplaced.normSigma = 1.0;
    in.readings = { judged, dash, elsewhere, unplaced };
    const ConsistencyResult res = consistencyResult(in);

    int expected = 0;
    for (const QString &area : consistencyAreas()) expected += int(consistencyColumnsOf(area).size());
    QCOMPARE(int(res.cells.size()), expected);          // every applicable cell, no other
    QCOMPARE(expected, 33);                             // doc §2.1: "the 33 in a session"
    QCOMPARE(res.cells.front().area, QStringLiteral("setup"));
    QCOMPARE(res.cells.back().area, QStringLiteral("finish"));
    QCOMPARE(cellOf(res, "setup", 0)->word, ConsistencyWord::Steady);
    QCOMPARE(cellOf(res, "impact", 6)->word, ConsistencyWord::Dash);
    QCOMPARE(cellOf(res, "finish", 7)->word, ConsistencyWord::Dash);        // no readings
    for (const ConsistencyCell &c : res.cells) QVERIFY(c.word != ConsistencyWord::Blank);
    QVERIFY(!cellOf(res, "sequence", 1));
    // readings outside any cell stay in the readings, unplaced included
    QVERIFY(judgedOf(res, "c", 1));
    QVERIFY(judgedOf(res, "d", 3));
}

void SessionConsistencyTest::weakestPointRule()
{
    const auto residuals = [](const std::vector<double> &first) {
        std::vector<double> r;
        const std::vector<double> o{ -0.3, -0.2, -0.1, 0.0, 0.1, 0.2, 0.3 };
        for (double x : first) r.push_back(x);
        for (double v : o) for (size_t j = 0; j < first.size(); ++j) r.push_back(v);
        return r;
    };
    // out at every point: the point nearest the pattern is the one reported
    ConsistencyPoint p = consistencyStandOutOf(residuals({ 5, 5, 1, 5, 5 }), 5, 0, kNaN);
    QVERIFY(p.standsOut);
    QCOMPARE(p.sample, 2);
    QVERIFY(near(p.z, 1.0 / kOthersSpread));
    QVERIFY(near(p.dv, 1.0));

    // back in the pattern at one point: not a stand-out, however far out the rest are
    QVERIFY(!consistencyStandOutOf(residuals({ 50, 50, 0.1, 50, 50 }), 5, 0, kNaN).standsOut);
    // a point with no value does not count against it
    p = consistencyStandOutOf(residuals({ 5, kNaN, 5, 5, kNaN }), 5, 0, kNaN);
    QVERIFY(p.standsOut);
    // the first of equals
    QCOMPARE(p.sample, 0);
    // just under the line
    QVERIFY(!consistencyStandOutOf(residuals({ 3.49 * kOthersSpread }), 1, 0, kNaN).standsOut);
    QVERIFY(consistencyStandOutOf(residuals({ 3.51 * kOthersSpread }), 1, 0, kNaN).standsOut);
    // both directions
    QVERIFY(consistencyStandOutOf(residuals({ -5 }), 1, 0, kNaN).z < 0.0);
    // others with no spread cannot judge
    QVERIFY(!consistencyStandOutOf({ 5, 0, 0, 0, 0, 0, 0, 0 }, 1, 0, kNaN).standsOut);
}

void SessionConsistencyTest::leaveOneOutAndSixOthers()
{
    // The shot's own huge value does not widen what it is compared with: z is the others' alone.
    const ConsistencyPoint p = consistencyStandOutOf(oneOut(1000.0), 1, 0, kNaN);
    QVERIFY(p.standsOut);
    QVERIFY(near(p.z, 1000.0 / kOthersSpread, 1e-6));

    // Six others is enough, five is not, and a NaN is not an other.
    QVERIFY(consistencyStandOutOf({ 9, -0.2, -0.1, 0.0, 0.05, 0.1, 0.2 }, 1, 0, kNaN).standsOut);
    QVERIFY(!consistencyStandOutOf({ 9, -0.2, -0.1, 0.0, 0.05, 0.1, kNaN }, 1, 0, kNaN).standsOut);
    QVERIFY(!consistencyStandOutOf({ 9, -0.2, -0.1, 0.0, 0.05, 0.1 }, 1, 0, kNaN).standsOut);
}

void SessionConsistencyTest::twoSigmaRule()
{
    QVERIFY(consistencyStandOutOf(oneOut(5.0), 1, 0, kNaN).standsOut);
    QVERIFY(consistencyStandOutOf(oneOut(5.0), 1, 0, 2.4).standsOut);       // 5 ≥ 4.8
    QVERIFY(consistencyStandOutOf(oneOut(5.0), 1, 0, 2.5).standsOut);       // 5 = 2σ: not under it
    QVERIFY(!consistencyStandOutOf(oneOut(5.0), 1, 0, 2.6).standsOut);      // 5 < 5.2
}

void SessionConsistencyTest::onePerKeyAndSortOrder()
{
    ConsistencyInput in = inputOf({ "7i", "7i", "7i", "7i", "7i", "7i", "7i", "7i" });
    in.readings = {
        position("a", "posture", 1, oneOut(5.0)),
        position("b", "posture", 1, oneOut(5.0)),
        position("b", "posture", 2, oneOut(10.0)),      // the larger of b: this one stays
        position("c", "posture", 1, oneOut(5.0)),
        position("c", "posture", 2, oneOut(-5.0)),      // equal |z|: the lowest column stays
        position("d", "posture", 4, oneOut(20.0)),
        position("e", "sequence", 1, oneOut(99.0)),     // sequence is not read here: never a stand-out
        position("f", "", 1, oneOut(99.0)),             // unplaced: never a stand-out
    };
    const ConsistencyResult res = consistencyResult(in);
    QStringList got;
    for (const ConsistencyStandOut &s : res.standouts)
        got << QStringLiteral("%1:%2:%3").arg(s.ordinal).arg(s.key).arg(s.column);
    QCOMPARE(got, QStringList({ "1:d:4", "1:b:2", "1:a:1", "1:c:1" }));
    QVERIFY(near(res.standouts[0].z, 20.0 / kOthersSpread, 1e-6));
    QCOMPARE(res.standouts[0].sample, 0);
}

void SessionConsistencyTest::guardTakesSpansAndTiming()
{
    ConsistencyInput in = inputOf({ "7i", "7i", "7i", "7i", "7i", "7i", "7i", "7i" });
    in.readings = {
        position("kept", "posture", 3, oneOut(5.0)),
        span("line", "posture", 2, oneOut(50.0)),
        position("line", "posture", 0, oneOut(5.0)),    // the guard comes before one-per-key: this one is left
        span("spanOnly", "lateral", 2, oneOut(5.0)),
        position("tempoBackswing", "sequence", 3, oneOut(5.0)),
        position("tempoRatio", "sequence", 3, oneOut(5.0)),
    };
    ConsistencyResult open = consistencyResult(in);
    QVERIFY(standOutOn(open, 1, "spanOnly"));
    QVERIFY(standOutOn(open, 1, "tempoBackswing"));
    QCOMPARE(standOutOn(open, 1, "line")->column, 2);

    in.shots[0].guarded = true;
    const ConsistencyResult guarded = consistencyResult(in);
    QVERIFY(standOutOn(guarded, 1, "kept"));
    QVERIFY(!standOutOn(guarded, 1, "spanOnly"));
    QVERIFY(!standOutOn(guarded, 1, "tempoBackswing"));
    QVERIFY(!standOutOn(guarded, 1, "tempoRatio"));
    QVERIFY(standOutOn(guarded, 1, "line"));
    QCOMPARE(standOutOn(guarded, 1, "line")->column, 0);

    // The shuffle counts real rows and ignores the guard.
    QCOMPARE(guarded.shuffledMean, open.shuffledMean);
    QCOMPARE(guarded.p95, open.p95);
}

void SessionConsistencyTest::ballFlags()
{
    ConsistencyInput in;
    const auto shot = [&](const QString &club, double carry, double offline) {
        ConsistencyShot s;
        s.ordinal = int(in.shots.size()) + 1;
        s.club = club; s.carry = carry; s.offline = offline;
        in.shots.push_back(s);
    };
    // 7i, 8 shots: shot 1 short, shot 2 offline, shot 3 both ways of offline's other side
    shot("7i", 60, 0);
    shot("7i", 150, 40);
    shot("7i", 152, -40);
    for (int i = 0; i < 5; ++i) shot("7i", 148 + i, -2 + i);
    // PW, 7 shots: one fewer than the rule, however wild
    shot("PW", 5, 90);
    for (int i = 0; i < 6; ++i) shot("PW", 120 + i, i);
    // 9i, 8 shots with carry and no offline: short only
    shot("9i", 40, kNaN);
    for (int i = 0; i < 7; ++i) shot("9i", 130 + i, kNaN);
    // No launch monitor at all: nothing
    for (int i = 0; i < 8; ++i) shot("6i", kNaN, kNaN);

    const ConsistencyResult res = consistencyResult(in);
    std::map<int, QStringList> flags;
    for (const ConsistencyBallFlag &f : res.flags) flags[f.ordinal] = f.flags;
    QCOMPARE(int(flags.size()), 4);
    QCOMPARE(flags[1], QStringList{ "short" });
    QCOMPARE(flags[2], QStringList{ "offline" });
    QCOMPARE(flags[3], QStringList{ "offline" });
    QCOMPARE(flags[16], QStringList{ "short" });
    QCOMPARE(res.flags.front().ordinal, 1);
}

void SessionConsistencyTest::launchMonitorRows()
{
    // Nine 7 irons and nine wedges. Shot 1's carry is past the low fence of its own club but inside the
    // session's, because the wedges fill the gap; shot 10 is the wedges' long one.
    SessionSnapshot s;
    const auto add = [&](int ordinal, const QString &club, const QHash<QString, double> &lm) {
        auto sh = std::make_shared<ShotSeries>();
        sh->ordinal = ordinal;
        sh->club = club;
        for (auto it = lm.constBegin(); it != lm.constEnd(); ++it) {
            MetricSeries m;
            m.key = it.key(); m.label = it.key(); m.unit = QStringLiteral("yd");
            m.phaseSamples.push_back(sampleAt(phase::Impact, it.value()));
            sh->metrics.insert(it.key(), m);
        }
        s.shots.push_back(sh);
    };
    const QString carry = QStringLiteral("lm.carryDistance"), offline = QStringLiteral("lm.offline");
    const QString speed = QStringLiteral("lm.clubheadSpeed"), spare = QStringLiteral("lm.backSpin");
    add(1, QStringLiteral("7i"), { { carry, 120.0 }, { offline, 40.0 }, { speed, 50.0 }, { spare, 1.0 } });
    for (int i = 0; i < 8; ++i)
        add(2 + i, QStringLiteral("7i"), { { carry, 150.0 + i }, { offline, double(i - 4) }, { speed, 80.0 + i }, { spare, double(i) } });
    add(10, QStringLiteral("GW"), { { carry, 130.0 } });
    for (int i = 0; i < 8; ++i) add(11 + i, QStringLiteral("GW"), { { carry, 100.0 + i } });

    const std::vector<ConsistencyLmRow> rows = consistencyLmRows(s, 1);
    QCOMPARE(int(rows.size()), 3);                      // the ball's first, in the rule's order; lm.backSpin is not one
    QCOMPARE(rows[0].key, carry);
    QVERIFY(rows[0].ball);
    QCOMPARE(rows[0].value, 120.0);
    QCOMPARE(rows[0].side, -1);
    QCOMPARE(rows[1].key, offline);
    QCOMPARE(rows[1].side, 1);
    QCOMPARE(rows[2].key, speed);
    QVERIFY(!rows[2].ball);
    QCOMPARE(int(rows[0].values.size()), 18);           // one per shot, the club's own
    QCOMPARE(rows[0].values[1], 150.0);
    QVERIFY(std::isnan(rows[0].values[9]));

    const std::vector<ConsistencyLmRow> gw = consistencyLmRows(s, 10);      // long: a row, though a flag would not be
    QCOMPARE(int(gw.size()), 1);
    QCOMPARE(gw[0].side, 1);
    QVERIFY(consistencyLmRows(s, 2).empty());
    QVERIFY(consistencyLmRows(s, 99).empty());          // no such shot

    // Eight values of the club are needed: at seven there is no fence.
    SessionSnapshot few = s;
    few.shots.erase(few.shots.begin() + 9, few.shots.end());
    few.shots.pop_back();
    few.shots.pop_back();
    QVERIFY(consistencyLmRows(few, 1).empty());
}

void SessionConsistencyTest::homeWord()
{
    const auto cell = [](const QString &area, int column, ConsistencyWord w) {
        ConsistencyCell c;
        c.area = area; c.column = column; c.word = w;
        return c;
    };
    using W = ConsistencyWord;
    const QString p = QStringLiteral("posture");

    // newest first, .75^j
    ConsistencyHomeWord h = consistencyHomeWord(p, { { cell(p, 1, W::Loose) }, { cell(p, 1, W::Steady) } });
    QVERIFY(near(h.score, 1.0 / 1.75));
    QCOMPARE(h.word, W::Loose);
    QCOMPARE(h.loosestColumn, 1);

    // a session with no word in the cell has no weight there, and the older ones keep their own fade
    h = consistencyHomeWord(p, { { cell(p, 1, W::Dash) }, { cell(p, 1, W::Loose) }, { cell(p, 1, W::Steady) } });
    QVERIFY(near(h.score, 0.75 / (0.75 + 0.5625)));

    // nearError scores 0, some ½; the column values are averaged; under .2 Steady names nothing
    h = consistencyHomeWord(p, { { cell(p, 1, W::Some), cell(p, 2, W::NearError), cell(p, 3, W::Steady) } });
    QVERIFY(near(h.score, 0.5 / 3.0));
    QCOMPARE(h.word, W::Steady);
    QCOMPARE(h.loosestColumn, -1);

    // at .2 it is Some, and at .5 Loose
    h = consistencyHomeWord(p, { { cell(p, 1, W::Some), cell(p, 2, W::Some), cell(p, 3, W::Steady), cell(p, 4, W::Steady),
                                   cell(p, 5, W::Steady) } });
    QVERIFY(near(h.score, 0.2));
    QCOMPARE(h.word, W::Some);
    QCOMPARE(h.loosestColumn, 1);        // the first of equals
    h = consistencyHomeWord(p, { { cell(p, 1, W::Loose), cell(p, 2, W::Steady) } });
    QCOMPARE(h.word, W::Loose);

    // P7-P8 is dropped, except for Finish
    h = consistencyHomeWord(p, { { cell(p, 1, W::Steady), cell(p, 7, W::Loose) } });
    QCOMPARE(h.word, W::Steady);
    QVERIFY(near(h.score, 0.0));
    const QString f = QStringLiteral("finish");
    h = consistencyHomeWord(f, { { cell(f, 7, W::Loose) } });
    QCOMPARE(h.word, W::Loose);
    QCOMPARE(h.loosestColumn, 7);

    // nothing to say: a dash. Other areas' cells are ignored.
    h = consistencyHomeWord(p, { { cell(p, 1, W::Dash), cell(p, 2, W::Blank), cell(f, 7, W::Loose) } });
    QCOMPARE(h.word, W::Dash);
    QVERIFY(std::isnan(h.score));
    QCOMPARE(consistencyHomeWord(p, {}).word, W::Dash);
}

void SessionConsistencyTest::splitmixReferenceVector()
{
    // CONTRACT "Pinned after W3" item 7.
    ConsistencySplitMix rng(kConsistencyShuffleSeed);
    const quint64 expected[8] = { 7134611160154358618ULL, 13877614986023876344ULL, 4292726422858613063ULL,
                                  1832488697174800709ULL, 3467252261107883461ULL, 7020995479949754436ULL,
                                  18180438093026040609ULL, 9428158358266441515ULL };
    for (quint64 e : expected) QCOMPARE(rng.next(), e);

    ConsistencySplitMix fresh(kConsistencyShuffleSeed);
    QCOMPARE(consistencyPermutation(10, fresh), std::vector<int>({ 3, 6, 0, 4, 5, 1, 2, 9, 7, 8 }));
    QCOMPARE(consistencyPermutation(10, fresh), std::vector<int>({ 8, 7, 3, 2, 1, 6, 9, 4, 0, 5 }));
}

// consistencyResult counts each row's stand-out once and moves it (header: COST). The slow way re-runs the
// rule on every shuffled reading, and must give the same figures.
void SessionConsistencyTest::shuffleMatchesTheSlowWay()
{
    QStringList clubs;
    for (int i = 0; i < 24; ++i) clubs << (i % 3 == 2 ? "PW" : i == 7 ? "4i" : "7i");
    ConsistencyInput in = inputOf(clubs);
    ConsistencySplitMix noise(99);
    const auto draw = [&] { return double(noise.next() % 2001) / 1000.0 - 1.0; };
    const QStringList keys{ "alpha", "beta", "gamma", "delta", "kappa", "zeta" };
    for (int k = 0; k < keys.size(); ++k)
        for (int c : { 1, 2, 3, 5 }) {
            ConsistencyReading r;
            r.key = keys[k]; r.area = "posture"; r.column = c;
            r.kind = ConsistencyReadingKind::Span;
            r.samples = kConsistencySamplesPerSpan;
            r.ownSigma = k == 2 ? 0.2 : kNaN;
            for (int i = 0; i < 24; ++i) {
                const double row = draw() + ((i + k + c) % 5 == 0 ? 9.0 : 0.0);
                for (int j = 0; j < r.samples; ++j)
                    r.values.push_back((i + j + k) % 11 == 0 ? kNaN : row + 0.1 * draw());
            }
            if (k == 5) std::fill(r.values.begin(), r.values.end() - 8, kNaN);      // dropped: no draws
            addSorted(in, r);
        }
    in.shots[3].guarded = true;
    const ConsistencyResult res = consistencyResult(in);

    std::vector<const ConsistencyReading *> list;
    for (const ConsistencyJudged &j : res.readings) list.push_back(&in.readings[size_t(j.reading)]);
    QVERIFY(list.size() < in.readings.size());
    const int n = int(in.shots.size());
    ConsistencySplitMix rng(kConsistencyShuffleSeed);
    double total = 0.0;
    std::vector<double> busiest;
    for (int s = 0; s < kConsistencyShuffles; ++s) {
        std::vector<std::set<QString>> found(static_cast<size_t>(n));
        for (const ConsistencyReading *r : list) {
            const std::vector<double> rows = consistencyResiduals(r->values, r->samples, clubs);
            const std::vector<int> perm = consistencyPermutation(n, rng);
            std::vector<double> shuffled(rows.size());
            for (int i = 0; i < n; ++i)
                for (int j = 0; j < r->samples; ++j)
                    shuffled[size_t(i * r->samples + j)] = rows[size_t(perm[size_t(i)] * r->samples + j)];
            for (int i = 0; i < n; ++i)
                if (consistencyStandOutOf(shuffled, r->samples, i, r->ownSigma).standsOut) found[size_t(i)].insert(r->key);
        }
        double most = 0.0;
        for (const auto &f : found) { total += double(f.size()); most = std::max(most, double(f.size())); }
        busiest.push_back(most);
    }
    QVERIFY(total > 0.0);
    QVERIFY(near(res.shuffledMean, total / double(kConsistencyShuffles * n)));
    QVERIFY(near(res.p95, quantile7(busiest, 0.95)));
}

void SessionConsistencyTest::readingsByKind()
{
    SessionSnapshot s;
    for (int i = 1; i <= 6; ++i) {
        auto sh = ladderShot(i, 100.0);
        // area impact, with a curve: its one reading is the position at P7
        MetricSeries imp = curveSeries("imp", sh->impactUs, -900, 300);
        imp.phaseSamples = { sampleAt(phase::Impact, 7.0 + i), sampleAt(phase::Top, -50.0) };
        sh->metrics.insert(imp.key, imp);
        // the backswing's timing, stamped at impact, sits at P3-P4
        MetricSeries tempo;
        tempo.key = "tempoBackswing";
        tempo.phaseSamples = { sampleAt(phase::Impact, 0.7 + 0.01 * i) };
        sh->metrics.insert(tempo.key, tempo);
        // instant only: a reading per P it has a sample at
        MetricSeries inst;
        inst.key = "inst";
        inst.phaseSamples = { sampleAt(phase::Top, 1.0 + i), sampleAt(phase::P5, 2.0 + i) };
        sh->metrics.insert(inst.key, inst);
        // a curve on one shot only makes it a curve key for all
        if (i == 1) {
            MetricSeries mixed = curveSeries("mixed", sh->impactUs, -900, 300);
            sh->metrics.insert(mixed.key, mixed);
        } else {
            MetricSeries mixed;
            mixed.key = "mixed";
            mixed.phaseSamples = { sampleAt(phase::Top, 3.0) };
            sh->metrics.insert(mixed.key, mixed);
        }
        MetricSeries curve = curveSeries("curve", sh->impactUs, -900, 300);
        curve.phaseSamples = { sampleAt(phase::Address, 11.0 + i), sampleAt(phase::Top, 99.0) };
        sh->metrics.insert(curve.key, curve);
        MetricSeries ball;
        ball.key = "lm.carryDistance";
        ball.phaseSamples = { sampleAt(phase::Impact, 150.0 + i) };
        sh->metrics.insert(ball.key, ball);
        MetricSeries offline;
        offline.key = "lm.offline";
        offline.phaseSamples = { sampleAt(phase::P8, kNaN), sampleAt(phase::Top, -1.5) };
        sh->metrics.insert(offline.key, offline);
        MetricSeries stray = curveSeries("stray", sh->impactUs, -900, 300);
        sh->metrics.insert(stray.key, stray);
        s.shots.push_back(sh);
    }
    const SessionAlignment al = alignSession(s);
    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "imp", "impact" }, { "tempoBackswing", "sequence" },
                                                                  { "inst", "posture" }, { "curve", "posture" },
                                                                  { "mixed", "lateral" } }));
    QStringList got;
    for (const ConsistencyReading &r : in.readings)
        if (r.key != "stray" && r.key != "curve") got << QStringLiteral("%1:%2").arg(r.key).arg(r.column);
    QCOMPARE(got, QStringList({ "imp:6", "inst:3", "inst:4", "mixed:1", "mixed:2", "mixed:3", "mixed:4", "mixed:5", "mixed:6",
                                "mixed:7", "tempoBackswing:3" }));
    QCOMPARE(readingOf(in, "imp", 6)->kind, ConsistencyReadingKind::Position);
    QCOMPARE(readingOf(in, "imp", 6)->at(2, 0), 10.0);
    QVERIFY(near(readingOf(in, "tempoBackswing", 3)->at(0, 0), 0.71));
    QCOMPARE(readingOf(in, "inst", 3)->at(0, 0), 2.0);
    QCOMPARE(readingOf(in, "inst", 4)->at(0, 0), 3.0);

    // a curve key: P1 as a position, then the seven spans
    const ConsistencyReading *p1 = readingOf(in, "curve", 0);
    QVERIFY(p1);
    QCOMPARE(p1->kind, ConsistencyReadingKind::Position);
    QCOMPARE(p1->at(0, 0), 12.0);
    for (int c = 1; c < 8; ++c) {
        QVERIFY(readingOf(in, "curve", c));
        QCOMPARE(readingOf(in, "curve", c)->kind, ConsistencyReadingKind::Span);
        QCOMPARE(readingOf(in, "curve", c)->samples, kConsistencySamplesPerSpan);
    }
    // the samples sit evenly inside the span: curve value = ms from impact, and all shots share one ladder
    const ConsistencyReading *c3 = readingOf(in, "curve", 3);    // P3-P4: -600..-300
    QVERIFY(near(c3->at(0, 0), -570.0, 1e-6));
    QVERIFY(near(c3->at(0, 4), -330.0, 1e-6));

    // unplaced stays in the input
    QVERIFY(readingOf(in, "stray", 1));
    QVERIFY(readingOf(in, "stray", 1)->area.isEmpty());
    // the ball, from any phase, first finite; no lm key becomes a reading
    QCOMPARE(in.shots[0].carry, 151.0);
    QCOMPARE(in.shots[0].offline, -1.5);
    for (const ConsistencyReading &r : in.readings) QVERIFY(!r.key.startsWith("lm."));
}

void SessionConsistencyTest::sigmasOfAReading()
{
    SessionSnapshot s;
    const double series[] = { 1.0, 2.0, 3.0, 4.0, kNaN, 6.0 };
    for (int i = 1; i <= 6; ++i) {
        auto sh = ladderShot(i, 100.0, i <= 4 ? "7i" : "PW");
        MetricSeries m = curveSeries("curve", sh->impactUs, -900, 300, series[i - 1]);
        m.phaseSamples = { sampleAt(phase::Address, 1.0, 0.1 * i) };
        sh->metrics.insert(m.key, m);
        MetricSeries noPhaseSigma = curveSeries("plain", sh->impactUs, -900, 300, series[i - 1]);
        noPhaseSigma.phaseSamples = { sampleAt(phase::Address, 1.0) };
        sh->metrics.insert(noPhaseSigma.key, noPhaseSigma);
        MetricSeries tempo;
        tempo.key = "tempoRatio";
        tempo.phaseSamples = { sampleAt(phase::Impact, 3.0) };
        sh->metrics.insert(tempo.key, tempo);
        s.shots.push_back(sh);
    }
    const SessionAlignment al = alignSession(s);

    ConsistencyEnv env;
    env.areaOf = [](const QString &) { return QStringLiteral("posture"); };
    std::set<int> phases;
    env.normSigma = [&](const QString &key, int ph, const QString &club) {
        phases.insert(ph);
        return key == QLatin1String("plain") ? kNaN : double(ph) + (club == QLatin1String("PW") ? 100.0 : 0.0);
    };
    const ConsistencyInput in = consistencyInput(s, al, env);

    // own σ: a position's phase samples, the median over shots
    QVERIFY(near(readingOf(in, "curve", 0)->ownSigma, 0.35));
    // else the series' median over the shots that have one
    QVERIFY(near(readingOf(in, "plain", 0)->ownSigma, 3.0));
    // a span: the series' median
    QVERIFY(near(readingOf(in, "curve", 2)->ownSigma, 3.0));

    // norm σ: per shot's club, median over shots, at the column's end P
    QVERIFY(near(readingOf(in, "curve", 3)->normSigma, double(phase::Top)));
    QVERIFY(near(readingOf(in, "curve", 7)->normSigma, double(phase::P8)));
    QVERIFY(near(readingOf(in, "tempoRatio", 3)->normSigma, double(phase::Top)));
    QVERIFY(std::isnan(readingOf(in, "plain", 3)->normSigma));
    for (int c = 0; c < kConsistencyColumns; ++c) QVERIFY(phases.count(kConsistencyPhases[c]));
}

void SessionConsistencyTest::spanWithNoCanonicalP()
{
    SessionSnapshot s;
    for (int i = 1; i <= 6; ++i) {
        auto sh = ladderShot(i, 100.0);
        if (i <= 4)     // P3 on under half the shots: no canonical time, so P2-P3 and P3-P4 have no reading
            sh->phases.erase(std::remove_if(sh->phases.begin(), sh->phases.end(),
                                            [](const PhaseMark &p) { return p.phase == phase::P3; }),
                             sh->phases.end());
        MetricSeries m = curveSeries("curve", sh->impactUs, -900, 300);
        sh->metrics.insert(m.key, m);
        s.shots.push_back(sh);
    }
    const SessionAlignment al = alignSession(s);
    QVERIFY(!al.canonicalOf(phase::P3));
    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "curve", "posture" } }));
    QVERIFY(readingOf(in, "curve", 1));
    QVERIFY(!readingOf(in, "curve", 2));
    QVERIFY(!readingOf(in, "curve", 3));
    QVERIFY(readingOf(in, "curve", 4));
}

void SessionConsistencyTest::columnSevenIgnoresCanonicalP8()
{
    // Every P8 at 0.30 (under gate 1), two modes, as 5, 7, 8 and 20 Oct. No canonical P8 at all.
    SessionSnapshot s;
    const double p8[] = { 90, 400, 90, 400, 90, 400, 90, 400 };
    for (int i = 1; i <= 8; ++i) {
        auto sh = ladderShot(i, p8[i - 1]);
        MetricSeries m = curveSeries("curve", sh->impactUs, -900, 600);
        sh->metrics.insert(m.key, m);
        s.shots.push_back(sh);
    }
    const SessionAlignment al = alignSession(s);
    QVERIFY(!al.canonicalOf(phase::P8));

    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "curve", "finish" } }));
    QCOMPARE(in.afterImpactMs, 100.0);
    const ConsistencyReading *c7 = readingOf(in, "curve", 7);
    QVERIFY(c7);
    QCOMPARE(c7->kind, ConsistencyReadingKind::Span);
    // same real time on every shot, the short P8s and the long: the curve's value is its own ms from impact
    const double expected[] = { 10, 30, 50, 70, 90 };
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 5; ++j) QVERIFY2(near(c7->at(i, j), expected[j], 1e-6), qPrintable(QString::number(c7->at(i, j))));

    // own clock, not P-aligned: a shot whose warp differs reads the same real instants
    auto stretched = std::make_shared<ShotSeries>(*s.shots[0]);
    stretched->phases[3].tUs -= 80000;      // a P4 further out: a different warp, within gate 3
    s.shots[0] = stretched;
    const ConsistencyInput again = consistencyInput(s, alignSession(s), stubEnv({ { "curve", "finish" } }));
    for (int j = 0; j < 5; ++j) QVERIFY(near(readingOf(again, "curve", 7)->at(0, j), expected[j], 1e-6));

    // a shot with a curve short of the end has NaN there, valid samples only
    auto cut = std::make_shared<ShotSeries>(*s.shots[1]);
    cut->metrics["curve"].valid.assign(cut->metrics["curve"].tUs.size(), 1);
    for (size_t k = 0; k < cut->metrics["curve"].tUs.size(); ++k)
        if (cut->metrics["curve"].tUs[k] > cut->impactUs + 40000 && cut->metrics["curve"].tUs[k] < cut->impactUs + 60000)
            cut->metrics["curve"].valid[k] = 0;
    s.shots[1] = cut;
    const ConsistencyInput bridged = consistencyInput(s, alignSession(s), stubEnv({ { "curve", "finish" } }));
    QVERIFY(std::isnan(readingOf(bridged, "curve", 7)->at(1, 2)));
    QVERIFY(std::isfinite(readingOf(bridged, "curve", 7)->at(1, 0)));
}

void SessionConsistencyTest::columnSevenIgnoresP8()
{
    const auto inputWith = [](const std::vector<double> &p8, double conf) {
        SessionSnapshot s;
        int ordinal = 0;
        for (double ms : p8) {
            auto sh = ladderShot(++ordinal, ms);
            for (PhaseMark &p : sh->phases) if (p.phase == phase::P8) p.conf = conf;
            MetricSeries m = curveSeries("curve", sh->impactUs, -900, 600);
            sh->metrics.insert(m.key, m);
            s.shots.push_back(sh);
        }
        return consistencyInput(s, alignSession(s), stubEnv({ { "curve", "finish" } }));
    };
    // No P8 on any shot: the column is still read, 100 ms after impact.
    const ConsistencyInput none = inputWith({ -1, -1, -1, -1, -1, -1, -1 }, 0.30);
    QCOMPARE(none.afterImpactMs, 100.0);
    const ConsistencyReading *c7 = readingOf(none, "curve", 7);
    QVERIFY(c7);
    QCOMPARE(c7->kind, ConsistencyReadingKind::Span);
    const double expected[] = { 10, 30, 50, 70, 90 };
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 5; ++j) QVERIFY(near(c7->at(i, j), expected[j], 1e-6));

    // Low marks, long marks, one mode or two, confident or not: not one value of the column moves.
    const std::vector<std::vector<double>> sessions{ { 8, 12, 90, 100, 120, 410, 650 }, { 8, 8, 8, 8, 8, 8, 8 },
                                                     { 400, 400, 400, 400, 400, 400, 400 }, { 90, 90, 90, 90, 90, 90, 90 },
                                                     { 40, 200, 200, -1, -1, -1, 300 } };
    for (const std::vector<double> &p8 : sessions)
        for (double conf : { 0.30, 0.9 }) {
            const ConsistencyInput in = inputWith(p8, conf);
            QCOMPARE(in.afterImpactMs, 100.0);
            QVERIFY(readingOf(in, "curve", 7));
            QCOMPARE(readingOf(in, "curve", 7)->values, c7->values);
        }
}

void SessionConsistencyTest::guardOnKeptAnchors()
{
    // Confident ladders but for what each case lowers: four kept of P1..P7 (impact is one) is enough, three is not.
    SessionSnapshot s;
    for (int i = 1; i <= 8; ++i) {
        auto sh = ladderShot(i, 100.0);
        MetricSeries m = curveSeries("curve", sh->impactUs, -900, 300);
        sh->metrics.insert(m.key, m);
        s.shots.push_back(sh);
    }
    const auto lowered = [&](int index, const std::vector<int> &phases) {
        auto sh = std::make_shared<ShotSeries>(*s.shots[size_t(index)]);
        for (PhaseMark &p : sh->phases)
            if (std::find(phases.begin(), phases.end(), p.phase) != phases.end()) p.conf = 0.30;
        s.shots[size_t(index)] = sh;
    };
    lowered(1, { phase::P2, phase::P3, phase::Top });                   // keeps P1, P5, P6, P7
    lowered(2, { phase::P2, phase::P3, phase::Top, phase::P5 });        // keeps P1, P6, P7
    const SessionAlignment al = alignSession(s);
    QCOMPARE(al.warpFor(3)->route, AlignRoute::PAnchors);       // no other way to be guarded
    for (const GatedAnchor &g : al.warpFor(3)->considered) {
        QVERIFY(g.gate != AnchorGate::OutlierSegment);
        QVERIFY(g.gate != AnchorGate::RunsBackwards);
    }
    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "curve", "posture" } }));
    QVERIFY(!in.shots[0].guarded);
    QVERIFY(!in.shots[1].guarded);
    QVERIFY(in.shots[2].guarded);

    // A lost P8 leaves the count alone.
    QVERIFY(!consistencyGuarded(*s.shots[0], al.warpFor(1)));
    QVERIFY(consistencyGuarded(*s.shots[0], nullptr));          // a curve and no warp
}

void SessionConsistencyTest::guardOnRunsBackwards()
{
    SessionSnapshot s;
    for (int i = 1; i <= 8; ++i) {
        auto sh = ladderShot(i, 100.0);
        for (PhaseMark &p : sh->phases) p.conf = 0.9;       // here P8 has to reach the walk, not stop at gate 1
        MetricSeries m = curveSeries("curve", sh->impactUs, -900, 300);
        sh->metrics.insert(m.key, m);
        s.shots.push_back(sh);
    }
    const auto moved = [&](int index, int ph, double msFromImpact) {
        auto sh = std::make_shared<ShotSeries>(*s.shots[size_t(index)]);
        for (PhaseMark &p : sh->phases)
            if (p.phase == ph) p.tUs = sh->impactUs + qint64(msFromImpact * 1000.0);
        s.shots[size_t(index)] = sh;
    };
    moved(1, phase::Top, -100.0);       // P4 after P5 (-150): it runs backwards
    moved(2, phase::P8, -20.0);         // P8 before impact: so does this, and a lost P8 never counts

    const SessionAlignment al = alignSession(s);
    const auto gateOf = [&](int ordinal, int ph) {
        for (const GatedAnchor &g : al.warpFor(ordinal)->considered) if (g.phase == ph) return g.gate;
        return AnchorGate::Kept;
    };
    QCOMPARE(gateOf(2, phase::Top), AnchorGate::RunsBackwards);
    QCOMPARE(gateOf(3, phase::P8), AnchorGate::RunsBackwards);
    QCOMPARE(al.warpFor(2)->route, AlignRoute::PAnchors);
    QCOMPARE(int(al.warpFor(2)->anchors.size()), 7);        // P1, P2, P3, P5, P6, P7 and P8: not the kept count's doing

    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "curve", "posture" } }));
    QVERIFY(in.shots[1].guarded);
    QVERIFY(!in.shots[2].guarded);
    QVERIFY(!in.shots[0].guarded);
}

void SessionConsistencyTest::launchMonitorOnlyShotIsNotGuarded()
{
    SessionSnapshot s;
    for (int i = 1; i <= 6; ++i) {
        auto sh = std::make_shared<ShotSeries>();
        sh->ordinal = i; sh->shotId = i; sh->club = "7i";
        sh->impactUs = i == 1 ? -1 : 1000000;       // with an impact or without: no route to speak of either way
        MetricSeries carry;
        carry.key = "lm.carryDistance";
        carry.phaseSamples = { sampleAt(phase::Impact, 150.0 + i) };
        sh->metrics.insert(carry.key, carry);
        s.shots.push_back(sh);
    }
    // ... beside a camera shot that lost its ladder, which is guarded
    auto camera = std::make_shared<ShotSeries>();
    camera->ordinal = 7; camera->shotId = 7; camera->club = "7i"; camera->impactUs = 1000000;
    MetricSeries m = curveSeries("curve", camera->impactUs, -900, 300);
    camera->metrics.insert(m.key, m);
    s.shots.push_back(camera);

    const SessionAlignment al = alignSession(s);
    QVERIFY(al.warpFor(1)->route != AlignRoute::PAnchors);
    QVERIFY(al.warpFor(2)->route != AlignRoute::PAnchors);
    const ConsistencyInput in = consistencyInput(s, al, stubEnv({ { "curve", "posture" } }));
    for (int i = 0; i < 6; ++i) QVERIFY(!in.shots[size_t(i)].guarded);
    QVERIFY(in.shots[6].guarded);
    QCOMPARE(in.shots[1].carry, 152.0);
    QVERIFY(!consistencyGuarded(*s.shots[0], nullptr));         // no curve and no warp
}

void SessionConsistencyTest::guardOnFixture()
{
    const charts_test::Fixture fx = charts_test::loadFixture(kData + QStringLiteral("/session_0704.json"));
    QVERIFY(fx.ok);
    const ConsistencyEnv env = stubEnv({ { "handSpeed", "sequence" } });
    SessionSnapshot s = fx.session;

    const ConsistencyInput before = consistencyInput(s, alignSession(s), env);
    for (const ConsistencyShot &shot : before.shots) QVERIFY2(!shot.guarded, qPrintable(QString::number(shot.ordinal)));

    const auto markOf = [](ShotSeries &sh, int ph) -> PhaseMark & {
        for (PhaseMark &p : sh.phases) if (p.phase == ph) return p;
        Q_UNREACHABLE();
    };
    auto p4Moved = std::make_shared<ShotSeries>(*s.shots[2]);
    markOf(*p4Moved, phase::Top).tUs -= 400000;
    auto p8Moved = std::make_shared<ShotSeries>(*s.shots[4]);
    markOf(*p8Moved, phase::P8).tUs += 300000;
    auto unsure = std::make_shared<ShotSeries>(*s.shots[6]);
    markOf(*unsure, phase::Top).conf = 0.30;
    auto noPs = std::make_shared<ShotSeries>(*s.shots[8]);
    noPs->phases.erase(std::remove_if(noPs->phases.begin(), noPs->phases.end(),
                                      [](const PhaseMark &p) { return p.phase != phase::Impact; }),
                       noPs->phases.end());
    s.shots[2] = p4Moved; s.shots[4] = p8Moved; s.shots[6] = unsure; s.shots[8] = noPs;

    const SessionAlignment al = alignSession(s);
    const auto gateOf = [&](int ordinal, int ph) {
        for (const GatedAnchor &g : al.warpFor(ordinal)->considered) if (g.phase == ph) return g.gate;
        return AnchorGate::Kept;
    };
    QCOMPARE(gateOf(3, phase::Top), AnchorGate::OutlierSegment);    // gate 3 fired, as the test means
    QCOMPARE(gateOf(5, phase::P8), AnchorGate::OutlierSegment);
    QCOMPARE(gateOf(7, phase::Top), AnchorGate::LowConfidence);

    const ConsistencyInput in = consistencyInput(s, al, env);
    QVERIFY(in.shots[2].guarded);       // P4 lost to gate 3
    QVERIFY(!in.shots[4].guarded);      // only P8 moved: a lost P8 never counts
    QVERIFY(!in.shots[6].guarded);      // low confidence is not the ladder looking wrong
    QVERIFY(in.shots[8].guarded);       // not on its P-anchors
    int guarded = 0;
    for (const ConsistencyShot &shot : in.shots) guarded += shot.guarded ? 1 : 0;
    QCOMPARE(guarded, 2);

    // a guarded shot keeps its readings
    int finite = 0;
    for (const ConsistencyReading &r : in.readings)
        for (int j = 0; j < r.samples; ++j) finite += std::isfinite(r.at(2, j)) ? 1 : 0;
    QVERIFY(finite > 100);
}

void SessionConsistencyTest::timing5Oct()
{
    const charts_test::Fixture fx = charts_test::loadFixture(kData + QStringLiteral("/session_1005.json"));
    QVERIFY(fx.ok);
    const ConsistencyEnv env = consistencyEnvFromPacks();
    QElapsedTimer t;
    t.start();
    const SessionAlignment al = alignSession(fx.session);
    const qint64 alignMs = t.restart();
    const ConsistencyInput in = consistencyInput(fx.session, al, env);
    const qint64 layer1Ms = t.restart();
    const ConsistencyResult res = consistencyResult(in);
    const qint64 layer2Ms = t.elapsed();
    qInfo("5 Oct: %d shots, %d readings: align %lld ms, layer 1 %lld ms, layer 2 + shuffle %lld ms", int(in.shots.size()),
          int(in.readings.size()), alignMs, layer1Ms, layer2Ms);
    QCOMPARE(int(in.shots.size()), 31);
    QVERIFY(isfinite(in.afterImpactMs));
    QCOMPARE(int(res.cells.size()), 33);
    QVERIFY(res.shuffledMean > 0.0);
}

// The golden: goldens_1005_consistency.json is consistency_tally.py's `result` for a dump of session_1005.json itself,
// and its `home` block, which goldenHome reads.
void SessionConsistencyTest::golden5Oct()
{
    QFile f(kData + QStringLiteral("/goldens_1005_consistency.json"));
    QVERIFY2(f.open(QIODevice::ReadOnly), "goldens_1005_consistency.json is missing");
    const QJsonObject g = QJsonDocument::fromJson(f.readAll()).object();

    const charts_test::Fixture fx = charts_test::loadFixture(kData + QStringLiteral("/session_1005.json"));
    QVERIFY(fx.ok);
    const ConsistencyInput in = consistencyInput(fx.session, alignSession(fx.session), consistencyEnvFromPacks());
    const ConsistencyResult res = consistencyResult(in);
    const auto same = [](const QJsonValue &v, double x) {
        return v.isDouble() ? near(v.toDouble(), x, 1e-6) : std::isnan(x);
    };

    const QJsonArray cells = g.value("cells").toArray();
    QCOMPARE(int(res.cells.size()), int(cells.size()));
    for (qsizetype i = 0; i < cells.size(); ++i) {
        const QJsonObject o = cells[i].toObject();
        const ConsistencyCell &c = res.cells[size_t(i)];
        QCOMPARE(c.area, o.value("area").toString());
        QCOMPARE(c.column, o.value("column").toInt());
        QCOMPARE(consistencyWordToken(c.word), o.value("word").toString());
        QCOMPARE(c.judged, o.value("judged").toInt());
        QCOMPARE(c.loose, o.value("loose").toInt());
        QCOMPARE(c.nearError, o.value("nearError").toInt());
        QCOMPARE(c.unjudged, o.value("unjudged").toInt());
    }
    const QJsonArray readings = g.value("readings").toArray();
    QCOMPARE(int(res.readings.size()), int(readings.size()));
    for (qsizetype i = 0; i < readings.size(); ++i) {
        const QJsonObject o = readings[i].toObject();
        const ConsistencyJudged &r = res.readings[size_t(i)];
        QCOMPARE(r.key, o.value("key").toString());
        QCOMPARE(r.column, o.value("column").toInt());
        QVERIFY2(same(o.value("spread"), r.spread), qPrintable(r.key));
        QCOMPARE(consistencyVerdictToken(r.verdict), o.value("verdict").toString());
    }
    const QJsonArray standouts = g.value("standouts").toArray();
    QCOMPARE(int(res.standouts.size()), int(standouts.size()));
    for (qsizetype i = 0; i < standouts.size(); ++i) {
        const QJsonObject o = standouts[i].toObject();
        const ConsistencyStandOut &s = res.standouts[size_t(i)];
        QCOMPARE(s.ordinal, o.value("ordinal").toInt());
        QCOMPARE(s.key, o.value("key").toString());
        QCOMPARE(s.column, o.value("column").toInt());
        QCOMPARE(s.sample, o.value("k").toInt());
        QVERIFY2(same(o.value("z"), s.z) && same(o.value("dv"), s.dv), qPrintable(s.key));
    }
    const QJsonArray flags = g.value("flags").toArray();
    QCOMPARE(int(res.flags.size()), int(flags.size()));
    for (qsizetype i = 0; i < flags.size(); ++i) {
        const QJsonObject o = flags[i].toObject();
        QCOMPARE(res.flags[size_t(i)].ordinal, o.value("ordinal").toInt());
        QStringList want;
        for (const QJsonValue &v : o.value("flags").toArray()) want << v.toString();
        QCOMPARE(res.flags[size_t(i)].flags, want);
    }
    QVERIFY(same(g.value("shuffledMean"), res.shuffledMean));
    QVERIFY(same(g.value("p95"), res.p95));
}

// Home's word against the tally's. goldens_home_consistency.json holds the `result.cells` of the five dumps the golden's
// `home` block was rolled up from, Mark's five judged sessions newest first, session_1005.json's dump standing for 5 Oct.
// The tally rolled them up in Python from its own cells; --check holds those equal to these.
void SessionConsistencyTest::goldenHome()
{
    QFile cf(kData + QStringLiteral("/goldens_home_consistency.json"));
    QVERIFY2(cf.open(QIODevice::ReadOnly), "goldens_home_consistency.json is missing");
    QFile gf(kData + QStringLiteral("/goldens_1005_consistency.json"));
    QVERIFY2(gf.open(QIODevice::ReadOnly), "goldens_1005_consistency.json is missing");
    const QJsonObject home = QJsonDocument::fromJson(gf.readAll()).object().value("home").toObject();
    QCOMPARE(home.size(), consistencyAreas().size());

    std::vector<std::vector<ConsistencyCell>> sessions;
    QStringList names;
    for (const QJsonValue &sv : QJsonDocument::fromJson(cf.readAll()).object().value("sessions").toArray()) {
        names << sv.toObject().value("session").toString();
        sessions.emplace_back();
        for (const QJsonValue &cv : sv.toObject().value("cells").toArray()) {
            const QJsonObject o = cv.toObject();
            ConsistencyCell c;
            c.area = o.value("area").toString();
            c.column = o.value("column").toInt();
            c.word = consistencyWordFromToken(o.value("word").toString());
            sessions.back().push_back(c);
        }
    }
    QCOMPARE(int(sessions.size()), 5);
    QVERIFY(names.first().startsWith("2026-10-20") && names.last().startsWith("2026-07-04"));   // newest first

    for (const QString &area : consistencyAreas()) {
        const QJsonObject want = home.value(area).toObject();
        const ConsistencyHomeWord got = consistencyHomeWord(area, sessions);
        if (want.value("word").isNull()) {
            QCOMPARE(got.word, ConsistencyWord::Dash);
            continue;
        }
        QCOMPARE(consistencyWordToken(got.word), want.value("word").toString().toLower());
        QVERIFY2(near(got.score, want.value("score").toDouble(), 1e-9), qPrintable(area));
        QCOMPARE(consistencyColumnName(got.loosestColumn), want.value("loosest").toString());
    }

    // These are this fixture's own cells, so the two goldens cannot have been cut from different dumps.
    const charts_test::Fixture fx = charts_test::loadFixture(kData + QStringLiteral("/session_1005.json"));
    QVERIFY(fx.ok);
    const ConsistencyResult res = consistencyResult(consistencyInput(fx.session, alignSession(fx.session), consistencyEnvFromPacks()));
    const std::vector<ConsistencyCell> &oct5 = sessions[size_t(names.indexOf(fx.name))];
    QCOMPARE(oct5.size(), res.cells.size());
    for (size_t i = 0; i < oct5.size(); ++i) QCOMPARE(oct5[i].word, res.cells[i].word);
}

QTEST_GUILESS_MAIN(SessionConsistencyTest)
#include "session_consistency_test.moc"
