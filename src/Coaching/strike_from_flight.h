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

// A bad strike read from the flight, for a monitor that doesn't report where the ball met the face
// (shot_outcomes_design.md §3.7). A shank has a signature of its own: a ball that leaves low and sharp
// right. Fat, thin, top and sky show in the launch, the attack angle and the smash.
//
// Everything is judged against this club's median in this session rather than the book's window for the
// club. Club labels are often wrong (a wedge filed as a 7 iron), and the session's own shots are right
// about what that club does however it was labelled. Only a bad strike shows up this way: a contact
// 10 mm low on the face usually flies normally, so "nothing inferred" does not mean "struck well".
//
// Measured always wins: a label is inferred only where the row that would have measured it is not
// assessable. The rows are never touched, so detection, corridors and tiers don't change.

#include "diagnostic_ledger.h"
#include "../Core/club_vocabulary.h"

#include <QHash>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

inline constexpr int    kFlightUsualMinShots    = 4;     // of this club in this session, per reading
inline constexpr double kFlightLowLaunchShare   = 0.50;  // launch under half of usual
inline constexpr double kFlightHighLaunchShare  = 1.50;  // launch at least 1.5 × usual
inline constexpr double kFlightShankStartDeg    = 8.0;   // starts this far to the golfer's hosel side
inline constexpr double kFlightTopSmashShare    = 0.85;  // smash at least 15 % below usual
inline constexpr double kFlightFatSmashShare    = 0.91;  // smash at least 9 % below usual
inline constexpr double kFlightFatSteeperDeg    = 1.5;   // attack this much steeper than usual
inline constexpr double kFlightShortCarryShare  = 0.70;  // carry under this share of usual is a mishit

// What this club does in this session. A reading with too few shots stays NaN.
struct ClubFlightUsual {
    double launch = std::numeric_limits<double>::quiet_NaN();
    double smash  = std::numeric_limits<double>::quiet_NaN();
    double attack = std::numeric_limits<double>::quiet_NaN();
    double carry  = std::numeric_limits<double>::quiet_NaN();
};

inline double flightMedian(std::vector<double> values)
{
    if (int(values.size()) < kFlightUsualMinShots) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2.0;
}

// Each club's usual flight in this session, keyed by canonical club. Part swings fly differently and are
// left out.
inline QHash<QString, ClubFlightUsual> sessionFlightUsuals(const std::vector<ShotRecord> &shots)
{
    struct Pool { std::vector<double> launch, smash, attack, carry; };
    QHash<QString, Pool> pools;
    for (const ShotRecord &s : shots) {
        if (s.swingLength == QLatin1String("partial")) continue;
        const QString club = canonicalClubId(s.club);
        if (club.isEmpty()) continue;
        Pool &p = pools[club];
        if (std::isfinite(s.launchAngle)) p.launch.push_back(s.launchAngle);
        if (std::isfinite(s.smash))       p.smash.push_back(s.smash);
        if (std::isfinite(s.attackAngle)) p.attack.push_back(s.attackAngle);
        if (std::isfinite(s.carry))       p.carry.push_back(s.carry);
    }
    QHash<QString, ClubFlightUsual> out;
    for (auto it = pools.constBegin(); it != pools.constEnd(); ++it)
        out.insert(it.key(), ClubFlightUsual{ flightMedian(it->launch), flightMedian(it->smash),
                                              flightMedian(it->attack), flightMedian(it->carry) });
    return out;
}

// True when the row exists and couldn't be assessed: the monitor didn't report what it reads.
inline bool strikeRowUnread(const ShotRecord &s, const char *conditionId)
{
    const ConditionRow *r = rowFor(s, QLatin1String(conditionId));
    return r && r->state == ShotState::NotAssessable;
}

// Neither fat nor thin could be measured: the monitor gave no strike height.
inline bool strikeHeightUnread(const ShotRecord &s)
{
    return strikeRowUnread(s, "chunk") && strikeRowUnread(s, "thin");
}

// The strike the flight points to ("shank", "thin", "top", "chunk", "sky"), or "" for none. A reading
// that's missing rules out only the labels that need it.
inline QString strikeFromFlight(const ShotRecord &s, const ClubFlightUsual &usual)
{
    if (s.swingLength == QLatin1String("partial")) return QString();
    if (!std::isfinite(s.launchAngle) || !std::isfinite(usual.launch) || !(usual.launch > 0.0)) return QString();

    const bool heightUnread = strikeHeightUnread(s);
    const bool haveSmash    = std::isfinite(s.smash) && std::isfinite(usual.smash) && usual.smash > 0.0;
    const bool haveAttack   = std::isfinite(s.attackAngle) && std::isfinite(usual.attack);

    if (s.launchAngle < kFlightLowLaunchShare * usual.launch) {
        // Measures are never mirrored, so the hosel side is the other way for a left-hander.
        const double towardHosel = s.handedness == QLatin1String("Left") ? -s.startDirection : s.startDirection;
        if (strikeRowUnread(s, "shank") && std::isfinite(towardHosel) && towardHosel >= kFlightShankStartDeg)
            return QStringLiteral("shank");
        if (!heightUnread) return QString();
        return haveSmash && s.smash <= kFlightTopSmashShare * usual.smash ? QStringLiteral("top") : QStringLiteral("thin");
    }

    if (!heightUnread || !haveSmash || !haveAttack) return QString();

    if (s.launchAngle >= kFlightHighLaunchShare * usual.launch)
        return s.attackAngle < usual.attack && s.smash < usual.smash ? QStringLiteral("sky") : QString();

    if (s.smash <= kFlightFatSmashShare * usual.smash && s.attackAngle <= usual.attack - kFlightFatSteeperDeg)
        return QStringLiteral("chunk");
    return QString();
}

// The strike the flight points to for this shot in its session, "" when the club has no usual yet.
inline QString strikeFromFlight(const ShotRecord &s, const QHash<QString, ClubFlightUsual> &usuals)
{
    const auto it = usuals.constFind(canonicalClubId(s.club));
    return it == usuals.constEnd() ? QString() : strikeFromFlight(s, *it);
}

} // namespace pinpoint::analysis
