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

// THE TENDENCIES OVER SESSIONS ALREADY READ, with the per-club distances — ONE RULE for its two callers (refactor step
// 3.6, round 2; audit D-17): AthleteDiagnostics, over the session summaries (src/DomainModel/athlete_diagnostics.h), and,
// until the catch-up's cut-over (3.7), WorkOnsController over the ledgers. Moved here from WorkOnsController unchanged:
// the club group by where the club resolves in the context tree (§7.1 of shot_outcomes_design.md), reduceTendencies,
// then reduceClubDistances over the same sessions.
//
// Rules ring (src/Diagnostics/derived): pure; reads the context tree it is handed.

#include "club_distances.h"
#include "tendencies.h"
#include "../model/context_tree.h"

#include <QJsonArray>

#include <atomic>
#include <vector>

namespace pinpoint::analysis {

// §7.1's club groups, by where the club resolves in the context tree. "" = a putter or an undeclared club.
inline QString tendencyClubGroup(const ContextTree &tree, const QString &club)
{
    const QString id = contextIdForClub(club);
    const auto under = [&tree, &id](const char *root) {
        const QString r = QString::fromLatin1(root);
        return id == r || tree.isDescendantOf(id, r);
    };
    if (under("driver"))       return QStringLiteral("driver");
    if (under("fairway_wood")) return QStringLiteral("woods");
    if (id == QLatin1String("wedge_pitching") || under("iron")) return tendencyIronGroup(club);
    if (under("wedge"))        return QStringLiteral("wedges");
    return QString();
}

// The reduction over sessions already read, with the per-club distances from those same sessions (`sessions` is moved
// from: its shots go to the distances). `flight` is the table every shot's height is judged against.
inline TendencySet tendencySetFromSessions(std::vector<TendencySessionInput> &sessions, const ShotOutcomePack &pack,
                                           const ContextTree &tree, const FlightNorms &flight,
                                           const std::atomic<bool> *cancel = nullptr)
{
    TendencySet set = reduceTendencies(sessions, pack, [&tree](const QString &club) { return tendencyClubGroup(tree, club); },
                                       flight, TendencyOptions{}, cancel);
    if (set.cancelled || (cancel && cancel->load())) return set;

    std::vector<ClubDistanceSession> clubSessions;
    clubSessions.reserve(sessions.size());
    for (TendencySessionInput &s : sessions) clubSessions.push_back(std::move(s.session));
    QJsonArray distances;
    for (const ClubDistanceItem &it : reduceClubDistances(clubSessions, {}, flight)) distances.append(clubDistanceItemToJson(it));
    set.clubDistances = distances;
    return set;
}

} // namespace pinpoint::analysis
