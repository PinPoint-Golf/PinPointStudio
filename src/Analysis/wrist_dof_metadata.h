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

#include <QString>

// Per-DOF DISPLAY metadata for the wrist diagnostics view (design §8.2). This is config — the view
// reads these strings, it never hardcodes them. In particular the y-axis CONSEQUENCE POLES (the
// short term at the top/bottom of each strip naming where that direction tends, so the band reads as
// the good middle) are a mockup-added requirement carried here so they also reach the strip's
// accessible description. Only the four instrumented strips carry poles; grid-only DOFs (e.g. lead
// elbow flexion) set hasStrip=false.

namespace pinpoint::analysis {

struct DofMetadata {
    QString name;        // group label, e.g. "Lead wrist"
    QString sub;         // full sub-label, e.g. "radial–ulnar · lag"
    QString axisName;    // the grid row qualifier, e.g. "radial–ulnar"
    QString source;      // provenance label, e.g. "IMU", "IMU + pose", "pose (no trail IMU)"
    QString poleTop;     // ↑ consequence term (e.g. "stuck")
    QString poleBottom;  // ↓ consequence term (e.g. "cast")
    bool    hasStrip = false;   // drawn as a trajectory strip (else grid-only)
};

// The metadata table (design §8.2 + the mockup's DOFS[]). Unspecified DOFs return a minimal default
// so the engine stays total over the whole PpJointDof enum.
inline DofMetadata dofMetadata(PpJointDof dof)
{
    switch (dof) {
    case PpJointDof::LeadWristRadUln:
        return { QStringLiteral("Lead wrist"), QStringLiteral("radial–ulnar · lag"),
                 QStringLiteral("radial–ulnar"), QStringLiteral("IMU"),
                 QStringLiteral("stuck"), QStringLiteral("cast"), true };
    case PpJointDof::LeadWristFlexExt:
        return { QStringLiteral("Lead wrist"), QStringLiteral("flexion–extension · face"),
                 QStringLiteral("flexion–extension"), QStringLiteral("IMU"),
                 QStringLiteral("closed"), QStringLiteral("open"), true };
    case PpJointDof::LeadForearmRot:
        return { QStringLiteral("Lead forearm"), QStringLiteral("pronation–supination · roll"),
                 QStringLiteral("pronation–supination"), QStringLiteral("IMU + pose"),
                 QStringLiteral("over-roll"), QStringLiteral("block"), true };
    case PpJointDof::TrailWristFlexExt:
        return { QStringLiteral("Trail wrist"), QStringLiteral("extension · tray"),
                 QStringLiteral("extension"), QStringLiteral("pose (no trail IMU)"),
                 QStringLiteral("held"), QStringLiteral("scoop"), true };
    case PpJointDof::LeadElbowFlex:
        return { QStringLiteral("Lead arm"), QStringLiteral("elbow flexion · structure"),
                 QStringLiteral("elbow flexion"), QStringLiteral("pose"),
                 QString(), QString(), false };
    default:
        return { QString::fromLatin1(dofName(dof)), QString(), QString(),
                 QString(), QString(), QString(), false };
    }
}

} // namespace pinpoint::analysis
