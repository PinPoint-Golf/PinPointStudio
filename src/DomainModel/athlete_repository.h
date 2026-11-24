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

// AthleteRepository — THE ONE DOOR TO THE ATHLETE'S RECORD (refactor step 2.3; build/refactor/step-2/
// BRIEF.md §4.5; docs/developer/information_model.md CLS-REPOSITORIES, FILE-ATH-01, SET-01, LC-ATHLETE).
// AthleteStore (src/Export/athlete_store.h) implements it; the composition root (main.cpp) holds the one
// instance and hands it to AthleteController, so a caller names this interface and never the store.
//
//   list      every athlete of the library: each <library>/*/athlete.json, and each settings record
//             (SET-01) that has no athlete.json yet (until M1)
//   load      the instance it holds for that id (the identity map: one per AthleteId, DEC-52)
//   create    mints the AthleteId (v4); the folder is the sanitised name ONCE, with -2, -3 … when it is
//             taken (DEC-04, D4: DEC-151); writes athlete.json there at once (DEC-05)
//   save      the record, where it lives: athlete.json (under the folder's advisory lock, for that one
//             write: J-11), or the settings record of an athlete M1 has not moved yet
//   trash     the whole athlete folder to the recoverable trash (DEC-79); the caller (Athlete::remove)
//             has refused it while a session is InProgress (DEC-99). The folder stays when another
//             athlete resolves to it (only this athlete's record goes), and the delete is refused when
//             the folder's athlete.json names another athlete (J-30; AthleteRemoval)
//   readActivity  the picker's two derived figures (DEC-89), from the index, by a session reader of its
//             own: the one call that is SAFE FROM ANY THREAD
//
// A READ NEVER WRITES: list and load write nothing — not the standard bag a settings record lacks, not
// an athlete.json for an athlete created before step 2 (M1 does that).
//
// A WRITE THAT FAILS SAYS SO (DEC-126, DEC-140): every write returns WriteResult. A newer athlete.json
// (schema above this build's) is read-only; a folder held by another writer past the wait is Failed,
// with the holder named.
//
// INTERIM (IM-S2-4: DEC-160): until step 3 the writer's caller is AthleteController (the QML adapter).
//
// Domain model ring (src/DomainModel): names no store type.

#include "athlete.h"
#include "session_repository.h"   // JsonFileRead
#include "../Identity/identity.h"

#include <QByteArray>
#include <QList>

namespace pinpoint {

struct AthleteCreateResult {
    Athlete    *athlete = nullptr;   // owned by the repository; null when nothing was made
    WriteResult result;
    bool ok() const { return athlete && result.ok(); }
};

class AthleteRepository
{
public:
    virtual ~AthleteRepository() = default;

    virtual QList<AthleteId> list() = 0;
    // Null for an id list() does not know (and has not handed out before).
    virtual Athlete *load(const AthleteId &id) = 0;
    // `record.name` must not be blank.
    virtual AthleteCreateResult create(const AthleteRecord &record) = 0;
    virtual WriteResult save(Athlete &athlete, const AthleteRecord &record) = 0;
    virtual AthleteRemoval trash(const AthleteId &id) = 0;

    // THE UNASSIGNED OWNER (information_model.md §5.11, R-UNASSIGNED-1..5; DEC-198; J-66, J-67): the athlete a session
    // captured with no golfer chosen belongs to — its fixed id, its folder `<library>/(unassigned)`, an athlete.json
    // holding only its id and the name "Unassigned". HELD IN MEMORY while the library holds no record of it (a read never
    // writes): listed by list() once it holds a session — its own folder's, or a no-golfer one an older build left in
    // `unknown/` (held where it is). `create` true (capture's Start, a move): the record is written first if it is not
    // there yet. Null when it cannot be had (`*result` says why).
    virtual Athlete *unassigned(bool create, WriteResult *result = nullptr)
    {
        Q_UNUSED(create);
        if (result) *result = { WriteResult::Status::Failed, QStringLiteral("This library has no Unassigned owner.") };
        return nullptr;
    }

    // Reads only, keeps no state: safe from a worker thread.
    virtual AthleteActivity readActivity(const SessionOwner &owner) const = 0;
    // THE INDEX WITH ITS SUMMARIES (platform refactor P-3b; PR-21): every session of `owner`'s SessionIndex, each entry's
    // summary filled through SessionRepository::deferredSummary, by a session reader of its own (readActivity's rule: the
    // owner thread's identity map is untouched). Reads only, keeps no state: SAFE FROM A WORKER THREAD. The model
    // browser's gate count reads every athlete's this way, on its worker. Default: none (summary.read false throughout).
    virtual QList<SessionIndexEntry> readIndexWithSummaries(const SessionOwner &owner) const
    {
        (void)owner;
        return {};
    }

    // THE TUNING LOG's append (OBJ-TUNING-LOG, FILE-ATH-04: append-only, never rewritten; DEC-43): one line
    // added to `athlete`'s log under its folder's lock, for that one append (refactor step 3.4b-i: the
    // Athlete's own append, on EVT-06).
    virtual WriteResult appendTuningLine(const Athlete &athlete, const QByteArray &line) = 0;

    // THE ATHLETE LEVEL'S DERIVED CACHES (refactor step 3.7-f; IM FILE-ATH-02, FILE-ATH-03 — their rows name this
    // repository): tendencies.json and swing_themes.json (every club; a club scope's or shot filter's own file stays the
    // owner's too since Mark's J-50 answer, DEC-195). Read and written by their one writer, AthleteDiagnostics. A write
    // replaces the file whole (atomic); a read of a file that is not there is `exists` false. GUI thread.
    enum class Cache { Tendencies, Themes };
    // `keys`: a club scope's (and shot filter's) own swing_themes file (FILE-ATH-03's per-scope files; DEC-195), each
    // made file-safe by the caller (clubScopeFileKey); none: every club's.
    virtual JsonFileRead readCache(const Athlete &athlete, Cache which, const QStringList &keys = {}) = 0;
    virtual WriteResult  writeCache(const Athlete &athlete, Cache which, const QJsonObject &root, const QStringList &keys = {}) = 0;
};

} // namespace pinpoint
