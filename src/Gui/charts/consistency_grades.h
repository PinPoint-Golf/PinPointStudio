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

// THE CONSISTENCY TAB'S GRADES (docs/design/system/variability.md; Mark's rulings R1, R2a, R3, 7 Nov): every reading of
// the table graded by the variability grader, and every cell of it graded at its loosest third. The graded path sits
// ALONGSIDE session_consistency.h's rules, which still find the stand-outs: this file reads a ConsistencyInput and never
// changes one.
//
//   A READING (key × column) is graded on the scalar basis. Its values are each shot's — a guarded shot's left out of a
//   reading that rests on its P-ladder (consistencyGuardCovers) — pooled within club (R2a,
//   pooledWithinClubInput: residuals about each club's median, a club under three shots left out — the matrix's own
//   centring). Its scale is the reading's normSigma, the corridor half-width (σLo + σHi)/2, then the descriptor's
//   variabilityScale; its scale type is the descriptor's. Its own propagated σ (ownSigma) goes as propagatedSigma, never
//   as noise (R3): a spread no larger than it is measurement-limited. Under N_MIN (8) shots it is grade 0; with no scale
//   it is not graded.
//   A SPAN has five points per shot. Each point is graded on its own and the reading takes the MEDIAN point by ρ (the
//   lower of the middle two), as the old rule took the median of the five spreads; with no point carrying a ρ it takes
//   the first graded point (grade 0), else the first point (not graded).
//   A CELL (an area at a column that applies to it) takes cellGrade() over its readings (R1).
//   AFTER THE BALL (P7-P8, Mark, 8 Nov). A reading is graded there when a condition reads its measure at the finish
//   (ConsistencyReading::afterBall, from afterBallKeysForPack). A P7-P8 cell holding such a reading is graded on those
//   alone; a cell holding none keeps the grade of its own readings, shown lighter (`soft`) and left out of the head.
//
// The head (consistencyHead) works on the cells, as the old headline did: the backswing is P1-P2 to P3-P4, the
// downswing P4-P5 to P6-P7, P7-P8 is left out except for a cell holding an after-ball reading, and under two areas with
// a grade there is no headline. Each half, and each area, takes cellGrade() over its cells' grades.
//
// Pure: value types in and out, safe on any thread. The scales come in through ConsistencyGradeScales, so a test can
// hand it a stub and the production caller wraps an IVariabilityScaleProvider (variability_env.h).

#include "session_consistency.h"

#include "../../Metrics/variability/variability_grader.h"   // CellGrade, VariabilityResult, ScaleType

#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace pinpoint::charts {

class IVariabilityScaleProvider;

// What a reading is graded against beyond its own normSigma, and what kind of number it is. Empty functions: Interval,
// no variabilityScale.
struct ConsistencyGradeScales {
    std::function<variability::ScaleType(const QString &key)>    scaleType;
    std::function<std::optional<double>(const QString &key)>     variabilityScale;
};

// The production scales. `provider` must outlive what this returns.
ConsistencyGradeScales consistencyGradeScales(const IVariabilityScaleProvider &provider);

// One applicable cell.
struct ConsistencyCellGrade {
    QString               area;
    int                   column = -1;
    variability::CellGrade grade;
    std::vector<int>      readings;     // indices into ConsistencyInput::readings at this cell, in (key, column) order
    bool                  afterBall = false;   // P7-P8: the cell holds a reading graded after the ball, so `grade` is theirs
};

struct ConsistencyGrades {
    std::vector<variability::VariabilityResult> readings;   // one per ConsistencyInput::readings, in its order
    std::vector<ConsistencyCellGrade>           cells;      // every applicable cell, consistencyAreas() order then column

    const ConsistencyCellGrade *cellAt(const QString &area, int column) const;
};

// One reading's grade, against `shots` (ConsistencyInput::shots, the reading's rows).
variability::VariabilityResult consistencyReadingGrade(const ConsistencyReading &reading,
                                                       const std::vector<ConsistencyShot> &shots,
                                                       const ConsistencyGradeScales &scales);

ConsistencyGrades consistencyGrades(const ConsistencyInput &input, const ConsistencyGradeScales &scales);

// What the head can say, from the cells (header comment).
//   Halves    both halves have a grade 1..5: "Your backswing is %1, your downswing %2; your %3 moves most."
//   Most      two or more areas have a grade, but a half has none: "Your %3 moves most."
//   Grade0    no headline, and some cell is graded: the Consistency vocabulary's grade-0 line
//   Ungraded  no cell is graded at all: "Nothing here has a range to judge it against."
struct ConsistencyHead {
    enum class Kind { Halves, Most, Grade0, Ungraded };
    Kind                   kind = Kind::Ungraded;
    variability::CellGrade back, down;   // the halves' grades (Halves); `graded` false otherwise
    QString                most;         // the area whose cells read loosest (Halves, Most): the highest loosest-third ρ,
                                         // the first in consistencyAreas() order on a tie
};

ConsistencyHead consistencyHead(const ConsistencyGrades &grades);

} // namespace pinpoint::charts
