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

#include "charts_words.h"
#include "consistency_env.h"
#include "session_consistency.h"
#include "session_series_model.h"

#include "../../Core/pp_debug.h"
#include "../../Diagnostics/norms/norm.h"           // the grade policy
#include "../../Diagnostics/norms/norm_provider.h"

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
using words::fin;
using words::Sentence;

const QString kFinish = QStringLiteral("finish");
const QString kImpact = QStringLiteral("impact");

// ── the areas' words ────────────────────────────────────────────────────────────────────────

struct AreaWords { const char *id, *name, *phrase; bool plural; };
const AreaWords kAreaWords[] = {
    { "setup",       "Setup",          "setup",           false },
    { "posture",     "Posture",        "posture",         false },
    { "armsAndClub", "Arms and club",  "arms and club",   true  },
    { "sequence",    "Sequence",       "sequence",        false },
    { "lateral",     "Lateral motion", "lateral motion",  false },
    { "release",     "Release",        "release",         false },
    { "impact",      "Impact",         "impact position", false },
    { "finish",      "Finish",         "finish",          false },
};

const AreaWords &areaWords(const QString &id)
{
    for (const AreaWords &a : kAreaWords) if (id == QLatin1String(a.id)) return a;
    return kAreaWords[0];
}

// "your impact position moves most", "your arms and club move most".
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

QString loadingWords() { return QStringLiteral("Reading this session…"); }
QVariantMap settlingLine() { return Sentence().append(loadingWords()).toVariant(); }

// ── the headline's scores ──────────────────────────────────────────────────────────────────

double scoreOf(ConsistencyWord w)
{
    switch (w) {
    case ConsistencyWord::Steady:
    case ConsistencyWord::NearError: return 0.0;
    case ConsistencyWord::Some:      return kConsistencyHomeSome;
    case ConsistencyWord::Loose:     return 1.0;
    default:                         return kNaN;
    }
}

struct Mean {
    double sum = 0.0;
    int    n = 0;
    void   add(double v) { sum += v; ++n; }
    double value() const { return n ? sum / double(n) : kNaN; }
};

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

QVariantMap headOf(const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyResult &res, const Sentence &empty,
                   const QString &noun = QStringLiteral("shots"))
{
    const auto clubs = clubCounts(in);
    QString kicker = QStringLiteral("Consistency  ·  %1 %2").arg(int(in.shots.size())).arg(words::plural(int(in.shots.size()), noun.chopped(1), noun));
    if (clubs.size() > 1) kicker += QStringLiteral("  ·  mixed clubs");
    else if (clubs.size() == 1 && !clubs.front().first.isEmpty()) kicker += QStringLiteral("  ·  ") + clubWord(clubs.front().first);

    const QString headline = ChartsConsistencyPresenter::headline(res);
    Sentence line;
    line.append(!headline.isEmpty() ? headline : !empty.isEmpty() ? empty.text() : QStringLiteral("Too few readings could be judged to say how your swing repeats."));

    QVariantList notes;
    if (anyValue(in, &ConsistencyShot::carry) && !anyValue(in, &ConsistencyShot::offline))
        notes.append(Sentence().append(QStringLiteral("This launch monitor gives carry but no offline, so only short shots are flagged.")).toVariant());
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
        notes.append(note.toVariant());
    }

    const Sentence ball = ballLine(session, in);
    return { { QStringLiteral("kicker"), kicker }, { QStringLiteral("headline"), line.toVariant() },
             { QStringLiteral("ball"), ball.isEmpty() ? QVariantMap{} : ball.toVariant() }, { QStringLiteral("notes"), notes } };
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
    if (compared < kConsistencyMinSpreadShots) {
        *kind = QStringLiteral("tooFew");
        return s.append(QStringLiteral("Most of this table is \"-\" because a reading needs %1 shots with one club to show how much it moves.")
                            .arg(words::word(kConsistencyMinSpreadShots)));
    }
    return s;
}

// ── the table ───────────────────────────────────────────────────────────────────────────────

const ConsistencyCell *cellOf(const ConsistencyResult &res, const QString &area, int column)
{
    for (const ConsistencyCell &c : res.cells) if (c.area == area && c.column == column) return &c;
    return nullptr;
}

