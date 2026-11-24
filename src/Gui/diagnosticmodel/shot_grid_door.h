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

// THE MODEL BROWSER'S DOOR TO THE SWING ON ITS SCREEN (platform refactor P-3, ruled 24 Nov 2026): the one shot the panel
// follows is read by its ShotRef through the Recording's reader (RecordingReader::phaseGrid), never by its folder. Only
// the panel's corridor scan (a recorded exception) and the gate count's fallback for a session not summarised yet still
// read by folder (corridor_grid_read.h readPhaseGrid); the gate count itself sums the session summaries (gate_counts.h).
//
// WHY A DOOR: ModelBrowser is a QML type of the module, which the QML suites compile without the domain model. The module
// holds only this header; the composition root (main.cpp) installs the app's resolver, which finds the Shot at the
// folder's edge through the Session that holds it (DEC-90, as the replay's resolver does) and hands back a read of that
// Shot's grid. With no resolver installed (a QML suite, a tool), or for a folder no loaded Session holds, the swing has
// no grid and its column shows nothing.
//
// Values only. The resolver runs on the GUI thread; the read it returns runs on any thread.

#include "../../Vocabulary/Analysis/phase_grid.h"   // SwingPhaseGrid, PhaseGridConfig

#include <QString>

#include <functional>
#include <optional>
#include <utility>

namespace pinpoint::diagmodel {

// The shot's grid under `cfg` (its sidecar policy is ParseAndWrite: the panel reads on a worker). Any thread. nullopt:
// the shot could not be read (its session closed since).
using ShotGridRead = std::function<std::optional<analysis::SwingPhaseGrid>(const analysis::PhaseGridConfig &cfg)>;

// (swingDir) → the read of the Shot a loaded Session holds at that folder; empty when none does. GUI thread.
using ShotGridResolver = std::function<ShotGridRead(const QString &swingDir)>;

inline ShotGridResolver &shotGridResolverSlot()
{
    static ShotGridResolver resolver;
    return resolver;
}
// The composition root's half: main.cpp installs the app's resolver, once, before the QML loads.
inline void installShotGridResolver(ShotGridResolver r) { shotGridResolverSlot() = std::move(r); }
inline const ShotGridResolver &shotGridResolver() { return shotGridResolverSlot(); }

} // namespace pinpoint::diagmodel
