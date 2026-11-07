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

#include "consistency_grades.h"

#include "variability_clubs.h"      // pooledWithinClubInput (R2a)
#include "variability_env.h"        // IVariabilityScaleProvider

#include <algorithm>
#include <cmath>
#include <map>

namespace pinpoint::charts {

namespace {

namespace pv = pinpoint::variability;

const QString kFinish = QStringLiteral("finish");

bool positive(double x) { return std::isfinite(x) && x > 0.0; }

// A cell's grade as a result, so the halves and the areas go through cellGrade() as a cell's readings do.
pv::VariabilityResult asResult(const pv::CellGrade &c)
{
    pv::VariabilityResult r;
    r.basis = pv::Basis::Scalar;
    r.graded = c.graded;
    r.grade = c.grade;
    r.rho = c.rho;
    r.measurementLimited = c.measurementLimited;
    r.n = c.n;
    if (c.graded && c.grade == 0) r.ungradedReason = pv::UngradedReason::TooFewSwings;
    return r;
}

} // namespace

ConsistencyGradeScales consistencyGradeScales(const IVariabilityScaleProvider &provider)
{
    const IVariabilityScaleProvider *p = &provider;
    return { [p](const QString &key) { return p->scaleType(key); },
             [p](const QString &key) { return p->variabilityScale(key); } };
}

const ConsistencyCellGrade *ConsistencyGrades::cellAt(const QString &area, int column) const
{
    for (const ConsistencyCellGrade &c : cells) if (c.area == area && c.column == column) return &c;
    return nullptr;
}

pv::VariabilityResult consistencyReadingGrade(const ConsistencyReading &reading, const std::vector<ConsistencyShot> &shots,
                                              const ConsistencyGradeScales &scales)
{
    static const auto scalar = pv::makeVariabilityGrader(pv::Basis::Scalar);     // pure maths, no state

    // gate rule: PP_GATE("rule.charts.consistencyVerdict") — grade 0 under N_MIN, ungraded with no scale
    pv::VariabilityInput base;
    if (positive(reading.normSigma)) base.corridorHalfWidth = reading.normSigma;
    if (scales.variabilityScale) base.variabilityScale = scales.variabilityScale(reading.key);
    base.scaleType = scales.scaleType ? scales.scaleType(reading.key) : pv::ScaleType::Interval;
    if (positive(reading.ownSigma)) base.propagatedSigma = reading.ownSigma;      // R3: never noiseSd

    // A guarded shot is left out of a reading that rests on its P-ladder (a span, the backswing timing): its value there
    // cannot be trusted, so it is no swing of the window at all, not even towards its club's count (R13; the
    // stand-outs, the Charts windows and the traces leave it out alike).
    const bool ladder = consistencyGuardCovers(reading);
    std::vector<pv::VariabilityResult> points;
    std::vector<VariabilityClubValue> swings;
    swings.reserve(shots.size());
    for (int j = 0; j < std::max(1, reading.samples); ++j) {
        swings.clear();
        for (size_t i = 0; i < shots.size(); ++i)
            if (!(ladder && shots[i].guarded)) swings.push_back(VariabilityClubValue{ shots[i].club, reading.at(int(i), j) });
        points.push_back(scalar->grade(pooledWithinClubInput(swings, base)));
    }
    if (points.size() == 1) return points.front();

    // a span: its median point by ρ
    std::vector<const pv::VariabilityResult *> withRho;
    for (const pv::VariabilityResult &r : points)
        if (r.graded && r.grade >= 1 && std::isfinite(r.rho)) withRho.push_back(&r);
    if (!withRho.empty()) {
        std::stable_sort(withRho.begin(), withRho.end(), [](const auto *a, const auto *b) { return a->rho < b->rho; });
        return *withRho[(withRho.size() - 1) / 2];
    }
    for (const pv::VariabilityResult &r : points) if (r.graded) return r;
    return points.front();
}

ConsistencyGrades consistencyGrades(const ConsistencyInput &in, const ConsistencyGradeScales &scales)
{
    ConsistencyGrades out;
    out.readings.reserve(in.readings.size());
    std::map<std::pair<QString, int>, std::vector<int>> at;
    for (size_t r = 0; r < in.readings.size(); ++r) {
        const ConsistencyReading &reading = in.readings[r];
        out.readings.push_back(consistencyReadingGrade(reading, in.shots, scales));
        at[{ reading.area, reading.column }].push_back(int(r));
    }
    for (const QString &area : consistencyAreas())
        for (int column : consistencyColumnsOf(area)) {
            ConsistencyCellGrade cell;
            cell.area = area;
            cell.column = column;
            cell.readings = at[{ area, column }];
            std::vector<pv::VariabilityResult> results;
            for (int r : cell.readings) results.push_back(out.readings[size_t(r)]);
            // gate rule: PP_GATE("rule.charts.consistencyCell") — readings but none graded: "-"
            cell.grade = pv::cellGrade(results);
            out.cells.push_back(std::move(cell));
        }
    return out;
}

ConsistencyHead consistencyHead(const ConsistencyGrades &grades)
{
    ConsistencyHead head;
    std::vector<pv::VariabilityResult> back, down;
    std::map<QString, std::vector<pv::VariabilityResult>> areas;
    bool anyGraded = false;
    for (const ConsistencyCellGrade &c : grades.cells) {
        anyGraded = anyGraded || c.grade.graded;
        if (!c.grade.graded || c.grade.grade < 1) continue;
        if (c.column == kConsistencyAfterBallColumn && c.area != kFinish) continue;     // after the ball has gone
        const pv::VariabilityResult r = asResult(c.grade);
        areas[c.area].push_back(r);
        if (c.column >= kConsistencyBackswingFirst && c.column <= kConsistencyBackswingLast) back.push_back(r);
        if (c.column >= kConsistencyDownswingFirst && c.column <= kConsistencyDownswingLast) down.push_back(r);
    }
    if (int(areas.size()) < kConsistencyHeadlineMinAreas) {     // one area's reading is not the swing
        head.kind = anyGraded ? ConsistencyHead::Kind::Grade0 : ConsistencyHead::Kind::Ungraded;
        return head;
    }
    double top = -1.0;
    for (const QString &area : consistencyAreas()) {
        const auto it = areas.find(area);
        if (it == areas.end()) continue;
        const pv::CellGrade g = pv::cellGrade(it->second);
        if (g.graded && std::isfinite(g.rho) && g.rho > top) { top = g.rho; head.most = area; }
    }
    head.back = pv::cellGrade(back);
    head.down = pv::cellGrade(down);
    const bool halves = head.back.graded && head.back.grade >= 1 && head.down.graded && head.down.grade >= 1;
    head.kind = halves ? ConsistencyHead::Kind::Halves : ConsistencyHead::Kind::Most;
    return head;
}

} // namespace pinpoint::charts