QVariantMap cellMap(const ConsistencyCell *c, const QString &area, int column, bool on, bool marked, bool screen)
{
    if (!c) return { { QStringLiteral("text"), QString() }, { QStringLiteral("short"), QString() } };   // does not apply
    QString text, mark;
    switch (c->word) {
    case ConsistencyWord::Steady:    text = QStringLiteral("steady");  mark = QStringLiteral("·"); break;
    case ConsistencyWord::Some:      text = QStringLiteral("some");    mark = QStringLiteral("◐"); break;
    case ConsistencyWord::Loose:     text = QStringLiteral("loose");   mark = QStringLiteral("●"); break;
    case ConsistencyWord::NearError: text = QStringLiteral("⚠ error"); mark = QStringLiteral("⚠"); break;
    default:                         text = QStringLiteral("-");       mark = QStringLiteral("-"); break;
    }
    const bool moved = c->word == ConsistencyWord::Some || c->word == ConsistencyWord::Loose;
    return { { QStringLiteral("text"), text }, { QStringLiteral("short"), mark }, { QStringLiteral("strong"), moved },
             { QStringLiteral("heavy"), c->word == ConsistencyWord::Loose },
             { QStringLiteral("heat"), c->word == ConsistencyWord::Loose ? 2 : c->word == ConsistencyWord::Some ? 1 : 0 },
             { QStringLiteral("soft"), column == kConsistencyAfterBallColumn && area != kFinish },
             { QStringLiteral("on"), on }, { QStringLiteral("mark"), marked ? QStringLiteral("◆") : QString() },
             { QStringLiteral("tap"), screen && c->judged + c->unjudged > 0 } };
}

QVariantMap matrixMap(const ConsistencyInput &in, const ConsistencyResult &res, int ordinal, const QString &openCell, bool screen)
{
    QVariantList columns;
    for (int c = 0; c < kConsistencyColumns; ++c) {
        const QString name = consistencyColumnName(c);
        columns.append(QVariantMap{ { QStringLiteral("text"), name },
                                    { QStringLiteral("short"), c == 0 ? name : QString(name).remove(QLatin1Char('P')) } });
    }
    std::map<std::pair<QString, int>, bool> marked;
    for (const ConsistencyStandOut &s : res.standouts)
        if (s.ordinal == ordinal) marked[{ in.readings[size_t(s.reading)].area, s.column }] = true;
    QVariantList rows;
    for (const QString &area : consistencyAreas()) {
        QVariantList cells;
        for (int c = 0; c < kConsistencyColumns; ++c)
            cells.append(cellMap(cellOf(res, area, c), area, c, openCell == area + QLatin1Char(':') + QString::number(c),
                                 marked.count({ area, c }) > 0, screen));
        rows.append(QVariantMap{ { QStringLiteral("id"), area }, { QStringLiteral("name"), QString::fromUtf8(areaWords(area).name) },
                                 { QStringLiteral("cells"), cells } });
    }
    return { { QStringLiteral("columns"), columns }, { QStringLiteral("rows"), rows } };
}

// `picks`: a shot can be picked here, so a cell can carry the ◆. The report has no picked shot.
QString keyWords(bool picks = true)
{
    return QStringLiteral("Steady: each reading moves little against the range it is judged by. Some: a few readings move. Loose: at "
                          "least a third do. ⚠ error: too close to measurement error to tell. \"-\": nothing could be judged. P7-P8 is "
                          "after the ball has gone. It is read over the same %1 after impact on every shot, and is shown lighter. A "
                          "blank does not apply to that area.").arg(words::figMs(kConsistencyAfterImpactMs))
           + (picks ? QStringLiteral(" ◆ this shot left your pattern here.") : QString());
}

QString keyMarks(bool picks = true)
{
    return QStringLiteral("· steady   ◐ some   ● loose   ⚠ too close to measurement error")
           + (picks ? QStringLiteral("   ◆ this shot left your pattern here") : QString());
}

// ── a cell, opened ──────────────────────────────────────────────────────────────────────────

QString placeWord(ConsistencyVerdict v)
{
    switch (v) {
    case ConsistencyVerdict::Loose:     return QStringLiteral("loose");
    case ConsistencyVerdict::NearError: return QStringLiteral("⚠ near error");
    case ConsistencyVerdict::Steady:    return QStringLiteral("steady");
    default:                            return QStringLiteral("not judged");
    }
}

