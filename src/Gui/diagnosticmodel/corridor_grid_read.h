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

// THE MODEL BROWSER'S FOLDER READ OF A SWING'S GRID (platform refactor P-3, P-3b; moved here from the sampler,
// measure_sample.h, by B1.14 under the Solution Architect's ruling R11, 24 Nov 2026): a forwarder to the grid's file door
// (PhaseGridStore, src/Export/phase_grid_store.h), which owns the read, the sidecar and its write (FILE-SHOT-07).
// Everything else reads a shot's grid by ShotRef (RecordingReader::phaseGrid, src/DomainModel/recording_reader.h; the
// swing on screen through shot_grid_door.h) or, inside the store, through the door itself. Kept for two callers, both on
// the model browser's worker, with no Shot to ask:
//   - THE CORRIDOR SCAN, a recorded exception (information_model.md FILE-SHOT-07; PR-21 part b): the corridor editor
//     reduces a measure that may not be saved yet over every swing of the library, which needs each swing's grid itself,
//     not a summary. It is a rare use, so it keeps this read rather than going through every session's Shots; if the use
//     grows, a faster way is found then.
//   - THE GATE COUNT'S FALLBACK (gate_count_reads.h): the count is a sum over the session summaries' `gates` blocks, and
//     only a session not summarised yet has its swings read here, until its summary is written again.
// `writeSidecar` true is SidecarPolicy::ParseAndWrite (both callers), false is CachedOnly ("never fat-parse").
//
// DECLARED HERE, DEFINED IN model_browser.cpp, beside the one include of the door: the architecture allowlist counts that
// include against model_browser.cpp's A5 line (R11: no line of its own), so this header includes no repository and
// gate_count_reads.h can use it freely. A suite that compiles gate_count_reads.h without model_browser.cpp defines it
// itself, onto the same door (session_gates_test).

#include "../../Vocabulary/Analysis/phase_grid.h"   // SwingPhaseGrid, PhaseGridConfig

#include <QString>

namespace pinpoint::diagmodel {

analysis::SwingPhaseGrid readPhaseGrid(const QString &swingDir, bool writeSidecar = true,
                                       const analysis::PhaseGridConfig &cfg = {});

} // namespace pinpoint::diagmodel
