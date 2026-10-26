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
        // The golfer's names for the trunk mounts (session_wizard_refactor_design.md §4.1, trunk
        // design §16.2 W2.2): where the sensor is worn, not the segment's anatomy.
        pelvis:       qsTr("Hip sensor (belt, lower back)"),
        thorax:       qsTr("Chest sensor (vest, between the shoulder blades)")
    })

    // A wG3 is one assignment that fills two roles, fixed by its cable (lower arm → leadForearm,
    // palm → leadHand; imu_role_map.cpp claimRole). Its one non-empty choice is "leadForearm".
    readonly property string hackMotionLabel: qsTr("Lead forearm + hand")
    readonly property string unassignedLabel: qsTr("— Unassigned —")

    // ── Which mounts are OFFERED ─────────────────────────────────────────────────────────────
    //
    // ⚠ THE ONE SWITCH. A mount is offered only if its instrument group has a calibrate step
    // (design §4.12). The arm has one; the TRUNK has one since W2.2 (trunk_imu_sequence_grf_design.md
    // §16.2: SetupSteps' calibrateTrunk / checkTrunk, the ceremony of §6.3), registered in the same
    // change that turned this on. Session setup's Sensors page takes the offered set from here; it
    // is the one place a mount is set (Mark, 26 Oct: Settings ▸ IMUs shows the mount, read-only).
    //
    // A sensor holding a mount that is NOT offered is still shown with it by every reader below;
    // not offered means not ASKED FOR, never hidden. Still a plain property: tests assign it.
    property bool trunkMountsOffered: true

    readonly property var offeredRoles: trunkMountsOffered ? allRoles : armRoles

    function isOffered(role) { return offeredRoles.indexOf(role) >= 0 }

    // The SHORT names, for one-line lists (Settings ▸ IMUs summary): the trunk mounts without their
    // placement hints, which belong where the mount is chosen (the Sensors page), not in a summary.
    readonly property var _shortLabels: ({
        leadForearm:  qsTr("Lead forearm"),
        leadHand:     qsTr("Lead hand"),
        leadUpperArm: qsTr("Lead upper arm"),
        pelvis:       qsTr("Hip sensor"),
        thorax:       qsTr("Chest sensor")
    })

    // "" for "" (unassigned) and for a name this build does not know.
    function label(role) { return role && _labels[role] !== undefined ? _labels[role] : "" }
    function shortLabel(role) { return role && _shortLabels[role] !== undefined ? _shortLabels[role] : "" }

    // The mount a DEVICE shows, from the role it was assigned (imuManager.roleForDevice): a wG3
    // answers "leadForearm" for its pair, which is shown as the pair.
    function deviceMountLabel(isHackMotion, role) {
        if (!role) return ""
        if (isHackMotion && role === "leadForearm") return hackMotionLabel
        return label(role)
    }
}
