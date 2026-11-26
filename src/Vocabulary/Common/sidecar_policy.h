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

// SidecarPolicy — what a cheap swing reader may do when a swing's cache sidecar (swing_phasegrid.json, a JSON-era
// swing_summary.json, a .ppsw's older summary block) misses or is stale (refactor step 3.7-0, the D22 fix).
//
// Before 3.7-0 the readers took ONE bool, `writeSidecar`, which meant two things at once: "may parse the document"
// and "may write the sidecar". False ("never fat-parse") is the GUI-thread contract; the parallel run's firing reader
// passed false to mean "write no file", and so — during a live session, before any sidecar existed — read every shot
// as an empty grid (build/refactor/step-3/3.7/D22-DIFF-NOTES.md). The two meanings are now apart:
//
//   CachedOnly     never parse: a miss or a stale sidecar reads as nothing (an empty grid, ok=false), so a GUI
//                  thread never stalls on a fat parse. The old `writeSidecar=false`.
//   ParseNoWrite   parse the document on a miss or a stale sidecar, exactly as ParseAndWrite reads it, but write
//                  NOTHING (no sidecar, no file). For a reader that must leave the session's files to their one
//                  writer: the parallel run's firing reader (§4.9 "in memory only"; REVIEW-3.5-ii-1 item 5(a)).
//   ParseAndWrite  parse on a miss and index the result (the sidecar written, best-effort). The old
//                  `writeSidecar=true`, every reader's default.
//
// Vocabulary ring: a value type and the stamp rule below, no I/O.

#include <QtGlobal>

namespace pinpoint {

enum class SidecarPolicy { CachedOnly, ParseNoWrite, ParseAndWrite };

// The old one-bool form, as every existing caller passes it: true → ParseAndWrite, false → CachedOnly.
constexpr SidecarPolicy sidecarPolicyOf(bool writeSidecar)
{
    return writeSidecar ? SidecarPolicy::ParseAndWrite : SidecarPolicy::CachedOnly;
}
constexpr bool mayParse(SidecarPolicy p) { return p != SidecarPolicy::CachedOnly; }
constexpr bool mayWriteSidecar(SidecarPolicy p) { return p == SidecarPolicy::ParseAndWrite; }

// IS THIS STILL THE DOCUMENT A SIDECAR WAS STAMPED FROM? Every cache written to disk beside a swing
// (swing_phasegrid.json, a JSON-era swing_summary.json) records the document's size and mtime when it
// was built, and every reader of such a stamp asks HERE, so they all agree on what "stale" means
// (issue #174: the Club delivery note counted copied grids as stale while the grid reader rebuilt them).
// Here, beside the policy, so the phase-grid sampler's pure loader (Diagnostics/model/measure_sample.h) asks the
// same rule without including the swing store; SwingStore::sameDocumentStamp (swing_store.h) is this function
// (platform refactor P-3).
//
// Same size, and the same mtime — at the resolution the document's clock still has. A copy or restore
// can carry a document's mtime to the whole second only: macOS's openrsync (rsync 2.6.9 compatible)
// truncates mtimes when it copies with -a, so 1794051638750 came back as 1794051638000 on 11 Nov 2026
// (the share itself, smbfs onto NTFS, keeps 100 ns; other old copy tools truncate too). So when the
// document's mtime NOW has no milliseconds, the stamp matches within that second. Safe because the other
// direction is never relaxed (a stamp of …000 against a document at …001 is a rewrite), and a false match
// needs a rewrite of the very same byte size inside the same second — a re-analysis takes far longer than
// that, and a filesystem that only ever keeps whole seconds already had that blind spot with an exact compare.
// A copy that resets the mtime (plain cp) is a different file to any stat, and still reads stale.
//
// In-process comparisons of two stats of the same file (LazyDocument::stillCurrent) stay exact.
inline bool sameDocumentStamp(qint64 stampSize, qint64 stampMtimeMs, qint64 size, qint64 mtimeMs)
{
    if (stampSize != size) return false;
    if (stampMtimeMs == mtimeMs) return true;
    return mtimeMs > 0 && stampMtimeMs > 0 && mtimeMs % 1000 == 0 && stampMtimeMs / 1000 == mtimeMs / 1000;
}

} // namespace pinpoint
