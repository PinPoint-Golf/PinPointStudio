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

// session_archiver.h — move a whole session out of the library and back (swing_storage_impl.md,
// Phase 2 stage 6). The SESSION is the unit: it is what a golfer thinks of as "that practice",
// and it is what the review drawer lists.
//
// BEHIND SessionRepository (refactor step 2.6; brief §4.12). A caller archives a session BY ITS
// SessionId: SessionRepository::archive / restore (src/DomainModel/session_repository.h). The store
// checks the session — held, not InProgress by a fresh read of its session file, not a newer build's —
// and hands its folder to the file work here (SessionArchiver, the store's SessionArchiving). A run on a
// worker makes its own repositories, never the app's identity map: SessionArchiveRun, below, which also
// translates the folder a caller was handed into the SessionId, once, at the edge (DEC-90).
//
// ARCHIVE:
//   1. copy the session into <archiveRoot>/<athlete>/<session>/ — a plain mirror folder, each file
//      written as <name>.partial and put in place only when complete;
//   2. VERIFY: every copy is read back and compared byte for byte with the file it came from, BEFORE it
//      is put in place. Nothing in the library is touched unless all of them match;
//   3. thin each swing to a STUB: a swing.ppsw holding the summary, the metric and phase rows and
//      the review — so the session list, trends and the diagnostics ledger keep working — plus an
//      `archive` block naming where the rest went; thumb.jpg stays so the drawer still shows it. The
//      stub is written through RecordingRepository::writeArchiveStub (FILE-SHOT-10). Nothing more is
//      needed in it: the session's findings stay in its diagnostics.json, which stays in the library
//      (MD-06: DEC-135);
//   4. <session>/archived.json (FILE-SESS-06, schema "pinpoint.archive/1"), the marker every reader checks,
//      through the one atomic write — written BEFORE the first swing is thinned (step 2.6, round 2), so a
//      stub never exists without a marker naming the archive that holds its full copy, and made final
//      (its counts) at the end. From the first write the session reads as archived.
// The session file (session.json, FILE-SESS-01) is copied like any other file, and stays in the library.
// Left out of the archive and the library alike: regenerable caches (swing_summary.json,
// swing_phasegrid.json). Raw frames go INTO the archive unless the caller says otherwise — they cannot
// be regenerated, so dropping them (SET-14 off) is never a default and is a permanent delete (DEC-137).
// If a run stops part-way (a stub refused, another writer's lock), the marker is already there over the
// complete, proven archive, so the stubs already made can be restored; the error says how many.
// ⚠ A STUB IS NEVER COPIED OVER A FULL DOCUMENT: a swing that is already a stub (a run stopped before the
// marker came first, or a marker lost) keeps the mirror's full document, and an archive whose stub has no
// full copy in the mirror is refused — restore it first. Its pose and tracks would otherwise be lost.
//
// ⚠ A FOLDER, NOT A ZIP. The proposal was an uncompressed zip (.ppsa). Qt's QZipWriter has no
// Zip64: no member and no archive past 4 GB — and a session that kept its raw frames is ~900 MB a
// swing, so exactly the archives worth most would have been corrupt. A mirror folder has no such
// limit, needs no tool to open, and restores by copying back even without this application.
//
// RESTORE: the marker is read with its schema checked (a newer one is refused as read-only, a marker
// that is not one is refused); every file is copied back beside its place, verified byte for byte, and
// only then put in place; then the marker is removed. The archive copy is kept: deleting it is the
// user's call. The session's report/ folder and its session file are the exceptions: they were never
// thinned, so the library's copy is at least as new as the archive's — a send or a remake made while
// archived lives only there — and a file of them the library still holds is left alone.
//
// ⚠ NO MOMENT WITHOUT THE FILE. A copy is put in place by ONE rename over the file it replaces (POSIX
// rename(2); MoveFileExW with REPLACE_EXISTING on Windows — std::filesystem::rename, libppswing's own
// primitive), never remove-then-rename: a crash, a kill or a dropped share mid-way leaves the old file or
// the new one. (Not atomicWrite's streaming form: it cannot read its temporary back before the rename, and
// the verify-before-replace is what keeps a corrupt copy off a stub.)
//
// THE LOCK, PER WRITE (D11: DEC-158; J-11). The session's advisory lock (FolderLock, <session>/.pinpoint-lock)
// is taken around EACH write into the library and let go after it: converting one JSON-era swing, one
// swing's stub (its read, the stub, the removal of what the archive now holds), one file put back (with,
// for a swing's document, the stub's review read just before and the merge just after), and the marker.
// Never across the run: a run is minutes, and a lock older than 60 s is taken over (J-11). A lock another
// writer holds past the wait (5 s) stops the run with WriteResult::Status::Locked, in the lock's words.
// The copies into the archive mirror and the reads of the library take no lock. What keeps a second
// writer off a session that is being recorded is its session file's status, checked by the store — at the
// start, and again under the lock before EACH swing is thinned (step 2.8b; J-52), with the marker: if the
// session has been resumed (InProgress) or its marker is gone or names another archive (a restore run from
// another computer while this one was thinning), the archive stops there. A restore removes the marker
// last, under the lock, and only when no swing in the library is still a stub; otherwise the marker stays
// and the restore says so. Between the two, no swing can be left a stub without a marker.
// The swing file's own writes (RecordingRepository) take the same lock per write since step 2.8b; the
// archiver's stub, conversion and review merge are written INSIDE its hold, and join it
// (FolderLock::lockOrJoin) rather than waiting for themselves (J-47).
//
// THE GOLFER'S REVIEW IS MERGED, NEVER OVERWRITTEN (step 1.4, round 3; H-10). A review can be edited on
// the stub while archived, AND on the restored document while the restore is still running (the
// session opens on its stubs and restores on a worker). Copying a swing's document back puts the
// ARCHIVED review in place, so each swing's review is merged three ways right after its document
// lands: base = the review as archived (the archive's copy), theirs = the stub's (read under the lock,
// just before the document lands — after its copy has been made and proven, so that read is a moment
// old), mine = the restored document's at the moment of the merge. A field changed since archiving wins
// over the base; when both sides changed it, the later edit — the one made on the restored document,
// during the restore — wins, and the conflict is logged naming swing and field.
// The app's own review edits (RecordingRepository::editReview, on any other thread) take the session's lock
// since step 2.8b (J-36), so they are shut out for the moment a swing's document lands and is merged: an
// edit waits for the hold to end (or, past the wait, fails Locked and is reported) and is then made on the
// restored document. H-10's window is closed against every PinPoint writer.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "../DomainModel/recording_repository.h"   // ReviewEdit, RecordingRepository
#include "session_store.h"                          // SessionArchiving, SessionStore
#include "swing_store_repository.h"                 // the swing door a run makes for itself