QVariantMap cellDetail(const ConsistencyReadingRows &rows, const QString &openCell, int pick, const QString &shotLabel, bool screen)
{
    const int colon = openCell.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0) return {};
    const QString area = openCell.left(colon);
    bool ok = false;
    const int column = openCell.mid(colon + 1).toInt(&ok);
    const ConsistencyInput &in = rows.input();
    const ConsistencyCell *c = ok ? cellOf(rows.result(), area, column) : nullptr;
    if (!c) return {};

    // loosest first; a reading with no norm has no such measure, so those follow in (key, column) order
    std::vector<const ConsistencyJudged *> list;
    for (const ConsistencyJudged &j : rows.result().readings)
        if (j.column == column && in.readings[size_t(j.reading)].area == area) list.push_back(&j);
    const auto looseness = [&](const ConsistencyJudged *j) {
        return j->verdict == ConsistencyVerdict::Unjudged ? -1.0 : j->spread / in.readings[size_t(j->reading)].normSigma;
    };
    std::stable_sort(list.begin(), list.end(), [&](const auto *a, const auto *b) { return looseness(a) > looseness(b); });

    const bool guarded = pick >= 0 && in.shots[size_t(pick)].guarded;
    QVariantList out;
    for (const ConsistencyJudged *j : list) {
        QVariantMap row = rows.readingRow(pick, *j);
        // doc §4.3: a guarded shot keeps its readings with a ⚠. Its value here rests on a ladder that cannot be trusted
        if (guarded && consistencyGuardCovers(in.readings[size_t(j->reading)]) && row.value(QStringLiteral("measured")).toBool()) {
            const QString had = row.value(QStringLiteral("cautionText")).toString();
            row.insert(QStringLiteral("caution"), true);
            row.insert(QStringLiteral("cautionText"), had.isEmpty() ? QStringLiteral("⚠ ") + consistencyGuardWords()
                                                                    : had + QStringLiteral("; ") + consistencyGuardWords());
        }
        row.insert(QStringLiteral("place"), placeWord(j->verdict));
        row.insert(QStringLiteral("word"), consistencyVerdictToken(j->verdict));
        row.insert(QStringLiteral("judged"), j->verdict != ConsistencyVerdict::Unjudged);
        out.append(row);
    }
    Sentence sub;
    if (c->judged == 0) sub.append(QStringLiteral("nothing here could be judged"));
    else if (c->loose == 0) sub.append(QStringLiteral("no reading loose"));
    else {
        sub.figure(QString::number(c->loose)).append(QStringLiteral(" of ")).figure(QString::number(c->judged));
        sub.append(words::plural(c->judged, QStringLiteral(" reading loose"), QStringLiteral(" readings loose")));
    }
    if (c->nearError > 0) sub.append(QStringLiteral("; ")).figure(QString::number(c->nearError)).append(QStringLiteral(" too close to measurement error"));
    if (screen && !out.isEmpty()) sub.append(QStringLiteral("; a row opens its curve"));

    const QString name = QString::fromUtf8(areaWords(area).name);
    const QString tag = area == kImpact ? QStringLiteral("P7") : consistencyColumnName(column);
    const int n = int(in.shots.size());
    return { { QStringLiteral("area"), name }, { QStringLiteral("areaId"), area }, { QStringLiteral("column"), column }, { QStringLiteral("tag"), tag },
             { QStringLiteral("word"), consistencyWordToken(c->word) }, { QStringLiteral("title"), name + QStringLiteral(", ") + tag },
             { QStringLiteral("sub"), sub.toVariant() }, { QStringLiteral("rows"), out }, { QStringLiteral("unjudged"), c->unjudged },
             { QStringLiteral("unjudgedNote"), c->unjudged ? QStringLiteral("%1 %2 no range to be judged against, and %3 listed as not judged.")
                                                                 .arg(c->unjudged).arg(c->unjudged == 1 ? QStringLiteral("reading has") : QStringLiteral("readings have"))
                                                                 .arg(c->unjudged == 1 ? QStringLiteral("is") : QStringLiteral("are"))
                                                           : QString() },
             { QStringLiteral("headLabel"), QStringLiteral("Loosest first") }, { QStringLiteral("shotLabel"), shotLabel },
             { QStringLiteral("countLabel"), QStringLiteral("%1 %2").arg(n).arg(words::plural(n, QStringLiteral("shot"), QStringLiteral("shots"))) } };
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

