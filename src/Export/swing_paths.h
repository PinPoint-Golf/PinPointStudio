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

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <atomic>

namespace pinpoint {

// The library's names: the one place that COMPOSES a session or swing folder name, reads the number
// of a `swing_NNNN` folder, and lists a session's swing folders (P7, lint A3; refactor step 2.1,
// brief §4.2). Reading a SESSION folder's name — its date, type and `_NN` — is SessionIdentity's
// (src/DomainModel/session_identity.h), and only for a session without a session file.
//
//   <athleteLibraryPath>/
//     <athlete-name-sanitised>/          athlete level
//       <session-folder>_NN/             session level — name per naming pattern
//         swing_0001/                    one folder per swing
//
// A folder's name is a property and provenance, never an identity: a session is a SessionId, a shot a
// ShotRef (src/Identity/identity.h).
//
// ONE HALF ONLY (refactor step 2.2). The session allocator that lived here — allocateSwingDir,
// findTodaySessionDir, beginSession, endSession and their per-run session cache — is gone: a session's
// folder is chosen ONCE, at Start, by SessionRepository (src/DomainModel/session_repository.h, adapter
// src/Export/session_store.h), which keeps it for the session's life and issues its swing numbers.
// What is left are pure names, the one swing_* listing and the trash.
class SwingPaths {
public:
    // ── Names ──────────────────────────────────────────────────────────────
    // Filesystem-safe token: trim, whitespace/path-hostile chars -> '-',
    // collapse separators, truncate; "unknown" when nothing survives.
    static QString sanitise(const QString& raw);

    // The base of a new session folder's name (before its "_NN"), per the naming pattern
    // (SET-04: "date-name-type" (the default, and any unknown key) | "date-type-name" |
    // "name-date-type" | "date-only"). `athleteToken` is already sanitised (the athlete folder's
    // name); `sessionTypeLabel` is the human label ("Swing", "Wrist", "GRF", "Coach"), sanitised
    // here, "session" when empty.
    static QString sessionFolderBase(const QString& namingPattern, const QDate& day,
                                     const QString& athleteToken, const QString& sessionTypeLabel);
    // "<base>_NN", the counter at least two digits: ("2026-06-05_Mark_Swing", 3) -> "…_Swing_03".
    static QString sessionFolderName(const QString& base, int counter);
    // One past the highest "_NN" of the "<base>_NN" folders under athleteDir (1 when there are none).
    static int nextSessionCounter(const QString& athleteDir, const QString& base);

    // "swing_0007" for 7; "" for a number below 1.
    static QString swingFolderName(int swingNumber);
    // The number of a "swing_NNNN" folder name; 0 when the name is not one.
    static int swingNumberOf(const QString& folderName);
    // THE one listing of a session's swing folders: the names of its "swing_*" sub-folders, by name.
    // Read each one's number with swingNumberOf.
    static QStringList swingFolders(const QString& sessionDir);

    // ⭐ DELETE = TRASH, AND THE VOLUME MAY NOT HAVE ONE (2 Sept 2026).  Every
    // delete in this application is a move to the OS trash, so it is
    // recoverable.  A network share has no trash — macOS answers
    // NSCocoaErrorDomain 3328, "the volume doesn't have one" — and until now
    // every delete of a session or shot on the SwingData share failed, with a
    // log line and nothing on screen.  This tries the OS trash and, where the
    // volume refuses, MOVES the folder into a library-local trash on the same
    // volume: `<libraryRoot>/.pinpoint-trash/<athlete>/<stamp>_[<session>_]<name>`,
    // hidden from every listing (dot-prefixed; the enumerators do not pass
    // QDir::Hidden) and as recoverable as the OS trash would have been.  `where`
    // names the destination for the log.
    // (Stays with the names; step 2.3 adds the athlete case, §3.6.)
    // A swing that leaves takes its number with it: its session's high-water records it
    // (SessionFiles::raiseSwingHighWater), so the number is never issued again (DEC-91).
    // `useOsTrash` false skips the OS trash and goes straight to the library-local one: a test's seam
    // (SessionStore::setOsTrashForTest), so a suite's trashes stay inside its temporary folder.
    static bool trashPath(const QString& path, QString* where = nullptr, bool useOsTrash = true);

    // THE LIBRARY'S OWN TRASH, LISTED AND PURGED (refactor step 3.8; FILE-LIB-01). Its entries are
    // `<libraryRoot>/.pinpoint-trash/<athlete>/<entry>`, each a session, a swing (trashPath above) or a whole athlete
    // (AthleteStore::trash), by path. The OS trash is not listed: what the app moved there is the OS's, and the OS
    // emptying it is a permanent delete outside the app (Mark, 15 Nov 2026; DEC-175).
    static QStringList trashEntries(const QString& libraryRoot);
    // When an entry was trashed: the yyyyMMdd-HHmmss stamp trashPath puts on its name, else its modification time.
    static QDateTime trashedAt(const QString& entry);
    // THE ONE PURGE (MD-08: DEC-80, DEC-137), carried out here for housekeeping (src/App/housekeeping.h, the one
    // application service that ends things for good; it reaches this through the composition root's callable, D18 (c):
    // DEC-167): every entry trashed before `cutoff` — an invalid cutoff: every entry — deleted for good. The only code
    // that ends a trashed thing; "Empty now" and the retention pass both come here.
    struct Purged { int removed = 0; int failed = 0; };
    static Purged purgeTrash(const QString& libraryRoot, const QDateTime& cutoff = QDateTime());

    // THE LIBRARY'S SIZE (refactor step 3.8; DEC-188: housekeeping and the repositories report sizes): every file under
    // `libraryRoot`, hidden ones included, the abort polled every 256 files (-1 when it stopped: half a library measured
    // is not a smaller library). Moved unchanged from AppSettings::refreshSessionBytes; housekeeping hands it to the
    // settings page through the composition root. Walks the tree: call it off the GUI thread.
    static qint64 libraryBytes(const QString& libraryRoot, const std::atomic_bool* abort = nullptr);
};

} // namespace pinpoint
