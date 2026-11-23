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

// SessionStore — SessionRepository over the session folders (refactor step 2.2; the interface and its
// rules: src/DomainModel/session_repository.h). Adapters ring.
//
// THE SESSION FILE, `<session>/session.json` (FILE-SESS-01; DEC-149; keys DEC-160: IM-S2-1), written
// whole through atomicWrite (src/Core/atomic_write.h) under the session folder's advisory lock
// (FolderLock, held for that one write only: J-11):
//
//     {
//       "schema":    "pinpoint.session/1",       checked on read; a newer one is read-only
//       "sessionId": "<uuid>",                   v4 at Start; D3's v5 for a session older than step 2
//       "athleteId": "<uuid>",                   absent when no athlete was selected
//       "type":      "wrist",                    by name (DEC-88); absent for none
//       "status":    "inProgress" | "ended",
//       "startedAt": "2026-11-15T10:04:05.123Z", ISO 8601 UTC with milliseconds
//       "endedAt":   "…",                        absent while InProgress, and when the end is not known
//       "endInferred": true,                     endedAt was inferred, not observed (J-18; Mark, 15 Nov
//                                                2026): the latest of its swings' capture ends, for a session
//                                                a crash left open and the next Start ended (or M2's
//                                                backfill); absent when endedAt was observed (End)
//       "origin":    { "kind": "captured" }      written at Start; or { "kind": "backfilled", "tool",
//                                                "version", "at", "read" } — how an inferred start was
//                                                inferred (P1)
//     }
//
// Keys this build does not know are kept on every rewrite.
//
// ⚠ A READ NEVER WRITES. enumerate, todaysSession, open and openFolder only read; a session without a
// file is read from its folder's name, in memory (D3). Only start, resume, end, allocateSwing, archive,
// restore, trash and trashShot write the session's own files and folders; the write* methods of its
// diagnostics files (J-42, step 3.4a) write those, through session_diagnostics_io.h's file work, and its report's
// (writeReportInput, writeReportRecord, migrateReport, the stills writer: step 5.6a) through session_report_io.h's.
//
// THE ARCHIVE AND THE TRASH (step 2.6). archive / restore check the session (held, not InProgress by a
// FRESH read of its file — another machine's live session, or one a crash left open, is refused — and not
// a newer build's) and hand its folder to the archive's file work, SessionArchiving: SessionArchiver
// (session_archiver.h), set by whoever composes a store that archives (SessionArchiveRun, there). The
// store does not link the archiver, so the many targets that compile this file need none of the swing
// writer's sources; a store with none set refuses an archive. trash checks the same way, takes the
// session's lock for a moment (no other PinPoint writer is mid-write), lets it go — a lock file must not
// travel with the folder, and Windows will not move a folder holding an open file — and moves the folder
// (SwingPaths::trashPath).
//
// THE ATHLETE'S FOLDER (until step 2.3): `<libraryRoot>/<sanitise(name), else sanitise(uuid)>`, with
// `<AppDataLocation>/swings` for an empty library — today's derivation, made here only (athleteFolder).
//
// Not thread-safe: one owner thread (the GUI thread in the app) — except swingFolder, which any thread may
// call: it reads the folders the identity map publishes (a mutex-guarded copy, written wherever the map
// gains, moves or drops a session), so a worker writing a shot by its ShotRef never reads the map itself.
//
//   cmake --build build/tests --target session_repository_test -j 6
//   ctest --test-dir build/tests -R '^session_repository_test$' --output-on-failure

#include "../DomainModel/session_repository.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QTimeZone>

#include <functional>

namespace pinpoint {

// The archive's file work, on a session folder the store has checked (session_archiver.h implements it).
// `stillArchivable` is the store's check run again (a fresh read of the session file): "" while the session
// may still be archived, else why not — the archive asks it before each swing it thins (J-52).
class SessionArchiving
{
public:
    using StillArchivable = std::function<QString()>;
    virtual ~SessionArchiving() = default;
    virtual SessionArchiveResult archiveFolder(const QString &sessionDir, const SessionArchiveOptions &options,
                                               const SessionArchiveProgress &progress,
                                               const StillArchivable &stillArchivable) = 0;
    virtual SessionArchiveResult restoreFolder(const QString &sessionDir, const SessionArchiveProgress &progress) = 0;
};

class SessionStore final : public SessionRepository
{
public:
    static constexpr const char *kSchema = "pinpoint.session/1";