// The ball's half of the sentence: the shot against its club's usual carry, "close to" only where the fence says so.
void ballSentence(Sentence &s, const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyResult &res,
                  const std::vector<ConsistencyLmRow> &lm, const ConsistencyShot &shot)
{
    const QString yd = lmUnit(session, QStringLiteral("lm.carryDistance"));
    if (fin(shot.carry)) {
        const std::vector<double> carry = clubValues(in, shot.club, &ConsistencyShot::carry);
        s.append(QStringLiteral("Carry ")).figure(words::figUnit(shot.carry, yd));
        if (int(carry.size()) >= kConsistencyMinClubShots) {
            const bool past = std::any_of(lm.begin(), lm.end(), [](const ConsistencyLmRow &r) { return r.key == QLatin1String("lm.carryDistance"); });
            const bool close = !past && int(carry.size()) >= kConsistencyMinBallShots;
            s.append(close ? QStringLiteral(", close to your usual ") : QStringLiteral(" against your usual "));
            s.figure(words::figUnit(medianOf(carry), yd));
        }
        s.append(QStringLiteral(". "));
    }
    if (flagsOf(res, shot.ordinal).contains(QStringLiteral("offline")))
        s.append(QStringLiteral("It finished ")).figure(sideWords(shot.offline, lmUnit(session, QStringLiteral("lm.offline")))).append(QStringLiteral(". "));
}

QVariantMap shotDetail(const ConsistencyReadingRows &rows, int pick, bool screen)
{
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();
    if (pick < 0) return { { QStringLiteral("available"), false },
                           { QStringLiteral("note"), screen ? QStringLiteral("Pick a shot to see what stood out on it.") : QString() } };

    const ConsistencyShot &shot = in.shots[size_t(pick)];
    const std::vector<ConsistencyLmRow> &lm = rows.lmRows(pick);
    QVariantList ball, club, swing;
    for (const ConsistencyLmRow &r : lm) {
        QVariantMap row = rows.lmRow(pick, r);
        row.insert(QStringLiteral("label"), QString(r.label).remove(QStringLiteral(" (measured)")));   // the group says whose reading it is
        (r.ball ? ball : club).append(row);
    }
    for (const ConsistencyStandOut &s : res.standouts)
        if (s.ordinal == shot.ordinal) swing.append(rows.standOutRow(pick, s));
    QVariantList groups;
    const auto group = [&](const QString &name, const QVariantList &list) {
        if (!list.isEmpty()) groups.append(QVariantMap{ { QStringLiteral("name"), name }, { QStringLiteral("rows"), list } });
    };
    group(QStringLiteral("The ball"), ball);
    group(QStringLiteral("The club"), club);
    group(QStringLiteral("Your swing"), swing);

    // the sentence (doc §4.3): the count said for what it means
    Sentence s;
    ballSentence(s, rows.session(), in, res, lm, shot);
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
    return { { QStringLiteral("available"), true }, { QStringLiteral("ordinal"), shot.ordinal },
             { QStringLiteral("kicker"), QStringLiteral("Shot %1 of %2").arg(shot.ordinal).arg(total)
                                         + (shot.club.isEmpty() ? QString() : QStringLiteral("  ·  ") + shot.club.toLower()) },
             { QStringLiteral("shotLabel"), QStringLiteral("Shot %1").arg(shot.ordinal) },
             { QStringLiteral("countLabel"), QStringLiteral("%1 %2").arg(total).arg(words::plural(total, QStringLiteral("shot"), QStringLiteral("shots"))) },
             { QStringLiteral("headLabel"), QStringLiteral("What stood out") }, { QStringLiteral("lm"), lmWords(in, res, shot) },
             { QStringLiteral("sentence"), s.toVariant() }, { QStringLiteral("guarded"), shot.guarded },
             { QStringLiteral("guardNote"), shot.guarded ? ConsistencyReadingRows::guardNote() : QString() },
             { QStringLiteral("groups"), groups }, { QStringLiteral("count"), count },
             { QStringLiteral("note"), count >= 2 ? QStringLiteral("These happened on the same shot. That does not show one caused another.") : QString() } };
}

// The grade policy's fault line, as Charts takes it.
double faultZOf(const pa::GradePolicy &policy)
{
    return policy.idealMaxZ > 0.0 ? policy.watchMaxZ / policy.idealMaxZ : 3.0;
}

