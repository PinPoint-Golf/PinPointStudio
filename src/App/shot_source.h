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

// THE CAROUSEL'S PORT ONTO ONE SESSION (refactor step 5.2; R-QML-5, DEC-199). The carousel (ShotListModel: live, review
// and the report's) is a view: its rows are one Session's Shots, it follows that Session's events, and it keeps no list,
// reload or folder read of its own. This is all it sees of the Session — its shots in order, each Shot's summary (the
// Shot's own, kept by the Shot, never a copy) and measurements (refactor 6.1a), the Shot's commands, the Session's trash,
// and the events.
//
// A port and not the Session itself because the view is compiled into targets that link no Session (the QML module the
// QML suites build, and the board's tests): session_shot_source.h implements it over a real Session for the app. It is
// not a domain object and holds no facts; an implementation answers from the owner on every call.
//
// Plain callbacks, not signals, so the port needs no moc in the targets that compile the view.

#include "shot_command_door.h"   // ShotSummary, WriteResult, SummaryRead
#include "../Metrics/measurement.h"   // MeasurementSet (refactor 6.1a)
#include "../Identity/identity.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace pinpoint {

// What the Session says happened, as the view hears it. Each is called synchronously, inside the owner's emit.
struct ShotSourceEvents {
    std::function<void(const ShotRef &)>       added;     // EVT-01: the shot's first document has landed
    std::function<void(const ShotRef &, bool)> changed;   // one of the Shot's events; true when its document was
                                                          // rewritten (EVT-22 a late part, EVT-02 a re-analysis)
    std::function<void(const ShotRef &)>       trashed;   // EVT-03
    std::function<void()>                      gone;      // the Session is let go of, trashed (EVT-20) or moved away
};

class ShotSource
{
public:
    virtual ~ShotSource() = default;

    virtual QString folder() const = 0;                     // the session's folder, as composed
    virtual QVector<ShotRef> shots() = 0;                   // its Shots, by swing number (documents or not)
    // The Shot's summary, read as `how` says (Kept: the Shot's kept one, read once). Null when the Session no longer
    // holds the shot. Points into the Shot: read it now, never keep it.
    virtual const ShotSummary *summary(const ShotRef &shot, SummaryRead how) = 0;
    virtual QString swingFolder(const ShotRef &shot) const = 0;   // the shot's folder (a property of the Shot)
    // The Shot's measurements (Shot::measurements: its one source-tagged set; refactor 6.1a). A copy, read now. Empty when
    // the Session no longer holds the shot — and from a source over no documents (the default).
    virtual analysis::MeasurementSet measurements(const ShotRef &) { return {}; }

    // The Shot's commands (the golfer's review facts: Shot::rate … excludeReading) and the Session's trash (EVT-03).
    virtual WriteResult rate(const ShotRef &shot, int stars) = 0;
    virtual WriteResult note(const ShotRef &shot, const QString &text) = 0;
    virtual WriteResult setDeclaredClub(const ShotRef &shot, const QString &club) = 0;
    virtual WriteResult correctClub(const ShotRef &shot, const QString &club) = 0;
    virtual WriteResult excludeReading(const ShotRef &shot, bool excluded) = 0;
    virtual WriteResult trash(const ShotRef &shot, QString *where) = 0;

    // The one listener (the view that owns this source).
    virtual void watch(ShotSourceEvents events) = 0;
};

// A session folder to its source (main.cpp: app.sessionAt, then a SessionShotSource), or null when no Session holds it.
using ShotSourceFor = std::function<std::unique_ptr<ShotSource>(const QString &sessionFolder)>;

} // namespace pinpoint