    SessionStore() = default;

    SessionStartResult start(const SessionOwner &owner, const QString &type,
                             const QString &namingPattern) override;
    SessionId          todaysSession(const SessionOwner &owner, const QString &type) override;
    WriteResult        resume(const SessionId &id, const QString &type) override;
    WriteResult        end(const SessionId &id) override;
    QList<SessionIndexEntry>         enumerate(const SessionOwner &owner) override;
    std::optional<SessionIndexEntry> open(const SessionId &id) const override;
    SessionId       openFolder(const QString &folder, const AthleteId &legacyAthlete = {}) override;
    ShotRef         openSwingFolder(const QString &swingFolder, const AthleteId &legacyAthlete = {}) override;
    SwingAllocation allocateSwing(const SessionId &id) override;
    QList<ShotRef>  shots(const SessionId &id) const override;
    QString         swingFolder(const ShotRef &shot) const override;
    bool            folderPresent(const SessionId &id) const override;
    SessionArchiveResult archive(const SessionId &id, const SessionArchiveOptions &options,
                                 const SessionArchiveProgress &progress = {}) override;
    SessionArchiveResult restore(const SessionId &id, const SessionArchiveProgress &progress = {}) override;
    WriteResult          trash(const SessionId &id, QString *where = nullptr) override;
    WriteResult moveToAthlete(const SessionId &id, const SessionOwner &to, QString *newFolder = nullptr) override;
    WriteResult          trashShot(const ShotRef &shot, QString *where = nullptr) override;

    // The session's diagnostics files by its id (J-42): session_diagnostics_io.h's file work on the held
    // session's folder.
    DiagnosticsRead  readDiagnostics(const SessionId &id) override;
    WriteResult      writeDiagnostics(const SessionId &id, const QJsonObject &session, const QJsonObject &ledger,
                                      const QJsonObject &legacyFallback,
                                      DiagnosticsRead::State *state = nullptr) override;
    WriteResult      backupDiagnostics(const SessionId &id, const QString &tag, QString *backupPath = nullptr) override;
    WriteResult      setAsideDiagnostics(const SessionId &id, QString *asidePath = nullptr) override;
    SessionInputRead readSessionInput(const SessionId &id) override;
    WriteResult      writeSessionInput(const SessionId &id, const SessionInput &input) override;
    JsonFileRead     readClubBaseline(const SessionId &id) override;
    WriteResult      writeClubBaseline(const SessionId &id, const QJsonObject &root) override;
    JsonFileRead     readWorkOns(const SessionId &id) override;
    std::function<JsonFileRead()> deferredReadWorkOns(const SessionId &id) override;   // the folder now, the read later
    WriteResult      writeWorkOns(const SessionId &id, const QJsonObject &root) override;
    // The session report's records and stills by its id (step 5.6a): session_report_io.h's file work on the held folder.
    ReportRead        readReport(const SessionId &id) override;
    WriteResult       writeReportInput(const SessionId &id, const ReportInput &input) override;
    WriteResult       writeReportRecord(const SessionId &id, const ReportRecord &record, bool removeReplacedPdf = false) override;
    WriteResult       migrateReport(const SessionId &id, bool *migrated = nullptr) override;
    QString           reportStillsFolder(const SessionId &id, const QString &key) override;
    ReportStillWriter reportStillsWriter(const SessionId &id, const QString &key) override;
    QString           reportPrintTarget(const SessionId &id, const QString &pdfName) override;
    WriteResult       placeReportPdf(const SessionId &id, const QString &printedPdf, const QString &pdfName) override;