QVariantList shotsList(const SessionSnapshot &session, const ConsistencyInput &in, const ConsistencyResult &res)
{
    QVariantList out;
    for (size_t i = 0; i < in.shots.size(); ++i) {
        const ConsistencyShot &s = in.shots[i];
        const ShotSeries &sh = *session.shots[i];
        out.append(QVariantMap{ { QStringLiteral("ordinal"), s.ordinal }, { QStringLiteral("shotId"), sh.shotId }, { QStringLiteral("swingDir"), sh.swingDir },
                                { QStringLiteral("club"), s.club }, { QStringLiteral("flags"), flagsOf(res, s.ordinal) }, { QStringLiteral("guarded"), s.guarded } });
    }
    return out;
}

} // namespace

// ── the worker's cache and the published output ─────────────────────────────────────────────

struct ChartsConsistencyPresenter::Cache {
    ConsistencyReadingRows rows;
    // what depends on the session alone: made once a snapshot
    QVariantMap head, empty;
    QVariantList shots;
    QString key;
};

struct ChartsConsistencyPresenter::Output {
    bool ready = false;
    QVariantMap status, head, matrix, cell, shot, empty;
    QVariantList shots;
    QString key, keyMarks;
    const void *thread = nullptr;
    double jobMs = 0;
};

