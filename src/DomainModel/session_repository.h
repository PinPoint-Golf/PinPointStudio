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
//   end              Ended, with its end time — and nothing more: the empty-session discard (DEC-93) is
//                    Session::end's, which ends the session here and then asks Athlete::trashSession
//                    (DEC-143; refactor step 3.4a-ii). A library re-pointed mid-session (D7: DEC-154) is
//                    an end and a start, made by the capture service through the Session and its
//                    Athlete; a session's folder never changes during its life, so nothing relocates
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
//   trashShot        one shot's swing folder to the same trash, its number kept as the high-water (step 3.4a;
//                    J-24), behind Session::trashShot
//   readDiagnostics … writeWorkOns   the session's diagnostics files (FILE-SESS-02…05) by its id (step 3.4a;
//                    J-42): the folder-keyed SessionDiagnosticsFiles' reads and writes, for the session held
//                    under that id
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
// ITS CALLERS (refactor step 3.4a): the Athlete and its Sessions (Athlete::startSession / resumeSession /
// trashSession, Session::end / addShot / trashShot: src/DomainModel/athlete.h, session.h), through which
// capture starts, resumes and ends its sessions and allocates every shot, the launch monitor's among them
// (run 3.4a-ii). INTERIM (IM-S2-4: DEC-160), still direct: capture's sessions that no Athlete holds (no
// golfer selected — the "unknown" folder — or a library re-pointed mid-session that does not list the golfer:
// ShotProcessor, NOTES-3.4a-ii N1); the Archiving tab and housekeeping (ArchiveController: archive, restore)
// and the drawer's delete (SessionReviewController: trash), from step 2.6, until steps 3.4b and 3.8.
//
// Domain model ring (src/DomainModel): names no store type.

#include "recording_repository.h"   // WriteResult (step 1's type)
#include "session_identity.h"
#include "session_input.h"          // SessionInput (FILE-SESS-02)
#include "../Identity/identity.h"

#include <QDateTime>
#include <QJsonObject>
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
    QDateTime       endedAtUtc;    // invalid while InProgress, and when the end is not known (a session
                                   // ended by the athlete's next start after a crash, none of whose swings
                                   // records a capture time)
    bool            endInferred = false;   // endedAtUtc was INFERRED, not observed: the latest swing's capture
                                           // end, for a session ended by the next start after a crash, or
                                           // backfilled by migration M2 (J-18: Mark, 15 Nov 2026; FILE-SESS-01)
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

// AN ARCHIVE THAT RUNS OFF ITS OWNER'S THREAD (refactor step 3.8; R-CON-3): the repository checks the session on its owner
// thread and hands back `run`, a job that touches no domain object and no shared repository state — it archives the
// session's folder through a repository of its own (SessionArchiveRun, as the Archiving tab's jobs do: step 2.4's J4), so
// it is safe on a worker. Empty `run`: refused, and `refused` says why. The result is posted back to the owner, which tells
// the repository (archiveLanded) and emits EVT-18 (Session::archiveLanded).
struct SessionArchiveJob {
    SessionArchiveResult                                                refused;
    std::function<SessionArchiveResult(const SessionArchiveProgress &)> run;
    explicit operator bool() const { return bool(run); }
};

// ── A SESSION'S DIAGNOSTICS FILES, AS READ (FILE-SESS-02…05; refactor step 2.5, moved here in 3.4a for
//    J-42's id-keyed methods below). The rules for each: src/Export/session_diagnostics_files.h. ──────────

// diagnostics.json as read: the cache half, and the older shape's golfer keys kept apart.
struct DiagnosticsEnvelope {
    int         schemaVersion = 0;   // the envelope's own version (kEnvelopeVersion), not the ledger's
    QJsonObject session;             // the session block (stage, shot count, the swing folders, the stamps)
    QJsonObject ledger;              // diagnostic_ledger.h's object, nested, with ITS schemaVersion
    QJsonObject legacyGolferInput;   // the older shape's golfer keys, verbatim (legacyGolferKeys())
    QJsonObject other;               // any other top-level key this build does not know: kept on rewrite
};

struct DiagnosticsRead {
    enum class State {
        Missing,      // no file: nothing reduced yet (or the cache was deleted)
        Read,         // parsed and of this build's schema or older
        Newer,        // parsed, but a newer build's: read what can be read, write nothing (READ-ONLY)
        Unparseable,  // it opens but is not a JSON object: the file itself is bad (set it aside)
        Unopenable,   // it is there and cannot be opened: met at a bad moment (a share, a permission)
    };
    State               state = State::Missing;
    DiagnosticsEnvelope envelope;
    QString             error;     // why, for Unparseable / Unopenable / Newer
    bool exists() const { return state != State::Missing; }
    bool parsed() const { return state == State::Read || state == State::Newer; }
};

