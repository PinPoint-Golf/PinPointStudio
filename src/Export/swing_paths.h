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

#include <QString>

namespace pinpoint {

// Resolves and creates the on-disk layout for swing exports:
//
//   <athleteLibraryPath>/
//     <athlete-name-sanitised>/          athlete level
//       <session-folder>_NN/             session level — name per naming pattern
//         swing_0001/                    one folder per swing
//
// The session-folder leaf is composed from the AppSettings sessionNamingPattern
// (date / athlete / session-type tokens) plus a per-day uniqueness counter.
// It is allocated lazily on the first save and cached for the lifetime of this
// object (i.e. per app run): all swings of one run share one session folder.
// The cache is invalidated when the athlete, date, library root, or composed
// session-base (pattern / session type) changes.
class SwingPaths {
public:
    struct Allocation {
        QString swingDir;     // absolute path, created; empty on failure
        QString sessionId;    // e.g. "2026-06-05_Mark-Liversedge_Swing_01"
        QString swingId;      // e.g. "swing_0007"
        int     swingIndex = 0;
    };

    // Allocates (and creates) the next swing directory.  libraryRoot may be
    // empty — falls back to <AppDataLocation>/swings with a warning.
    // namingPattern is the AppSettings sessionNamingPattern key
    // ("date-name-type" | "date-type-name" | "name-date-type" | "date-only");
    // sessionTypeLabel is a human label ("Swing"/"Wrist"/...), empty when none.
    Allocation allocateSwingDir(const QString& libraryRoot,
                                const QString& athleteName,
                                const QString& athleteUuid,
                                const QString& namingPattern = QStringLiteral("date-name-type"),
                                const QString& sessionTypeLabel = QString());

    // Filesystem-safe token: trim, whitespace/path-hostile chars -> '-',
    // collapse separators, truncate; "unknown" when nothing survives.
    static QString sanitise(const QString& raw);

private:
    // Session cache key — a new session folder is allocated when any part changes.
    QString m_cachedRoot;
    QString m_cachedAthleteUuid;
    QString m_cachedDate;        // ISO yyyy-MM-dd
    QString m_cachedBase;        // composed session-folder base (pre-counter)
    QString m_cachedSessionDir;  // absolute path
    QString m_cachedSessionId;   // folder name
};

} // namespace pinpoint
