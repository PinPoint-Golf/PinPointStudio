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

#include "charts_consistency_presenter.h"

#include "charts_gadget.h"            // chartsGadgetMap: the report's maps, off the typed results
#include "charts_words.h"
#include "consistency_env.h"
#include "../../Diagnostics/derived/series/consistency_grades.h"
#include "../../Diagnostics/derived/series/session_consistency.h"
#include "session_series_extract.h"   // extractShot, folderShotId (refactor 5.5a)
#include "../../Stores/series_store.h"   // seriesSwingLabelOf: the ★ label
#include "variability_env.h"

#include "../variability/variability_facade.h"           // VariabilityResultView, typicalityPayload
#include "../../Vocabulary/Metrics/variability/variability_outcome.h"       // isOutcomeMetric (R4)
#include "../../Vocabulary/Metrics/variability/variability_typicality.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/model/norm.h"           // the grade policy
#include "../../Diagnostics/content/norm_provider.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <map>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;
namespace pv = pinpoint::variability;
using words::fin;
using words::Sentence;

// ── the areas' words ────────────────────────────────────────────────────────────────────────

struct AreaWords { const char *id, *name, *phrase; bool plural; };
const AreaWords kAreaWords[] = {
    { "setup",     "Setup",               "setup",                      false },
    { "posture",   "Posture",             "posture",                    false },
    { "lateral",   "Lateral and balance", "lateral motion and balance", true  },
    { "rotation",  "Turn and sequence",   "turn and sequence",          true  },
    { "arms",      "Arms",                "arms",                       true  },
    { "wrists",    "Wrists and release",  "wrists",                     true  },
    { "planePath", "Plane and path",      "plane and path",             true  },
    { "delivery",  "Delivery",            "delivery at impact",         false },
};

const AreaWords &areaWords(const QString &id)
{
    for (const AreaWords &a : kAreaWords) if (id == QLatin1String(a.id)) return a;
    return kAreaWords[0];
}

// "your delivery at impact moves most", "your arms move most".
QString movesMost(const QString &area)
{
    const AreaWords &a = areaWords(area);
    return QStringLiteral("your ") + QString::fromUtf8(a.phrase) + (a.plural ? QStringLiteral(" move most") : QStringLiteral(" moves most"));
}

QString clubWord(const QString &club) { return words::cap1(club.toLower()); }

QString cleanDir(const QString &d) { return d.trimmed().isEmpty() ? QString() : QDir::cleanPath(d.trimmed()); }

// ── the shots, by club ──────────────────────────────────────────────────────────────────────

// The clubs in the order they were first hit, with how many shots each had.
std::vector<std::pair<QString, int>> clubCounts(const ConsistencyInput &in)
{
    std::vector<std::pair<QString, int>> out;
    for (const ConsistencyShot &s : in.shots) {
        const auto it = std::find_if(out.begin(), out.end(), [&](const auto &c) { return c.first == s.club; });
        if (it == out.end()) out.push_back({ s.club, 1 }); else ++it->second;
    }
    return out;
}

std::vector<double> clubValues(const ConsistencyInput &in, const QString &club, double ConsistencyShot::*field)
{
    std::vector<double> out;
    for (const ConsistencyShot &s : in.shots)
        if (s.club == club && fin(s.*field)) out.push_back(s.*field);
    return out;
}

bool anyValue(const ConsistencyInput &in, double ConsistencyShot::*field)
{
    return std::any_of(in.shots.begin(), in.shots.end(), [&](const ConsistencyShot &s) { return fin(s.*field); });
}

QString lmUnit(const SessionSnapshot &s, const QString &key)
{
    for (const auto &sh : s.shots)
        if (const MetricSeries *m = sh->metric(key)) return m->unit;
    return QStringLiteral("yd");
}

// Camera means a curve. A launch monitor's figures can yield a swing reading (lowPointFromAttack), but no curve.
bool hasCurve(const ShotSeries &shot)
{
    for (auto it = shot.metrics.constBegin(); it != shot.metrics.constEnd(); ++it)
        if (!it.key().startsWith(QLatin1String("lm.")) && it.value().hasCurve()) return true;
    return false;
}

bool hasCurves(const SessionSnapshot &session)
{
    return std::any_of(session.shots.begin(), session.shots.end(), [](const auto &sh) { return hasCurve(*sh); });
}

// A Sentence as the typed results carry it: the same { text, runs } its toVariant() gave.
QList<ConsistencyTabRun> runsOf(const Sentence &s)
{
    QList<ConsistencyTabRun> out;
    out.reserve(qsizetype(s.runs.size()));
    for (const Sentence::Run &r : s.runs) out.append(ConsistencyTabRun{ r.text, r.fig });
    return out;
}
ConsistencyTabSentence sentenceOf(const Sentence &s) { return ConsistencyTabSentence{ s.text(), runsOf(s) }; }

QString loadingWords() { return QStringLiteral("Reading this session…"); }
ConsistencyTabSentence settlingLine() { return sentenceOf(Sentence().append(loadingWords())); }

// ── the grades, as QML reads them ─────────────────────────────────────────────────────────

// A result as PpVariabilityBadge reads it: the façade's variabilityResult (variability_facade.h), as a map, because it
// travels inside a payload. Every field is the view's own.
QVariantMap resultMap(const pv::VariabilityResultView &v) { return v.toMap(); }

// The unit a basis string sets ("ratio" is no unit word), and its places: a figure at the row's own decimals, a ratio
// two. A σ takes as many more as two significant figures need (the basis builder's one rule, variability_result.cpp).
QString basisUnit(const QString &unit)
{
    const QString u = words::shortUnit(unit);
    return u == QLatin1String("ratio") ? QString() : u;
}
int valueDecimals(const QString &unit)
{
    const QString u = words::shortUnit(unit);
    return u == QLatin1String("ratio") ? 2 : words::decimalsFor(u);
}

pv::VariabilityResultView readingResult(const ConsistencyReading &r, const pv::VariabilityResult &res)
{
    return pv::VariabilityResultView::fromResult(res, basisUnit(r.unit), valueDecimals(r.unit));
}

