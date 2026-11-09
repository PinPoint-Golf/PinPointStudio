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

// CLUB SCOPE — pure, header-only. Which clubs Home's Coaching, Your swing and Matchups read: every club,
// one group, or one club. The rows on a shot are already graded against its own club's corridor, so a
// scope never re-detects or re-grades anything: it only chooses which shots the reductions see.
//
// A key is "all", "group:mid_irons" or "club:7 IRON". A group is turned into its club ids where the
// options are built, so a worker needs nothing but the set.
//
// Unit-tested standalone in src/Analysis/tests/club_scope_test.cpp.

#include "club_distances.h"
#include "diagnostic_ledger.h"
#include "tendencies.h"
#include "../Core/club_vocabulary.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int kClubScopeRuleVersion = 1;

inline const QString &clubScopeAllKey() { static const QString k = QStringLiteral("all"); return k; }
inline QString clubScopeGroupKey(const QString &group) { return QStringLiteral("group:") + group; }
inline QString clubScopeClubKey(const QString &clubId) { return QStringLiteral("club:") + clubId; }

struct ClubScope {
    QString       key = clubScopeAllKey();
    QSet<QString> clubIds;                  // canonical ("7 IRON"); empty for all

    bool isAll() const { return key == clubScopeAllKey(); }
    bool contains(const QString &clubId) const { return isAll() || clubIds.contains(clubId); }
    bool contains(const ShotRecord &shot) const { return contains(canonicalClubId(shot.club)); }
};

// The shots a scope sees, in ledger order.
inline std::vector<ShotRecord> shotsInScope(const std::vector<ShotRecord> &shots, const ClubScope &scope)
{
    std::vector<ShotRecord> out;
    for (const ShotRecord &s : shots)
        if (scope.contains(s)) out.push_back(s);
    return out;
}

// True when every shot is in scope, so the session's stored record is already this scope's answer.
inline bool wholeSessionInScope(const std::vector<ShotRecord> &shots, const ClubScope &scope)
{
    return std::all_of(shots.begin(), shots.end(), [&scope](const ShotRecord &s) { return scope.contains(s); });
}

struct ClubScopeOption {
    QString value;
    QString label;
};

struct ClubScopeGroupOption {
    QString value;
    QString group;
    QString label;
    std::vector<ClubScopeOption> clubs;
    QStringList clubIds;
};

// The groups and clubs among `clubs`, the bag's order within a group. A club groupOf gives no group
// (an undeclared club, a putter) is only ever seen under All.
inline std::vector<ClubScopeGroupOption> clubScopeOptions(const QStringList &clubs,
                                                          const std::function<QString(const QString &)> &groupOf)
{
    QStringList ids;
    for (const QString &c : clubs) {
        const QString id = canonicalClubId(c);
        if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
    }
    const QStringList &bag = clubVocabulary();
    const auto bagRank = [&bag](const QString &c) { const int i = int(bag.indexOf(c)); return i < 0 ? int(bag.size()) : i; };
    std::sort(ids.begin(), ids.end(), [&bagRank](const QString &a, const QString &b) {
        const int ra = bagRank(a), rb = bagRank(b);
        return ra != rb ? ra < rb : a < b;
    });

    std::vector<ClubScopeGroupOption> out;
    for (const QString &id : ids) {
        const QString group = groupOf ? groupOf(id) : QString();
        if (group.isEmpty()) continue;
        auto it = std::find_if(out.begin(), out.end(), [&group](const ClubScopeGroupOption &g) { return g.group == group; });
        if (it == out.end()) {
            out.push_back(ClubScopeGroupOption{ clubScopeGroupKey(group), group, tendencyClubLabel(group), {}, {} });
            it = out.end() - 1;
        }
        it->clubs.push_back(ClubScopeOption{ clubScopeClubKey(id), clubDistanceLabel(id) });
        it->clubIds.append(id);
    }
    std::stable_sort(out.begin(), out.end(), [](const ClubScopeGroupOption &a, const ClubScopeGroupOption &b) {
        return tendencyClubRank(a.group) < tendencyClubRank(b.group);
    });
    return out;
}

// The scope a key names among the options; All when it names none of them.
inline ClubScope resolveClubScope(const QString &key, const std::vector<ClubScopeGroupOption> &options)
{
    ClubScope scope;
    for (const ClubScopeGroupOption &g : options) {
        if (g.value == key) {
            scope.key = key;
            scope.clubIds = QSet<QString>(g.clubIds.begin(), g.clubIds.end());
            return scope;
        }
        for (int i = 0; i < int(g.clubs.size()); ++i)
            if (g.clubs[size_t(i)].value == key) {
                scope.key = key;
                scope.clubIds = { g.clubIds[i] };
                return scope;
            }
    }
    return scope;
}

// A file name's part for a key: "club_7_iron", "group_mid_irons".
inline QString clubScopeFileKey(const QString &key)
{
    QString out;
    for (const QChar c : key.toLower())
        out += c.isLetterOrNumber() ? c : QLatin1Char('_');
    return out;
}

} // namespace pinpoint::analysis