static std::shared_ptr<ChartsConsistencyPresenter::Output>
computeConsistency(const std::shared_ptr<const SessionSnapshot> &snapshot, const QString &pickedRaw, int pickedOrdinal,
                   const QString &openCell, bool screen, bool loading, const ConsistencyEnv &env, double faultZ, const pa::GradePolicy &policy,
                   const std::shared_ptr<const pa::INormProvider> &norms, ChartsConsistencyPresenter::Cache &cache)
{
    QElapsedTimer timer;
    timer.start();
    auto out = std::make_shared<ChartsConsistencyPresenter::Output>();
    ConsistencyReadingRows &rows = cache.rows;
    const bool fresh = rows.update(snapshot, env, faultZ, policy, norms);
    const SessionSnapshot &S = rows.session();
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();
    if (fresh) {
        QString kind;
        const Sentence empty = emptySentence(S, in, &kind);
        cache.empty = kind.isEmpty() ? QVariantMap{} : QVariantMap{ { QStringLiteral("kind"), kind }, { QStringLiteral("sentence"), empty.toVariant() } };
        cache.head = headOf(S, in, res, empty);
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
    out->matrix = matrixMap(in, res, ordinal, openCell, screen);
    out->cell = cellDetail(rows, openCell, pick, pick >= 0 ? QStringLiteral("Shot %1").arg(ordinal) : QString(), screen);
    out->shot = shotDetail(rows, pick, screen);
    out->ready = true;
    out->thread = QThread::currentThread();
    out->jobMs = double(timer.nsecsElapsed()) / 1e6;
    if (loading) {      // nothing is known of this session yet, so nothing is said of it
        out->head = { { QStringLiteral("kicker"), QStringLiteral("Consistency") },
                      { QStringLiteral("headline"), settlingLine() },
                      { QStringLiteral("ball"), QVariantMap{} }, { QStringLiteral("notes"), QVariantList{} } };
        out->empty.clear();
        out->key.clear();
        out->keyMarks.clear();
        out->shot.insert(QStringLiteral("note"), QString());
    }
    out->status = { { QStringLiteral("ready"), true }, { QStringLiteral("shotCount"), int(in.shots.size()) }, { QStringLiteral("pickedOrdinal"), ordinal },
                    { QStringLiteral("loading"), loading }, { QStringLiteral("settling"), settlingLine() },
                    { QStringLiteral("timings"), QVariantMap{ { QStringLiteral("jobMs"), out->jobMs }, { QStringLiteral("consistencyMs"), rows.ms() } } } };
    return out;
}

// ── the session report's reading (doc §3, Print) ────────────────────────────────────────────

QVariantMap consistencySessionReportPayload(const std::shared_ptr<const SessionSnapshot> &snapshot, const ConsistencyEnv &env,
                                            const QString &gradePolicy, const std::shared_ptr<const pa::INormProvider> &norms,
                                            const QString &noun)
{
    ConsistencyReadingRows rows;
    const pa::GradePolicy policy = pa::gradePolicyByName(gradePolicy);
    rows.update(snapshot, env, faultZOf(policy), policy, norms);
    const SessionSnapshot &S = rows.session();
    const ConsistencyInput &in = rows.input();
    const ConsistencyResult &res = rows.result();

    QVariantList flagged;
    for (size_t i = 0; i < in.shots.size(); ++i) {
        if (flagsOf(res, in.shots[i].ordinal).isEmpty()) continue;
        QVariantMap shot = shotDetail(rows, int(i), false);
        shot.insert(QStringLiteral("swingDir"), S.shots[i]->swingDir);
        shot.insert(QStringLiteral("club"), in.shots[i].club);
        flagged.append(shot);
    }

    QString kind;
    const Sentence empty = emptySentence(S, in, &kind);
    const QVariantMap head = headOf(S, in, res, empty, noun);
    const QVariantMap ball = head.value(QStringLiteral("ball")).toMap();
    const bool table = std::any_of(res.cells.begin(), res.cells.end(), [](const ConsistencyCell &c) { return fin(scoreOf(c.word)); });
    const QString line = head.value(QStringLiteral("headline")).toMap().value(QStringLiteral("text")).toString();
    return { { QStringLiteral("available"), table || !ball.isEmpty() || !flagged.isEmpty() },
             { QStringLiteral("kicker"), head.value(QStringLiteral("kicker")) }, { QStringLiteral("headline"), head.value(QStringLiteral("headline")) },
             { QStringLiteral("ball"), ball }, { QStringLiteral("notes"), head.value(QStringLiteral("notes")) },
             { QStringLiteral("empty"), kind.isEmpty() || empty.text() == line ? QString() : empty.text() },
             { QStringLiteral("table"), table }, { QStringLiteral("matrix"), matrixMap(in, res, 0, QString(), false) },
             { QStringLiteral("key"), keyWords(false) }, { QStringLiteral("keyMarks"), keyMarks(false) },
             { QStringLiteral("flagged"), flagged } };
}

// ── the headline (doc §3): built from the table, never by QML ───────────────────────────────

QString ChartsConsistencyPresenter::headline(const ConsistencyResult &res)
{
    Mean back, down;
    std::map<QString, Mean> areas;
    bool moved = false;
    for (const ConsistencyCell &c : res.cells) {
        const double score = scoreOf(c.word);
        if (!fin(score)) continue;
        if (c.column == kConsistencyAfterBallColumn && c.area != kFinish) continue;     // after the ball has gone
        moved = moved || score > 0.0;
        areas[c.area].add(score);
        if (c.column >= kConsistencyBackswingFirst && c.column <= kConsistencyBackswingLast) back.add(score);
        if (c.column >= kConsistencyDownswingFirst && c.column <= kConsistencyDownswingLast) down.add(score);
    }
    if (int(areas.size()) < kConsistencyHeadlineMinAreas) return QString();
    if (!moved) return QStringLiteral("Your swing repeats from shot to shot.");

    QString loosest;
    double top = -1.0;
    for (const QString &area : consistencyAreas()) {
        const auto it = areas.find(area);
        if (it != areas.end() && it->second.value() > top) { top = it->second.value(); loosest = area; }
    }
    const QString most = movesMost(loosest);
    if (!back.n || !down.n) return words::cap1(most) + QLatin1Char('.');    // a table with no band to compare
    const double margin = kConsistencyHeadlineMargin - 1e-9;                // .1 as a mean of halves, not as its binary neighbour
    if (down.value() - back.value() >= margin)
        return QStringLiteral("You repeat your swing best in the backswing. It loosens on the way down, and ") + most + QLatin1Char('.');
    if (back.value() - down.value() >= margin)
        return QStringLiteral("You repeat your swing best on the way down. It is looser in the backswing, and ") + most + QLatin1Char('.');
    return QStringLiteral("Your swing holds about the same all the way through, and ") + most + QLatin1Char('.');
}

// ── the QObject ───────────────────────────────────────────────────────────────────────────

ChartsConsistencyPresenter::ChartsConsistencyPresenter(QObject *parent)
    : QObject(parent), m_cache(std::make_shared<Cache>())
{
    m_pool.setMaxThreadCount(1);
    m_env = consistencyEnvFromPacks();      // on the GUI thread, for the worker to hold by value
}

ChartsConsistencyPresenter::~ChartsConsistencyPresenter()
{
    m_pool.waitForDone();
}

#define PP_SET(member, value, sig) \
    do { if (member == value) return; member = value; emit sig(); schedule(); } while (0)

void ChartsConsistencyPresenter::setModel(QObject *m)
{
    if (m_model == m) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = m;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m))
    {
        // Queued: activateSession publishes its empty snapshot before it counts the load as begun, so taken at once
        // that snapshot would pass for a session with no shots.
        connect(sm, &SessionSeriesModel::snapshotChanged, this, &ChartsConsistencyPresenter::onSnapshotChanged, Qt::QueuedConnection);
        connect(sm, &SessionSeriesModel::busyChanged, this, &ChartsConsistencyPresenter::onSnapshotChanged);
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
    if (m_directSnapshot) return m_directSnapshot;
    if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot();
    return nullptr;
}