// R8: this shot against the session's other shots with its club, itself left out, at least N_MIN of them. `values` is
// one per shot in strike order: the ROW's own (its `values`, what the row draws: a stand-out's point on this shot's
// club's scale, a launch-monitor row's club values), so the badge speaks of the numbers beside it. A guarded shot is
// not in the window of a reading that rests on its P-ladder (`ladder`: consistencyGuardCovers), as it is not in the
// grade; this shot is compared whatever it is. An invalid QVariant with fewer: no typicality badge, and the row stays as
// it was. Else a RelationsVariability (kind "typicality", the one builder Charts' rows take too), its corridor not shown.
QVariant typicalityOf(const ConsistencyInput &in, int shot, const std::vector<double> &values, const QString &unit,
                      pv::ScaleType type, bool ladder)
{
    std::vector<pv::TypicalitySwing> swings;
    swings.reserve(in.shots.size());
    for (size_t i = 0; i < in.shots.size(); ++i) {
        const bool counted = int(i) == shot || !(ladder && in.shots[i].guarded);
        swings.push_back(pv::TypicalitySwing{ QString::number(in.shots[i].ordinal), in.shots[i].club,
                                              counted && i < values.size() ? values[i] : kNaN });
    }
    const auto window = pv::scalarTypicalityWindow(swings, QString::number(in.shots[size_t(shot)].ordinal), type);
    if (!window) return {};
    const pv::TypicalityPayload p = pv::typicalityPayload(*window, basisUnit(unit), valueDecimals(unit));
    return QVariant::fromValue(relationsTypicality(*window, p.basisText, false));
}

// A row's own `values` (one per shot, strike order; NaN where a shot has none), as numbers.
std::vector<double> rowValues(const RelationsReadingRow &row)
{
    return std::vector<double>(row.values.cbegin(), row.values.cend());
}

pv::ScaleType scaleTypeOf(const ConsistencyGradeScales &scales, const QString &key)
{
    return scales.scaleType ? scales.scaleType(key) : pv::ScaleType::Interval;
}

// ── the head ────────────────────────────────────────────────────────────────────────────────

// "5 yd left". Rounds to no offline at all: "on line".
QString sideWords(double v, const QString &unit)
{
    const QString f = words::fig(std::fabs(v), words::decimalsFor(unit));
    if (f == words::fig(0.0, words::decimalsFor(unit))) return QStringLiteral("on line");
    return words::withUnit(f, unit) + (v < 0 ? QStringLiteral(" left") : QStringLiteral(" right"));   // lm.offline: + is right
}

// "1 to 9 yd left", nearest the line first; across the line "3 yd left to 4 yd right". An end that rounds to the line
// says so: "on line to 2 yd left", and "on line" when both do.
QString sideRange(double q1, double q3, const QString &unit)
{
    const QString line = QStringLiteral("on line");
    const QString low = sideWords(q1, unit), high = sideWords(q3, unit);
    if (low == line && high == line) return line;
    if (q1 < 0 && q3 > 0) return low + QStringLiteral(" to ") + high;
    const double nearer = std::min(std::fabs(q1), std::fabs(q3)), farther = std::max(std::fabs(q1), std::fabs(q3));
    const QString far = sideWords(q1 < 0 ? -farther : farther, unit);
    if (sideWords(nearer, unit) == line) return line + QStringLiteral(" to ") + far;
    return words::fig(nearer, words::decimalsFor(unit)) + QStringLiteral(" to ") + far;
}

// The middle half is a club's own: with several clubs, the one with the most carries, and the line says which.
Sentence ballLine(const SessionSnapshot &session, const ConsistencyInput &in)
{
    Sentence s;
    QString club;
    size_t most = 0;
    const auto clubs = clubCounts(in);
    for (const auto &c : clubs) {
        const size_t n = clubValues(in, c.first, &ConsistencyShot::carry).size();
        if (n > most) { most = n; club = c.first; }
    }
    if (int(most) < kConsistencyMinClubShots) return s;
    const std::vector<double> carry = clubValues(in, club, &ConsistencyShot::carry);
    const QString yd = lmUnit(session, QStringLiteral("lm.carryDistance"));
    s.append(clubs.size() > 1 && !club.isEmpty() ? QStringLiteral("Your ball with the ") + club.toLower() + QStringLiteral(": carry ")
                                                 : QStringLiteral("Your ball: carry "));
    s.figure(words::figUnit(medianOf(carry), yd)).append(QStringLiteral(", middle half "));
    s.figure(words::fig(quantile7(carry, 0.25), words::decimalsFor(yd)) + QStringLiteral(" to ") + words::figUnit(quantile7(carry, 0.75), yd));
    const std::vector<double> offline = clubValues(in, club, &ConsistencyShot::offline);
    if (int(offline.size()) >= kConsistencyMinClubShots) {
        const QString off = lmUnit(session, QStringLiteral("lm.offline"));
        s.append(QStringLiteral(". It finishes ")).figure(sideWords(medianOf(offline), off)).append(QStringLiteral(", middle half "));
        s.figure(sideRange(quantile7(offline, 0.25), quantile7(offline, 0.75), off));
    }
    return s.append(QStringLiteral("."));
}

ConsistencyTabHead headOf(const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyGrades &grades, const Sentence &empty,
                          const QString &noun = QStringLiteral("shots"))
{
    const auto clubs = clubCounts(in);
    QString kicker = QStringLiteral("Consistency  ·  %1 %2").arg(int(in.shots.size())).arg(words::plural(int(in.shots.size()), noun.chopped(1), noun));
    if (clubs.size() > 1) kicker += QStringLiteral("  ·  mixed clubs");
    else if (clubs.size() == 1 && !clubs.front().first.isEmpty()) kicker += QStringLiteral("  ·  ") + clubWord(clubs.front().first);

    // the grades' sentence when the table has one; else what the table cannot say; else the grade-0 or ungraded line
    const ConsistencyHead graded = consistencyHead(grades);
    const bool says = graded.kind == ConsistencyHead::Kind::Halves || graded.kind == ConsistencyHead::Kind::Most;
    ConsistencyTabHeadline line = ChartsConsistencyPresenter::headline(graded);
    if (!says && !empty.isEmpty()) {
        line = ConsistencyTabHeadline{};        // the empty sentence in its place: no grades, no fallback
        line.text = empty.text();
        line.runs = runsOf(empty);
        line.kind = QStringLiteral("empty");
    }

    QList<ConsistencyTabSentence> notes;
    if (anyValue(in, &ConsistencyShot::carry) && !anyValue(in, &ConsistencyShot::offline))
        notes.append(sentenceOf(Sentence().append(QStringLiteral("This launch monitor gives carry but no offline, so only short shots are flagged."))));
    std::vector<std::pair<QString, int>> left;
    for (const auto &c : clubs)
        if (c.second < kConsistencyMinClubShots) left.push_back(c);
    if (!left.empty() && clubs.size() > 1) {
        Sentence note;
        note.append(QStringLiteral("Left out of the table: "));
        for (size_t i = 0; i < left.size(); ++i) {
            if (i > 0) note.append(i + 1 == left.size() ? QStringLiteral(" and ") : QStringLiteral(", "));
            note.append((left[i].first.isEmpty() ? QStringLiteral("shots with no club") : QStringLiteral("the ") + left[i].first.toLower()) + QStringLiteral(" ("));
            note.figure(QString::number(left[i].second)).append(words::plural(left[i].second, QStringLiteral(" shot)"), QStringLiteral(" shots)")));
        }
        note.append(QStringLiteral(". A club needs %1 shots to be set against itself.").arg(words::word(kConsistencyMinClubShots)));
        notes.append(sentenceOf(note));
    }

    ConsistencyTabHead head;
    head.kicker = kicker;
    head.headline = line;
    head.ball = sentenceOf(ballLine(session, in));      // no runs with no carry
    head.notes = notes;
    return head;
}

