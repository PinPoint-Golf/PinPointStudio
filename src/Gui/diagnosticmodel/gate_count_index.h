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

// THE GATE COUNT'S SESSIONS, FROM THE ATHLETES (platform refactor P-3b; gate_counts.h), in two halves:
//   gateCountOwnersOf     on the GUI thread: each athlete's SessionOwner, held in memory (no file is read);
//   gateCountSessionsOf   on the worker: the entries of those owners' SessionIndex with their summaries, as the athlete
//                         repository's worker-safe read gives them (AthleteRepository::readIndexWithSummaries, its
//                         summaries through deferredSummary), one GateCountSession each.
// The composition root's source hands the model browser the worker half; session_gates_test runs the same two.
// Not in the QML module: it names the Athlete.

#include "gate_counts.h"
#include "../../DomainModel/athlete.h"   // Athlete, SessionOwner, SessionIndexEntry

#include <QFileInfo>

namespace pinpoint::diagmodel {

inline QList<SessionOwner> gateCountOwnersOf(const QList<Athlete *> &athletes)
{
    QList<SessionOwner> out;
    for (const Athlete *a : athletes)
        if (a) out.append(a->sessionOwner());
    return out;
}

// Entries whose summary is read (SessionIndexEntry::summary); one GateCountSession each.
inline QList<GateCountSession> gateCountSessionsOf(const QList<SessionIndexEntry> &entries)
{
    QList<GateCountSession> out;
    for (const SessionIndexEntry &e : entries)
        out.append(GateCountSession{ QFileInfo(e.folder).fileName(), e.folder, e.summary });
    return out;
}

} // namespace pinpoint::diagmodel
