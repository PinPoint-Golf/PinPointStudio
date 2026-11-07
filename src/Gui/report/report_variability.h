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

// THE REPORT'S VARIABILITY MARKS (docs/design/system/variability.md; Mark's rulings R4b, R10, R14, R19, 7 Nov), the pure
// half: what SessionReportModel::build() copies, as value types in and out, so report_model_test can hold them without
// the diagnostics stack.
//
//   A WORK-ON's consistency (R10, R14): that session's readings of the work-on condition's driving measure, graded by
//   the rule Home grades a fault's window by: src/Gui/variability/variability_ledger.h (ledgerConsistencyReadings,
//   ledgerConsistencyGrade), asked of one session.
//   A LAUNCH MONITOR summary row (R4b, R19): the row carries its club's `variability` map as the panel's model published
//   it (LmSessionModel: club-delivery readings only, never an outcome, isOutcomeMetric), or null.

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace pinpoint::report {

// One row of the launch monitor appendix's "Averages and spread, per club": the reading's label and unit, its figures,
// and the `variability` map its club's stats carry (LmSessionModel's, copied as published), else null.
inline QVariantMap lmSummaryRow(const QString &label, const QString &unit, const QStringList &cells, const QVariantMap &stat)
{
    const QVariantMap v = stat.value(QStringLiteral("variability")).toMap();
    return { { QStringLiteral("label"), label }, { QStringLiteral("unit"), unit }, { QStringLiteral("cells"), cells },
             { QStringLiteral("variability"), v.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(v) } };
}

} // namespace pinpoint::report