namespace pinpoint {

class SessionArchiver final : public SessionArchiving
{
public:
    static constexpr const char *kMarkerSchema = "pinpoint.archive/1";

    // The swing files are written through `swings`, by ShotRef: the conversion before archiving, the
    // stub (writeArchiveStub, FILE-SHOT-10), and the review restored after (step 2.4).
    explicit SessionArchiver(RecordingRepository &swings) : m_swings(swings) {}

    // SessionArchiving: the file work on a folder the store has checked. Call SessionRepository::archive /
    // restore, not these.
    SessionArchiveResult archiveFolder(const QString &sessionDir, const SessionArchiveOptions &options,
                                       const SessionArchiveProgress &progress,
                                       const StillArchivable &stillArchivable) override;
    SessionArchiveResult restoreFolder(const QString &sessionDir, const SessionArchiveProgress &progress) override;

    static QString markerPath(const QString &sessionDir);   // <session>/archived.json (SessionFiles)
    // The marker is there. What a list checks; restore reads it with its schema checked (readMarker).
    static bool    isArchived(const QString &sessionDir);
    // Every file under a folder, hidden ones included, in bytes: what it takes on disk (the Archiving
    // tab's sizes and the session list's). Walks the tree; call it off the GUI thread.
    static qint64  folderBytes(const QString &dir);
    static QString archivePathFor(const QString &sessionDir, const QString &archiveRoot);

    // archived.json, read with its schema checked (FILE-SESS-06).
    struct Marker {
        enum class State { Missing, Read, NotAMarker, Newer } state = State::Missing;
        QString     location;   // the mirror folder (Read only)
        QJsonObject json;
    };
    static Marker readMarker(const QString &sessionDir);

    // The three-way merge of one swing's review (see the file header), per field — rating, note, club,
    // excluded, clubOverride. `edit` holds only the fields to take from the stub: changed there since
    // archiving, unchanged on the restored document. `conflicts` names the fields both sides changed
    // differently; the restored document's value (the later edit) is kept for each.
    struct ReviewMerge {
        ReviewEdit  edit;
        bool        any = false;     // edit sets at least one field
        QStringList conflicts;
    };
    static ReviewMerge mergeReview(const QJsonObject &archived, const QJsonObject &stub, const QJsonObject &now);

    // Tests: how long a write waits for a held lock (default FolderLock::kWaitMs); and the rename that puts
    // a proven copy over its place (default: std::filesystem::rename) — a test makes it fail, or looks at
    // the lock while it runs. `why` is set on failure.
    using ReplaceFn = std::function<bool(const QString &from, const QString &to, QString *why)>;
    void setLockWaitForTest(int ms) { m_lockWaitMs = ms; }
    void setReplaceForTest(ReplaceFn replace) { m_replace = std::move(replace); }
    // Tests: called before each swing is thinned (its index from 0, its folder), before its lock is taken.
    using BeforeThinFn = std::function<void(int index, const QString &swingDir)>;
    void setBeforeThinForTest(BeforeThinFn hook) { m_beforeThin = std::move(hook); }

private:
    int  lockWait() const;
    bool replace(const QString &from, const QString &to, QString *why) const;
    bool putInPlace(const QString &src, const QString &dst, QString *error, const std::function<bool()> &beforeReplace = {},
                    bool *replaced = nullptr) const;

