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
// Vocabulary ring: a value type, no behaviour.

namespace pinpoint {

enum class SidecarPolicy { CachedOnly, ParseNoWrite, ParseAndWrite };

// The old one-bool form, as every existing caller passes it: true → ParseAndWrite, false → CachedOnly.
constexpr SidecarPolicy sidecarPolicyOf(bool writeSidecar)
{
    return writeSidecar ? SidecarPolicy::ParseAndWrite : SidecarPolicy::CachedOnly;
}
constexpr bool mayParse(SidecarPolicy p) { return p != SidecarPolicy::CachedOnly; }
constexpr bool mayWriteSidecar(SidecarPolicy p) { return p == SidecarPolicy::ParseAndWrite; }

} // namespace pinpoint
