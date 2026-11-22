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

// FiringReader — HOW A FIRING JOB READS ONE SHOT'S READINGS, ON ANY THREAD (refactor step 3.5-i; build/refactor/
// step-3/BRIEF.md §4.4 step 5: "the job reads the shot's readings through its RecordingSource (or the phase-grid
// cache)"). A small interface keyed by the shot's ShotRef, as RecordingReader is: its adapter
// (SwingStoreFiringReader, src/Export/firing_reader_store.h) resolves the folder ONLY through
// SessionRepository::swingFolder — the one store call any thread may make — and makes exactly the read the
// diagnostics model's the per-shot firing makes today: the phase grid (its sidecar cache, else one parse), the summary
// (the hand, the capture instant, the two integrity facts), and the review's shot intent and label corrections.
// So a session no longer published (trashed, released from the store, its library gone) reads NOTHING: ok = false,
// with the reason, and the job comes back *unread* (§4.4 step 5).
//
// WHY NOT RecordingReader (the lead's call, logged in NOTES-3.5-i.md J1): RecordingReader::load hands back the
// Recording — a disk-backed window of the frames for the analysis to run on. Firing never needs the frames; it
// reads the analysis's numbers. One reading door per need keeps the frame decoder out of every target that fires.
//
// Stateless and safe from worker threads; immutable once made, so a job holds it as a shared_ptr<const>.
//
// Domain model ring (src/DomainModel): names no store type.

#include "recording_repository.h"                     // ShotReview
#include "../Diagnostics/engine/fire_findings.h"      // FiringReadings
#include "../Identity/identity.h"

#include <QString>
#include <QtGlobal>

#include <vector>

namespace pinpoint {

// The swing document's size and mtime, taken BEFORE the read (a re-analysis that rewrites the document after it
// leaves a stamp that no longer matches, so the next load asks the shot again). exists false: no document yet.
struct DocumentStamp {
    bool   exists  = false;
    qint64 size    = 0;
    qint64 mtimeMs = 0;
    friend bool operator==(const DocumentStamp &a, const DocumentStamp &b)
    {
        return a.exists == b.exists && a.size == b.size && a.mtimeMs == b.mtimeMs;
    }
    friend bool operator!=(const DocumentStamp &a, const DocumentStamp &b) { return !(a == b); }
};

// One shot's readings as a firing job reads them. ok false: nothing was read (`error` says why) and nothing is
// computed from it.
struct FiringRead {
    bool                         ok = false;
    QString                      error;
    analysis::FiringReadings     readings;
    qint64                       timestampMs = 0;   // the capture instant the document records, else its mtime
    // The capture instant alone (clock.wallclock); 0 when the document records none — never the file's mtime, which a
    // copy or a re-analysis moves. What the shot's norm cohort is dated by (DEC-171; review 2 of 3.5-i).
    qint64                       captureMs = 0;
    DocumentStamp                stamp;             // taken before the read
    // The golfer's word on the shot, its review (FILE-SHOT-02: its shot intent and label corrections), read with the
    // readings so a re-fire costs one read. Resolved against the older copy (J-33) by golfer_input_rules.h's rule,
    // in the job. ok false: the review could not be read (the older copy then stands, as golferInput has it).
    ShotReview review;
};

class FiringReader
{
public:
    virtual ~FiringReader() = default;
    // The shot's readings for `pack`'s measures. Any thread.
    virtual FiringRead read(const ShotRef &shot, const analysis::CharacteristicPack &pack) const = 0;
    // The shot's document stamp now (a stat; no read). A load compares it with the kept one (R-STAMP-2).
    virtual DocumentStamp stamp(const ShotRef &shot) const = 0;
    // The shot's review alone (the document's root, never its tracks): what a derive reads for a shot whose kept
    // result came from the file, which holds no golfer input (review 1 of 3.5-i, C1). Any thread.
    virtual ShotReview review(const ShotRef &shot) const = 0;
};

} // namespace pinpoint
