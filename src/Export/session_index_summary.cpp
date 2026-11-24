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

#include "session_index_summary.h"

#include "session_archiver.h"
#include "session_diagnostics_files.h"   // work_ons.json's one reader (step 2.5)
#include "swing_doc.h"
#include "../Coaching/session_gates.h"
#include "../Coaching/work_ons.h"

#include <QVector>

namespace pinpoint {

SessionIndexSummary SessionIndexSummaries::read(const QString &sessionFolder)
{
    SessionIndexSummary out;
    out.read = true;
    const QStringList swingDirs = SwingDocReader::findSwingDirs(sessionFolder);
    QVector<ShotSummaryInput> shots;
    for (const QString &sd : swingDirs) {
        const SwingSummary s = SwingDocReader::readSwingSummary(sd, /*writeSidecar=*/false);
        if (!s.ok) continue;   // not indexed: counted, not folded
        shots.append({ s.score, s.hasVideo, s.wallclockMs, s.club, s.thumbnailPath, s.devices });
    }
    out.swings = rollUpSwings(shots, int(swingDirs.size()));
    out.bytes  = SessionArchiver::folderBytes(sessionFolder);

    // The record's ordering rule is the only one a list may quote: a record from an older rule is about to be derived
    // again, and its work-on 1 may not be the new rule's (the report list's rule before 5.7a).
    const JsonFileRead f = SessionDiagnosticsFiles::readWorkOns(sessionFolder);
    if (f.exists) {
        // The gate counts (platform refactor P-3b), by presence and under any work-on rule: they count the swings' records,
        // which no work-on rule decides. Absent (a summary written before the block): `has` false, not summarised yet.
        out.gates = analysis::sessionGatesFromJson(f.root.value(QStringLiteral("gates")));
        bool ok = false;
        const analysis::SessionWorkOns rec = analysis::sessionWorkOnsFromJson(f.root, &ok);
        if (ok) {
            out.workOn.known = true;
            for (const analysis::WorkOnEntry &e : rec.entries) {
                if (e.rank == 1 || out.workOn.id.isEmpty()) { out.workOn.id = e.id; out.workOn.name = e.name; }
                if (e.rank == 1) break;
            }
        }
    }
    return out;
}

} // namespace pinpoint