    RecordingRepository &m_swings;
    int                  m_lockWaitMs = -1;
    ReplaceFn            m_replace;
    BeforeThinFn         m_beforeThin;
};

// One archive, restore or scan run's own repositories. A run happens on a worker (the Archiving tab,
// housekeeping), so it never shares the app's identity map (step 2.4's J4): a SessionStore of its own, the
// swing door over it, and the archiver wired into the store.
class SessionArchiveRun
{
public:
    SessionArchiveRun() { m_sessions.setArchiving(&m_archiver); }
    SessionArchiveRun(const SessionArchiveRun &) = delete;
    SessionArchiveRun &operator=(const SessionArchiveRun &) = delete;

    SessionRepository &sessions() { return m_sessions; }

    // THE EDGE (DEC-90): a session folder to its SessionId, once. Its session file's id; for a session
    // older than step 2, D3's from the athlete who owns the folder (athlete.json), else from the athlete
    // its swings record (the swing door's rule, J-39) — the id every other reader of that folder derives.
    // Null when the folder cannot be named (no session file, no owner, no swing that records one). The
    // session is then held, so SessionRepository::open answers for it.
    SessionId openSession(const QString &sessionDir);

    // ONE SESSION FOLDER ARCHIVED IN A RUN OF ITS OWN (refactor step 3.8; moved unchanged from the Archiving tab's
    // archiveOne): the folder translated to its SessionId at this edge, then SessionRepository::archive. Safe on any thread
    // (it shares nothing). The Archiving tab's by-hand archive, housekeeping's archive of a folder no Athlete holds (N1),
    // and the app's SessionStore::archiveJob all run this.
    static SessionArchiveResult archiveInOwnRun(const QString &sessionDir, const SessionArchiveOptions &options,
                                                const SessionArchiveProgress &progress = {});
    // The same for a restore (refactor step 5.6b, moved unchanged from the Archiving tab's restore): SessionRepository::
    // restore in a run of its own. The app's SessionStore::restoreJob runs this; so does the tab, for a folder no Athlete
    // holds.
    static SessionArchiveResult restoreInOwnRun(const QString &sessionDir, const SessionArchiveProgress &progress = {});

    // AN ARCHIVE OR RESTORE OF A FOLDER NO ATHLETE HOLDS KEEPS A CURRENT REPORT CURRENT (S-D6; refactor step 5.6a; since
    // 5.6b only that folder's: a held session's restamp is its Session's archive and restore command): the folder translated to its
    // SessionId in a run of its own (this edge), the report's records read through that run's repository before and after
    // `rewrite`, and FILE-SESS-07's fingerprint re-stamped through it (SessionRepository::writeReportRecord: the derived
    // facts only, never FILE-SESS-13; an older report_meta.json keeps its source keys until report_input.json reads
    // back). Safe on any thread (it shares nothing). With no `fingerprint`, or a folder that cannot be named, `rewrite`
    // simply runs. session_report_io.h keepCurrent holds the rule.
    static SessionArchiveResult keepReportCurrent(const QString &sessionDir, const ReportFingerprintFn &fingerprint,
                                                  const std::function<SessionArchiveResult()> &rewrite);

    // THE LIBRARY'S SESSION FOLDERS AS THE ARCHIVING TAB LISTS THEM (refactor step 3.8; moved unchanged from
    // archive_controller.cpp's scanSessions): every `<root>/<dir>/<dir>` holding a swing, its day its SessionIdentity's
    // (else the folder's modification time), its archive marker, newest first (then folder, descending). `withBytes`
    // false skips the size walk (housekeeping needs no sizes). Reads only; call it off the GUI thread.
    struct LibrarySession {
        QString dir, name, athlete;
        qint64  dateMs = 0;
        qint64  bytes  = 0;
        int     swings = 0;
        bool    archived = false;
    };
    static QList<LibrarySession> scanLibrary(const QString &root, bool withBytes = true);

    // Tests.
    void setLockWaitForTest(int ms) { m_archiver.setLockWaitForTest(ms); m_sessions.setLockWaitForTest(ms); }
    void setReplaceForTest(SessionArchiver::ReplaceFn replace) { m_archiver.setReplaceForTest(std::move(replace)); }
    void setBeforeThinForTest(SessionArchiver::BeforeThinFn hook) { m_archiver.setBeforeThinForTest(std::move(hook)); }

private:
    SessionStore                  m_sessions;
    SwingStoreRecordingRepository m_swings{ m_sessions };
    SessionArchiver               m_archiver{ m_swings };
};

} // namespace pinpoint
