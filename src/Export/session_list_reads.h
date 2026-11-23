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

// THE SESSION LIST'S KEPT READS (refactor step 5.7a; build/refactor/step-5/DUTIES-5.7a.md): the facts of a session row
// that its SessionIndexEntry does not hold yet, read exactly as the three lists each read them before 5.7a, now once,
// here, for the one presenter (Gui/session/session_list_presenter.h). The entry gaining them is proposal P1, an
// information-model change for the Solution Architect; until it is answered they are read from the folder:
//   - each swing's cheap summary (SwingDocReader::readSwingSummary, writeSidecar = false: a list read never fat-parses and
//     never writes), and how many swing folders there are;
//   - the folder's size on disk, every file (SessionArchiver::folderBytes) and its modification time (the drawer's order);
//   - work-on 1 of the session's summary (work_ons.json through its one reader), under this build's rule only.
// Pure apart from the file system: any thread.

#include <QString>
#include <QStringList>
#include <QVector>

namespace pinpoint {

// One swing's cheap summary, as the session lists fold it (session_summary.h's ShotSummaryInput fields).
struct SessionListSwing {
    int         score       = 0;
    bool        hasVideo    = false;
    qint64      wallclockMs = 0;    // 0 = unknown
    QString     club;
    QString     thumbnailPath;      // absolute; "" when none
    QStringList devices;            // swingDeviceTokens(), as stored (the fold makes them canonical)
};

struct SessionListFacts {
    bool        read       = false;   // false: not read yet (a row whose facts are still to come)
    int         swings     = 0;       // swing folders listed
    bool        indexed    = true;    // every swing has its cheap summary
    QVector<SessionListSwing> shots;  // the swings whose cheap summary read, in folder order
    qint64      bytes      = 0;       // every file in the folder (0 when not asked)
    qint64      modifiedMs = 0;       // the folder's modification time (epoch ms)
    bool        workOnKnown = false;  // work_ons.json holds a record of this build's rule
    QString     workOn1Id;            // its rank-1 entry (else its first)
    QString     workOn1Name;
};

namespace SessionListReads {
// The kept reads of one session folder. `withBytes` false leaves `bytes` 0 (a walk of every file).
SessionListFacts read(const QString &sessionFolder, bool withBytes = true);
}

} // namespace pinpoint
