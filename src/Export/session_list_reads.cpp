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

#include "session_list_reads.h"

#include "session_archiver.h"
#include "session_diagnostics_files.h"   // work_ons.json's one reader (step 2.5)
#include "swing_doc.h"
#include "../Coaching/work_ons.h"

#include <QFileInfo>

namespace pinpoint {

SessionListFacts SessionListReads::read(const QString &sessionFolder, bool withBytes)
{
    SessionListFacts out;
    out.read = true;
    const QStringList swingDirs = SwingDocReader::findSwingDirs(sessionFolder);
    out.swings = int(swingDirs.size());
    for (const QString &sd : swingDirs) {
        const SwingSummary s = SwingDocReader::readSwingSummary(sd, /*writeSidecar=*/false);
        if (!s.ok) { out.indexed = false; continue; }
        out.shots.append({ s.score, s.hasVideo, s.wallclockMs, s.club, s.thumbnailPath, s.devices });
    }
    out.modifiedMs = QFileInfo(sessionFolder).lastModified().toMSecsSinceEpoch();
    if (withBytes)
        out.bytes = SessionArchiver::folderBytes(sessionFolder);

    // The record's ordering rule is the only one a list may quote: a record from an older rule is about to be derived
    // again, and its work-on 1 may not be the new rule's (the report list's rule before 5.7a).
    const JsonFileRead f = SessionDiagnosticsFiles::readWorkOns(sessionFolder);
    if (f.exists) {
        bool ok = false;
        const analysis::SessionWorkOns rec = analysis::sessionWorkOnsFromJson(f.root, &ok);
        if (ok) {
            out.workOnKnown = true;
            for (const analysis::WorkOnEntry &e : rec.entries) {
                if (e.rank == 1 || out.workOn1Id.isEmpty()) { out.workOn1Id = e.id; out.workOn1Name = e.name; }
                if (e.rank == 1) break;
            }
        }
    }
    return out;
}

} // namespace pinpoint
