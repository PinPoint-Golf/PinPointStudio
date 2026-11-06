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

#pragma once

// THE CONSISTENCY TABLE'S SHAPE, AND HOME'S WORD (docs/design/consistency_design.md §4.2). Pure,
// header-only, QString and std only, so work_ons_controller.cpp can roll a golfer's sessions up
// from each session's stored cells without pulling in the charts maths. The rules that fill the cells
// are src/Gui/charts/session_consistency.h, which includes this file.
//
// HOME'S WORD. Sessions newest first, session j weighing 0.75^j, as the work-ons fade. Per column of
// the area, the weighted mean of the score over the sessions with a word in that cell: steady and ⚠
// score 0, some ½, loose 1. A cell with "-" or no reading has no word and no weight. P7-P8 is after the
// ball has gone, so it counts for Finish alone. The area's score is the mean of its columns: under 0.2
// Steady, under 0.5 Some, else Loose. The loosest span is the column with the highest value, the first
// on a tie, and it is named unless the word is Steady.

#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::charts {

// Home's rule. These sit here rather than with session_consistency.h's constants so that Home takes
// them without the charts maths.
inline constexpr double kConsistencyHomeFade    = 0.75;  // doc §3 Home: fading 0.75 a session, as the work-ons do
inline constexpr double kConsistencyHomeSome    = 0.5;   // doc §4.2: some scores ½, loose 1, steady and ⚠ 0
inline constexpr double kConsistencyHomeSteady  = 0.2;   // doc §4.2: an area under this is Steady
inline constexpr double kConsistencyHomeLoose   = 0.5;   // doc §4.2: an area at or over this is Loose

// THE VERSION OF THE RULES BEHIND A STORED TABLE (<session>/consistency.json, src/Gui/charts/consistency_summary.h).
// Bump it when anything that can move a cell changes: the sampling, a rule, a constant, the hand placing of a reading.
// The bump re-derives every session's table and nothing else: it is in no work-on or themes fingerprint.
inline constexpr int kConsistencyRuleVersion = 2;

// The table's columns: P1, then the seven spans. Index 7 is after the ball has gone (doc §4.2).
inline constexpr int kConsistencyColumns = 8;
inline constexpr int kConsistencyAfterBallColumn = 7;

inline const QString &consistencyColumnName(int column)
{
    static const QStringList names{ QStringLiteral("P1"),    QStringLiteral("P1-P2"), QStringLiteral("P2-P3"),
                                    QStringLiteral("P3-P4"), QStringLiteral("P4-P5"), QStringLiteral("P5-P6"),
                                    QStringLiteral("P6-P7"), QStringLiteral("P7-P8") };
    static const QString none;
    return column >= 0 && column < names.size() ? names.at(column) : none;
}

// The areas down the side, top to bottom (the ConditionGroup tokens; ball flight has no spans).
inline const QStringList &consistencyAreas()
{
    static const QStringList v{ QStringLiteral("setup"),    QStringLiteral("posture"), QStringLiteral("armsAndClub"),
                                QStringLiteral("sequence"), QStringLiteral("lateral"), QStringLiteral("release"),
                                QStringLiteral("impact"),   QStringLiteral("finish") };
    return v;
}

// The columns an area is read at, ascending (doc §4.2's table). None for an unplaced or unknown area.
inline std::vector<int> consistencyColumnsOf(const QString &area)
{
    if (area == QLatin1String("setup"))    return { 0 };
    if (area == QLatin1String("posture"))  return { 0, 1, 2, 3, 4, 5, 6, 7 };
    if (area == QLatin1String("armsAndClub") || area == QLatin1String("lateral"))
        return { 1, 2, 3, 4, 5, 6, 7 };
    if (area == QLatin1String("sequence")) return { 2, 3, 4, 5, 6 };
    if (area == QLatin1String("release"))  return { 5, 6, 7 };
    if (area == QLatin1String("impact"))   return { 6 };
    if (area == QLatin1String("finish"))   return { 7 };
    return {};
}

inline bool consistencyApplies(const QString &area, int column)
{
    for (int c : consistencyColumnsOf(area)) if (c == column) return true;
    return false;
}

// What a cell says. Dash: the area is read at this column and nothing was judged there, with or without
// readings. Blank: the area is not read at this column, so a session's cells hold no record for it.
enum class ConsistencyWord { Blank, Dash, Steady, NearError, Some, Loose };

inline QString consistencyWordToken(ConsistencyWord w)
{
    switch (w) {
    case ConsistencyWord::Blank:     return QStringLiteral("blank");
    case ConsistencyWord::Dash:      return QStringLiteral("dash");
    case ConsistencyWord::Steady:    return QStringLiteral("steady");
    case ConsistencyWord::NearError: return QStringLiteral("nearError");
    case ConsistencyWord::Some:      return QStringLiteral("some");
    case ConsistencyWord::Loose:     return QStringLiteral("loose");
    }
    return QStringLiteral("blank");
}

inline ConsistencyWord consistencyWordFromToken(const QString &token)
{
    for (ConsistencyWord w : { ConsistencyWord::Dash, ConsistencyWord::Steady, ConsistencyWord::NearError,
                               ConsistencyWord::Some, ConsistencyWord::Loose })
        if (consistencyWordToken(w) == token) return w;
    return ConsistencyWord::Blank;
}

// One cell of a session's table: its word and the counts behind it.
struct ConsistencyCell {
    QString area;
    int     column = -1;
    ConsistencyWord word = ConsistencyWord::Blank;
    int     judged = 0, loose = 0, nearError = 0, unjudged = 0;
};

// An area rolled up. `word` is Steady, Some or Loose, or Dash when no session had a word for it.
struct ConsistencyHomeWord {
    ConsistencyWord word = ConsistencyWord::Dash;
    double score = std::numeric_limits<double>::quiet_NaN();
    int    loosestColumn = -1;      // -1 when Steady or Dash
};

// `sessions` holds each session's cells, newest session first; cells of other areas are ignored.
inline ConsistencyHomeWord consistencyHomeWord(const QString &area,
                                               const std::vector<std::vector<ConsistencyCell>> &sessions)
{
    ConsistencyHomeWord out;
    double sum = 0.0, top = -1.0;
    int columns = 0, loosest = -1;
    for (int column : consistencyColumnsOf(area)) {
        if (column == kConsistencyAfterBallColumn && area != QLatin1String("finish")) continue;
        double weighted = 0.0, weight = 0.0;
        for (size_t j = 0; j < sessions.size(); ++j) {
            for (const ConsistencyCell &c : sessions[j]) {
                if (c.area != area || c.column != column) continue;
                double score = 0.0;
                if (c.word == ConsistencyWord::Blank || c.word == ConsistencyWord::Dash) break;  // no word here
                if (c.word == ConsistencyWord::Some) score = kConsistencyHomeSome;
                else if (c.word == ConsistencyWord::Loose) score = 1.0;
                const double w = std::pow(kConsistencyHomeFade, double(j));
                weighted += w * score;
                weight += w;
                break;
            }
        }
        if (weight <= 0.0) continue;
        const double value = weighted / weight;
        sum += value;
        ++columns;
        if (value > top) { top = value; loosest = column; }
    }
    if (!columns) return out;
    out.score = sum / double(columns);
    out.word = out.score < kConsistencyHomeSteady ? ConsistencyWord::Steady
             : out.score < kConsistencyHomeLoose  ? ConsistencyWord::Some
                                                  : ConsistencyWord::Loose;
    if (out.word != ConsistencyWord::Steady) out.loosestColumn = loosest;
    return out;
}

} // namespace pinpoint::charts
