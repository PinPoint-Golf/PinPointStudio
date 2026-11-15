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

// SessionRepository — THE ONE DOOR TO A SESSION'S OWN FILES (refactor step 2.2; build/refactor/step-2/
// BRIEF.md §4.3, §4.4; docs/developer/information_model.md FILE-SESS-01, FILE-SESS-10, LC-SESSION,
// OBJ-SESSION-INDEX). SessionStore (src/Export/session_store.h) implements it; the composition root
// (main.cpp) holds the one instance and hands it to the callers, so a caller names this interface and
// never the store.
//
//   start            mints the SessionId (v4, DEC-41), names and makes the folder (SwingPaths, SET-04)
//                    and writes the session file InProgress; first ends any other InProgress session
//                    of the athlete (one InProgress per athlete: DEC-78, DEC-73)
//   todaysSession    the session a resume would take: the athlete's latest session STARTED TODAY of
//                    the same type (MD-05: DEC-134 — today's "extend" rule)
//   resume           that session InProgress again, with the same SessionId (DEC-77); refused for any
//                    other; any other InProgress session of the athlete is ended first
//   end              Ended, with its end time; today's empty-session discard, unchanged (DEC-93):
//                    a session this run started and that holds no swing goes to the trash instead
//   restartInLibrary the library re-pointed mid-session (D7: DEC-154): the running session ends
//                    (written if its folder is still reachable; else it stays InProgress, DEC-73) and
//                    a new one of the same type starts under the new library. A session's folder
//                    never changes during its life, so nothing relocates
//   enumerate        one typed entry per session folder of the athlete, without loading the session:
//                    identity, status, start and end times (MD-07: DEC-136); n-of-day by start time
//   allocateSwing    the next swing number past the recorded high-water (DEC-91), its folder made
//   shots            the session's swings, as ShotRefs (the one swing_* listing is SwingPaths')
//   open, openFolder the identity map's record; openFolder is the translation of a foreign id (a
//                    path from a tool or a picker) into a SessionId, once, at the edge (DEC-90)
//   openSwingFolder  the same for a swing folder: its ShotRef (step 2.4)
//   swingFolder      a shot's folder (for RecordingRepository's and RecordingReader's adapters, which are
//                    keyed by ShotRef: step 2.4). THE ONE METHOD SAFE FROM ANY THREAD: it reads the folders
//                    the identity map publishes, so a worker writing a shot (the live join, re-analysis)
//                    resolves it without touching the map
//   archive, restore the whole session out of the library into its archive mirror and back (FILE-SESS-06,
//                    FILE-LIB-02; brief §4.12; step 2.6). Refused while the session file says InProgress
//                    (D11: DEC-158 — the session file's status is what keeps a second writer off a live
//                    session). The session's advisory lock is taken around EACH write into the library —
//                    one stub, one file put back, the marker — never across the run (J-11)
//   trash            the whole session folder to the recoverable trash (FILE-LIB-01), refused while its
//                    session file says InProgress; the identity map lets it go (step 2.6)
//
// THE IDENTITY MAP. One record per SessionId while held, on the owner thread (the GUI thread in the
// app; one thread in a tool). The open session's folder lives here and is never re-resolved per shot
// (E-01): capture asks for the folder of the session it writes into. Workers read only the folders it
// publishes (swingFolder; P8: "workers read immutable values out of them").
//
// A SESSION WITHOUT A SESSION FILE (every session before step 2's migration M2, a corpus or scratch
// copy) is read from its folder's name with D3's identity (SessionIdentity::fromLegacyFolder; DEC-150),
// in memory. A READ NEVER WRITES IT. Its first write — a resume — writes the file (origin "backfilled",
// saying what it read); writing the file for every old session is M2's (step 2.8).
//
// A WRITE THAT FAILS SAYS SO (DEC-126, DEC-140): every write returns WriteResult, and the caller
// surfaces a failure with the existing notifications. A newer session file (schema above this build's)
// is read-only: every write refuses it (WriteResult::Status::ReadOnly). A folder held by another
// writer's advisory lock (FolderLock, R-PERS-6) past the wait is Locked, with the holder named (J-22).
//
// INTERIM (IM-S2-4: DEC-160): until step 3 the callers are today's — ShotProcessor's session-folder
// commands and the launch monitor's standalone shot (one allocator: D10, DEC-157); the Archiving tab and
// housekeeping (ArchiveController: archive, restore) and the drawer's delete (SessionReviewController:
// trash), from step 2.6. Step 3's
// Athlete::startSession / resumeSession and Session::end / addShot take them over.
//
// Domain model ring (src/DomainModel): names no store type.