// What the table cannot say, and its kind; an empty kind when it can.
Sentence emptySentence(const SessionSnapshot &session, const ConsistencyInput &in, QString *kind)
{
    Sentence s;
    if (in.shots.empty()) {
        *kind = session.sessionDir.isEmpty() ? QStringLiteral("noSession") : QStringLiteral("noShots");
        return s.append(session.sessionDir.isEmpty() ? QStringLiteral("No session is loaded.")
                                                     : QStringLiteral("No shot in this session has been analysed yet."));
    }
    if (!hasCurves(session)) {
        *kind = QStringLiteral("noCamera");
        return s.append(QStringLiteral("This session has no camera shots, so only launch monitor readings can be judged."));
    }
    int compared = 0;
    for (const auto &c : clubCounts(in)) if (c.second >= kConsistencyMinClubShots) compared += c.second;
    if (compared < pv::kNMin) {
        *kind = QStringLiteral("tooFew");
        return s.append(QStringLiteral("Most of this table can't be graded yet: a reading needs %1 shots with one club to show how much it moves.")
                            .arg(words::word(pv::kNMin)));
    }
    return s;
}

// ── the table ───────────────────────────────────────────────────────────────────────────────

// R1: a cell takes cellGrade() over its readings and shows that grade's word alone, which QML takes from the
// vocabularies (`result`); the marks go on the readings it opens to. `text` is "-" for a cell with no graded reading
// and "" for a graded one (QML's word goes there); a column that does not apply to the area is blank. The short form is
// the grade's figure ("4"), "⚠" for a measurement-limited cell ("Checking"), "-" ungraded; keyMarks says which is which.
// The heat says how much, as before: grades 1-2 the most, grade 3 some, nothing else.
// The word's vocabulary (C1, refactor 5.5b): which vocabulary names a graded cell, and at which grade, is decided here;
// QML only looks the word up (charts_consistency_types.h). A measurement-limited cell is the Measurement grade 0.
ConsistencyTabCell cellOf(const ConsistencyCellGrade *g, int column, bool on, bool marked, bool screen)
{
    ConsistencyTabCell cell;
    if (!g) return cell;                // does not apply: "" in both texts, nothing else set
    const pv::CellGrade &c = g->grade;
    const bool limited = c.graded && c.measurementLimited;
    const bool moved = c.graded && !limited && c.grade >= 1 && c.grade <= 3;
    const bool most = moved && c.grade <= 2;
    cell.text = c.graded ? QString() : QStringLiteral("-");
    cell.shortText = !c.graded ? QStringLiteral("-") : limited ? QStringLiteral("⚠") : QString::number(c.grade);
    cell.strong = moved;
    cell.heavy = most;
    cell.heat = most ? 2 : moved ? 1 : 0;
    cell.soft = column == kConsistencyAfterBallColumn && !g->afterBall;
    cell.on = on;
    cell.mark = marked ? QStringLiteral("◆") : QString();
    cell.tap = screen && !g->readings.empty();
    cell.result = pv::VariabilityResultView::fromCellGrade(c);
    cell.vocabulary = !c.graded ? QString() : limited ? QStringLiteral("measurement") : QStringLiteral("consistency");
    cell.vocabularyGrade = c.graded && !limited ? c.grade : 0;
    return cell;
}

ConsistencyTabMatrix matrixOf(const ConsistencyInput &in, const ConsistencyResult &res, const ConsistencyGrades &grades, int ordinal,
                              const QString &openCell, bool screen)
{
    ConsistencyTabMatrix matrix;
    for (int c = 0; c < kConsistencyColumns; ++c) {
        const QString name = consistencyColumnName(c);
        matrix.columns.append(ConsistencyTabColumn{ name, c == 0 ? name : QString(name).remove(QLatin1Char('P')) });
    }
    std::map<std::pair<QString, int>, bool> marked;
    for (const ConsistencyStandOut &s : res.standouts)
        if (s.ordinal == ordinal) marked[{ in.readings[size_t(s.reading)].area, s.column }] = true;
    for (const QString &area : consistencyAreas()) {
        ConsistencyTabRow row;
        row.id = area;
        row.name = QString::fromUtf8(areaWords(area).name);
        for (int c = 0; c < kConsistencyColumns; ++c)
            row.cells.append(cellOf(grades.cellAt(area, c), c, openCell == area + QLatin1Char(':') + QString::number(c),
                                    marked.count({ area, c }) > 0, screen));
        matrix.rows.append(row);
    }
    return matrix;
}

// ── the report's maps, off the typed results (until 5.6 types the report) ───────────────────────

// The shape the report's payload had before 5.5b: a column that does not apply had its two texts alone, a result its
// façade map (ρ null when there is none), and no cell named its vocabulary (the report's page reads the result).
QVariantMap reportCellMap(const ConsistencyTabCell &c)
{
    if (c.shortText.isEmpty()) return { { QStringLiteral("text"), c.text }, { QStringLiteral("short"), c.shortText } };
    QVariantMap m = chartsGadgetMap(c);
    m.remove(QStringLiteral("vocabulary"));
    m.remove(QStringLiteral("vocabularyGrade"));
    m.insert(QStringLiteral("result"), resultMap(c.result));
    return m;
}

QVariantMap reportMatrixMap(const ConsistencyTabMatrix &matrix)
{
    QVariantList rows;
    for (const ConsistencyTabRow &r : matrix.rows) {
        QVariantList cells;
        for (const ConsistencyTabCell &c : r.cells) cells.append(reportCellMap(c));
        QVariantMap row = chartsGadgetMap(r);
        row.insert(QStringLiteral("cells"), cells);
        rows.append(row);
    }
    QVariantMap m = chartsGadgetMap(matrix);
    m.insert(QStringLiteral("rows"), rows);
    return m;
}

