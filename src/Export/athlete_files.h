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

// AthleteFiles — the names of an athlete folder's own files, spelled once (H-12; refactor step 2.3,
// brief §4.7). Adapters ring (src/Export), beside the repository that reads and writes them
// (athlete_store.h). Lint A4 names this file the owner of "athlete.json".
//
//   <library>/<athlete>/
//     athlete.json            FILE-ATH-01: the record and the bag (DEC-05, DEC-151). Written by
//                             AthleteStore only
//     .pinpoint-lock          EXT-12: the advisory lock (FolderLock, src/Core/folder_lock.h)
//     <session folders>/      SessionFiles (session_files.h)
//
// ⚠ THE OTHER ATHLETE FILES JOIN HERE WITH THE ITEM THAT MOVES THEIR READER OR WRITER, not before (as
// SessionFiles does, step 2.2's J14): `tendencies.json` and `swing_themes*.json` are owned by lint A4's
// table today (the work-ons controller; their writer is step 3's AthleteDiagnostics), `fault_profile.json`
// by the diagnostics model (deleted in step 3), and the tuning log `outcome_corrections.jsonl`
// (FILE-ATH-04) is still spelled by the diagnostics model's tuningLogPath(), whose folder derivation is
// 2.7a's — its APPEND already goes through the repository's appendTuningLine below, under the folder's
// lock. Header-only (like SessionFiles), so the diagnostics model's six link lines gain folder_lock.cpp
// only, not the store.

#include "../Core/atomic_write.h"   // AtomicWriteResult
#include "../Core/folder_lock.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace pinpoint {

class AthleteFiles
{
public:
    // "athlete.json"
    static QString athleteFileName() { return QStringLiteral("athlete.json"); }
    static QString athleteFile(const QString &athleteDir) { return athleteDir + QLatin1Char('/') + athleteFileName(); }

    // ".pinpoint-lock": FolderLock's own, named here so the list is complete.
    static QString lockFileName() { return FolderLock::fileName(); }

    // THE TUNING LOG's append (FILE-ATH-04: append-only, never rewritten; DEC-43): one line added to
    // `logFile` under its athlete folder's advisory lock, held for this one append (brief §4.9). The
    // athlete repository's write (AthleteStore::appendTuningLog forwards here).
    static AtomicWriteResult appendTuningLine(const QString &logFile, const QByteArray &line,
                                              int lockWaitMs = FolderLock::kWaitMs)
    {
        const QString folder = QFileInfo(logFile).absolutePath();
        FolderLock    lock(folder);
        if (lock.lock(lockWaitMs) != FolderLock::Result::Acquired)
            return { false, lock.errorString().isEmpty() ? QStringLiteral("The athlete folder %1 is locked.").arg(folder)
                                                         : lock.errorString(), lock.holder() };
        QFile f(logFile);
        if (!f.open(QIODevice::Append | QIODevice::Text))
            return { false, QStringLiteral("Could not append to %1.").arg(logFile), f.errorString() };
        QByteArray bytes = line;
        if (!bytes.endsWith('\n'))
            bytes.append('\n');
        if (f.write(bytes) != bytes.size() || !f.flush())
            return { false, QStringLiteral("Could not append to %1.").arg(logFile), f.errorString() };
        return { true, {}, {} };
    }
};

} // namespace pinpoint
