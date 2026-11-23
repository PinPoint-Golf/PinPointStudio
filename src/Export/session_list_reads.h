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

// THE SESSION LIST'S ONE KEPT READ (refactor step 5.7a; cut down in 5.7a-P1): the folder's modification time, the Sessions
// drawer's order (the folder last written first) and the last fallback date of a row nothing else dates. The rest of 5.7a's
// kept reads (the swing roll-up, the size on disk, work-on 1) are now the SessionIndexEntry's summary (proposal P1, approved
// by the Solution Architect on 23 Nov 2026), read through SessionRepository::deferredSummary (session_index_summary.h).
// The modification time is not one of P1's three fields: putting it on the entry would be an information-model change
// beyond P1, so it is read here, for the one presenter (Gui/session/session_list_presenter.h), until the Solution Architect
// rules on it (NOTES-5.7a.md, "5.7a-P1"). Pure apart from the file system: any thread.

#include <QString>

namespace pinpoint {

struct SessionListFacts {
    bool   read       = false;   // false: not read yet
    qint64 modifiedMs = 0;       // the folder's modification time (epoch ms)
};

namespace SessionListReads {
SessionListFacts read(const QString &sessionFolder);
}

} // namespace pinpoint
