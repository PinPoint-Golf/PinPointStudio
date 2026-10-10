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

// The session-setup flow engine (session_wizard_refactor_design.md §4.5). Non-visual. It owns the
// plan, the current step, the navigation queue and the page lifecycle; the decisions themselves are
// the pure functions in setup_flow.js, and the visit's state (goals, step states, outcomes) lives
// in the SetupDraft.
//
// ── Wiring (the host — the setup shell, or a test's FlowShell) ─────────────────────────────────
//   SetupContext { id: ctx; draft: draft }
//   SetupDraft   { id: draft; ctx: ctx }
//   SetupSteps   { id: steps }
//   Loader       { id: pageLoader; … geometry … }
//   SetupFlow    { registry: steps; ctx: ctx; draft: draft; loader: pageLoader }
//
// DECISION(stage4): the HOST owns the Loader and hands it in; the flow drives it with setSource().
// The Loader is a visual item that the shell lays out (full-bleed or reading column), so it belongs
// in the shell's tree; a flow-owned Loader would have to be re-parented, which is more code and
// one more thing to get wrong. The flow never touches its geometry.
//
// ── Rules it enforces ─────────────────────────────────────────────────────────────────────────────
// R2  Exactly one page object is alive. A transition leaves the old page, unloads it, WAITS until it
//     has actually been destroyed (the Loader deletes it with deleteLater) and only then creates
//     the next one — so two pages, or two View3Ds, never coexist even for a turn.
// R4  leave(reason) runs, then `active` goes false, BEFORE the page is unloaded. A new page gets
//     `active = true` and then enter(direction).
// R5  Every operation is QUEUED: it is applied on a later event-loop turn (Qt.callLater), never
//     inside the caller's handler, so a page that calls flow.next() can still read its own
//     properties afterwards (L3). A NAVIGATION request is accepted only when nothing is queued;
//     a second one in the same turn is dropped (N19: a double-click advances once).
//     DECISION(stage4): lifecycle requests (open, exit, suspend, resume) REPLACE a queued
//     navigation request rather than being dropped: a Cancel or a trip to Settings in the same turn
//     as a Continue must not be lost. A navigation request never replaces anything.
//     DECISION(stage5a): lifecycle requests QUEUE BEHIND one another, in order, instead of
//     replacing each other. The real shell needs it: Main calls open() from Home and then makes the
//     shell visible in the same turn, and the visibility edge requests resume() — which, replacing
//     the queued open(), lost the whole reset. Applied in order, open() runs and the resume() that
//     follows is refused as "notSuspended" (one log line, no effect).
// R10 Every request ends in exactly ONE app-log line, "[Setup] op=… from=… to=… reason=…
//     plan=a,b,c", whether it was applied, refused by a guard, or dropped.
//
// Header ‹ / › (design §4.5, D2): › is next("done") and nothing else — it never runs a page's
// `primary` (Connect), so it cannot mark a step done without connecting (F5). Bind the header's
// enablement to canHeaderForward / canHeaderBack.
import QtQuick
import PinPointStudio
import "setup_flow.js" as SF