#include "recording_repository.h"   // WriteResult (step 1's type)
#include "session_identity.h"
#include "../Identity/identity.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace pinpoint {

// OBJ-SESSION-STATUS (FILE-SESS-01). A session without a session file is Ended: nothing is in
// progress in it.
enum class SessionStatus { InProgress, Ended };

// Whose sessions, and where they are found. UNTIL STEP 2.3 the athlete's folder is today's derivation
// — the library, then the athlete's name sanitised (the uuid when there is no name) — made in ONE
// place, the store; 2.3's Athlete::folder() replaces `name` and `libraryRoot`.
struct SessionOwner {
    AthleteId athlete;        // null only when no athlete is selected (the folder is then "unknown")
    QString   name;           // as typed
    QString   libraryRoot;    // SET-03, athleteLibraryPath; empty = the app-data fallback
};

// OBJ-SESSION-INDEX's entry (MD-07: DEC-136): what a list of sessions needs, read without loading the
// session. Step 3 adds the session's summary.
struct SessionIndexEntry {
    SessionIdentity identity;      // id, folder (cleaned), start, local day, type, n-of-day
    QString         folder;        // absolute, spelled as composed (the path handed to capture)
    AthleteId       athlete;       // the session file's athleteId; for a legacy folder, the owner's
    SessionStatus   status = SessionStatus::Ended;
    QDateTime       startedAtUtc;  // invalid for a legacy folder (its day is its name's)
    QDateTime       endedAtUtc;    // invalid while InProgress, and when the end is not known (a
                                   // session ended by the athlete's next start after a crash)
    bool            hasSessionFile = false;   // false: read from its name (D3), never written by a read
    bool            readOnly = false;         // a newer build's session file: every write refuses it
    bool            archived = false;         // its archive marker (archived.json, FILE-SESS-06) is there:
                                              // the library holds its stubs, the archive the rest (step 2.6)

    SessionId id() const { return identity.id(); }
};

struct SessionStartResult {
    SessionId   id;       // null when nothing started
    QString     folder;   // as SessionIndexEntry::folder
    WriteResult result;
    bool ok() const { return !id.isNull() && result.ok(); }
};

// A swing number issued, and its folder made.
struct SwingAllocation {
    ShotRef     shot;                // the session and the swing number
    QString     swingFolder;         // absolute, created; empty on failure
    QString     swingName;           // "swing_0007"
    QString     sessionFolderName;   // the session folder's name: provenance only, never a key
    WriteResult result;
    bool ok() const { return !shot.isNull() && !swingFolder.isEmpty() && result.ok(); }
};

// THE ARCHIVE (FILE-SESS-06, FILE-LIB-02; brief §4.12; step 2.6). What is asked for, and what happened.
struct SessionArchiveOptions {
    QString archiveRoot;      // SET-13: the session is mirrored at <archiveRoot>/<athlete folder>/<session folder>
    bool    keepRaw = true;   // SET-14: false leaves the raw sensor frames out of the archive AND the library —
                              // a permanent delete of raw frames (DEC-137); never the default
};