struct SessionInputRead {
    enum class State { Missing, Read, Newer, Unparseable };
    State        state = State::Missing;
    SessionInput input;            // each field present only when the file holds it
    QString      error;
    bool readOnly() const { return state == State::Newer || state == State::Unparseable; }
};

// A small JSON file read whole (club_baseline.json, work_ons.json).
struct JsonFileRead {
    bool        exists = false;   // the file is there
    bool        ok = false;       // it opened and is a JSON object
    QJsonObject root;
    QString     error;
};

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

    // Unchanged for a session that is not held or has no session file (nothing to end). Never discards:
    // the empty discard is Session::end's (DEC-143).
    virtual WriteResult end(const SessionId &id) = 0;

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
    // Whether the held session's folder is still on disk, whatever state its session file is in (a library that
    // has gone, a dropped share: false; not held: false). Reads only. Its one caller: the capture service's
    // library change (D7), which shows the golfer an old session's failed End only while the folder is still
    // there (DEC-140), and says nothing for a library that has gone (the expected case, DEC-73).
    virtual bool folderPresent(const SessionId &id) const = 0;

    // The archive (step 2.6). The session must be held (enumerate, open, openFolder). Refused while its
    // session file says InProgress, and for a newer build's session file (ReadOnly). Owner thread, like
    // every method but swingFolder: a worker that archives makes a repository of its own.
    virtual SessionArchiveResult archive(const SessionId &id, const SessionArchiveOptions &options,
                                         const SessionArchiveProgress &progress = {}) = 0;
    virtual SessionArchiveResult restore(const SessionId &id, const SessionArchiveProgress &progress = {}) = 0;
    // The archive's two halves for an owner that must not block (refactor step 3.8; above): the check, on the owner
    // thread, refusing what `archive` refuses, and the job; then, back on the owner thread, the landing — the held
    // record reads the marker again. A repository that cannot archive off its thread refuses (the default).
    virtual SessionArchiveJob archiveJob(const SessionId &id, const SessionArchiveOptions &options)
    {
        Q_UNUSED(id);
        Q_UNUSED(options);
        SessionArchiveJob j;
        j.refused.error = QStringLiteral("Archiving is not available here.");
        return j;
    }
    virtual void archiveLanded(const SessionId &id) { Q_UNUSED(id); }

    // The whole session folder to the recoverable trash (the OS trash, else the library's own:
    // FILE-LIB-01). Refused while its session file says InProgress (Failed) or another writer holds its
    // lock (Locked). `where`, when given, names where it went. The identity map lets the session go.
    virtual WriteResult trash(const SessionId &id, QString *where = nullptr) = 0;

    // ONE SHOT TO THE TRASH (refactor step 3.4a; J-24; behind Session::trashShot): its swing folder to the
    // recoverable trash (the OS trash, else the library's own: FILE-LIB-01; where it goes is unchanged, D11:
    // DEC-175), its number recorded first as the session's high-water (FILE-SESS-10), so it is never issued
    // again (DEC-91). Under the session's lock for that one write (Locked when another writer holds it).
    // Failed for a shot whose session is not held or whose folder is not there. `where` names where it went.
    virtual WriteResult trashShot(const ShotRef &shot, QString *where = nullptr) = 0;

    // ── THE SESSION'S DIAGNOSTICS FILES BY ITS ID (refactor step 3.4a; J-42) ─────────────────────────────
    // The same reads and writes as SessionDiagnosticsFiles' folder-keyed functions, with the same rules
    // (a newer file read-only, keys this build does not know kept, the session's lock for each write, an
    // atomic write), for the session held under `id`. A session that is not held: every read answers
    // empty (Missing / not there) with `error` saying so, every write Failed. Owner thread, like every
    // method but swingFolder. Their callers come with step 3.5 (SessionDiagnostics); until then the
    // folder-keyed functions stay the callers' door (IM-S2-4's interim).
    virtual DiagnosticsRead  readDiagnostics(const SessionId &id) = 0;
    virtual WriteResult      writeDiagnostics(const SessionId &id, const QJsonObject &session,
                                              const QJsonObject &ledger, const QJsonObject &legacyFallback,
                                              DiagnosticsRead::State *state = nullptr) = 0;
    virtual WriteResult      backupDiagnostics(const SessionId &id, const QString &tag,
                                               QString *backupPath = nullptr) = 0;
    virtual WriteResult      setAsideDiagnostics(const SessionId &id, QString *asidePath = nullptr) = 0;
    virtual SessionInputRead readSessionInput(const SessionId &id) = 0;
    virtual WriteResult      writeSessionInput(const SessionId &id, const SessionInput &input) = 0;
    virtual JsonFileRead     readClubBaseline(const SessionId &id) = 0;
    virtual WriteResult      writeClubBaseline(const SessionId &id, const QJsonObject &root) = 0;
    virtual JsonFileRead     readWorkOns(const SessionId &id) = 0;
    virtual WriteResult      writeWorkOns(const SessionId &id, const QJsonObject &root) = 0;
};

} // namespace pinpoint
