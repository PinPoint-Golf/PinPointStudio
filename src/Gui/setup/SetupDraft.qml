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
// ⚠ reset() IS THE ONLY PLACE THE VISIT IS SEEDED (F4) — the focus from the open() request, the
// per-session camera/sensor enablement from saved settings. The retired wizard re-seeded on every
// `visible` edge (wizard l.571–588), so a trip to Settings and back threw away the choices made in
// this visit. A suspend/resume does not touch this object.
//
// Every write REPLACES `states` / `outcomes` with a new object: bindings over a var property see
// an assignment, never an in-place mutation.
import QtQuick

QtObject {
    id: draft

    // SetupContext — for condition labels and the enablement seeding. The host wires it.
    property var ctx: null

    // ── The visit ────────────────────────────────────────────────────────────────────────────────
    // The focus the session starts with: a pack condition id, "" = an open assessment. Seeded from
    // the open() request, changed on the Intent step, handed back on Start (startRequest()).
    property string focusConditionId: ""
    property string focusLabel: ""      // its golfer-facing label; "" with no focus
    // The focus the visit was OPENED with ({ id, label }), kept so the Intent page can still offer
    // it after another is picked — it may be none of the work-ons (Practise on a cleared condition).
    property var  requestedFocus: ({ id: "", label: "" })
    property var  states: ({})          // step key → "pending" | "done" | "skipped" (missing = pending)
    // group → { routine, at, devices } as recorded; read it through outcome(), which applies the
    // live validity check.
    property var  outcomes: ({})
    // The open() request as handed in (a plain object; see reset()). Start hands it back with the
    // visit's focus written over it.
    property var  _openRequest: ({})

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

    // ── Focus ────────────────────────────────────────────────────────────────────────────────────
    // id "" = an open assessment. A missing label is looked up (the pack's, else the id).
    function setFocus(id, label) {
        var fid = id ? String(id) : ""
        focusConditionId = fid
        focusLabel = fid === "" ? "" : (label ? String(label) : (ctx !== null ? ctx.conditionLabel(fid) : fid))
    }
    // What Start hands on: the open() request with this visit's focus.
    function startRequest() {
        var out = {}
        for (var k in _openRequest) out[k] = _openRequest[k]
        out.focusConditionId = focusConditionId
        out.focusLabel = focusLabel
        return out
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
        if (routine === "hackmotion") return obj.imuConnected === true && obj.fullyCalibrated === true
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
    // request: { focusConditionId: "" | id, focusLabel: "" | label } (phase 2 C2); missing or
    // null = an open assessment. The focus comes from it, per-session enablement from the global
    // exclusions (through ctx, the one object that touches the managers). States and outcomes start
    // empty.
    function reset(request) {
        var r = (request !== null && typeof request === "object") ? request : ({})
        var copy = {}
        for (var k in r) copy[k] = r[k]
        _openRequest = copy
        setFocus(r.focusConditionId, r.focusLabel)
        requestedFocus = { id: focusConditionId, label: focusLabel }
        states = ({})
        outcomes = ({})
        ctx.seedSessionEnablement()
    }
}
