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
// ARCHIVE:
//   1. copy the session into <archiveRoot>/<athlete>/<session>/ — a plain mirror folder, each file
//      written as <name>.partial and renamed only when complete;
//   2. VERIFY: every copy is read back and compared byte for byte with the file it came from.
//      Nothing in the library is touched unless all of them match;
//   3. thin each swing to a STUB: a swing.ppsw holding the summary, the metric and phase rows and
//      the review — so the session list, trends and the diagnostics ledger keep working — plus an
//      `archive` block naming where the rest went; thumb.jpg stays so the drawer still shows it;
//   4. write <session>/archived.json, the marker every reader checks.
// Left out of the archive and the library alike: regenerable caches (swing_summary.json,
// swing_phasegrid.json). Raw frames go INTO the archive unless the caller says otherwise — they
// cannot be regenerated, so dropping them is never a default.
//
// ⚠ A FOLDER, NOT A ZIP. The proposal was an uncompressed zip (.ppsa). Qt's QZipWriter has no
// Zip64: no member and no archive past 4 GB — and a session that kept its raw frames is ~900 MB a
// swing, so exactly the archives worth most would have been corrupt. A mirror folder has no such
// limit, needs no tool to open, and restores by copying back even without this application.
//
// RESTORE: copy every file back over the stubs, verify each byte for byte, and only then remove
// the marker. The archive copy is kept: deleting it is the user's call.

#include <QString>

#include <functional>

namespace pinpoint::SessionArchiver {

struct Options {
    bool keepRaw = true;   // pack <alias>.raw sidecars; false drops them (irreversible)
};

struct Result {
    bool    ok = false;
    QString error;
    QString archivePath;
    int     swings      = 0;
    int     members     = 0;
    qint64  bytesPacked = 0;   // written into the archive
    qint64  bytesFreed  = 0;   // removed from the library (archive) / restored (restore)
};

// done/total in files; return false to cancel (before anything in the library is touched).
using Progress = std::function<bool(int done, int total)>;

QString markerPath(const QString &sessionDir);                 // <session>/archived.json
bool    isArchived(const QString &sessionDir);
QString archivePathFor(const QString &sessionDir, const QString &archiveRoot);

Result archive(const QString &sessionDir, const QString &archiveRoot, const Options &opt = {},
               const Progress &progress = {});
Result restore(const QString &sessionDir, const Progress &progress = {});

} // namespace pinpoint::SessionArchiver
