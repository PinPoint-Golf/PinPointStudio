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

// The sensor MOUNTS, by name — the one place a placement role becomes words on screen, and
// the one place that says which mounts the pickers offer (session_wizard_refactor_design.md
// §4.12, §4.13). Every page that shows or asks for a mount reads this: Settings ▸ IMUs, the
// toolbar sensor panel, the arm view's legend, and the setup pages after them.
//
// Roles are the SETTINGS spelling (AppSettings::imuRoles, segment_role.h placementRoleName):
// "leadForearm", "leadHand", "leadUpperArm", "pelvis", "thorax". No slot letters: the letters
// survive only as the legacy imuPlacement view, which this file never reads.

pragma Singleton
import QtQuick

QtObject {
    id: mounts

    // Picker order: the arm from the wrist up, then the trunk.
    readonly property var armRoles:   ["leadForearm", "leadHand", "leadUpperArm"]
    readonly property var trunkRoles: ["pelvis", "thorax"]
    readonly property var allRoles:   armRoles.concat(trunkRoles)

    readonly property var _labels: ({
        leadForearm:  qsTr("Lead forearm"),
        leadHand:     qsTr("Lead hand"),
        leadUpperArm: qsTr("Lead upper arm"),
        pelvis:       qsTr("Pelvis (belt, sacrum)"),
        thorax:       qsTr("Thorax (vest, upper back)")
    })

    // A wG3 is one assignment that fills two roles, fixed by its cable (lower arm → leadForearm,
    // palm → leadHand; imu_role_map.cpp claimRole). Its one non-empty choice is "leadForearm".
    readonly property string hackMotionLabel: qsTr("Lead forearm + hand")
    readonly property string unassignedLabel: qsTr("— Unassigned —")

    // ── Which mounts are OFFERED ─────────────────────────────────────────────────────────────
    //
    // ⚠ THE ONE SWITCH. A mount is offered only if its instrument group has a calibrate step
    // (design §4.12). The arm has one. The TRUNK DOES NOT YET: there is no pelvis/thorax
    // ceremony (trunk_imu_design.md §6.7–6.8), and the trunk analysis path has never run
    // (§0) — offering these mounts now would feed it sensors nobody calibrated, and its first
    // run would be on garbage. The day the trunk calibrate step is registered, flip this to
    // true and nothing else: the Settings picker, its "Missing" summary and every later page
    // take the offered set from here. (Stage 4's step registry replaces this flag with
    // "groups that have a calibrate step".)
    //
    // A sensor already HOLDING a trunk role (a hand-edited settings file) is still shown with
    // its mount by every reader below; not offered means not ASKED FOR, never hidden.
    property bool trunkMountsOffered: false

    readonly property var offeredRoles: trunkMountsOffered ? allRoles : armRoles

    function isOffered(role) { return offeredRoles.indexOf(role) >= 0 }

    // "" for "" (unassigned) and for a name this build does not know.
    function label(role) { return role && _labels[role] !== undefined ? _labels[role] : "" }

    // The mount a DEVICE shows, from the role it was assigned (imuManager.roleForDevice): a wG3
    // answers "leadForearm" for its pair, which is shown as the pair.
    function deviceMountLabel(isHackMotion, role) {
        if (!role) return ""
        if (isHackMotion && role === "leadForearm") return hackMotionLabel
        return label(role)
    }
}
