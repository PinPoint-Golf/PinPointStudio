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

// SessionFiles — the names of a session folder's own files, spelled once (H-12; refactor step 2.2,
// brief §4.7). Adapters ring (src/Export), beside the repository that reads and writes them
// (session_store.h).
//
//   <session>/
//     session.json            FILE-SESS-01: the session's id, athlete, type, status, start and end
//                             times and origin (DEC-149, DEC-160: IM-S2-1). Written by SessionStore only
//     .pinpoint-last-swing    FILE-SESS-10: the highest swing number the session has issued (DEC-91)
//     .pinpoint-lock          EXT-12: the advisory lock (FolderLock, src/Core/folder_lock.h)
//     swing_NNNN/             one folder per shot (SwingPaths composes the name)
//
// ⚠ THE OTHER SESSION FILES JOIN HERE WITH THE ITEM THAT MOVES THEIR READER OR WRITER, not before:
// `diagnostics.json`, `club_baseline.json`, `work_ons.json` and `session_input.json` are owned today by
// lint A4's table (src/Gui/tests/architecture_lint_test.cpp: the diagnostics model and the work-ons
// controller), so spelling them here now would be a second spelling, not the one (2.5, and 2.8's A4
// extension); `archived.json` comes with the archive (2.6); the report folder, its meta file and the
// PDF, and the trunk ceremony, come with 2.8's A4 extension as path functions their writers call (D9).
//
// The swing counter's two accessors live here too, because two writers use it until step 3: the
// repository's allocation, and SwingPaths::trashPath, which records the number of a swing that
// leaves its session so it is never issued again.

#include "../Core/atomic_write.h"
#include "../Core/folder_lock.h"

#include <QFile>
#include <QString>

// Header-only, like atomic_write.h: SwingPaths (compiled by some 26 test targets) reads the swing counter,
// and a .cpp here would join every one of their link lines.

namespace pinpoint {

class SessionFiles
{
public:
    // "session.json"
    static QString sessionFileName() { return QStringLiteral("session.json"); }
    static QString sessionFile(const QString &sessionDir) { return sessionDir + QLatin1Char('/') + sessionFileName(); }

    // ".pinpoint-last-swing"
    static QString swingHighWaterFileName() { return QStringLiteral(".pinpoint-last-swing"); }
    static QString swingHighWaterFile(const QString &sessionDir)
    {
        return sessionDir + QLatin1Char('/') + swingHighWaterFileName();
    }

    // ".pinpoint-lock": FolderLock's own (src/Core/folder_lock.h), named here so the list is complete.
    static QString lockFileName() { return FolderLock::fileName(); }

    // The highest swing number the session has recorded issuing; 0 when the file is missing or
    // unreadable.
    static int recordedSwingHighWater(const QString &sessionDir)
    {
        QFile f(swingHighWaterFile(sessionDir));
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        bool ok = false;
        const int n = QString::fromLatin1(f.read(32)).trimmed().toInt(&ok);
        return ok && n > 0 ? n : 0;
    }

    // Raise the recorded high-water to n; never lowers it (an n at or below it writes nothing and
    // answers ok). Written whole through the one atomic write (R-PERS-2): a crash mid-write leaves the
    // old number, never a truncated file that reads as 0. Into the session's existing folder only:
    // this never makes one.
    static AtomicWriteResult raiseSwingHighWater(const QString &sessionDir, int n)
    {
        if (n <= recordedSwingHighWater(sessionDir))
            return { true, {}, {} };
        return atomicWrite(swingHighWaterFile(sessionDir), QByteArray::number(n),
                           AtomicWriteOptions::intoExistingFolder());
    }
};

} // namespace pinpoint