struct SessionArchiveResult {
    bool                ok = false;   // a member, not a function: report_meta.h's keepReportCurrent reads it
    WriteResult::Status status = WriteResult::Status::Failed;   // Written when ok; else Failed, ReadOnly or
                                                                // Locked (the session lock held past the wait)
    QString error;                    // why, for the user, when !ok
    QString archivePath;              // the mirror folder
    int     swings      = 0;
    int     members     = 0;          // files copied
    qint64  bytesPacked = 0;          // written into the archive
    qint64  bytesFreed  = 0;          // removed from the library (archive) / put back (restore)
};

// done / total in files, between files (never during one); return false to stop. An archive stopped before
// its copies are all made and proven leaves the library untouched; a restore stopped part-way stays marked
// archived, and restoring again finishes it.
using SessionArchiveProgress = std::function<bool(int done, int total)>;

class SessionRepository
{
public:
    virtual ~SessionRepository() = default;

    // `type` by name (DEC-88): "swing", "wrist", "grf", "coach", or "" for none. `namingPattern` is
    // SET-04 (sessionNamingPattern).
    virtual SessionStartResult start(const SessionOwner &owner, const QString &type,
                                     const QString &namingPattern) = 0;

    // The athlete's latest session started today, of `type`; null when there is none.
    virtual SessionId todaysSession(const SessionOwner &owner, const QString &type) = 0;
    // Refused (Failed) unless it was started today and is of `type`; ReadOnly for a newer file.
    virtual WriteResult resume(const SessionId &id, const QString &type) = 0;

    // Unchanged for a session that is not held or has no session file (nothing to end).
    virtual WriteResult end(const SessionId &id, bool discardIfEmpty) = 0;

    // D7. `owner` names the new library. The result is the NEW session's. `oldEndProblem`, when given,
    // is set to why the old session could not be ended while its folder is still there (a held lock, a
    // read-only file) — a failure for the caller to show; it stays empty when the old library is
    // unreachable (the expected case, DEC-73) or the end succeeded.
    virtual SessionStartResult restartInLibrary(const SessionId &running, const SessionOwner &owner,
                                                const QString &namingPattern, QString *oldEndProblem = nullptr) = 0;

    virtual QList<SessionIndexEntry> enumerate(const SessionOwner &owner) = 0;

    virtual std::optional<SessionIndexEntry> open(const SessionId &id) const = 0;
    // A session folder's SessionId: its session file's, or D3's from `legacyAthlete` and its name
    // (null when it has no file and `legacyAthlete` is null). Reads only.
    virtual SessionId openFolder(const QString &folder, const AthleteId &legacyAthlete = {}) = 0;
    // A swing folder's ShotRef: its session's id and its swing number (SwingPaths reads the number; null
    // when the folder is not named as a swing). The session is the held one at that folder if there is
    // one (so a folder already listed keeps the id it was listed with), else openFolder's (null for a
    // session without a file when `legacyAthlete` is null). Reads only.
    virtual ShotRef openSwingFolder(const QString &swingFolder, const AthleteId &legacyAthlete = {}) = 0;

    virtual SwingAllocation allocateSwing(const SessionId &id) = 0;
    // By swing number.
    virtual QList<ShotRef> shots(const SessionId &id) const = 0;
    // "" when the session is not held or the ref is null. Safe from any thread.
    virtual QString swingFolder(const ShotRef &shot) const = 0;

    // The archive (step 2.6). The session must be held (enumerate, open, openFolder). Refused while its
    // session file says InProgress, and for a newer build's session file (ReadOnly). Owner thread, like
    // every method but swingFolder: a worker that archives makes a repository of its own.
    virtual SessionArchiveResult archive(const SessionId &id, const SessionArchiveOptions &options,
                                         const SessionArchiveProgress &progress = {}) = 0;
    virtual SessionArchiveResult restore(const SessionId &id, const SessionArchiveProgress &progress = {}) = 0;

    // The whole session folder to the recoverable trash (the OS trash, else the library's own:
    // FILE-LIB-01). Refused while its session file says InProgress (Failed) or another writer holds its
    // lock (Locked). `where`, when given, names where it went. The identity map lets the session go.
    virtual WriteResult trash(const SessionId &id, QString *where = nullptr) = 0;
};

} // namespace pinpoint