// The headline as before: "empty" was the empty sentence and its kind alone.
QVariantMap reportHeadlineMap(const ConsistencyTabHeadline &line)
{
    QVariantMap m = chartsGadgetMap(line);
    if (line.kind == QLatin1String("empty")) {
        m.remove(QStringLiteral("grades"));
        m.remove(QStringLiteral("fallback"));
    }
    return m;
}

// A sentence as before: {} when there is none.
QVariantMap reportSentenceMap(const ConsistencyTabSentence &s)
{
    return s.runs.isEmpty() ? QVariantMap{} : chartsGadgetMap(s);
}

// The key and the marks' key are TEMPLATES (the header): QML puts the vocabularies' words in, %1 to %5 the Consistency
// words of grades 5 to 1, %6 its grade 0 (the table's form, "Too few", R33: the key names what the cells show), %7 the
// Measurement word of grade 0. Nothing here spells one.
// `picks`: a shot can be picked here, so a cell can carry the ◆. The report has no picked shot.
QString keyWords(bool picks = true)
{
    return QStringLiteral("A cell takes the grade of the third of its readings that move most: %1, %2, %3, %4 or %5, from least "
                          "movement to most. %6: fewer than ") + words::word(pv::kNMin)
           + QStringLiteral(" swings to grade it. %7: it moves no more than the reading's own measurement error. \"-\": nothing "
                            "here has a range to judge it against. P7-P8 is after the ball has gone. It is read over the same ")
           + words::figMs(kConsistencyAfterImpactMs)
           + QStringLiteral(" after impact on every shot, and is shown lighter. A blank does not apply to that area.")
           + (picks ? QStringLiteral(" ◆ this shot left your pattern here.") : QString());
}

QString keyMarks(bool picks = true)
{
    return QStringLiteral("5 %1   4 %2   3 %3   2 %4   1 %5   0 %6   ⚠ %7")
           + (picks ? QStringLiteral("   ◆ this shot left your pattern here") : QString());
}

// ── a cell, opened ──────────────────────────────────────────────────────────────────────────

// The readings behind a cell's word, widest spread first (ρ), then those under N_MIN, then those with no scale. Each
// row is the shared READING ROW with its grade (`variability`, PpChProfile's badge slot).
ConsistencyTabCellDetail cellDetail(const ConsistencyReadingRows &rows, const ConsistencyGrades &grades, const QString &openCell, int pick,
                                    const QString &shotLabel, bool screen)
{
    const int colon = openCell.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0) return {};
    const QString area = openCell.left(colon);
    bool ok = false;
    const int column = openCell.mid(colon + 1).toInt(&ok);
    const ConsistencyInput &in = rows.input();
    const ConsistencyCellGrade *g = ok ? grades.cellAt(area, column) : nullptr;
    if (!g) return {};

    const auto rank = [&](int r) {
        const pv::VariabilityResult &v = grades.readings[size_t(r)];
        return v.graded && v.grade >= 1 && std::isfinite(v.rho) ? 0 : v.graded ? 1 : 2;
    };
    std::vector<int> list = g->readings;
    std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return rank(a) == 0 && grades.readings[size_t(a)].rho > grades.readings[size_t(b)].rho;
    });

    const bool guarded = pick >= 0 && in.shots[size_t(pick)].guarded;
    int gradedN = 0, tooFew = 0, ungraded = 0, limited = 0, notCounted = 0;
    QList<RelationsReadingRow> out;
    for (int r : list) {
        const ConsistencyReading &reading = in.readings[size_t(r)];
        const pv::VariabilityResult &v = grades.readings[size_t(r)];
        const int k = rank(r);
        gradedN += k == 0 ? 1 : 0;
        tooFew += k == 1 ? 1 : 0;
        ungraded += k == 2 ? 1 : 0;
        limited += k == 0 && v.measurementLimited ? 1 : 0;

        ConsistencyJudged judged{ reading.key, reading.column, r };
        for (const ConsistencyJudged &j : rows.result().readings) if (j.reading == r) { judged = j; break; }
        RelationsReadingRow row = rows.readingRow(pick, judged);
        // doc §4.3: a guarded shot keeps its readings with a ⚠. Its value here rests on a ladder that cannot be trusted
        if (guarded && consistencyGuardCovers(reading) && row.measured) {
            const QString had = row.cautionText;
            row.caution = true;
            row.cautionText = had.isEmpty() ? QStringLiteral("⚠ ") + words::consistencyGuardWords()
                                            : had + QStringLiteral("; ") + words::consistencyGuardWords();
        }
        RelationsVariability grade;
        grade.kind = QStringLiteral("consistency");
        grade.result = readingResult(reading, v);
        row.variability = QVariant::fromValue(grade);
        row.graded = v.graded;
        // A mixed P7-P8 cell: this reading is shown but the cell's grade is the after-ball readings' (consistency_grades.h).
        // The row says so in two words; the sub-line below says it once in full.
        const bool soft = g->afterBall && !reading.afterBall;
        row.soft = soft;
        if (soft) {
            row.softWord = QStringLiteral("not counted");
            ++notCounted;
        }
        out.append(row);
    }
    const int total = int(list.size());
    Sentence sub;
    if (gradedN > 0) {
        sub.figure(QString::number(gradedN)).append(QStringLiteral(" of ")).figure(QString::number(total));
        sub.append(words::plural(total, QStringLiteral(" reading graded"), QStringLiteral(" readings graded")));
    } else if (tooFew > 0) sub.append(QStringLiteral("too few swings to grade a reading here"));
    else sub.append(QStringLiteral("nothing here could be judged"));
    if (limited > 0) sub.append(QStringLiteral("; ")).figure(QString::number(limited)).append(QStringLiteral(" measurement-limited"));
    if (notCounted > 0) sub.append(QStringLiteral("; ")).figure(QString::number(notCounted)).append(QStringLiteral(" shown but not counted after the ball"));
    if (screen && !out.isEmpty()) sub.append(QStringLiteral("; a row opens its curve"));

    const QString name = QString::fromUtf8(areaWords(area).name);
    const QString tag = consistencyReadsAtImpact(area) ? QStringLiteral("P7") : consistencyColumnName(column);
    const int n = int(in.shots.size());
    ConsistencyTabCellDetail cell;
    cell.area = name;
    cell.areaId = area;
    cell.column = column;
    cell.tag = tag;
    cell.result = pv::VariabilityResultView::fromCellGrade(g->grade);
    cell.title = name + QStringLiteral(", ") + tag;
    cell.sub = relationsSentence(sub);
    cell.rows = out;
    cell.unjudged = ungraded;
    cell.unjudgedNote = ungraded ? QStringLiteral("%1 %2 no range to be judged against, and no grade.")
                                       .arg(ungraded).arg(ungraded == 1 ? QStringLiteral("reading has") : QStringLiteral("readings have"))
                                 : QString();
    cell.headLabel = QStringLiteral("Widest spread first");
    cell.shotLabel = shotLabel;
    cell.countLabel = QStringLiteral("%1 %2").arg(n).arg(words::plural(n, QStringLiteral("shot"), QStringLiteral("shots")));
    return cell;
}

