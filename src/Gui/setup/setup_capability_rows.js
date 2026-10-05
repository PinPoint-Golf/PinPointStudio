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

// "What this session will record" — the closing page's six rows, worded (design §4.14). THE ONE
// PLACE this wording lives: the family table, the state rule and the reason vocabulary are below
// and nowhere else, so a change of word is a change to this file and its unit cases
// (tst_setup_capability.qml).
//
// Input: MetricCatalog.setupSummary(setupFacts) — one entry per catalogue group with its
// measured / estimated / unavailable / planned / derived counts and `deviceGapIds` (the kit this
// setup lacks for the group's estimated and unavailable metrics; captureDeviceId() spellings,
// Metrics/metric_descriptor.h). Nothing here decides availability: the catalogue's resolver did.
//
// ⚠ DESCRIBE, NEVER INSTRUCT. Cameras are fixed and sensors are optional (memory note
// feedback_camera_fallback_best_effort_honest): a reason says what is missing ("no down-the-line
// camera"), never what to do about it. And nothing a golfer reads here may carry an engineer's
// word — no role names ("LeadForearm"), no "IMU", no slot letters, no route summaries.

// ── The families ─────────────────────────────────────────────────────────────────────────────────
// Row label ← catalogue groups (MetricDescriptor::group, src/Metrics/metric_catalogue_manifest.cpp).
// Every catalogue group must land in exactly one family; the unit case asserts it against the live
// catalogue, so a new group cannot vanish from the page unnoticed.
// `derivedCounts`: the group's `derived` metrics need no capture of their OWN (tempo off the phase
// ladder) — but the phase ladder needs something to see the swing. They count as MEASURED when the
// session has a connected face-on camera or an arm sensor group, and otherwise as UNAVAILABLE, the
// missing face-on camera their reason — given first (Stage 5c).
// DECISION(stage5c): this rule lives here, not in setup_capability.cpp: the catalogue's `derived`
// bucket says truthfully what the metric needs of the capture (nothing of its own); what counts as
// "something sees the swing" is a statement about this setup's wording, and this file is the one
// place that wording lives. The C++ summary and its test are unchanged.
var families = [
    { key: "wrist",    label: qsTr("Wrist"),
      groups: ["Wrist & forearm", "Score"] },
    { key: "body",     label: qsTr("Body turn and posture"),
      groups: ["Body rotation", "Spine & tilt", "Pelvis & lateral"] },
    { key: "setup",    label: qsTr("Setup and stance"),
      groups: ["Feet & stance", "Alignment", "Head", "Arms"] },
    { key: "sequence", label: qsTr("Sequence and tempo"),
      groups: ["Tempo & sequence"], derivedCounts: true },
    { key: "club",     label: qsTr("Club"),
      groups: ["Club & speed", "Club delivery"] },
    { key: "ball",     label: qsTr("Ball and strike"),
      groups: ["Ball flight", "Strike"] }
]

// The family a catalogue group belongs to, or "" (a group no family names).
function familyOf(group) {
    for (var i = 0; i < families.length; ++i)
        if (families[i].groups.indexOf(group) >= 0) return families[i].key
    return ""
}
// How many families name `group` — the unit case wants exactly one for every catalogue group.
function familiesNaming(group) {
    var n = 0
    for (var i = 0; i < families.length; ++i)
        if (families[i].groups.indexOf(group) >= 0) ++n
    return n
}

// ── The state rule (Stage 5c) ────────────────────────────────────────────────────────────────────
// M / E / U = measured / estimated / unavailable, summed over the family's groups; T = M + E + U.
// Planned metrics are excluded (nothing the golfer could mount produces them today).
//   Not recorded                M + E === 0                      muted
//   Measured                    U === 0 && E === 0               good
//   Estimated                   U === 0 && M === 0               warn
//   Recorded, E of T estimated  U === 0 && M > 0 && E > 0        good — everything is recorded;
//                                                                 the estimate count is the caveat
//   Partly, N of T              U > 0, N = M + E                 warn
function stateOf(m, e, u) {
    if (m + e === 0)        return "none"
    if (u === 0 && e === 0) return "measured"
    if (u === 0 && m === 0) return "estimated"
    if (u === 0)            return "recorded"
    return "partly"
}
function stateText(state, m, e, u) {
    if (state === "measured")  return qsTr("Measured")
    if (state === "measuredByLm") return qsTr("Measured by the launch monitor")
    if (state === "estimated") return qsTr("Estimated")
    if (state === "recorded")  return qsTr("Recorded, %1 of %2 estimated").arg(e).arg(m + e + u)
    if (state === "partly")    return qsTr("Partly, %1 of %2").arg(m + e).arg(m + e + u)
    return qsTr("Not recorded")
}
function toneOf(state) {
    if (state === "measured" || state === "measuredByLm" || state === "recorded") return "good"
    if (state === "none")                             return "muted"
    return "warn"
}

