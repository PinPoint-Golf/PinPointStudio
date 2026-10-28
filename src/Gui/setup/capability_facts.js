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
.pragma library

// WHAT THE KIT CAN RECORD, as facts: the map MetricCatalog.setupSummary() takes
// ({ faceOn, dtl, imuRoles, hackMotion, launchMonitor }) and the context setup_capability_rows.js
// words its reasons from ({ armInSession, armCalibrated, upperArmHeld, cameraCount, faceOn, dtl,
// lmName }). Pure: it reads only what it is given, so the wizard and Home answer from one rule.
//
// Two modes:
//   "wizard"  What THIS session will record (session wizard §4.14) — SetupContext's rule, unchanged:
//             a sensor group counts once its calibration OUTCOME in the visit's draft holds (an
//             assigned-but-uncalibrated sensor produces nothing); a camera counts when it is
//             session-enabled and connected, and not when the Cameras step was skipped.
//   "home"    What this rig can measure NOW (ux_refactor_away_from_4_modes D10): the kit connected
//             now, with no calibration gate. A camera counts when it is enabled (not excluded in
//             Settings — the global enablement every session is seeded from) and connected; a
//             sensor role when it is assigned, its sensor enabled (not excluded) and connected; the
//             HackMotion when it holds the arm and is connected; the launch monitor when configured.
//
// Input (SetupContext's own derivations, which read cameraManager / imuManager / appSettings /
// launchMonitor with their reactive dependencies written once):
//   faceOn, dtl   cameraList entries per perspective ({ cameraKey, sessionEnabled, selected, … })
//   roles         role → { assigned, deviceId, device, inSession, connected, … } (SetupContext.roles)
//   groups        group → { inSession, routine, … } (SetupContext.groups)
//   groupDefs     group → { roles: [...] } (SetupContext.groupDefs)
//   launchMonitor true when a launch monitor is configured
//   lmName        its short name, "" for none
//   draft         wizard: the visit's draft (outcome(group), state(step)), or null
//   cameraExcluded, imuExcluded   home: the global exclusion lists (appSettings), by key / id

function _anyConnected(cams, enabled) {
    for (var i = 0; i < (cams || []).length; ++i)
        if (enabled(cams[i]) && cams[i].selected) return true
    return false
}

function _excluded(list, key) {
    return (list || []).indexOf(key) >= 0
}

// home: a role counts when it is assigned and its sensor is enabled and connected now.
function _roleCounts(input, role) {
    var e = input.roles[role]
    return e !== undefined && e.assigned === true && e.connected === true
           && !_excluded(input.imuExcluded, e.deviceId)
}

// The setupSummary() facts.
function facts(input, mode) {
    var r = input.roles, g = input.groups, defs = input.groupDefs
    var out = []
    var hm = false
    if (mode === "home") {
        for (var hg in defs) {
            var hr = defs[hg].roles
            for (var j = 0; j < hr.length; ++j) if (_roleCounts(input, hr[j])) out.push(hr[j])
        }
        // The wG3 holds the arm (it takes only leadForearm, and leadHand by the cable).
        var lf = r.leadForearm
        hm = lf !== undefined && lf.device !== null && lf.device !== undefined
             && lf.device.isHackMotion === true && _roleCounts(input, "leadForearm")
        var camOn = function(c) { return !_excluded(input.cameraExcluded, c.cameraKey) }
        return { faceOn: _anyConnected(input.faceOn, camOn), dtl: _anyConnected(input.dtl, camOn),
                 imuRoles: out, hackMotion: hm, launchMonitor: input.launchMonitor === true }
    }
    // wizard
    var draft = input.draft || null
    for (var grp in defs) {
        var oc = draft ? draft.outcome(grp) : null
        if (!oc || !oc.done) continue
        var rl = defs[grp].roles
        for (var i = 0; i < rl.length; ++i) if (r[rl[i]].inSession) out.push(rl[i])
        if (grp === "arm" && g.arm.routine === "hackmotion") hm = true
    }
    // DECISION(stage5c): a skipped Cameras step records no video (its issue says so), so
    // its cameras do not count, connected or not.
    var camsSkipped = draft !== null && draft.state("cameras") === "skipped"
    var sessionOn = function(c) { return c.sessionEnabled }
    return { faceOn: _anyConnected(input.faceOn, sessionOn) && !camsSkipped,
             dtl: _anyConnected(input.dtl, sessionOn) && !camsSkipped,
             imuRoles: out, hackMotion: hm, launchMonitor: input.launchMonitor === true }
}

// The same setup without the launch monitor — only when one is configured — so the rows can tell
// a family the launch monitor alone measures (CapRows "Whose word it is"). null otherwise.
function factsWithoutLm(f) {
    if (!f.launchMonitor) return null
    return { faceOn: f.faceOn, dtl: f.dtl, imuRoles: f.imuRoles, hackMotion: f.hackMotion,
             launchMonitor: false }
}

// The context CapRows.rows() words its reasons from, for facts `f` made by facts(input, mode).
function rowContext(input, f, mode) {
    // A camera's state for the reasons (Stage 5c): "connected" when it counts, "unconnected" when
    // one is assigned but does not count, "absent" when none is.
    var cam = function(counted, assigned) {
        return counted ? "connected" : ((assigned || []).length > 0 ? "unconnected" : "absent")
    }
    var armInSession, armCalibrated, upperArmHeld
    if (mode === "home") {
        // No calibration gate: an arm sensor connected now counts as able to measure. One assigned
        // but not connected does not count at all — the row says "no wrist sensor", never "not
        // calibrated", which is a fact about a session.
        var arm = input.groupDefs.arm.roles
        armInSession = false
        for (var i = 0; i < arm.length; ++i) if (_roleCounts(input, arm[i])) armInSession = true
        armCalibrated = armInSession
        upperArmHeld  = _roleCounts(input, "leadUpperArm")
    } else {
        var oc = input.draft ? input.draft.outcome("arm") : null
        armInSession  = input.groups.arm.inSession
        armCalibrated = oc !== null && oc !== undefined && oc.done
        upperArmHeld  = input.roles.leadUpperArm.inSession
    }
    return {
        armInSession:  armInSession,
        armCalibrated: armCalibrated,
        upperArmHeld:  upperArmHeld,
        cameraCount:   (f.faceOn ? 1 : 0) + (f.dtl ? 1 : 0),
        faceOn:        cam(f.faceOn, input.faceOn),
        dtl:           cam(f.dtl, input.dtl),
        lmName:        input.lmName || ""
    }
}