// ── the picked shot ─────────────────────────────────────────────────────────────────────────

QStringList flagsOf(const ConsistencyResult &res, int ordinal)
{
    for (const ConsistencyBallFlag &f : res.flags) if (f.ordinal == ordinal) return f.flags;
    return {};
}

// How many readings a usual shot has: the median of 1 is "one or two", of 2.5 is "two or three".
QString usualCount(double median)
{
    const int lo = int(std::floor(median));
    return (lo == 0 ? QStringLiteral("none") : words::word(lo)) + QStringLiteral(" or ") + words::word(lo + 1);
}

double medianStandOuts(const ConsistencyInput &in, const ConsistencyResult &res)
{
    std::vector<double> counts(in.shots.size(), 0.0);
    for (size_t i = 0; i < in.shots.size(); ++i)
        for (const ConsistencyStandOut &s : res.standouts) if (s.ordinal == in.shots[i].ordinal) counts[i] += 1.0;
    return medianOf(std::move(counts));
}

// What the launch monitor made of it, in a line. "" with no launch monitor on this session.
QString lmWords(const ConsistencyInput &in, const ConsistencyResult &res, const ConsistencyShot &shot)
{
    const bool anyCarry = anyValue(in, &ConsistencyShot::carry), anyOffline = anyValue(in, &ConsistencyShot::offline);
    if (!anyCarry && !anyOffline) return QString();
    const QString lead = QStringLiteral("launch monitor: ");
    const QStringList flags = flagsOf(res, shot.ordinal);
    if (!flags.isEmpty()) return lead + words::list(flags);
    if (!fin(shot.carry) && !fin(shot.offline)) return lead + QStringLiteral("no reading of this shot");
    // only what the fences could say of this shot: its flags, not a verdict on it
    const bool shortKnown = fin(shot.carry) && int(clubValues(in, shot.club, &ConsistencyShot::carry).size()) >= kConsistencyMinBallShots;
    const bool offlineKnown = fin(shot.offline) && int(clubValues(in, shot.club, &ConsistencyShot::offline).size()) >= kConsistencyMinBallShots;
    if (!shortKnown && !offlineKnown) return lead + QStringLiteral("too few shots with this club to say");
    return lead + (shortKnown && offlineKnown ? QStringLiteral("not short or offline") : shortKnown ? QStringLiteral("not short") : QStringLiteral("not offline"));
}

// The ball's half of the sentence. With a typicality for the carry (R8: eight or more other shots of its club), the
// badge beside the sentence says how it compares, so the sentence gives the carry alone (R11); with fewer, the carry
// against its club's median, as long as the club has three.
void ballSentence(Sentence &s, const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyResult &res,
                  const ConsistencyShot &shot, bool typical)
{
    const QString yd = lmUnit(session, QStringLiteral("lm.carryDistance"));
    if (fin(shot.carry)) {
        const std::vector<double> carry = clubValues(in, shot.club, &ConsistencyShot::carry);
        s.append(QStringLiteral("Carry ")).figure(words::figUnit(shot.carry, yd));
        if (!typical && int(carry.size()) >= kConsistencyMinClubShots)
            s.append(QStringLiteral(" against a median of ")).figure(words::figUnit(medianOf(carry), yd));
        s.append(QStringLiteral(". "));
    }
    if (flagsOf(res, shot.ordinal).contains(QStringLiteral("offline")))
        s.append(QStringLiteral("It finished ")).figure(sideWords(shot.offline, lmUnit(session, QStringLiteral("lm.offline")))).append(QStringLiteral(". "));
}

ConsistencyTabShotDetail shotDetail(const ConsistencyReadingRows &rows, const ConsistencyGradeScales &scales, int pick, bool screen)
{
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();
    ConsistencyTabShotDetail out;
    if (pick < 0) {
        out.available = false;
        out.note = screen ? QStringLiteral("Pick a shot to see what stood out on it.") : QString();
        return out;
    }

    const ConsistencyShot &shot = in.shots[size_t(pick)];
    const std::vector<ConsistencyLmRow> &lm = rows.lmRows(pick);
    QList<RelationsReadingRow> ball, club, swing;
    for (const ConsistencyLmRow &r : lm) {
        RelationsReadingRow row = rows.lmRow(pick, r);
        row.label = QString(r.label).remove(QStringLiteral(" (measured)"));   // the group says whose reading it is
        // Ball flight has no badge at all, typicality included (R4): only a club-delivery row carries one.
        if (!pv::isOutcomeMetric(r.key)) {
            const QVariant t = typicalityOf(in, pick, rowValues(row), r.unit, scaleTypeOf(scales, r.key), false);
            if (t.isValid()) row.variability = t;
        }
        (r.ball ? ball : club).append(row);
    }
    for (const ConsistencyStandOut &s : res.standouts) {
        if (s.ordinal != shot.ordinal) continue;
        RelationsReadingRow row = rows.standOutRow(pick, s);
        const ConsistencyReading &reading = in.readings[size_t(s.reading)];
        const QVariant t = typicalityOf(in, pick, rowValues(row), reading.unit, scaleTypeOf(scales, reading.key),
                                        consistencyGuardCovers(reading));
        if (t.isValid()) row.variability = t;
        swing.append(row);
    }
    QList<RelationsRowGroup> groups;
    const auto group = [&](const QString &name, const QList<RelationsReadingRow> &list) {
        if (list.isEmpty()) return;
        RelationsRowGroup g;
        g.name = name;
        g.rows = list;
        groups.append(g);
    };
    group(QStringLiteral("The ball"), ball);
    group(QStringLiteral("The club"), club);
    group(QStringLiteral("Your swing"), swing);

    // the carry is ball flight: no typicality beside the sentence (R4, "Ball flight has no badge at all"), so the
    // sentence sets the carry against its club's median; `carry` stays null

    // the sentence (doc §4.3): the count said for what it means
    Sentence s;
    ballSentence(s, rows.session(), in, res, shot, false);
    const int n = int(swing.size());
    const bool measured = hasCurve(*rows.session().shots[size_t(pick)]);
    if (n == 0) {
        const QString cannot = rows.cannotCompare(pick, true);
        if (!measured) s.append(QStringLiteral("This shot has no camera readings, so there is no swing to set against the others."));
        else if (!cannot.isEmpty()) s.append(cannot);
        else {
            // a guarded shot's spans were not counted, so its silence is about its positions alone
            s.append(shot.guarded ? QStringLiteral("The spans of this swing can't be compared with your other shots. Nothing at its positions stood out.")
                                  : QStringLiteral("Nothing in your swing stood out."));
            if (!lm.empty()) s.append(QStringLiteral(" The ball and club readings below did."));
        }
    } else {
        s.figure(QString::number(n)).append(n == 1 ? QStringLiteral(" swing reading stood out") : QStringLiteral(" swing readings stood out"));
        const double usual = medianStandOuts(in, res);
        if (double(n) > res.p95) s.append(QStringLiteral(", more than chance would give."));
        else if (double(n) > usual) s.append(QStringLiteral(", more than most shots this session, but chance can reach that."));
        else s.append(QStringLiteral(". Most shots this session show ") + usualCount(usual) + QStringLiteral(", so this may be chance."));
    }

    const int total = int(in.shots.size()), count = int(ball.size() + club.size()) + n;
    out.available = true;
    out.ordinal = shot.ordinal;
    out.kicker = words::shotOfText(shot.shotId, shot.ordinal, total)
                 + (shot.club.isEmpty() ? QString() : QStringLiteral("  ·  ") + shot.club.toLower());
    out.shotLabel = QStringLiteral("Shot %1").arg(words::shotNumber(shot.shotId, shot.ordinal));
    out.countLabel = QStringLiteral("%1 %2").arg(total).arg(words::plural(total, QStringLiteral("shot"), QStringLiteral("shots")));
    out.headLabel = QStringLiteral("What stood out");
    out.lm = lmWords(in, res, shot);
    out.sentence = relationsSentence(s);
    out.guarded = shot.guarded;
    out.guardNote = shot.guarded ? ConsistencyReadingRows::guardNote() : QString();
    out.groups = groups;
    out.count = count;
    out.note = count >= 2 ? QStringLiteral("These happened on the same shot. That does not show one caused another.") : QString();
    return out;
}