    // enumerate(), for a caller that holds an athlete's FOLDER rather than the athlete (the diagnostics model,
    // the work-ons controller and their tools, until step 3 keys them by SessionId; refactor step 2.7a). Every
    // session folder in `athleteDir`, its day and its place in the day by start time (SessionIdentity), each one
    // held as enumerate holds it. `legacyAthlete` names the D3 ids of the sessions without a session file
    // (J-39: the folder's owner). With it null such a session cannot be named: it is still LISTED, placed
    // by its name (SessionIdentity::placeOfLegacyFolder: its day, type and n-of-day), with a null id, and
    // is never held — so no command can reach it, and a caller that needs only the order still has it.
    QList<SessionIndexEntry> enumerateFolder(const QString &athleteDir, const AthleteId &legacyAthlete);

    // The archive's file work (not owned; outlives the store's use of it). Null: archive and restore refuse.
    void setArchiving(SessionArchiving *archiving) { m_archiving = archiving; }
    // The worker-safe archive job's file work (refactor step 3.8): today's archive of a session folder in a run of its own
    // (SessionArchiveRun::archiveInOwnRun), set by the composition root. Null: archiveJob refuses.
    using ArchiveJobs = std::function<SessionArchiveResult(const QString &sessionDir, const SessionArchiveOptions &options,
                                                           const SessionArchiveProgress &progress)>;
    void setArchiveJobs(ArchiveJobs jobs) { m_archiveJobs = std::move(jobs); }
    // The restore's, the same way (refactor step 5.6b): today's restore of a session folder in a run of its own
    // (SessionArchiveRun::restoreInOwnRun). Null: restoreJob refuses.
    using RestoreJobs = std::function<SessionArchiveResult(const QString &sessionDir, const SessionArchiveProgress &progress)>;
    void setRestoreJobs(RestoreJobs jobs) { m_restoreJobs = std::move(jobs); }
    SessionArchiveJob archiveJob(const SessionId &id, const SessionArchiveOptions &options) override;
    SessionArchiveJob restoreJob(const SessionId &id) override;
    void              archiveLanded(const SessionId &id) override;

    // Today's athlete-folder derivation (until 2.3's Athlete::folder()). The Unassigned owner's is its own fixed name,
    // taken verbatim, never sanitised (R-UNASSIGNED-2: `sanitise` would make it "unassigned", a golfer's possible name).
    static QString athleteFolder(const SessionOwner &owner);
    // THE UNASSIGNED OWNER'S FOLDERS (information_model.md §5.11, R-UNASSIGNED-2/4; DEC-198; J-66, J-67): its own,
    // `<library>/(unassigned)`, a name SwingPaths::sanitise can never produce (it keeps only A-Z a-z 0-9 . _ -); and the
    // folder no-golfer sessions were captured into before step 5.1, `<library>/unknown`, held where it is (J-66: never
    // moved) — but only while it is a NO-GOLFER folder: it has no athlete.json (with one it is a golfer's, whose name
    // sanitised to "unknown"). Of that folder only the sessions whose file names no athlete (or Unassigned), or that
    // have no file, are Unassigned's.
    static QString unassignedFolderName() { return QStringLiteral("(unassigned)"); }
    static QString legacyNoGolferFolder(const QString &libraryRoot);   // "" when there is none, or it is a golfer's
    // The folder name's type token for a type by name (SET-04): "swing" → "Swing", "grf" → "GRF";
    // "" for none (the name then says "session").
    static QString folderTypeLabel(const QString &type);

    // A TOOL MEETS A LIVE SESSION (D11: DEC-158; audit H-14). Whether `sessionFolder`'s session file says the
    // session is in progress, read afresh (a read only: no lock, nothing written). False for a session without
    // a file (older than step 2: never in progress) and for an unreadable one. Every tool that writes into a
    // session refuses one in progress — being recorded here or on the other machine — unless the user passes
    // `--allow-in-progress` (for one a crash left open, which the athlete's next Start would end: DEC-73);
    // a dry run or a read-only mode needs no check. The archive and the trash use the store's own check.
    static bool fileSaysInProgress(const QString &sessionFolder);
    // The refusal a tool prints, naming the session and the override.
    static QString inProgressRefusal(const QString &sessionFolder);
    // The session folder a swing folder belongs to (its parent), for a tool handed a swing.
    static QString sessionFolderOfSwing(const QString &swingFolder);