void ChartsConsistencyPresenter::schedule()
{
    if (m_running) { m_dirty = true; return; }
    start();
}

void ChartsConsistencyPresenter::start()
{
    m_dirty = false;
    const auto snap = currentSnapshot();
    const QString picked = m_pickedDir, open = m_openCell;
    const int ordinal = m_pickedOrdinal;
    const bool screen = m_medium == QLatin1String("screen");
    const pa::GradePolicy policy = pa::gradePolicyByName(m_gradePolicy);
    const double faultZ = faultZOf(policy);
    const auto norms = pa::sharedNormProvider();
    const ConsistencyEnv env = m_env;
    // Still reading it. A live session's next shot keeps the model busy too, but it has shots to show meanwhile.
    const auto *model = m_directSnapshot ? nullptr : qobject_cast<SessionSeriesModel *>(m_model.data());
    const bool loading = model && model->busy() && (!snap || snap->shots.empty());
    auto cache = m_cache;
    if (m_synchronous) {
        m_pool.waitForDone();
        deliver(computeConsistency(snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, *cache));
        return;
    }
    m_running = true;
    emit busyChanged();
    m_pool.start([this, snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, cache]() {
        auto out = computeConsistency(snap, picked, ordinal, open, screen, loading, env, faultZ, policy, norms, *cache);
        QMetaObject::invokeMethod(this, [this, out]() {
            m_running = false;
            deliver(out);
            if (m_dirty) start(); else emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

void ChartsConsistencyPresenter::deliver(std::shared_ptr<const Output> out)
{
    m_out = std::move(out);
    emit resultsChanged();
    if (m_out->jobMs > 250.0)
        ppInfo() << "[Consistency] recompute" << qRound(m_out->jobMs) << "ms for" << m_out->status.value(QStringLiteral("shotCount")).toInt() << "shots";
}

bool ChartsConsistencyPresenter::ready() const { return m_out && m_out->ready; }
QVariantMap ChartsConsistencyPresenter::status() const
{
    return m_out ? m_out->status : QVariantMap{ { QStringLiteral("settling"), settlingLine() } };   // before the first job too
}
QVariantMap ChartsConsistencyPresenter::head() const { return m_out ? m_out->head : QVariantMap{}; }
QVariantMap ChartsConsistencyPresenter::matrix() const { return m_out ? m_out->matrix : QVariantMap{}; }
QString ChartsConsistencyPresenter::key() const { return m_out ? m_out->key : QString(); }
QString ChartsConsistencyPresenter::keyMarks() const { return m_out ? m_out->keyMarks : QString(); }
QVariantList ChartsConsistencyPresenter::shots() const { return m_out ? m_out->shots : QVariantList{}; }
QVariantMap ChartsConsistencyPresenter::cell() const { return m_out ? m_out->cell : QVariantMap{}; }
QVariantMap ChartsConsistencyPresenter::shot() const { return m_out ? m_out->shot : QVariantMap{}; }
QVariantMap ChartsConsistencyPresenter::empty() const { return m_out ? m_out->empty : QVariantMap{}; }

bool ChartsConsistencyPresenter::waitForIdle(int msTimeout)
{
    if (m_synchronous) return true;
    QDeadlineTimer deadline(msTimeout);
    while ((m_running || m_dirty) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        m_pool.waitForDone(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_running && !m_dirty;
}

int ChartsConsistencyPresenter::sessionRuns() const { return m_cache->rows.runs(); }
const void *ChartsConsistencyPresenter::lastJobThread() const { return m_out ? m_out->thread : nullptr; }

} // namespace pinpoint::charts