// The grade policy's fault line, as Charts takes it.
double faultZOf(const pa::GradePolicy &policy)
{
    return policy.idealMaxZ > 0.0 ? policy.watchMaxZ / policy.idealMaxZ : 3.0;
}

QList<ConsistencyTabShot> shotsList(const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyResult &res)
{
    QList<ConsistencyTabShot> out;
    out.reserve(qsizetype(in.shots.size()));
    for (size_t i = 0; i < in.shots.size(); ++i) {
        const ConsistencyShot &s = in.shots[i];
        const ShotSeries &sh = *session.shots[i];
        out.append(ConsistencyTabShot{ s.ordinal, sh.shotId, sh.swingDir, s.club, flagsOf(res, s.ordinal), s.guarded });
    }
    return out;
}

} // namespace

// ── the worker's cache and the published output ─────────────────────────────────────────────

struct ChartsConsistencyPresenter::Cache {
    ConsistencyReadingRows rows;
    ConsistencyGrades grades;           // the readings and cells graded (consistency_grades.h): once a snapshot
    // what depends on the session alone: made once a snapshot
    ConsistencyTabHead head;
    ConsistencyTabEmpty empty;
    QList<ConsistencyTabShot> shots;
    QString key;
};

struct ChartsConsistencyPresenter::Output {
    bool ready = false;
    ConsistencyTabStatus status;
    ConsistencyTabHead head;
    ConsistencyTabMatrix matrix;
    ConsistencyTabCellDetail cell;      // READING ROWs inside (RelationsReadingRow, refactor 5.5c)
    ConsistencyTabShotDetail shot;
    ConsistencyTabEmpty empty;
    QList<ConsistencyTabShot> shots;
    QString key, keyMarks;
    const void *thread = nullptr;
    double jobMs = 0;
};

static std::shared_ptr<ChartsConsistencyPresenter::Output>
computeConsistency(const std::shared_ptr<const SessionSnapshot> &snapshot, const QString &pickedRaw, int pickedOrdinal,
                   const QString &openCell, bool screen, bool loading, const ConsistencyEnv &env, double faultZ, const pa::GradePolicy &policy,
                   const std::shared_ptr<const pa::INormProvider> &norms, const IVariabilityScaleProvider &provider,
                   ChartsConsistencyPresenter::Cache &cache)
{
    QElapsedTimer timer;
    timer.start();
    auto out = std::make_shared<ChartsConsistencyPresenter::Output>();
    ConsistencyReadingRows &rows = cache.rows;
    const bool fresh = rows.update(snapshot, env, faultZ, policy, norms);
    const SessionSnapshot &S = rows.session();
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();
    const ConsistencyGradeScales scales = consistencyGradeScales(provider);
    if (fresh) {
        cache.grades = consistencyGrades(in, scales);
        QString kind;
        const Sentence empty = emptySentence(S, in, &kind);
        cache.empty = kind.isEmpty() ? ConsistencyTabEmpty{} : ConsistencyTabEmpty{ kind, sentenceOf(empty) };
        cache.head = headOf(S, in, cache.grades, empty);
        cache.shots = shotsList(S, in, res);
        cache.key = keyWords();
    }

    // the picked shot: the swing folder when there is one, in this session or no pick at all, else the ordinal
    int pick = -1;
    const QString picked = cleanDir(pickedRaw);
    if (!picked.isEmpty()) {
        for (size_t i = 0; pick < 0 && i < S.shots.size(); ++i)
            if (cleanDir(S.shots[i]->swingDir) == picked) pick = int(i);
    } else {
        for (size_t i = 0; pick < 0 && pickedOrdinal > 0 && i < in.shots.size(); ++i)
            if (in.shots[i].ordinal == pickedOrdinal) pick = int(i);
    }
    const int ordinal = pick >= 0 ? in.shots[size_t(pick)].ordinal : 0;

    out->head = cache.head;
    out->empty = cache.empty;
    out->shots = cache.shots;
    out->key = cache.key;
    out->keyMarks = keyMarks();
    out->matrix = matrixOf(in, res, cache.grades, ordinal, openCell, screen);
    out->cell = cellDetail(rows, cache.grades, openCell, pick, pick >= 0 ? QStringLiteral("Shot %1").arg(words::shotNumber(in.shots[size_t(pick)].shotId, ordinal)) : QString(), screen);
    out->shot = shotDetail(rows, scales, pick, screen);
    out->ready = true;
    out->thread = QThread::currentThread();
    out->jobMs = double(timer.nsecsElapsed()) / 1e6;
    if (loading) {      // nothing is known of this session yet, so nothing is said of it
        out->head = ConsistencyTabHead{};
        out->head.kicker = QStringLiteral("Consistency");
        const ConsistencyTabSentence settling = settlingLine();     // the line alone: no kind, no grades
        out->head.headline.text = settling.text;
        out->head.headline.runs = settling.runs;
        out->empty = ConsistencyTabEmpty{};
        out->key.clear();
        out->keyMarks.clear();
        out->shot.note.clear();
    }
    out->status.ready = true;
    out->status.shotCount = int(in.shots.size());
    out->status.pickedOrdinal = ordinal;
    out->status.loading = loading;
    out->status.settling = settlingLine();
    out->status.timings = ConsistencyTabTimings{ out->jobMs, rows.ms() };
    return out;
}

