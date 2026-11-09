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

// The club scope (club_scope.h): which shots a scope sees, its options and how a key resolves; and that
// the quick half of the swing summary is exactly Layer 1 of the full reduction.

#include "../../Coaching/club_scope.h"
#include "../../Coaching/swing_themes.h"

#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static ShotRecord shot(int id, const char *club)
{
    ShotRecord s;
    s.shotId = id;
    s.club   = QString::fromLatin1(club);
    return s;
}

static QString groupOf(const QString &club)
{
    if (club.contains(QLatin1String("DRIVER"))) return QStringLiteral("driver");
    if (club.contains(QLatin1String("IRON")))   return tendencyIronGroup(club);
    if (club.contains(QLatin1String("WEDGE")))  return QStringLiteral("wedges");
    return QString();
}

// A swing on which `fired` fires and `quiet` is judged clean.
static ShotRecord swing(int id, const char *club, const QStringList &fired, const QStringList &quiet)
{
    ShotRecord s = shot(id, club);
    const auto row = [](const QString &cond, ShotState state) {
        ConditionRow r;
        r.conditionId = cond;
        r.drivingMeasureId = QStringLiteral("m_") + cond;
        r.state = state;
        r.value = state == ShotState::Fired ? 10.0 : 1.0;
        r.corridorLo = 0.0;
        r.corridorHi = 4.0;
        r.corridorShape = CorridorShape::Ceiling;
        return r;
    };
    for (const QString &c : fired) s.rows.push_back(row(c, ShotState::Fired));
    for (const QString &c : quiet) s.rows.push_back(row(c, ShotState::Clean));
    return s;
}

int main()
{
    std::printf("club_scope_test\n");

    const std::vector<ClubScopeGroupOption> options =
        clubScopeOptions({ "7 IRON", "DRIVER", "6 IRON", "GAP WEDGE", "PUTTER", "", "7 IRON" }, groupOf);

    std::printf("\nthe options:\n");
    {
        check(options.size() == 3, "only the groups the golfer hit, a putter and an undeclared club in none");
        check(options[0].value == QStringLiteral("group:driver") && options[1].value == QStringLiteral("group:mid_irons")
                  && options[2].value == QStringLiteral("group:wedges"), "the groups in the bag's order");
        check(options[1].label == QStringLiteral("Mid irons"), "a group's label");
        check(options[1].clubs.size() == 2 && options[1].clubs[0].value == QStringLiteral("club:6 IRON")
                  && options[1].clubs[1].label == QStringLiteral("7 iron"), "the clubs in the bag's order, once each, in words");
    }

    std::printf("\nwhat a key resolves to:\n");
    {
        check(resolveClubScope(QStringLiteral("all"), options).isAll(), "all");
        const ClubScope group = resolveClubScope(QStringLiteral("group:mid_irons"), options);
        check(group.key == QStringLiteral("group:mid_irons") && group.clubIds == QSet<QString>{ "6 IRON", "7 IRON" }, "a group is its clubs");
        const ClubScope club = resolveClubScope(QStringLiteral("club:GAP WEDGE"), options);
        check(club.clubIds == QSet<QString>{ "GAP WEDGE" }, "a club is itself");
        check(resolveClubScope(QStringLiteral("club:4 IRON"), options).isAll(), "a club this golfer has not hit reads as all");
        check(clubScopeFileKey(QStringLiteral("club:7 IRON")) == QStringLiteral("club_7_iron"), "a key as part of a file name");
    }

    std::printf("\nthe shots a scope sees:\n");
    {
        const std::vector<ShotRecord> mixed{ shot(1, "DRIVER"), shot(2, "7 IRON"), shot(3, "6 IRON"), shot(4, "Driver"), shot(5, "7 IRON") };
        const ClubScope mid = resolveClubScope(QStringLiteral("group:mid_irons"), options);
        const std::vector<ShotRecord> in = shotsInScope(mixed, mid);
        check(in.size() == 3 && in[0].shotId == 2 && in[1].shotId == 3 && in[2].shotId == 5, "a group's shots, in ledger order");
        const ClubScope driver = resolveClubScope(QStringLiteral("club:DRIVER"), options);
        check(shotsInScope(mixed, driver).size() == 2, "matched by canonical club, whatever the stored spelling");
        check(shotsInScope(mixed, ClubScope()).size() == 5, "all sees every shot");
        check(!wholeSessionInScope(mixed, mid), "a mixed session is not wholly in scope");
        check(wholeSessionInScope({ shot(1, "6 IRON"), shot(2, "7 IRON") }, mid), "a session of nothing else is");
    }

    std::printf("\nthe quick half is Layer 1:\n");
    {
        QHash<QString, ThemeConditionInfo> conditions;
        for (const char *id : { "sway", "early" }) {
            ThemeConditionInfo c;
            c.id = QString::fromLatin1(id);
            c.fault = true;
            c.detection = kThemeDetectionAny;
            c.prominence = 3;
            c.families = QStringList{ c.id };
            conditions.insert(c.id, c);
        }
        std::vector<ThemeSessionInput> sessions(3);
        for (int s = 0; s < 3; ++s) {
            sessions[size_t(s)].name = QStringLiteral("s%1").arg(s);
            for (int i = 0; i < 12; ++i)
                sessions[size_t(s)].shots.push_back(i % 4 ? swing(i + 1, "7 IRON", { "sway" }, { "early" })
                                                          : swing(i + 1, "7 IRON", {}, { "sway", "early" }));
        }
        ThemeOptions fast;
        fast.paShuffles = 5;
        fast.boot = 5;
        const SwingThemes quick = reduceSwingSummary(sessions, conditions, fast);
        const SwingThemes full  = reduceSwingThemes(sessions, {}, conditions, fast);
        check(quick.allSwings == 36 && full.allSwings == 36 && quick.allSessions == 3, "every swing and session counted");
        bool same = quick.seenMost.size() == full.seenMost.size() && !quick.seenMost.empty();
        for (size_t i = 0; same && i < quick.seenMost.size(); ++i)
            same = quick.seenMost[i].conditionId == full.seenMost[i].conditionId && quick.seenMost[i].share == full.seenMost[i].share;
        check(same, "what is seen most is the full reduction's");
        same = quick.doWell.size() == full.doWell.size();
        for (size_t i = 0; same && i < quick.doWell.size(); ++i) same = quick.doWell[i].conditionId == full.doWell[i].conditionId;
        check(same, "what you do well is the full reduction's");
        same = quick.focusOrder.size() == full.focusOrder.size() && !quick.focusOrder.empty();
        for (size_t i = 0; same && i < quick.focusOrder.size(); ++i) same = quick.focusOrder[i].conditionIds == full.focusOrder[i].conditionIds;
        check(same, "the focus order is the full reduction's");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
