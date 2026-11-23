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

// SeriesReader — HOW THE SESSION'S SERIES READ ONE SHOT'S ANALYSIS, ON ANY THREAD (refactor 5.5a; OBJ-SESSION-SERIES:
// "from the shots' measurements"). A small interface keyed by the shot's ShotRef, as FiringReader and RecordingReader
// are: one reading door per need (the lead's call J1 of 3.5-i), so the frame decoder stays out of every target that
// draws a chart. Its adapter (SwingStoreSeriesReader, src/Export/series_store.h) resolves the folder ONLY through
// SessionRepository::swingFolder, the one store call any thread may make; a session no longer published reads nothing.
//
// What it reads: the shot's analysis WITHOUT its per-frame tracks (the root chunk and /analysis/metrics; never the pose
// blob), with the document's stamp taken BEFORE the read, so a re-analysis that rewrites the document after it leaves
// a stamp that no longer matches. And, for the one view that draws poses (the Posture picture strip, refactor 5.5c), the
// whole document, stamped the same way.
//
// Stateless and safe from worker threads; a job holds it as a shared_ptr<const>.
//
// Domain model ring (src/DomainModel): names no store type.

#include "firing_reader.h"   // DocumentStamp
#include "../Identity/identity.h"

#include <QJsonObject>
#include <QString>

namespace pinpoint {

struct SeriesRead {
    DocumentStamp stamp;   // taken before the read; exists false: no document yet
    QJsonObject   root;    // empty when nothing was read
    QString       error;   // why nothing was read (no document, the session not open, unreadable)
};

class SeriesReader
{
public:
    virtual ~SeriesReader() = default;
    // The shot's document stamp now (a stat; no read).
    virtual DocumentStamp stamp(const ShotRef &shot) const = 0;
    // The shot's analysis without its tracks. Any thread.
    virtual SeriesRead analysis(const ShotRef &shot) const = 0;
    // The shot's WHOLE document, its per-frame tracks included: the Posture picture's poses (refactor 5.5c). The heavy
    // read (150–220 ms a swing), made only for the few shots a picture draws. Any thread.
    virtual SeriesRead document(const ShotRef &shot) const = 0;
};

} // namespace pinpoint
