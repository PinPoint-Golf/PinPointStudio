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

// One visit to session setup (session_wizard_refactor_design.md §4.6): what outlives the pages,
// which exist only while current (R2). Reset by SetupFlow.open() and nowhere else.
//
// ⚠ reset() IS THE ONLY PLACE SAVED SETTINGS ARE SEEDED (F4). Today's wizard re-seeded the goals
// and the per-session camera/sensor enablement on every `visible` edge (wizard l.571–588), so a
// trip to Settings and back threw away the choices made in this visit. A suspend/resume does not
// touch this object.
//
// Every write REPLACES `states` / `outcomes` with a new object: bindings over a var property see
// an assignment, never an in-place mutation.
import QtQuick

QtObject {
    id: draft

    // SetupContext — for the presets (goal lists) and the enablement seeding. The host wires it.
    property var ctx: null

    // ── The visit ────────────────────────────────────────────────────────────────────────────────
    property int  preset: -1            // SessionController.Type index into ctx.presets
    property var  goals: []             // selected goal keys, in pick order
    property bool goalsInteracted: false
    property var  states: ({})          // step key → "pending" | "done" | "skipped" (missing = pending)
    // group → { routine, at, devices } as recorded; read it through outcome(), which applies the
    // live validity check.
    property var  outcomes: ({})

    // The preset's goal chips, and the default goal Start uses when none is picked (wizard l.2216).
    readonly property var goalDefs: ctx !== null && preset >= 0 && preset < ctx.presets.length
                                    ? ctx.presets[preset].goals : []
    readonly property var defaultGoal: goalDefs.length > 0 ? goalDefs[0] : null

    // ── Steps ────────────────────────────────────────────────────────────────────────────────────
    function state(key) {
        var s = states[key]
        return (s === "done" || s === "skipped") ? s : "pending"
    }
    function setState(key, s) {
        var next = {}
        for (var k in states) next[k] = states[k]
        next[key] = s
        states = next
    }
    function setStates(map) {
        var next = {}
        for (var k in states) next[k] = states[k]
        for (var m in map) next[m] = map[m]
        states = next
    }

    // ── Goals (wizard l.901–909) ─────────────────────────────────────────────────────────────────
    function toggleGoal(key) {
        goalsInteracted = true
        var arr = goals.slice()
        var i = arr.indexOf(key)
        if (i === -1) arr.push(key)
        else          arr.splice(i, 1)
        goals = arr
    }
    // What Start hands on: the picked goals, else the preset's first (wizard l.2216–2218).
    function goalsForStart() {
        if (goals.length > 0) return goals.slice()
        return defaultGoal !== null ? [defaultGoal.key] : []
    }

    // ── Calibration outcomes (R8, fixes F1) ──────────────────────────────────────────────────────
    // A recorded outcome counts only while its devices' LIVE validity holds:
    //   witmotion — every recorded segment (ImuInstance) reads fullyCalibrated;
    //   hackmotion — every recorded device (HmInstance) is connected and calibrationState is
    //                CALIBRATED (2).
    // A link drop therefore voids it with no one having to clear it. A destroyed object reads its
    // properties as undefined and fails the check the same way.
    //
    // `devices`: the objects to check — the segment units for a Witmotion run, the peripheral for a
    // wG3 (its units carry no connect state).
    function recordOutcome(group, routine, devices) {
        var next = {}
        for (var k in outcomes) next[k] = outcomes[k]
        next[group] = { routine: routine, at: Date.now(), devices: (devices || []).slice() }
        outcomes = next
    }
    function clearOutcome(group) {
        if (outcomes[group] === undefined) return
        var next = {}
        for (var k in outcomes) if (k !== group) next[k] = outcomes[k]
        outcomes = next
    }
    function _valid(routine, obj) {
        if (!obj) return false
        if (routine === "hackmotion") return obj.imuConnected === true && obj.calibrationState === 2
        return obj.fullyCalibrated === true
    }
    // { done, recorded, routine, at, devices }. `done` is the live answer; `recorded` says an
    // outcome exists at all (a page can tell "never calibrated" from "calibration lost").
    function outcome(group) {
        var rec = outcomes[group]
        if (rec === undefined)
            return { done: false, recorded: false, routine: "", at: 0, devices: [] }
        var ok = rec.devices.length > 0
        for (var i = 0; i < rec.devices.length; ++i)
            if (!_valid(rec.routine, rec.devices[i])) ok = false   // read them all: each is a dependency
        return { done: ok, recorded: true, routine: rec.routine, at: rec.at, devices: rec.devices }
    }

    // ── A new visit ──────────────────────────────────────────────────────────────────────────────
    // Goals from the profile (appSettings.sessionGoalsByType, keyed by the type as a string —
    // wizard l.573–575), per-session enablement from the global exclusions (through ctx, the one
    // object that touches the managers). States and outcomes start empty.
    function reset(presetIndex) {
        preset = presetIndex
        var saved = ctx.settings.sessionGoalsByType[presetIndex.toString()]
        goals = (saved && saved.length > 0) ? saved.slice() : []
        goalsInteracted = false
        states = ({})
        outcomes = ({})
        ctx.seedSessionEnablement()
    }
}
