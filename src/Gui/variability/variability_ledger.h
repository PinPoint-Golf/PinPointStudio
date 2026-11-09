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

// THE CONSISTENCY OF A CONDITION'S MEASURE OVER LEDGER ROWS (docs/design/system/variability.md; Mark's rulings R2a,
// R5, R10, R12, R13, R14, 7 Nov): the one rule Home's work-ons (WorkOnsController::runMarks: a fault's last 20 readings
// across the golfer's ledgers) and the report's work-on page (SessionReportModel: that session's readings) grade by.
// Pure: value types in and out, safe on any thread, no pack, no norms (the scale comes in through a callback).
//
//   A READING is one shot's row of the condition (ledgerConsistencyReading):
//     - the row the ledger could ASSESS, with a finite value. The ledger's own data-integrity exclusion (frames lost
//       during capture, an IMU record that does not reproduce) already made every row of such a shot NotAssessable
//       (SessionDiagnosticsModel, "Data-integrity exclusion"), so that is how a broken recording is left out. The
//       shot's `dataWarning` flag is NOT a filter: it also marks warnings that withhold nothing (a refused club
//       track), whose rows the ledger counts and Home's habit counts show;
//     - EVERY SHOT STAYS IN (R13): the golfer is warmed up before PinPoint assesses them;
//     - only a row whose DRIVING measure is the measure graded;
//     - `noBand`: judged against an authored number (faultAt), against no band, or a composite's reading.
//   THE GRADE (ledgerConsistencyGrade): pooled within club (R2a, pooledWithinClubInput) on the scalar basis, against
//   the measure's norm corridor half-width (σLo + σHi)/2 in each reading's own context, the median over the readings
//   (R12: whatever the norm's shape). No result (nullopt) when the caller says the condition is ungraded (no measure,
//   yes/no, a composite measure, an outcome: R4), when any reading has no band, when no context has a norm, or when the
//   grader grades nothing (no scale). Under N_MIN (8) readings the grader's grade 0 ("Not enough swings") stands: that
//   IS a result. A ledger row carries no per-reading σ, so no propagatedSigma.

#include "../charts/variability_clubs.h"                     // pooledWithinClubInput (R2a)
#include "../../Coaching/diagnostic_ledger.h"                // ShotRecord, ConditionRow, rowFor
#include "../../Metrics/variability/variability_grader.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace pinpoint::variability {

struct LedgerConsistencyReading {
    QString club;
    QString contextId;          // the row's corridor context, else the shot's
    double  value = 0.0;
    bool    noBand = false;     // judged against an authored number (faultAt), no band, or a composite's reading
};

// `row` (one of `shot`'s) as a reading of `measureId`, or nullopt when the rule leaves it out (header).
inline std::optional<LedgerConsistencyReading> ledgerConsistencyReading(const analysis::ShotRecord &shot,
                                                                        const analysis::ConditionRow &row,
                                                                        const QString &measureId)
{
    if (measureId.isEmpty() || row.drivingMeasureId != measureId) return std::nullopt;
    if (row.state == analysis::ShotState::NotAssessable || !std::isfinite(row.value)) return std::nullopt;
    LedgerConsistencyReading r;
    r.club      = shot.club;
    r.contextId = row.contextId.isEmpty() ? shot.contextId : row.contextId;
    r.value     = row.value;
    r.noBand    = std::isfinite(row.faultAt) || row.corridorShape == analysis::CorridorShape::None || row.composite.present();
    return r;
}

// The readings of `conditionId` on `measureId` over `shots`, in their order.
inline std::vector<LedgerConsistencyReading> ledgerConsistencyReadings(const std::vector<const analysis::ShotRecord *> &shots,
                                                                       const QString &conditionId, const QString &measureId)
{
    std::vector<LedgerConsistencyReading> out;
    for (const analysis::ShotRecord *s : shots) {
        if (!s) continue;
        const analysis::ConditionRow *row = analysis::rowFor(*s, conditionId);
        if (!row) continue;
        if (std::optional<LedgerConsistencyReading> r = ledgerConsistencyReading(*s, *row, measureId)) out.push_back(std::move(*r));
    }
    return out;
}

// The grade of `readings` (strike order), or nullopt for no badge (header). `halfWidthOf(contextId)`: the measure's norm
// corridor half-width in that context (normCorridorHalfWidth), NaN with no norm.
inline std::optional<VariabilityResult> ledgerConsistencyGrade(const std::vector<LedgerConsistencyReading> &readings,
                                                               bool ungraded, ScaleType scaleType,
                                                               const std::function<double(const QString &contextId)> &halfWidthOf)
{
    if (ungraded || readings.empty() || !halfWidthOf) return std::nullopt;
    if (std::any_of(readings.begin(), readings.end(), [](const LedgerConsistencyReading &r) { return r.noBand; })) return std::nullopt;
    std::vector<double> halves;
    for (const LedgerConsistencyReading &r : readings) {
        const double h = halfWidthOf(r.contextId);
        if (std::isfinite(h) && h > 0.0) halves.push_back(h);
    }
    if (halves.empty()) return std::nullopt;
    std::sort(halves.begin(), halves.end());
    const size_t mid = halves.size() / 2;
    VariabilityInput base;
    base.corridorHalfWidth = halves.size() % 2 ? halves[mid] : 0.5 * (halves[mid - 1] + halves[mid]);
    base.scaleType = scaleType;
    std::vector<charts::VariabilityClubValue> swings;
    swings.reserve(readings.size());
    for (const LedgerConsistencyReading &r : readings) swings.push_back({ r.club, r.value });
    const VariabilityResult result = makeVariabilityGrader(Basis::Scalar)->grade(charts::pooledWithinClubInput(swings, base));
    if (!result.graded) return std::nullopt;
    return result;
}

} // namespace pinpoint::variability
