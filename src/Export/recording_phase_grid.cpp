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

// SwingStorePhaseGridReader::phaseGrid (recording_loader.h; platform refactor P-3): the Recording's reader's phase grid,
// in a file of its own so only the targets that make this reader link the sampler and the firing reader's store.

#include "recording_loader.h"

#include "firing_reader_store.h"   // writeShotSidecarsUnderLock: the shot-keyed guarded write
#include "phase_grid_store.h"      // the phase grid's file door (FILE-SHOT-07)
#include "swing_store.h"
#include "../DomainModel/session_repository.h"

namespace pinpoint {

std::optional<analysis::SwingPhaseGrid> SwingStorePhaseGridReader::phaseGrid(const ShotRef &shot,
                                                                             const analysis::PhaseGridConfig &config,
                                                                             SidecarPolicy policy)
{
    // The folder ONLY through SessionRepository::swingFolder (any thread). "": the session is not open (trashed,
    // released, its library gone) — no door to the shot, so nothing is read.
    const QString swingDir = sessions().swingFolder(shot);
    if (swingDir.isEmpty())
        return std::nullopt;

    // The read itself never writes: a ParseAndWrite's rebuilt grid is indexed below under the session's lock and outside
    // the archive window, as the firing read's is (R1-m6), never by the unlocked folder-keyed write.
    SwingStore::LazyDocument parsed(swingDir);
    const SidecarPolicy readPolicy = mayWriteSidecar(policy) ? SidecarPolicy::ParseNoWrite : policy;
    analysis::SwingPhaseGrid grid = PhaseGridStore::read(swingDir, readPolicy, config, &parsed);
    if (mayWriteSidecar(policy) && parsed.loaded())
        writeShotSidecarsUnderLock(swingDir, &grid, nullptr, parsed);
    parsed.release();
    return grid;
}

} // namespace pinpoint