QtObject {
    id: flow

    // ── Wiring ───────────────────────────────────────────────────────────────────────────────────
    property var  registry: null      // SetupSteps
    property var  ctx:      null      // SetupContext
    property var  draft:    null      // SetupDraft
    property Loader loader: null      // the host's Loader; the flow calls setSource() on it

    // ── Signals for the shell ────────────────────────────────────────────────────────────────────
    signal cancelled()                          // exit("cancel"): devices released → go home
    signal startRequested(int preset, var goals) // exit("start"): devices kept → start the session
    signal exitRequested(string reason)         // "back": ‹ on the first step; devices released →
                                                // navController.back()
    // Requests a page makes of the HOST, passed straight through (not queued: neither changes the
    // flow — opening Settings hides the shell, whose visibility edge then suspends it).
    // DECISION(stage5a): on the flow, because the flow is the one object every page is handed
    // that also reaches the shell; a page never names the shell.
    signal settingsRequested(int panelIndex)    // a "→ Open … settings" link (ScreenSettings sub-panel)
    signal cameraRecalibrateRequested()         // Triangulate's Recalibrate

    // ── State ────────────────────────────────────────────────────────────────────────────────────
    property bool   isOpen:    false
    property bool   suspended: false
    property string current:   ""
    property WizardPage page:  null           // the live page, or null

    // All three collaborators set. The bindings below wait for it: the host's objects are created
    // together, and the order their bindings first evaluate in is not ours to choose.
    readonly property bool   wired:       registry !== null && ctx !== null && draft !== null

    readonly property int    preset:      draft ? draft.preset : -1
    readonly property var    descriptors: registry ? registry.descriptors : []
    readonly property var    plan:        wired ? SF.plan(descriptors, ctx, draft, current) : []
    readonly property var    numbering:   SF.numbering(plan)
    readonly property var    states:      draft ? draft.states : ({})
    readonly property var    currentDescriptor: _descriptor(current)
    readonly property bool   isFirst:     current !== "" && plan.indexOf(current) === 0
    readonly property bool   isLast:      current !== "" && plan.indexOf(current) === plan.length - 1
    // The current step stopped applying (a DTL camera deselected on Triangulate). It stays current
    // and keeps its number (N14); the shell's hint says "No longer needed — Continue", and
    // leaving drops it from the plan.
    readonly property bool   noLongerNeeded: wired && currentDescriptor !== null && !currentDescriptor.applies(ctx, draft)
    readonly property string stepLabel: stepLabelFor(current)
    readonly property url    currentPageUrl: current !== "" && registry ? registry.pageUrl(current) : ""

    // Continue (footer and header ›) would advance. DECISION(stage4): a step that no longer applies
    // has nothing left to require, so its page and descriptor gates are not consulted.
    readonly property bool canContinue: isOpen && !suspended && current !== "" && !isLast
                                        && (noLongerNeeded
                                            || (page !== null && page.canContinue
                                                && currentDescriptor !== null && currentDescriptor.gate(ctx, draft)))
    readonly property bool canHeaderForward: canContinue
    // ‹ always does something while open: the previous step, or the exit from the first.
    readonly property bool canHeaderBack: isOpen && !suspended

    // A request is queued or a page is loading. Tests settle on this.
    readonly property bool busy: _queue.length > 0 || _loading

    // The indicator's model: one entry per plan step. `attention`: a visited (done or skipped)
    // step whose issues are not empty now — e.g. a camera that dropped after Cameras was done.
    readonly property var steps: {
        var p = flow.plan, out = []
        for (var i = 0; i < p.length; ++i) {
            var d = flow._descriptor(p[i])
            var s = SF.stateOf(flow.states, p[i])
            var visited = s !== "pending"
            var iss = visited ? d.issues(flow.ctx, flow.draft) : []
            out.push({ key: p[i], label: d.title, group: d.group, state: s,
                       current: p[i] === flow.current, attention: visited && iss.length > 0,
                       number: i + 1 })
        }
        return out
    }
    readonly property var progress:    SF.progress(plan, states)
    // Ready's aggregates, over the plan, evaluated without the pages.
    readonly property var issues:      wired ? SF.issues(descriptors, plan, ctx, draft) : []
    readonly property var summaryRows: wired ? SF.summaries(descriptors, plan, ctx, draft) : []

    function stepLabelFor(key) {
        var d = _descriptor(key)
        if (d === null || numbering[key] === undefined) return ""
        return Theme.caps(qsTr("Step %1 of %2 · %3")).arg(numbering[key]).arg(plan.length).arg(d.eyebrow)
    }

    // ── Operations (all queued, R5) ──────────────────────────────────────────────────────────────
    // A new visit for a preset (SessionController.Type): the draft is reset (goals and enablement
    // seeded from settings — the ONLY place), the paced sensor connect is cancelled, every step is
    // pending and the first step is entered.
    function open(presetIndex)  { _request("open", presetIndex) }
    // mark: "done" (guard: page.canContinue && descriptor.gate) | "skipped" (guard: page.canSkip).
    function next(mark)         { _request("next", mark === "skipped" ? "skipped" : "done") }
    // The previous step; on the first step, the exit: releaseDevices() + exitRequested("back").
    function back()             { _request("back", "") }
    // Jump back to a visited step (canGoTo). resetStates: the target and every step from it up to
    // the current one go back to pending — the Check page's "Recalibrate" (N15) uses it after
    // clearing the outcome. DECISION(stage4): an optional argument on goTo, not a separate op.
    function goTo(key, resetStates) { _request("goTo", { key: key, reset: resetStates === true }) }
    function suspend()          { _request("suspend", "") }
    function resume()           { _request("resume", "") }
    // reason: "cancel" (devices released, cancelled()) | "start" (devices kept, startRequested()).
    function exit(reason)       { _request("exit", reason === "start" ? "start" : "cancel") }

    function canGoTo(key) { return isOpen && !suspended && SF.canGoTo(plan, states, current, key) }

    // Host requests (see the signals): immediate, not queued.
    function openSettings(panelIndex) { settingsRequested(panelIndex) }
    function requestCameraRecalibrate() { cameraRecalibrateRequested() }

    // Camera stop + disconnect, the paced sensor queue cancelled, sensors disconnected — through
    // the context, the one object that touches the managers. Called on cancel and on ‹ from the
    // first step, never on start (the session needs them).
    function releaseDevices() { ctx.releaseDevices() }

    // ── Internals ────────────────────────────────────────────────────────────────────────────────
    // [{ op, arg }], oldest first: at most one navigation request, or lifecycle requests in order.
    // Always REPLACED, never mutated, so `busy` sees every change.
    property var    _queue: []
    property bool   _loading: false      // a page is due: waiting for the old one to die, or loading
    property string _loadKey: ""
    property string _loadDir: ""
    // The page being unloaded (internal). An object-typed property is guarded: when the object is
    // destroyed it reads null and its change signal fires — which is when the next page may be
    // created. (Not underscore-named only so the change handler's name is unambiguous.)
    property Item   dyingPage: null
    onDyingPageChanged: if (dyingPage === null && _loading) Qt.callLater(_finishLoad)

    function _descriptor(key) { return registry && key !== "" ? registry.descriptor(key) : null }
    function _planText() { return plan.join(",") }
    function _log(op, from, to, reason, extra) {
        appLog.info("Setup", "op=" + op + " from=" + (from === "" ? "-" : from) + " to=" + (to === "" ? "-" : to)
                             + " reason=" + reason + (extra ? " " + extra : "") + " plan=" + _planText())
    }
    function _argText(op, arg) {
        if (op === "goTo") return arg.key + (arg.reset ? " reset" : "")
        return arg === "" || arg === undefined ? "-" : String(arg)
    }

    function _isNav(op) { return op === "next" || op === "back" || op === "goTo" }

    function _request(op, arg) {
        if (_queue.length > 0) {
            if (_isNav(op)) {
                _log(op, current, "", "dropped", "arg=" + _argText(op, arg) + " pending=" + _queue[0].op)
                return
            }
            // A lifecycle request: a queued navigation request is superseded; queued lifecycle
            // requests stay, and this one runs after them.
            var kept = []
            for (var i = 0; i < _queue.length; ++i) {
                var q = _queue[i]
                if (_isNav(q.op)) _log(q.op, current, "", "superseded", "arg=" + _argText(q.op, q.arg) + " by=" + op)
                else              kept.push(q)
            }
            kept.push({ op: op, arg: arg })
            _queue = kept
        } else {
            _queue = [{ op: op, arg: arg }]
        }
        Qt.callLater(_drain)
    }

    function _drain() {
        if (_queue.length === 0 || _loading) return   // a load in progress drains when it completes
        var req = _queue[0]
        _queue = _queue.slice(1)
        if (req.op === "open")         _applyOpen(req.arg)
        else if (req.op === "next")    _applyNext(req.arg)
        else if (req.op === "back")    _applyBack()
        else if (req.op === "goTo")    _applyGoTo(req.arg.key, req.arg.reset)
        else if (req.op === "suspend") _applySuspend()
        else if (req.op === "resume")  _applyResume()
        else if (req.op === "exit")    _applyExit(req.arg)
        // Qt.callLater collapses repeats within a turn, so the next queued request needs its own
        // call; one that started a page load drains from _finishLoad instead.
        if (_queue.length > 0 && !_loading) Qt.callLater(_drain)
    }

    function _applyOpen(presetIndex) {
        var from = current
        _leavePage("exit")
        draft.reset(presetIndex)        // goals + per-session enablement seeded here and only here
        ctx.cancelPacedConnect()        // a queue left from the last visit must not connect into this one
        isOpen = true
        suspended = false
        current = ""                    // unpin, so plan[0] is the first step that applies
        var first = plan.length > 0 ? plan[0] : ""
        current = first
        _unloadThenLoad(first, "forward")
        _log("open", from, first, "preset=" + presetIndex)
    }

    function _applyNext(mark) {
        var from = current
        var to = SF.next(plan, current)
        var refused = ""
        if (!isOpen)                        refused = "closed"
        else if (suspended)                 refused = "suspended"
        else if (to === "")                 refused = "last"
        else if (mark === "done" && !canContinue)
            refused = page === null ? "nopage" : (noLongerNeeded || page.canContinue ? "gate" : "page")
        else if (mark === "skipped" && !(page !== null && page.canSkip)) refused = "noskip"
        if (refused !== "") { _log("next", from, from, mark, "refused=" + refused); return }
        draft.setState(from, mark)
        _leavePage(mark === "done" ? "forward" : "skip")
        current = to
        _unloadThenLoad(to, "forward")
        _log("next", from, to, mark)
    }

    function _applyBack() {
        var from = current
        if (!isOpen || suspended) { _log("back", from, from, "back", "refused=" + (isOpen ? "suspended" : "closed")); return }
        var to = SF.back(plan, current)
        if (to === "") { _exit("back", "back"); return }
        draft.setState(from, "pending")
        _leavePage("back")
        current = to
        _unloadThenLoad(to, "back")
        _log("back", from, to, "back")
    }

    function _applyGoTo(key, reset) {
        var from = current
        if (!(isOpen && !suspended && SF.canGoTo(plan, states, current, key))) {
            _log("goTo", from, key, "jump", "refused=" + (isOpen && !suspended ? "notVisited" : "closed"))
            return
        }
        if (reset) {
            var map = {}
            for (var i = plan.indexOf(key); i <= plan.indexOf(current); ++i) map[plan[i]] = "pending"
            draft.setStates(map)
        }
        _leavePage("jump")
        current = key
        _unloadThenLoad(key, "jump")
        _log("goTo", from, key, "jump", reset ? "reset=1" : "")
    }

    function _applySuspend() {
        if (!isOpen || suspended) { _log("suspend", current, current, "suspend", "refused=" + (isOpen ? "already" : "closed")); return }
        _leavePage("suspend")           // the page stays; only its activity stops
        suspended = true
        _log("suspend", current, current, "suspend")
    }

    function _applyResume() {
        if (!isOpen || !suspended) { _log("resume", current, current, "resume", "refused=" + (isOpen ? "notSuspended" : "closed")); return }
        suspended = false
        if (page !== null) {
            page.active = true
            page.enter("resume")
        }
        _log("resume", current, current, "resume")
    }

    function _applyExit(reason) {
        if (!isOpen) { _log("exit", current, "", reason, "refused=closed"); return }
        // Defensive, as today (wizard l.2213): the comingSoon types are not startable; Home never
        // opens setup for them.
        if (reason === "start" && ctx.presets[draft.preset] !== undefined && ctx.presets[draft.preset].comingSoon) {
            _log("exit", current, "", reason, "refused=comingSoon")
            return
        }
        _exit("exit", reason)
    }

    // op "exit" (cancel/start) or "back" (‹ on the first step).
    function _exit(op, reason) {
        var from = current
        _leavePage("exit")          // a suspended page has already left (leave("suspend")); not twice
        _unload()
        current = ""
        isOpen = false
        suspended = false
        var release = reason !== "start"
        if (release) releaseDevices()
        _log(op, from, "", reason, release ? "release=1" : "release=0")
        if (reason === "cancel")      cancelled()
        else if (reason === "start")  startRequested(draft.preset, draft.goalsForStart())
        else                          exitRequested("back")
    }

    // R4: leave first, then inactive. Safe with no page (still loading) or an already-suspended one.
    function _leavePage(reason) {
        if (page === null || suspended) return
        page.leave(reason)
        page.active = false
    }

    function _unload() {
        var p = page
        page = null
        if (p !== null) {
            dyingPage = p
            loader.source = ""          // the Loader deleteLater()s it; dyingPage turns null when it is gone
        }
    }

    function _unloadThenLoad(key, direction) {
        _unload()
        _loadKey = key
        _loadDir = direction
        _loading = true
        if (dyingPage === null) Qt.callLater(_finishLoad)
    }

    function _finishLoad() {
        if (!_loading || dyingPage !== null) return
        var key = _loadKey
        var url = registry.pageUrl(key)
        loader.setSource(url, { flow: flow, ctx: ctx, draft: draft, stepKey: key })
        var p = loader.item
        _loading = false
        if (p === null) {
            appLog.warn("Setup", "page for " + key + " did not load: " + url)
        } else {
            p.stepLabel = Qt.binding(function() { return flow.stepLabelFor(key) })
            page = p
            if (!suspended) {
                p.active = true
                p.enter(_loadDir)
            }
        }
        if (_queue.length > 0) Qt.callLater(_drain)
    }
}
