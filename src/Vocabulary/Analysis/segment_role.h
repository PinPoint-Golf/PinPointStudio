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

#include <QString>

// The anatomical segment an IMU is mounted on, and the two string spellings of it.
//
// Split out of swing_analysis.h (which includes it, so every existing user still sees
// the enum there) because the SETTINGS layer needs the role names too — AppSettings
// persists `imu/roles` and derives the legacy `imuPlacement` view from it — and
// swing_analysis.h drags the whole analysis model behind it. This header is QtCore only.
namespace pinpoint::analysis {

// APPEND-ONLY: the role persists as a raw int in swing.json (stream device.role and
// analysis.bindings[].role), and old swings are read back through it. Existing values
// must keep their positions; a new segment goes on the end.
// Unknown = no role (an unassigned sensor, or a slot nothing maps).
enum class SegmentRole {
    Unknown = 0,
    Pelvis, Thorax, T12,
    LeadUpperArm, LeadForearm, LeadHand,
    TrailThigh, LeadThigh,
    Club,
};

// Stable string name for a role — written to swing.json (stream device.roleName,
// analysis.bindings[].roleName) and consumed by SwingLab / future post-hoc tools.
// Stable across enum renumbering; pair it with the int role value.
//
// ⚠ NOT THE SETTINGS SPELLING BELOW. "LeadHand" here is persisted in every swing file
// ever written; "leadHand" below is persisted in the settings file. Two vocabularies,
// two persistence sites, and neither may be changed to match the other.
inline QString segmentRoleName(SegmentRole r)
{
    switch (r) {
    case SegmentRole::Pelvis:       return QStringLiteral("Pelvis");
    case SegmentRole::Thorax:       return QStringLiteral("Thorax");
    case SegmentRole::T12:          return QStringLiteral("T12");
    case SegmentRole::LeadUpperArm: return QStringLiteral("LeadUpperArm");
    case SegmentRole::LeadForearm:  return QStringLiteral("LeadForearm");
    case SegmentRole::LeadHand:     return QStringLiteral("LeadHand");
    case SegmentRole::TrailThigh:   return QStringLiteral("TrailThigh");
    case SegmentRole::LeadThigh:    return QStringLiteral("LeadThigh");
    case SegmentRole::Club:         return QStringLiteral("Club");
    case SegmentRole::Unknown:      break;
    }
    return QStringLiteral("Unknown");
}

// ── Placement role names — the `imu/roles` setting's values ───────────────────────
//
// The roles a sensor can be ASSIGNED to (session_wizard_refactor_design.md §4.13). A
// strict subset of SegmentRole: T12, the thighs and the club have no mount, no
// calibration and no picker entry, so they have no placement name and answer "".
//
// ⚠ PERSISTED, AND READ BY QML BY STRING. Renaming one orphans every sensor assigned to
// it with nothing to say so — the sensor simply reads as unassigned.
inline QString placementRoleName(SegmentRole r)
{
    switch (r) {
    case SegmentRole::Pelvis:       return QStringLiteral("pelvis");
    case SegmentRole::Thorax:       return QStringLiteral("thorax");
    case SegmentRole::LeadUpperArm: return QStringLiteral("leadUpperArm");
    case SegmentRole::LeadForearm:  return QStringLiteral("leadForearm");
    case SegmentRole::LeadHand:     return QStringLiteral("leadHand");
    default:                        break;
    }
    return {};
}

// The inverse. Anything that is not one of the five names — "", a slot letter, a
// misspelling — is Unknown, never a guess.
inline SegmentRole segmentRoleForPlacementRole(const QString &name)
{
    for (SegmentRole r : { SegmentRole::Pelvis, SegmentRole::Thorax, SegmentRole::LeadUpperArm,
                           SegmentRole::LeadForearm, SegmentRole::LeadHand })
        if (name == placementRoleName(r)) return r;
    return SegmentRole::Unknown;
}

}   // namespace pinpoint::analysis
