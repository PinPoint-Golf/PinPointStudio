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

#include "wrist_assessment_types.h"

#include <memory>

// Reference bands — the "where good comes from" provider (design §6). A band is the expected
// Δ-from-address corridor for a (DOF, position) cell: a GREEN core with an AMBER margin either side;
// outside amber is RED. Supplied behind an abstract provider so the source is swappable (config /
// player-baseline / archetype — design §6); v1 ships ConfigReferenceBandProvider only.

namespace pinpoint::analysis {

// Context the bands are keyed by (design §6 "context parameterisation"). v1 hard-defaults to
// neutral archetype / mid-iron / neutral shape and exposes the seam; the provider may ignore it.
struct BandContext {
    int archetype = 0;   // 0 = neutral (future: bowed-tour / cupped-tour …)
    int club      = 0;   // 0 = mid-iron
    int shape     = 0;   // 0 = neutral (future: draw / fade bias)
};

// A resolved band for one cell. `valid == false` means "no band for this cell" (not in the table,
// or a position with no defined corridor, e.g. trail-wrist at P8) → the engine greys the cell.
struct Band {
    double greenLo = 0.0, greenHi = 0.0;
    double amberLo = 0.0, amberHi = 0.0;
    bool   valid   = false;
};

// Tier-1 band classification of a Δ value (design §7.1). Robust to a malformed band: an invalid or
// inverted green range yields Grey (no assessment) rather than a false Green; outside every valid
// range is Red. Reference handling (P1) and unavailable cells (Grey) are decided by the engine,
// which only calls this for assessable, non-reference cells.
inline PpRag classifyDelta(double delta, const Band &b)
{
    if (!b.valid || b.greenLo > b.greenHi)
        return PpRag::Grey;
    if (delta >= b.greenLo && delta <= b.greenHi)
        return PpRag::Green;
    if (b.amberLo <= b.amberHi && delta >= b.amberLo && delta <= b.amberHi)
        return PpRag::Amber;
    return PpRag::Red;
}

// The abstract provider seam.
class IReferenceBandProvider {
public:
    virtual ~IReferenceBandProvider() = default;
    virtual Band band(PpJointDof dof, PpSwingPosition pos, const BandContext &ctx = {}) const = 0;
};

// Declarative bands from a versioned, compiled-in table (design §5 seeds; §11: every number is a
// starting heuristic and is expected to move). Green corridors are stored per (DOF, position); the
// amber margin is per-DOF. Ships first; the IReferenceBandProvider seam allows JSON / player-baseline
// / archetype providers later without touching the engine.
class ConfigReferenceBandProvider : public IReferenceBandProvider {
public:
    static constexpr int kVersion = 1;
    Band band(PpJointDof dof, PpSwingPosition pos, const BandContext &ctx = {}) const override;
};

// Archetype-aware bands (design §6): the neutral config corridors, shifted on the **face DOF** (the
// style-dependent lead-wrist flex-ext axis) per `ctx.archetype` — so a valid bowed / cupped style is
// scored against its own model rather than red-flagged. Archetype 0 (neutral) ≡ the config bands.
// Other DOFs are archetype-invariant in v1.
class ArchetypeBandProvider : public IReferenceBandProvider {
public:
    Band band(PpJointDof dof, PpSwingPosition pos, const BandContext &ctx = {}) const override;
private:
    ConfigReferenceBandProvider m_config;
};

enum class BandProviderKind { Config, Archetype };

std::unique_ptr<IReferenceBandProvider> makeReferenceBandProvider(BandProviderKind kind = BandProviderKind::Config);

} // namespace pinpoint::analysis
