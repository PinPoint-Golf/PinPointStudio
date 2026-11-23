/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// SessionNeighbours — THE GOLFER'S OTHER SESSIONS AS THEIR SUMMARIES HOLD THEM (refactor 5.5d; DEC-74). What the Charts
// delivery and posture presenters and the report's Club delivery and Posture pages read of OTHER sessions: each one's
// place (its day and place in the day), whether its summary is this build's with its day readings, and those readings
// (Coaching/session_day_readings.h). Taken ON THE GUI THREAD from the session's athlete level — the Athlete's SessionIndex
// summaries (AthleteLevel::summaries(), information_model.md OBJ-SESSION-INDEX) — and handed to a job BY VALUE. Never
// another session's files: before 5.5d the presenters read the day's swing_phasegrid.json and every earlier
// diagnostics.json and swing document themselves (P11).
//
// A SESSION NOT SUMMED UP YET (an older build's summary without day readings, none, or a newer build's) is listed with
// its state, never dropped silently: its readers say so in words (show with a warning, never hide). It lasts until the
// catch-up writes its summary again, minutes after a new build's first launch.
//
// THE DOOR. The composition root (main.cpp) sets one door for the process: the session folder → its neighbours, from the
// session's own golfer's athlete level (none for a session with no golfer: R-UNASSIGNED-5, J-68 B1), and a watch that
// calls back when that athlete level changes (a summary written). No door (the QML suites, a tool, a test that sets
// none): no neighbours, `known` false — the day is the session alone and the history is empty, as for a golfer with no
// other session.
//
// Adapters ring (Gui/charts): values only; no file. Header-only, so every target that compiles a presenter links it.

#include "../../Coaching/club_distances.h"   // SessionPlace, sessionPlaceBefore
#include "../../Coaching/session_day_readings.h"
#include "../../DomainModel/athlete_diagnostics.h"   // AthleteLevel, AthleteSessionSummary (an interface: nothing to link)

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <vector>

namespace pinpoint::charts {

struct NeighbourSession {
    enum class State { Summed, NotYet, Newer };
    analysis::SessionPlace       place;      // its folder's name, its day and place in the day
    State                        state = State::Summed;
    analysis::SessionDayReadings readings;   // State::Summed only
};

struct SessionNeighbours {
    bool                          known = false;   // an athlete level listed this session
    analysis::SessionPlace        self;            // this session's place
    std::vector<NeighbourSession> others;          // oldest first (sessionPlaceBefore); this session left out
};

// From one athlete level, on the GUI thread. A session whose summary is not this build's, or is but holds no day readings,
// is NotYet when it has shots (the athlete level's notSummedUp, or a read summary with shots); a newer build's is Newer.
inline SessionNeighbours neighboursFrom(AthleteLevel *level, const QString &sessionDir)
{
    SessionNeighbours n;
    if (!level || sessionDir.trimmed().isEmpty()) return n;
    const QString clean = QDir::cleanPath(sessionDir.trimmed());
    const QString me = QFileInfo(clean).fileName();   // path text: its folder's name
    const std::vector<AthleteSessionSummary> &all = level->summaries();
    const AthleteLevel::NotSummedUp missing = level->notSummedUp();
    for (const AthleteSessionSummary &s : all) {
        if (s.place.folder == me) {
            n.known = true;
            n.self = s.place;
            continue;
        }
        NeighbourSession o;
        o.place = s.place;
        if (s.fromNewerBuild) {
            o.state = NeighbourSession::State::Newer;
        } else if (s.read && s.results.dayReadings.has) {
            o.state = NeighbourSession::State::Summed;
            o.readings = s.results.dayReadings;
        } else if (missing.folders.contains(s.place.folder) || (s.read && !s.results.shots.empty())) {
            o.state = NeighbourSession::State::NotYet;
        } else {
            continue;   // no shots: nothing to read, nothing missing
        }
        n.others.push_back(std::move(o));
    }
    std::stable_sort(n.others.begin(), n.others.end(),
                     [](const NeighbourSession &a, const NeighbourSession &b) { return analysis::sessionPlaceBefore(a.place, b.place); });
    return n;
}

// ── the process's door (main.cpp) ────────────────────────────────────────────────────────────────────────────────
struct NeighboursDoor {
    std::function<SessionNeighbours(const QString &sessionDir)> read;
    // Calls `changed` (on the GUI thread) whenever the athlete level of `sessionDir` changes; the connections end with
    // `context`.
    std::function<QList<QMetaObject::Connection>(QObject *context, const QString &sessionDir, std::function<void()> changed)> watch;
};

namespace neighbours_detail {
inline QMutex &mutex() { static QMutex m; return m; }
inline NeighboursDoor &door() { static NeighboursDoor d; return d; }
} // namespace neighbours_detail

inline void setNeighboursDoor(NeighboursDoor door)
{
    QMutexLocker l(&neighbours_detail::mutex());
    neighbours_detail::door() = std::move(door);
}

// GUI thread. No door: none (`known` false).
inline SessionNeighbours neighboursOf(const QString &sessionDir)
{
    NeighboursDoor d;
    {
        QMutexLocker l(&neighbours_detail::mutex());
        d = neighbours_detail::door();
    }
    return d.read ? d.read(sessionDir) : SessionNeighbours{};
}

inline QList<QMetaObject::Connection> watchNeighbours(QObject *context, const QString &sessionDir, std::function<void()> changed)
{
    NeighboursDoor d;
    {
        QMutexLocker l(&neighbours_detail::mutex());
        d = neighbours_detail::door();
    }
    return d.watch ? d.watch(context, sessionDir, std::move(changed)) : QList<QMetaObject::Connection>{};
}

} // namespace pinpoint::charts
