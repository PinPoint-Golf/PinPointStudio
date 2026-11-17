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

// WITHIN-CLUB POOLING (Mark's ruling R2a, 7 Nov; docs/design/system/variability.md, "Where a consistency mark is allowed:
// Clubs") — the maths that centres each club's swings on that club's own median so spread is never graded across clubs.
// Moved here from src/Gui/charts (consistencyResiduals in session_consistency.cpp; centreCircular and the scalar
// pooledWithinClubInput in variability_clubs.cpp) in refactor step 3.8, UNCHANGED, so the Rules ring's consistency grade
// (src/Coaching/consistency_grade.h; J-34) uses it; the Charts code keeps thin calls to these. Vocabulary: pure maths,
// header-only, safe on any thread.
//
//   withinClubResiduals   each value less its club's median at each sample (the swing itself included); a club with
//                         fewer than kPoolMinClubShots (3) swings carrying its label is left out (NaN). A blank label
//                         is a club of its own. `values` is clubs.size() × samples, row-major
//   centreCircularByClub  ScaleType::Circular only: each club's values wrapped about that club's circular mean at each
//                         sample first, so a club that straddles ±180° is not split by the seam
//   withinClubPooledInput R2a for scalar readings (Charts' pooledWithinClubInput, under its own name: the Charts one
//                         takes this file's ClubReading, so one name would be ambiguous by argument lookup): the residuals of the swings that count, strike order; `base` is copied
//                         (the scale type, a propagated σ, …), only the values replaced

#include "variability_grader.h"   // VariabilityInput, ScaleType, circularMeanDeg, wrapDeg
#include "../box_stats.h"          // quantile7: the one median

#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace pinpoint::variability {

inline constexpr int kPoolMinClubShots = 3;   // doc §4.1: a club under 3 shots is left out, counting every shot

// One swing's scalar reading. A non-finite value is a missing reading.
struct ClubReading {
    QString club;
    double  value = std::numeric_limits<double>::quiet_NaN();
};

inline std::vector<double> withinClubResiduals(const std::vector<double> &values, int samples, const QStringList &clubs)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> out(values.size(), kNaN);
    if (samples <= 0) return out;
    std::map<QString, std::vector<int>> groups;     // a blank label is a group of its own
    for (int i = 0; i < clubs.size(); ++i) groups[clubs.at(i)].push_back(i);
    std::vector<double> column;
    for (const auto &[club, rows] : groups) {
        if (int(rows.size()) < kPoolMinClubShots) continue;
        for (int j = 0; j < samples; ++j) {
            column.clear();
            for (int i : rows) column.push_back(values[size_t(i) * size_t(samples) + size_t(j)]);
            const double median = box::quantile7(column, 0.5);     // the shot itself included
            for (int i : rows) {
                const size_t at = size_t(i) * size_t(samples) + size_t(j);
                out[at] = values[at] - median;
            }
        }
    }
    return out;
}

inline void centreCircularByClub(std::vector<double> &values, int samples, const QStringList &clubs)
{
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    std::map<QString, std::vector<int>> groups;
    for (int i = 0; i < clubs.size(); ++i) groups[clubs.at(i)].push_back(i);
    std::vector<double> column;
    for (const auto &[club, rows] : groups) {
        for (int j = 0; j < samples; ++j) {
            column.clear();
            for (int i : rows) {
                const double x = values[size_t(i) * size_t(samples) + size_t(j)];
                if (std::isfinite(x)) column.push_back(x);
            }
            const double centre = circularMeanDeg(column);
            for (int i : rows) {
                double &x = values[size_t(i) * size_t(samples) + size_t(j)];
                x = std::isfinite(centre) && std::isfinite(x) ? wrapDeg(x - centre) : kNaN;
            }
        }
    }
}

// The residuals under `type`: centred on each club's circular mean first when circular.
inline std::vector<double> withinClubResidualsOf(std::vector<double> values, int samples, const QStringList &clubs,
                                                 ScaleType type)
{
    if (type == ScaleType::Circular) centreCircularByClub(values, samples, clubs);
    return withinClubResiduals(values, samples, clubs);
}

inline VariabilityInput withinClubPooledInput(const std::vector<ClubReading> &swings, const VariabilityInput &base = {})
{
    std::vector<double> values;
    QStringList clubs;
    for (const ClubReading &s : swings) { values.push_back(s.value); clubs << s.club; }
    const std::vector<double> residuals = withinClubResidualsOf(std::move(values), 1, clubs, base.scaleType);

    VariabilityInput in = base;
    in.values.clear();
    for (double r : residuals) if (std::isfinite(r)) in.values.push_back(r);
    return in;
}

} // namespace pinpoint::variability
