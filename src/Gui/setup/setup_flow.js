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

// The session-setup flow's DECISIONS, as pure functions (session_wizard_refactor_design.md §4.5).
// No QML objects, no Qt: SetupFlow.qml holds the state and the page lifecycle and asks these what
// the next step is; tst_setup_flow.qml presses them directly with hand-made registries.
//
// DECISION(stage4): a `.pragma library` .js, not the `.mjs` the design names. The module already
// packages one (cameras/AutoAnnotations.js, imported relatively by PpCameraFrame.qml and by path
// from tst_auto_annotations.qml), so this is the route known to work in both the app's module and
// the test binary's copy of it. Nothing here needs ES-module features.
//
// Vocabulary:
//   registry  an ordered array of step descriptors: { key, applies(ctx, draft), gate(ctx, draft),
//             issues(ctx, draft), summary(ctx, draft) } — StepDescriptor.qml, or a plain object.
//   plan      the ordered keys of the steps that apply now (registry order, never reordered).
//   states    key → "pending" | "done" | "skipped"; a missing key is "pending".

function _keyOf(d) { return d ? d.key : "" }

function _descriptor(registry, key) {
    for (var i = 0; i < registry.length; ++i)
        if (registry[i] && registry[i].key === key) return registry[i]
    return null
}

// The ordered keys of the applicable steps. `pinnedKey` — the CURRENT step — stays in the plan even
// when it no longer applies, in its registry place, until it is left (design §4.5 "The current step
// is pinned"). A newly applicable step appears in registry order; nothing ever reorders.
function plan(registry, ctx, draft, pinnedKey) {
    var out = []
    if (!registry) return out
    for (var i = 0; i < registry.length; ++i) {
        var d = registry[i]
        if (!d) continue
        var k = _keyOf(d)
        if (k === pinnedKey || d.applies(ctx, draft)) out.push(k)
    }
    return out
}

// The step after / before `current` in the plan; "" at either end or when `current` is not in it.
function next(planKeys, current) {
    var i = planKeys.indexOf(current)
    return (i < 0 || i + 1 >= planKeys.length) ? "" : planKeys[i + 1]
}
function back(planKeys, current) {
    var i = planKeys.indexOf(current)
    return i <= 0 ? "" : planKeys[i - 1]
}

function stateOf(states, key) {
    var s = states ? states[key] : undefined
    return (s === "done" || s === "skipped") ? s : "pending"
}

// Jump-back (design §4.9, D5 "back only"): the target is EARLIER in the plan than the current
// step and was visited (done or skipped). Never forward, never the current step itself.
function canGoTo(planKeys, states, current, key) {
    var i = planKeys.indexOf(key)
    var c = planKeys.indexOf(current)
    if (i < 0 || c < 0 || i >= c) return false
    return stateOf(states, key) !== "pending"
}

// key → 1-based display number. The indicator and every page eyebrow read this one map, so the
// two can never disagree (design §4.9; F8's two literal state arrays that did).
function numbering(planKeys) {
    var out = {}
    for (var i = 0; i < planKeys.length; ++i) out[planKeys[i]] = i + 1
    return out
}

// Every step's issues, in plan order, each tagged with its step: [{ key, text, panel }].
// Evaluated WITHOUT the pages: a descriptor's issues() reads the context and the draft only.
function issues(registry, planKeys, ctx, draft) {
    var out = []
    for (var i = 0; i < planKeys.length; ++i) {
        var d = _descriptor(registry, planKeys[i])
        if (!d) continue
        var list = d.issues(ctx, draft) || []
        for (var j = 0; j < list.length; ++j)
            out.push({ key: planKeys[i], text: list[j].text, panel: list[j].panel })
    }
    return out
}

// The closing page's summary rows, in plan order: [{ key, label, value, good }]. A step whose
// summary() returns null has no row.
function summaries(registry, planKeys, ctx, draft) {
    var out = []
    for (var i = 0; i < planKeys.length; ++i) {
        var d = _descriptor(registry, planKeys[i])
        if (!d) continue
        var row = d.summary(ctx, draft)
        // `tone` (optional): "neutral" draws the row muted — neither in place nor missing.
        if (row) out.push({ key: planKeys[i], label: row.label, value: row.value, good: row.good,
                            tone: row.tone || "" })
    }
    return out
}

// How far through the plan: a skipped step counts as passed (the indicator's "3 of 7 done"
// counts the ⚠ pip too — design §4.9's sketch).
function progress(planKeys, states) {
    var done = 0, skipped = 0
    for (var i = 0; i < planKeys.length; ++i) {
        var s = stateOf(states, planKeys[i])
        if (s === "done") ++done
        else if (s === "skipped") ++skipped
    }
    var total = planKeys.length
    return { done: done, skipped: skipped, passed: done + skipped, total: total,
             fraction: total > 0 ? (done + skipped) / total : 0 }
}
