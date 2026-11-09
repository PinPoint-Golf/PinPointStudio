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

#include <QHash>
#include <QString>

#include <limits>
#include <memory>
#include <optional>
#include <utility>

// THE ONE PATH FROM A GOLFER'S HISTORY TO A FINDING (docs/design/shot_outcomes_design.md §3.3, §1 rule
// 5). "Short" and "lost ball speed" are judged against the golfer's own usual numbers with that club;
// every other corridor stays population-based, and a test holds it to these two measures. The numbers
// are the session's frozen baseline, read from its sidecar by whoever constructs the provider.

namespace pinpoint::analysis {

struct ClubBaselineEntry {
    QString club;              // canonical bag id
    double carryMean = std::numeric_limits<double>::quiet_NaN();           // yd
    double carrySd = std::numeric_limits<double>::quiet_NaN();             // yd
    double ballSpeedMean = std::numeric_limits<double>::quiet_NaN();       // as stored by the LM
    double ballSpeedSd = std::numeric_limits<double>::quiet_NaN();         // as stored by the LM
    double speedMean = std::numeric_limits<double>::quiet_NaN();           // for the waterfall
    double smashMean = std::numeric_limits<double>::quiet_NaN();           // for the waterfall
    double shaftAngleP4Mean = std::numeric_limits<double>::quiet_NaN();    // for swing length
    int    n = 0;              // qualifying shots
};

class IClubBaselineProvider {
public:
    virtual ~IClubBaselineProvider() = default;
    virtual std::optional<ClubBaselineEntry> forClub(const QString &club) const = 0;
};

inline constexpr int kClubBaselineMinShots = 10;

namespace detail {

class ClubBaselineTable final : public IClubBaselineProvider {
public:
    explicit ClubBaselineTable(QHash<QString, ClubBaselineEntry> clubs) : m_clubs(std::move(clubs)) {}

    std::optional<ClubBaselineEntry> forClub(const QString &club) const override
    {
        const auto it = m_clubs.constFind(club);
        if (it == m_clubs.constEnd()) return std::nullopt;
        return it.value();
    }

private:
    QHash<QString, ClubBaselineEntry> m_clubs;
};

} // namespace detail

inline std::shared_ptr<IClubBaselineProvider> makeClubBaselineProvider(QHash<QString, ClubBaselineEntry> clubs)
{
    return std::make_shared<detail::ClubBaselineTable>(std::move(clubs));
}

} // namespace pinpoint::analysis
