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

// THE REPORT'S TENDENCIES PAGE — the map's `tendencies` block (docs/design/shot_outcomes_design.md §7.2): Home's Tendencies
// rows on paper, for the tendencies as they stood at the report's session (WorkOnsController::tendenciesAsOf). Pure and
// header-only. Absent (empty) when no club has a row. A building row states no verdict: it says it is building, and how
// far along. Every sentence is here; the page sets only the column heads.
//
//  tendencies: { kicker ("68 shots  ·  6 sessions  ·  Tendencies"), standfirst, foot ("" when no factor is building),
//                rows: [{ key, club ("7 iron · part swing"), distance ("147 ± 31 yd" | ""),
//                         strike, direction, flight: { label ("Pull", "Solid", "-"), fault, watch (Low), building,
//                                 seen ("Seen with …  ·  likely  ·  ↑ growing" | "Building, 5 sessions so far" | ""),
//                                 mixLine, mixParts: [{ count, kind }] } }] }
//
// W4: the three factor maps are the minimum for this to compile against the three-factor TendencyItem
// (shot_factors.h); the page's columns and words are W4's.

#include "../../Coaching/tendencies.h"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace pinpoint::report {

inline QVariantMap tendenciesReportBlock(const pinpoint::analysis::TendencySet &set)
{
    using namespace pinpoint::analysis;
    if (set.items.empty()) return {};

    const auto counted = [](int n, const char *one, const char *many) {
        return QStringLiteral("%1 %2").arg(n).arg(QString::fromLatin1(n == 1 ? one : many));
    };
    const TendencyOptions opt;

    QVariantList rows;
    bool anyBuilding = false;
    const auto factorMap = [&counted, &anyBuilding](const TendencyFactor &f) {   // W4:
        anyBuilding = anyBuilding || (f.building && f.shots > 0);
        QString seen;
        if (f.building && f.shots > 0) {
            seen = QStringLiteral("Building, %1 so far").arg(counted(f.sessions, "session", "sessions"));
        } else if (f.fault && !f.seenWith.isEmpty()) {
            seen = QStringLiteral("Seen with %1").arg(f.seenWith);
            if (!f.strength.isEmpty())   seen += QStringLiteral("  ·  ") + f.strength;
            if (!f.trendWords.isEmpty()) seen += QStringLiteral("  ·  ") + f.trendWords;
        }
        QVariantList parts;
        for (const ShotMixPart &p : f.mix.parts)
            parts.append(QVariantMap{ { QStringLiteral("count"), p.count }, { QStringLiteral("kind"), p.kind } });
        return QVariantMap{
            { QStringLiteral("label"),    f.label },
            { QStringLiteral("fault"),    f.fault },
            { QStringLiteral("watch"),    !f.fault && shotFactorValueWatch(f.value) },
            { QStringLiteral("building"), f.building },
            { QStringLiteral("seen"),     seen },
            { QStringLiteral("mixLine"),  f.mix.line },
            { QStringLiteral("mixParts"), parts },
        };
    };
    for (const TendencyItem &it : set.items) {
        rows.append(QVariantMap{
            { QStringLiteral("key"),       it.key },
            { QStringLiteral("club"),      it.length == QLatin1String("partial") ? it.club + QStringLiteral(" · part swing") : it.club },
            { QStringLiteral("distance"),  tendencyUsualDistance(it.clubId, it.length, set.clubDistances) },
            { QStringLiteral("strike"),    factorMap(it.strike) },
            { QStringLiteral("direction"), factorMap(it.direction) },
            { QStringLiteral("flight"),    factorMap(it.flight) },
        });
    }

    return {
        { QStringLiteral("kicker"), QStringLiteral("%1  ·  %2  ·  Tendencies").arg(counted(set.countedShots, "shot", "shots"),
                                                                                   counted(set.countedSessions, "session", "sessions")) },
        { QStringLiteral("standfirst"), QStringLiteral("How each club has been behaving, from your sessions up to and including this one.") },
        { QStringLiteral("foot"), anyBuilding ? QStringLiteral("Building: this is what has shown up so far, not a verdict. A club's miss becomes a tendency after %1 of %2 or more shots.")
                                                    .arg(counted(opt.minSessions, "session", "sessions")).arg(opt.minSessionShots)
                                              : QString() },
        { QStringLiteral("rows"), rows },
    };
}

} // namespace pinpoint::report