// ── the session report's reading (doc §3, Print) ────────────────────────────────────────────

ConsistencySessionReport consistencySessionReport(const std::shared_ptr<const SessionSnapshot> &snapshot, const ConsistencyEnv &env,
                                                  const QString &gradePolicy, const std::shared_ptr<const pa::INormProvider> &norms,
                                                  const QString &noun, const IVariabilityScaleProvider *scaleProvider)
{
    ConsistencyReadingRows rows;
    const pa::GradePolicy policy = pa::gradePolicyByName(gradePolicy);
    rows.update(snapshot, env, faultZOf(policy), policy, norms);
    const SessionSnapshot &S = rows.session();
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();
    // the env was taken on the GUI thread (the caller's contract), so the production provider may be made here
    std::unique_ptr<IVariabilityScaleProvider> made;
    if (!scaleProvider) { made = makeVariabilityScaleProvider(); scaleProvider = made.get(); }
    const ConsistencyGradeScales scales = consistencyGradeScales(*scaleProvider);
    const ConsistencyGrades grades = consistencyGrades(in, scales);

    ConsistencySessionReport out;
    for (size_t i = 0; i < in.shots.size(); ++i) {
        if (flagsOf(res, in.shots[i].ordinal).isEmpty()) continue;
        out.flagged.append(ConsistencySessionReport::Flagged{ shotDetail(rows, scales, int(i), false), S.shots[i]->swingDir,
                                                              in.shots[i].club });
    }

    QString kind;
    const Sentence empty = emptySentence(S, in, &kind);
    out.head = headOf(S, in, grades, empty, noun);
    out.table = std::any_of(grades.cells.begin(), grades.cells.end(), [](const ConsistencyCellGrade &c) { return c.grade.graded; });
    const QString line = out.head.headline.text;
    out.available = out.table || !out.head.ball.runs.isEmpty() || !out.flagged.isEmpty();
    out.empty = kind.isEmpty() || empty.text() == line ? QString() : empty.text();
    out.matrix = matrixOf(in, res, grades, 0, QString(), false);
    return out;
}

QVariantMap reportPayloadOf(const ConsistencySessionReport &page)
{
    QVariantList flagged;
    for (const ConsistencySessionReport::Flagged &f : page.flagged) {
        QVariantMap shot = chartsGadgetMap(f.shot);
        shot.insert(QStringLiteral("swingDir"), f.swingDir);
        shot.insert(QStringLiteral("club"), f.club);
        flagged.append(shot);
    }
    QVariantList notes;
    for (const ConsistencyTabSentence &n : page.head.notes) notes.append(chartsGadgetMap(n));
    return { { QStringLiteral("available"), page.available },
             { QStringLiteral("kicker"), page.head.kicker }, { QStringLiteral("headline"), reportHeadlineMap(page.head.headline) },
             { QStringLiteral("ball"), reportSentenceMap(page.head.ball) }, { QStringLiteral("notes"), notes },
             { QStringLiteral("empty"), page.empty },
             { QStringLiteral("table"), page.table }, { QStringLiteral("matrix"), reportMatrixMap(page.matrix) },
             { QStringLiteral("key"), keyWords(false) }, { QStringLiteral("keyMarks"), keyMarks(false) },
             { QStringLiteral("flagged"), flagged } };
}

// ── the headline (doc §3): built from the grades. The grade words are QML's (the header) ─────────

ConsistencyTabHeadline ChartsConsistencyPresenter::headline(const ConsistencyHead &head)
{
    const QString most = head.most.isEmpty() ? QString() : movesMost(head.most);
    QString text, kind;
    QList<ConsistencyTabHeadlineGrade> grades;
    QString fallback;
    switch (head.kind) {
    case ConsistencyHead::Kind::Halves:
        kind = QStringLiteral("halves");
        text = QStringLiteral("Your backswing is %1, your downswing %2; ") + most + QLatin1Char('.');
        fallback = words::cap1(most) + QLatin1Char('.');
        for (const variability::CellGrade *g : { &head.back, &head.down })
            grades.append(ConsistencyTabHeadlineGrade{ g->grade, g->measurementLimited });
        break;
    case ConsistencyHead::Kind::Most:
        kind = QStringLiteral("most");
        text = words::cap1(most) + QLatin1Char('.');
        break;
    case ConsistencyHead::Kind::Grade0:
        kind = QStringLiteral("grade0");      // the Consistency vocabulary's grade-0 line, set by QML
        break;
    case ConsistencyHead::Kind::Ungraded:
        kind = QStringLiteral("ungraded");
        text = QStringLiteral("Nothing here has a range to judge it against.");
        break;
    }
    const Sentence s = Sentence().append(text);
    ConsistencyTabHeadline line;
    line.text = s.text();
    line.runs = runsOf(s);
    line.kind = kind;
    line.grades = grades;
    line.fallback = fallback.isEmpty() ? text : fallback;
    return line;
}

// ── the QObject ───────────────────────────────────────────────────────────────────────────

ChartsConsistencyPresenter::ChartsConsistencyPresenter(QObject *parent)
    : QObject(parent), m_cache(std::make_shared<Cache>())
{
    m_normsGeneration = consistencyEnvGeneration();   // read first: a save in between only re-makes them
    m_env = consistencyEnvFromPacks();      // on the GUI thread, for the worker to hold by value
    m_scales = makeVariabilityScaleProvider();     // on the GUI thread too; then safe on any thread (variability_env.h)
}

ChartsConsistencyPresenter::~ChartsConsistencyPresenter()
{
    // No wait: a job still running finishes into nothing (charts_work.h: the lane is cancelled, #173).
}

#define PP_SET(member, value, sig) \
    do { if (member == value) return; member = value; emit sig(); schedule(); } while (0)