// ── Whose word it is (Stage 5d) ───────────────────────────────────────────────────────────────────
// Before a session, a launch monitor's numbers are only CONFIGURED, so a family whose measured
// metrics all come from it says so: "Measured by the launch monitor", with the configured device's
// name in brackets when the connector gives one ("(GSPro)"). A family where the launch monitor adds
// only some of the measured metrics (Club) keeps plain "Measured".
// The signal: the SAME setup with no launch monitor measures nothing in this family. That is the
// catalogue's own answer, per family, and needs no list of keys here. (DECISION(stage5d): not
// "every measured key starts with lm." — Ball flight's compoundMiss is measured from launch-monitor
// fields under a key of its own, and a prefix rule would miss it or need a list kept in step.)
// Only a "Measured" family takes the phrase.
function lmStateText(name) {
    var t = qsTr("Measured by the launch monitor")
    return name ? t + " (" + name + ")" : t
}

// ── The reason vocabulary ────────────────────────────────────────────────────────────────────────
// Keyed by the union of the family's deviceGapIds. Shown in this order, at most two, joined " · ".
//   bodyImus                → "no trunk sensors"
//   wristImus | hackMotion  → "no wrist sensor"; or, when an arm sensor IS in the session:
//                             "wrist sensor not calibrated" (its calibration outcome is not valid),
//                             "no upper-arm sensor" (calibrated, and the upper arm is the one
//                             missing — roll needs it)
//   launchMonitor           → "no launch monitor"
//   faceOn                  → "no face-on camera"; "face-on camera not connected" when one is
//                             assigned but not enabled and connected (or Cameras was skipped)
//   dtl                     → "no down-the-line camera" / "down-the-line camera not connected",
//                             the same pair
//   clubSensor              → nothing: not something a golfer can mount today
// clubTrack and ballTrack never appear (camera products; the catalogue reports them as the face-on
// camera's gap).
// DECISION(stage5b): the order puts what the golfer can add first (sensors, launch monitor) and the
// cameras after, face-on before down-the-line — a missing face-on camera is the larger loss.
var reasons = {
    noTrunk:            qsTr("no trunk sensors"),
    noWrist:            qsTr("no wrist sensor"),
    wristNotCalibrated: qsTr("wrist sensor not calibrated"),
    noUpperArm:         qsTr("no upper-arm sensor"),
    noLaunchMonitor:    qsTr("no launch monitor"),
    noFaceOn:           qsTr("no face-on camera"),
    faceOnNotConnected: qsTr("face-on camera not connected"),
    noDtl:              qsTr("no down-the-line camera"),
    dtlNotConnected:    qsTr("down-the-line camera not connected")
}
var maxReasons = 2
// When the family has estimated metrics. DECISION(stage5b): singular or plural by the cameras that
// are connected; with none (an estimate from the sensors alone) the bare word.
// DECISION(stage5c): not after "Recorded, E of T estimated", whose state already says it.
function estimatedNote(cameraCount) {
    if (cameraCount >= 2) return qsTr("estimated from the cameras")
    if (cameraCount === 1) return qsTr("estimated from the camera")
    return qsTr("estimated")
}

// The wrist reason, from what the session holds (see the vocabulary).
//   arm: { armInSession, armCalibrated, upperArmHeld }
function wristReason(arm) {
    if (arm && arm.armInSession && !arm.armCalibrated) return reasons.wristNotCalibrated
    if (arm && arm.armInSession && arm.armCalibrated && !arm.upperArmHeld) return reasons.noUpperArm
    return reasons.noWrist
}
// A camera's reason: "unconnected" (assigned, but not enabled and connected, or Cameras skipped)
// or anything else (none assigned).
function cameraReason(which, state) {
    if (which === "faceOn") return state === "unconnected" ? reasons.faceOnNotConnected : reasons.noFaceOn
    return state === "unconnected" ? reasons.dtlNotConnected : reasons.noDtl
}
// The reasons for a set of gap ids, in vocabulary order, at most maxReasons.
//   ctx: { armInSession, armCalibrated, upperArmHeld, faceOn, dtl } — faceOn / dtl are
//        "connected" | "unconnected" | "absent"
function reasonsFor(gapIds, ctx) {
    ctx = ctx || {}
    var has = function(id) { return gapIds.indexOf(id) >= 0 }
    var out = []
    if (has("wristImus") || has("hackMotion")) out.push(wristReason(ctx))
    if (has("bodyImus"))                       out.push(reasons.noTrunk)
    if (has("launchMonitor"))                  out.push(reasons.noLaunchMonitor)
    if (has("faceOn"))                         out.push(cameraReason("faceOn", ctx.faceOn))
    if (has("dtl"))                            out.push(cameraReason("dtl", ctx.dtl))
    return out.slice(0, maxReasons)
}

