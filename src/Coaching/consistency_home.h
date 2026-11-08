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

// THE CONSISTENCY TABLE'S SHAPE (docs/design/consistency_design.md §4.2): its columns and its areas. A cell's grade is
// the variability grader's (src/Gui/charts/consistency_grades.h); the old cell words were retired (Mark, 7 Nov).
// Pure, header-only, QString and std only. The rules that fill the cells are src/Gui/charts/session_consistency.h,
// which includes this file. Home no longer rolls the table up (Mark's rulings R5 and R15, 7 Nov): its Consistency
// column is the consistency mark of the biggest fault's measure (work_ons_controller.h, CONSISTENCY MARKS), and the
// old per-session <session>/consistency.json files stay in the libraries, unread.

#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::charts {

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
                                QStringLiteral("swingPlane"), QStringLiteral("sequence"), QStringLiteral("lateral"), QStringLiteral("release"),
                                QStringLiteral("impact"),   QStringLiteral("finish") };
    return v;
}

// The columns an area is read at, ascending (doc §4.2's table). None for an unplaced or unknown area.
inline std::vector<int> consistencyColumnsOf(const QString &area)
{
    if (area == QLatin1String("setup"))    return { 0 };
    if (area == QLatin1String("posture"))  return { 0, 1, 2, 3, 4, 5, 6, 7 };
    if (area == QLatin1String("armsAndClub") || area == QLatin1String("swingPlane")
        || area == QLatin1String("lateral"))
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

} // namespace pinpoint::charts
