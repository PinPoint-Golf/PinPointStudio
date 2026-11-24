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

// THE SESSION INDEX ENTRY'S SUMMARY, AS READ FROM ITS FOLDER (refactor step 5.7a-P1; information_model.md OBJ-SESSION-INDEX,
// proposal P1: the Solution Architect, 23 Nov 2026). SessionStore's file work behind SessionRepository::deferredSummary, set on
// it by the composition root (SessionStore::setSummaryReads), as the archive's is. Exactly the reads the three session lists
// made before (5.7a's kept reads, Export/session_list_reads.h, which now keeps only the folder's modification time):
//   - the swing roll-up: each swing's cheap summary (SwingDocReader::readSwingSummary, writeSidecar = false: a list read never
//     fat-parses and never writes), folded by rollUpSwings, and how many swing folders there are;
//   - the size on disk: every file in the folder (SessionArchiver::folderBytes);
//   - the lead work-on: work-on 1 of the session's summary (work_ons.json, FILE-SESS-05, through its one reader), under this
//     build's rule only;
//   - the gate counts: that summary's `gates` block (platform refactor P-3b), by presence.
// Pure apart from the file system: any thread.

#include "../DomainModel/session_roll_up.h"

#include <QString>

namespace pinpoint {

namespace SessionIndexSummaries {
SessionIndexSummary read(const QString &sessionFolder);
}

} // namespace pinpoint