// ── The rows ─────────────────────────────────────────────────────────────────────────────────────
//   summary  MetricCatalog.setupSummary(...) — [{ group, measured, estimated, unavailable, derived,
//            deviceGapIds, … }]
//   context  { armInSession, armCalibrated, upperArmHeld, cameraCount, faceOn, dtl, lmName }
//            (faceOn / dtl: "connected" | "unconnected" | "absent"; lmName: the configured
//            launch monitor's short name, "" for none)
//   withoutLm  setupSummary for the same setup with launchMonitor false — passed only when a
//            launch monitor is configured (see "Whose word it is")
// One row per family, in family order:
//   { key, label, state, stateText, tone, reasons: [..], reason, measured, estimated, unavailable,
//     total, text }
// `reason` is the reasons and the estimate note joined " · "; `text` is the whole row as one line,
// "Body turn and posture — Partly, 9 of 20 · no trunk sensors · no down-the-line camera".
function rows(summary, context, withoutLm) {
    context = context || {}
    var byGroup = {}, noLm = null
    for (var i = 0; i < (summary || []).length; ++i) byGroup[summary[i].group] = summary[i]
    if (withoutLm) {
        noLm = {}
        for (var w = 0; w < withoutLm.length; ++w) noLm[withoutLm[w].group] = withoutLm[w]
    }
    var out = []
    for (var f = 0; f < families.length; ++f) {
        var fam = families[f]
        var m = 0, e = 0, u = 0, gaps = []
        // Something sees the swing: the phase ladder the derived metrics come off has a source.
        // An arm group only counts once it is calibrated: an uncalibrated or unconnected sensor
        // yields no usable lanes, so it sees nothing.
        var seen = context.faceOn === "connected"
                   || (context.armInSession === true && context.armCalibrated === true)
        var blocked = false      // derived metrics made unavailable for want of it
        for (var g = 0; g < fam.groups.length; ++g) {
            var s = byGroup[fam.groups[g]]
            if (s === undefined) continue
            m += s.measured
            e += s.estimated
            u += s.unavailable
            if (fam.derivedCounts && (s.derived || 0) > 0) {
                if (seen) m += s.derived
                else { u += s.derived; blocked = true }
            }
            var ids = s.deviceGapIds || []
            for (var k = 0; k < ids.length; ++k) if (gaps.indexOf(ids[k]) < 0) gaps.push(ids[k])
        }
        var state = stateOf(m, e, u)
        if (state === "measured" && noLm !== null) {
            var mNoLm = 0
            for (var h = 0; h < fam.groups.length; ++h)
                if (noLm[fam.groups[h]] !== undefined) mNoLm += noLm[fam.groups[h]].measured
            if (mNoLm === 0) state = "measuredByLm"
        }
        var why = (state === "measured" || state === "measuredByLm") ? [] : reasonsFor(gaps, context)
        // The missing camera is THE reason the derived metrics went: it leads, whatever the order.
        if (blocked) {
            var cam = cameraReason("faceOn", context.faceOn)
            why = [cam].concat(reasonsFor(gaps.filter(function(x) { return x !== "faceOn" }), context))
                       .slice(0, maxReasons)
        }
        var parts = why.slice()
        if (e > 0 && state !== "recorded") parts.push(estimatedNote(context.cameraCount || 0))
        var reason = parts.join(" · ")
        var st = state === "measuredByLm" ? lmStateText(context.lmName || "") : stateText(state, m, e, u)
        out.push({ key: fam.key, label: fam.label, state: state, stateText: st, tone: toneOf(state),
                   reasons: why, reason: reason, measured: m, estimated: e, unavailable: u,
                   total: m + e + u, gapIds: gaps,
                   text: fam.label + " — " + st + (reason !== "" ? " · " + reason : "") })
    }
    return out
}
