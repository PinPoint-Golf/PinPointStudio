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

// THE step registry (session_wizard_refactor_design.md §4.4, R1). A step exists here, as one
// StepDescriptor, and in one page file under pages/ — nowhere else. Order here is the order of the
// flow; nothing reorders it.
//
// ⚠ R6 / §4.12 / lint W8: no `applies` and no `gate` below may mention the session type or the
// preset. Steps come from the hardware in the session: with no arm sensor there is no arm
// Calibrate or Check step, whatever the session was opened as.
//
// The issue and summary texts are today's (wizard l.489–565, 1947–2015) wherever today had one;
// the sensor issues are new because the per-type slot table they came from is gone (§4.12). Each
// new string is marked DECISION(stage4), and Stage 6 re-baselines Ready's golden tables on purpose.
//
// Adding a step (§4.10): one descriptor here, one page file. The trunk work adds the two lines
// marked TRUNK below. A step can also be added from outside through `extensions` (test N17).
import QtQuick
import PinPointStudio

QtObject {
    id: registry

    // key → page URL, replacing the descriptor's own page (tests point keys at test pages).
    property var pageOverride: ({})

    // The progress indicator's group labels (StepDescriptor.group → label). Here, beside the
    // descriptors that name the groups, so the shell and the indicator name none (lint W10).
    // DECISION(stage5a): title case; the indicator capitalises them over the runs and uses them
    // as written in a collapsed group's pip ("Cameras 3/3", design §4.9).
    readonly property var groupLabels: ({ session: qsTr("Session"), cameras: qsTr("Cameras"),
                                          sensors: qsTr("Sensors"), ready: qsTr("Ready") })
    // Descriptors added from outside, each inserted after the key named by its `after` ("" = end).
    property list<StepDescriptor> extensions

    readonly property list<StepDescriptor> builtIn: [
        StepDescriptor {
            key: "goals"; title: qsTr("Goals"); eyebrow: qsTr("GOALS"); group: "session"
            page: "pages/GoalsPage.qml"
            // applies, gate: always.
            summary: function(ctx, draft) {
                var n = draft.goals.length
                return { label: qsTr("Goals"), good: true,
                         value: n > 0 ? qsTr("%1 selected · saved to profile").arg(n)
                                      : qsTr("None — defaulting to %1").arg(draft.defaultGoal ? draft.defaultGoal.name : "") }
            }
        },
        StepDescriptor {
            key: "cameras"; title: qsTr("Cameras"); eyebrow: qsTr("CAMERAS"); group: "cameras"
            page: "pages/CamerasPage.qml"
            // applies: always. gate: none — Connect is the page's primary until nothing is left
            // to connect; the header › never runs it (F5).
            issues: function(ctx, draft) {
                if (draft.state("cameras") === "skipped")
                    return [{ text: qsTr("Cameras skipped — no video will be captured this session"), panel: -1 }]
                var out = []
                if (ctx.faceOn.length === 0)
                    out.push({ text: qsTr("Face-on camera not assigned"), panel: ctx.settingsPanelCameras })
                var cams = ctx.sessionCameras
                for (var i = 0; i < cams.length; ++i) {
                    if (!cams[i].sessionEnabled) continue
                    if (!cams[i].selected)
                        out.push({ text: qsTr("%1 not connected — press Connect in the cameras step")
                                             .arg(cams[i].alias || cams[i].description), panel: -1 })
                }
                if (cams.length > 0 && !ctx.anyCameraEnabled)
                    out.push({ text: qsTr("All cameras disabled for this session"), panel: -1 })
                return out
            }
            summary: function(ctx, draft) {
                var skipped = draft.state("cameras") === "skipped"
                var value
                if (skipped)
                    value = qsTr("Skipped — no video capture")
                else if (ctx.camsAllConnected)
                    value = ctx.connectedCameraCount === 1 ? qsTr("1 camera connected")
                                                           : qsTr("%1 cameras connected").arg(ctx.connectedCameraCount)
                else if (ctx.faceOn.length === 0)
                    value = qsTr("Face-on camera not assigned")
                else
                    value = qsTr("Not connected — press Connect in the cameras step")
                return { label: qsTr("Cameras"), value: value, good: !skipped && ctx.camsAllConnected }
            }
        },
        StepDescriptor {
            // Straight after Cameras, before anything that depends on where a camera points:
            // moving one to fix its framing would undo a triangulation or a learnt hitting area.
            key: "framing"; title: qsTr("Framing"); eyebrow: qsTr("FRAMING"); group: "cameras"
            page: "pages/FramingPage.qml"
            // applies: a connected camera that sees the golfer (not the impact camera). gate: none
            // — the verdict is advice; a camera that cannot be moved still records.
            applies: function(ctx, draft) { return ctx.framingCameras.length > 0 }
        },
        StepDescriptor {
            key: "triangulate"; title: qsTr("Triangulate"); eyebrow: qsTr("TRIANGULATION"); group: "cameras"
            page: "pages/TriangulatePage.qml"
            applies: function(ctx, draft) { return ctx.hasFaceOnAndDtlSelected }
            // The stub checks (D7, kept as today; wizard l.520–525): only when the cameras step
            // was not skipped and neither camera of the pair is fixed in place.
            issues: function(ctx, draft) {
                if (draft.state("cameras") === "skipped" || ctx.anyFixedCamera) return []
                var out = []
                if (!ctx.stereoCalibrationValid)
                    out.push({ text: qsTr("Stereo calibration not confirmed — use Recalibrate in the triangulation step"), panel: -1 })
                if (!ctx.triangulationValid)
                    out.push({ text: qsTr("Triangulation not confirmed"), panel: -1 })
                return out
            }
            summary: function(ctx, draft) {
                return { label: qsTr("Triangulation"),
                         good: ctx.triangulationValid || ctx.anyFixedCamera,
                         value: ctx.triangulationValid ? qsTr("Baseline confirmed")
                              : ctx.anyFixedCamera     ? qsTr("Optional — cameras fixed in place")
                                                       : qsTr("Not confirmed") }
            }
        },
        StepDescriptor {
            key: "ball"; title: qsTr("Ball"); eyebrow: qsTr("BALL DETECTION"); group: "cameras"
            page: "pages/BallPage.qml"
            // Continue only on a LIVE detected ball (wizard l.157); Skip is the way past.
            gate: function(ctx, draft) { return ctx.ballPresent }
            summary: function(ctx, draft) {
                return { label: qsTr("Ball detection"), good: ctx.ballPresent,
                         value: ctx.ballPresent ? qsTr("Ball detected")
                              : draft.state("ball") === "skipped" ? qsTr("Skipped")
                                                                 : qsTr("No ball detected") }
            }
        },
        StepDescriptor {
            // DECISION(stage5b): "Sensors" in the indicator and on the closing page — the page's
            // name in the brief, and the golfer's word; the eyebrow is today's.
            key: "imus"; title: qsTr("Sensors"); eyebrow: qsTr("MOTION SENSORS"); group: "sensors"
            page: "pages/ImusPage.qml"
            // applies: always — the Sensors page is where a found sensor gets its mount (§4.12).
            issues: function(ctx, draft) {
                if (draft.state("imus") === "skipped")
                    return [{ text: qsTr("Motion sensors skipped — no movement data will be captured"), panel: -1 }]
                var out = [], r = ctx.roles, g = ctx.groups
                // DECISION(stage4): the per-type slot rows (wizard l.540–559) become role rows.
                // DECISION(stage5b): a mount whose holder the scan cannot see — one issue per absent
                // SENSOR (a wG3's pair is one), worded as the Sensors page's row: today's text with
                // the mount and the sensor named instead of the slot.
                var ab = ctx.absentMounts
                for (var a = 0; a < ab.length; ++a)
                    out.push({ text: qsTr("%1 — %2 not found. Power it on and Scan.")
                                         .arg(ab[a].mountLabel).arg(ab[a].name), panel: ctx.settingsPanelImus })
                // A group in the session missing part of its minimum set. DECISION(stage5b): a role
                // held by an absent sensor is reported once, by the not-found issue above — not
                // again as "not assigned" (Stage 0's R1 C08: Ready contradicted itself).
                for (var grp in g)
                    if (g[grp].inSession)
                        for (var m = 0; m < g[grp].missingRoles.length; ++m)
                            if (!r[g[grp].missingRoles[m]].assigned)
                                out.push({ text: qsTr("%1 not assigned").arg(r[g[grp].missingRoles[m]].label),
                                           panel: ctx.settingsPanelImus })
                // DECISION(stage4): a sensor in the session that is not connected (today's Ready
                // never checked — Stage 0's R1 C07, a wG3 dropped on Check still read "assigned").
                var tc = ctx.devicesToConnect
                for (var i = 0; i < tc.length; ++i)
                    out.push({ text: qsTr("%1 not connected — press Connect in the sensors step").arg(tc[i].name),
                               panel: -1 })
                // DECISION(stage4): found is not wanted — a sensor with no mount sits out (§4.12).
                var un = ctx.unassignedDevices
                for (var j = 0; j < un.length; ++j)
                    out.push({ text: qsTr("%1 has no mount — it sits out this session").arg(un[j].name),
                               panel: ctx.settingsPanelImus })
                return out
            }
            summary: function(ctx, draft) {
                var skipped = draft.state("imus") === "skipped"
                // DECISION(stage4): "assigned" now counts the roles held in the session, and needs
                // every group in the session complete (today: the required slots of the type).
                var g = ctx.groups, held = 0, ok = true
                for (var grp in g) {
                    held += g[grp].roles.length
                    if (g[grp].inSession && !g[grp].complete) ok = false
                }
                // DECISION(stage5b): a mount whose sensor is not found reads as that, not as "not
                // assigned" under an issue that says it is assigned (Stage 0's R1 C08).
                if (skipped)
                    return { label: qsTr("Sensors"), good: false, value: qsTr("Skipped — no motion data") }
                if (ctx.absentMounts.length > 0)
                    return { label: qsTr("Sensors"), good: false, value: qsTr("Some sensors not found") }
                // DECISION(stage5c): with no sensor in the session there is nothing to set up —
                // neither in place nor missing (sensors are optional), so the row is neutral.
                if (held === 0)
                    return { label: qsTr("Sensors"), good: false, tone: "neutral",
                             value: qsTr("None in this session") }
                if (!ok)
                    return { label: qsTr("Sensors"), good: false, value: qsTr("Some sensors not assigned") }
                // Stage 5c: never green while a sensor of the session is not connected or a group
                // of it has no valid calibration outcome (Stage 0's R1 C07 read green over both).
                var notConnected = ctx.devicesToConnect.length > 0
                var notCalibrated = false
                for (var gg in g)
                    if (g[gg].inSession && !draft.outcome(gg).done) notCalibrated = true
                return { label: qsTr("Sensors"), good: !notConnected && !notCalibrated,
                         value: qsTr("%1 sensors assigned").arg(held)
                                + (notConnected  ? qsTr(" · not connected")  : "")
                                + (notCalibrated ? qsTr(" · not calibrated") : "") }
            }
        },
        StepDescriptor {
            key: "calibrateArm"; title: qsTr("Calibrate"); eyebrow: qsTr("CALIBRATE"); group: "sensors"
            instrumentGroup: "arm"
            page: "pages/CalibrateArmPage.qml"
            // §4.12, D6 superseded: the arm steps exist when the arm GROUP is in the session —
            // a present, session-enabled device holds an arm role. Hardware only.
            applies: function(ctx, draft) { return ctx.groups.arm.inSession }
            gate:    function(ctx, draft) { return draft.outcome("arm").done }
            // Today's issue (wizard l.560–561), suppressed as today when the sensors step was
            // skipped.
            issues: function(ctx, draft) {
                if (draft.state("imus") === "skipped" || draft.outcome("arm").done) return []
                return [{ text: qsTr("Sensor position calibration not completed — return to the Calibrate step"), panel: -1 }]
            }
        },
        StepDescriptor {
            key: "checkArm"; title: qsTr("Confirm"); eyebrow: qsTr("CONFIRM TRACKING"); group: "sensors"
            instrumentGroup: "arm"
            page: "pages/CheckArmPage.qml"
            applies: function(ctx, draft) { return ctx.groups.arm.inSession }
        },
        // ── TRUNK — the two lines the trunk work adds (trunk_imu_design.md §6.7; design §4.10).
        // Registering them is ALSO what offers the trunk mounts (§4.12): flip
        // ImuMounts.trunkMountsOffered in the same change until the two are wired together.
        //
        // StepDescriptor { key: "calibrateTrunk"; title: qsTr("Calibrate trunk"); eyebrow: qsTr("CALIBRATE TRUNK"); group: "sensors"; instrumentGroup: "trunk"; page: "pages/CalibrateTrunkPage.qml"; applies: function(ctx, draft) { return ctx.groups.trunk.inSession }; gate: function(ctx, draft) { return draft.outcome("trunk").done } },
        // StepDescriptor { key: "checkTrunk"; title: qsTr("Check trunk"); eyebrow: qsTr("CHECK TRUNK"); group: "sensors"; instrumentGroup: "trunk"; page: "pages/CheckTrunkPage.qml"; applies: function(ctx, draft) { return ctx.groups.trunk.inSession } },
        StepDescriptor {
            key: "ready"; title: qsTr("Ready"); eyebrow: qsTr("READY"); group: "ready"
            page: "pages/ReadyPage.qml"
            // applies: always. Start is flow.exit("start"), not a next(): nothing gates it.
        }
    ]

    // The registry in flow order: built-in steps, with each extension inserted after its `after`.
    readonly property var descriptors: {
        var out = []
        for (var i = 0; i < builtIn.length; ++i) out.push(builtIn[i])
        for (var j = 0; j < extensions.length; ++j) {
            var e = extensions[j], at = out.length
            if (e.after !== "")
                for (var k = 0; k < out.length; ++k)
                    if (out[k].key === e.after) { at = k + 1; break }
            out.splice(at, 0, e)
        }
        return out
    }
    readonly property var keys: {
        var out = [], d = descriptors
        for (var i = 0; i < d.length; ++i) out.push(d[i].key)
        return out
    }
    // The instrument groups that have a calibrate step registered (§4.12: only their mounts are
    // offered by the pickers).
    readonly property var instrumentGroups: {
        var out = [], d = descriptors
        for (var i = 0; i < d.length; ++i)
            if (d[i].instrumentGroup !== "" && out.indexOf(d[i].instrumentGroup) < 0) out.push(d[i].instrumentGroup)
        return out
    }

    function descriptor(key) {
        var d = descriptors
        for (var i = 0; i < d.length; ++i) if (d[i].key === key) return d[i]
        return null
    }
    // The page to load for `key`: the override, else the descriptor's page resolved against THIS
    // file's directory (src/Gui/setup/). An absolute URL resolves to itself.
    function pageUrl(key) {
        if (pageOverride[key] !== undefined) return pageOverride[key]
        var d = descriptor(key)
        return d ? Qt.resolvedUrl(d.page) : ""
    }
}
