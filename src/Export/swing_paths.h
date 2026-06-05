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
//       <YYYY-MM-DD_session-NN>/         session level — date + per-day index
//         swing_0001/                    one folder per swing
//
// The session folder is allocated lazily on the first save and cached for the
// lifetime of this object (i.e. per app run): all swings recorded today by the
// same athlete into the same library share one session-NN.  The cache is
// invalidated when the athlete, date, or library root changes.
class SwingPaths {
public:
    struct Allocation {
        QString swingDir;     // absolute path, created; empty on failure
        QString sessionId;    // e.g. "2026-06-05_session-01"
        QString swingId;      // e.g. "swing_0007"
        int     swingIndex = 0;
    };

    // Allocates (and creates) the next swing directory.  libraryRoot may be
    // empty — falls back to <AppDataLocation>/swings with a warning.
    Allocation allocateSwingDir(const QString& libraryRoot,
                                const QString& athleteName,
                                const QString& athleteUuid);

    // Filesystem-safe token: trim, whitespace/path-hostile chars -> '-',
    // collapse separators, truncate; "unknown" when nothing survives.
    static QString sanitise(const QString& raw);

private:
    // Session cache key — a new session-NN is allocated when any part changes.
    QString m_cachedRoot;
    QString m_cachedAthleteUuid;
    QString m_cachedDate;        // ISO yyyy-MM-dd
    QString m_cachedSessionDir;  // absolute path
    QString m_cachedSessionId;   // folder name
};

} // namespace pinpoint