void ChartsConsistencyPresenter::setModel(SeriesSource *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (m)
    {
        // Queued: activateSession publishes its empty snapshot before it counts the load as begun, so taken at once
        // that snapshot would pass for a session with no shots.
        connect(m, &SeriesSource::snapshotChanged, this, &ChartsConsistencyPresenter::onSnapshotChanged, Qt::QueuedConnection);
        connect(m, &SeriesSource::busyChanged, this, &ChartsConsistencyPresenter::onSnapshotChanged);
    }
    emit modelChanged();
    onSnapshotChanged();
}
void ChartsConsistencyPresenter::setPickedSwingDir(const QString &d) { PP_SET(m_pickedDir, d, pickedSwingDirChanged); }
void ChartsConsistencyPresenter::setPickedOrdinal(int ordinal) { PP_SET(m_pickedOrdinal, ordinal, pickedOrdinalChanged); }
void ChartsConsistencyPresenter::setOpenCell(const QString &c) { PP_SET(m_openCell, c, openCellChanged); }
void ChartsConsistencyPresenter::setMedium(const QString &m) { PP_SET(m_medium, m, mediumChanged); }
void ChartsConsistencyPresenter::setGradePolicy(const QString &p) { PP_SET(m_gradePolicy, p, gradePolicyChanged); }
#undef PP_SET

void ChartsConsistencyPresenter::setSnapshot(std::shared_ptr<const SessionSnapshot> s)
{
    m_directSnapshot = std::move(s);
    onSnapshotChanged();
}

// An ordinal and a cell mean something only in the session they were picked in, so leaving it drops them. The first
// session to arrive keeps what was set ahead of it; a new revision of the same session keeps them too.
void ChartsConsistencyPresenter::onSnapshotChanged()
{
    const auto snap = currentSnapshot();
    const QString dir = snap ? snap->sessionDir : QString();
    if (dir != m_sessionDir) {
        const bool left = !m_sessionDir.isEmpty();
        m_sessionDir = dir;
        if (left && m_pickedOrdinal != 0) { m_pickedOrdinal = 0; emit pickedOrdinalChanged(); }
        if (left && !m_openCell.isEmpty()) { m_openCell.clear(); emit openCellChanged(); }
    }
    schedule();
}

std::shared_ptr<const SessionSnapshot> ChartsConsistencyPresenter::currentSnapshot() const
{
    return chosenSnapshot(m_directSnapshot, m_model.data());   // P2: a direct snapshot wins
}

void ChartsConsistencyPresenter::start()
{
    // THE ONE SCHEDULER (charts_presenter_core.h, BusyFromAsk): coalesced, busy from the ask (P11); synchronous at once.
    m_sched.begin();
    // A corridor saved in the Diagnostic Model since the last job: start again as a new presenter would — the env,
    // the scales and the cache (its consistency run read the old corridors) — so this job reads ONE norm set, the new.
    // A job still running keeps the cache and env it was handed.
    if (m_normsGeneration != consistencyEnvGeneration()) {
        m_normsGeneration = consistencyEnvGeneration();
        m_env = consistencyEnvFromPacks();
        m_scales = makeVariabilityScaleProvider();
        m_cache = std::make_shared<Cache>();
    }
    const auto snap = currentSnapshot();
    const QString picked = m_pickedDir, open = m_openCell;
    const int ordinal = m_pickedOrdinal;
    const bool screen = m_medium == QLatin1String("screen");
    const pa::GradePolicy policy = pa::gradePolicyByName(m_gradePolicy);
    const double faultZ = faultZOf(policy);
    const auto norms = pa::sharedNormProvider();
    const ConsistencyEnv env = m_env;
    const std::shared_ptr<const IVariabilityScaleProvider> scales = m_scales;
    // Still reading it. A live session's next shot keeps the model busy too, but it has shots to show meanwhile.
    const SeriesSource *model = m_directSnapshot ? nullptr : m_model.data();
    const bool loading = model && model->busy() && (!snap || snap->shots.empty());
    auto cache = m_cache;
    if (m_sched.synchronous()) {
        m_pool.waitForDone();
        deliver(computeConsistency(snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, *scales, *cache));
        return;
    }
    emit busyChanged();
    askLane(m_pool, [snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, scales, cache]() { return computeConsistency(snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, *scales, *cache); },
            [this](auto out) {
            const bool wasRunning = m_sched.land();
            deliver(out);
            m_sched.afterLanding(wasRunning);
        });
}

void ChartsConsistencyPresenter::deliver(std::shared_ptr<const Output> out)
{
    m_out = std::move(out);
    emit resultsChanged();
    if (m_out->jobMs > 250.0)
        ppInfo() << "[Consistency] recompute" << qRound(m_out->jobMs) << "ms for" << m_out->status.shotCount << "shots";
}

bool ChartsConsistencyPresenter::ready() const { return m_out && m_out->ready; }
ConsistencyTabStatus ChartsConsistencyPresenter::status() const
{
    if (m_out) return m_out->status;
    ConsistencyTabStatus before;            // before the first job too
    before.settling = settlingLine();
    return before;
}
ConsistencyTabHead ChartsConsistencyPresenter::head() const { return m_out ? m_out->head : ConsistencyTabHead{}; }
ConsistencyTabMatrix ChartsConsistencyPresenter::matrix() const { return m_out ? m_out->matrix : ConsistencyTabMatrix{}; }
QString ChartsConsistencyPresenter::key() const { return m_out ? m_out->key : QString(); }
QString ChartsConsistencyPresenter::keyMarks() const { return m_out ? m_out->keyMarks : QString(); }
QList<ConsistencyTabShot> ChartsConsistencyPresenter::shots() const { return m_out ? m_out->shots : QList<ConsistencyTabShot>{}; }
ConsistencyTabCellDetail ChartsConsistencyPresenter::cell() const { return m_out ? m_out->cell : ConsistencyTabCellDetail{}; }
ConsistencyTabShotDetail ChartsConsistencyPresenter::shot() const { return m_out ? m_out->shot : ConsistencyTabShotDetail{}; }
ConsistencyTabEmpty ChartsConsistencyPresenter::empty() const { return m_out ? m_out->empty : ConsistencyTabEmpty{}; }

bool ChartsConsistencyPresenter::waitForIdle(int msTimeout)
{
    return m_sched.waitForIdle(m_pool, msTimeout);
}

int ChartsConsistencyPresenter::sessionRuns() const { return m_cache->rows.runs(); }
const void *ChartsConsistencyPresenter::lastJobThread() const { return m_out ? m_out->thread : nullptr; }

} // namespace pinpoint::charts