    // When one swing's capture ended (UTC; invalid when its document records none): the swing door's
    // captureEndOf (SwingStoreRecordingRepository), set by the composition root. The store reads no swing
    // document itself (it links none of the swing store). With none set, a session a crash left open is
    // ended with its end not known, as before J-18.
    using CaptureEndOf = std::function<QDateTime(const QString &swingDir)>;
    void setCaptureEndReader(CaptureEndOf reader) { m_captureEndOf = std::move(reader); }

    // Tests: the clock (UTC) and the zone "today" is read in; how long a write waits for a held lock.
    void setClockForTest(std::function<QDateTime()> nowUtc) { m_now = std::move(nowUtc); }
    void setTimeZoneForTest(const QTimeZone &zone) { m_zone = zone; }
    void setLockWaitForTest(int ms) { m_lockWaitMs = ms; }
    // Tests: never use the OS trash — trash and trashShot (and so Session::end's empty discard) move into the library's own
    // `.pinpoint-trash` (FILE-LIB-01) instead, inside the test's temporary folder (AthleteStore's seam).
    void setOsTrashForTest(bool on) { m_osTrash = on; }

private:
    struct Held {
        SessionIndexEntry entry;
        QJsonObject       file;              // the session file as read (unknown keys kept)
        bool              createdHere = false;   // start() made it this run and nothing has ended it: the session
                                                 // capture writes into (the running one's folder is kept, and a
                                                 // trash or archive of it refused)
    };
    enum class FileState { Missing, Read, Unreadable };

    QDateTime nowUtc() const;
    QDate     today() const;
    // Read `folder`'s session file into an entry. Missing / Unreadable leave `out` untouched.
    FileState readFile(const QString &folder, SessionIndexEntry &out, QJsonObject &file) const;
    // The entry for one folder: its file's, or D3's legacy identity. Null identity: not a session.
    SessionIndexEntry readFolder(const QString &folder, const AthleteId &owner, QJsonObject &file,
                                 bool requireSessionName) const;
    // Write `h`'s entry into its folder's session file (merged over h.file), under the folder's lock.
    WriteResult write(Held &h);
    // Why `h`'s session may not be archived, restored or trashed now — "" when it may. Reads its session
    // file afresh: the held entry may be older than another writer's change.
    WriteResult checkNotLive(const Held &h, bool refuseReadOnly) const;
    // End every other InProgress session in `athleteDir`: its end inferred from its swings (J-18).
    void endOthersInProgress(const QString &athleteDir, const SessionId &except);
    // The latest capture end of `folder`'s swings (m_captureEndOf); invalid when none records one.
    QDateTime inferredEnd(const QString &folder) const;
    // The held session's folder; "" when `id` is not held.
    QString heldFolder(const SessionId &id) const;
    // Bring an entry read from disk into the identity map, keeping what only the map knows.
    Held &hold(const SessionIndexEntry &entry, const QJsonObject &file);
    // The folder of a held session, for swingFolder on any thread.
    void publish(const SessionId &id, const QString &folder);
    void unpublish(const SessionId &id);

    QHash<SessionId, Held>      m_held;
    mutable QMutex              m_publishedMutex;
    QHash<SessionId, QString>   m_published;   // each held session's folder (swingFolder's, any thread)
    std::function<QDateTime()>  m_now;
    QTimeZone                   m_zone = QTimeZone::systemTimeZone();
    int                         m_lockWaitMs = -1;   // -1: FolderLock::kWaitMs
    bool                        m_osTrash    = true;
    SessionArchiving           *m_archiving  = nullptr;
    ArchiveJobs                 m_archiveJobs;
    RestoreJobs                 m_restoreJobs;
    CaptureEndOf                m_captureEndOf;
};

} // namespace pinpoint
