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

// RigRepository: THE RIG'S REPOSITORY (information_model.md OBJ-RIG, CLS-RIG, FILE-APP-07; docs/design/rig_design.md
// §4.1, §4.4; P11: an object's persistence is its own concern). The Rig (rig.h) is the one writer of both its parts
// and of rig.json, and touches no file itself: it reaches the file through this interface, which the composition root
// gives it over the one store that implements it (src/Stores/rig_store.h). Domain-model ring, beside its owner. Only
// rig.h and the store include it (lint A5's repository table).
//
// The file holds the RigId, both parts (the rig state, never a sensor's alignment; and the Rig's other facts), and
// what the store itself keeps: which groups of settings keys have been taken, each with its fingerprint, and the
// entries a copy could not type, kept verbatim (rig_design.md §4.4; rig_design_addendum.md decision 10). The Rig never
// sees the last two: the store writes them back on every save the Rig makes.

#include "../Vocabulary/Identity/identity.h"
#include "../Vocabulary/Rig/rig_state.h"

namespace pinpoint {

// What the store hands the Rig at launch.
struct RigLoad {
    enum class Source {
        Nothing,    // no rig.json: a machine with nothing stored, or the file was deleted
        File,       // rig.json read, schema pinpoint.rig/1
        ReadOnly,   // a rig.json this build must not write: a newer pinpoint.rig/N (the fields it knows are read),
                    // or one it cannot read at all and could not move aside (nothing is read; P3). The store never
                    // saves over it (ppWarn says so)
    };
    Source        source = Source::Nothing;
    RigId         id;                  // null when the file held none: the Rig mints one
    rig::RigParts parts;               // empty on a machine with nothing stored
    bool          saveNeeded = false;  // the store changed what it keeps at this load (a group copied from settings):
                                       // the Rig saves once, with no event
};

class RigRepository
{
public:
    virtual ~RigRepository() = default;

    // Read once, at launch.
    virtual RigLoad load() = 0;

    // The whole file, written atomically: the id, both parts (a sensor's alignment is never written) and what the
    // store keeps (the groups taken, the settings entries kept, and the file's entries this build cannot read, P2).
    // False when nothing was written (a read-only file, a failed write): the store says why with ppWarn, and the
    // caller keeps its state in memory and tries again (P4).
    virtual bool save(const RigId &id, const rig::RigParts &parts) = 0;
};

} // namespace pinpoint
