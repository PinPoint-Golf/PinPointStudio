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

// The context a wrist band is resolved in, and SwingLab's margin overrides on it: value types the
// metric definitions carry (ShotContext::band, metric_provider.h), so they sit in the vocabulary
// beside the wrist types. The bands themselves and their provider are rules (Wrist/reference_bands.h,
// which includes this). Moved out of reference_bands.h unchanged (refactor 7.3, B-08).

#include "wrist_assessment_types.h"   // PpJointDof

#include <QString>

namespace pinpoint::analysis {

// SwingLab `bands.*` overrides — per-DOF amber margin (degrees either side of green). A negative
// value means "use the compiled-in table default", so a default-constructed BandTuning is a no-op
// and the bands are byte-identical to the frozen corridors. Full corridor (lo/hi) tuning is a
// Corpus-2 re-seat (pipeline_validation_and_tuning.md §6.6), not a sweep target — only the margins
// are exposed here.
struct BandTuning {
    double radUlnMargin     = -1.0;
    double flexExtMargin    = -1.0;
    double forearmMargin    = -1.0;
    double trailWristMargin = -1.0;
    double elbowMargin      = -1.0;

    double marginFor(PpJointDof dof) const
    {
        switch (dof) {
        case PpJointDof::LeadWristRadUln:   return radUlnMargin;
        case PpJointDof::LeadWristFlexExt:  return flexExtMargin;
        case PpJointDof::LeadForearmRot:    return forearmMargin;
        case PpJointDof::TrailWristFlexExt: return trailWristMargin;
        case PpJointDof::LeadElbowFlex:     return elbowMargin;
        default:                            return -1.0;
        }
    }
};

// Context the bands are keyed by (design §6 "context parameterisation").
//
// `contextId` is the node in the diagnostics context tree this shot belongs to, and it is now the
// real key: archetype/club/shape all fold into it, and NormBandProvider resolves by walking the
// tree upward from it. The three legacy ints remain ONLY so a caller that has not been migrated
// still compiles and behaves; `resolvedContextId()` maps them across.
//
// Empty contextId + archetype 0 means "full swing, nothing more specific known", which is exactly
// what the default context is for.
struct BandContext {
    QString    contextId;       // diagnostics context tree node; empty => derive from `archetype`
    int        archetype = 0;   // LEGACY: 0 = neutral, 1 = bowed, 2 = cupped
    int        club      = 0;   // LEGACY: unused by the norm path
    int        shape     = 0;   // LEGACY: unused by the norm path
    BandTuning tuning;          // SwingLab bands.* margin overrides (default = no-op)

    // The context to resolve norms in. A caller that sets contextId wins outright; otherwise the
    // legacy archetype int is translated, so detectArchetype()'s existing 0/1/2 keeps working
    // without every call site changing at once.
    QString resolvedContextId() const
    {
        if (!contextId.isEmpty()) return contextId;
        switch (archetype) {
        case 1:  return QStringLiteral("archetype_bowed");
        case 2:  return QStringLiteral("archetype_cupped");
        default: return QStringLiteral("full_swing");
        }
    }
};

} // namespace pinpoint::analysis
