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

// THE READER'S PLACE IN A STAGE PANEL, kept across the panel's own rebuilds — and the carousel's click
// path for a shot picked on one of its figures. The Charts panel and the Session diagnostics panel each
// instantiate one; it holds the part of their shell that was the same algorithm twice (785f1c2f had to
// make the same edit to both copies).
//
// THE PANEL DOES NOT SURVIVE A MODE SWITCH. PpModeStage rebuilds its panels on every Capture↔Replay flip
// — twice per auto-replayed shot — and a new panel is a new model and a new body at their defaults, so
// whatever the reader had open and each list's scroll were lost on every shot. So the place is saved on
// SessionMode (chartsNav, diagnosticsNav) as it changes and put back on build. WHAT the place is beyond
// the scroll (an open row, a tab, a filter) is the panel's: it hands those fields to capture() and puts
// them back itself before calling restore().
//
// SAVED AS IT CHANGES, NOT ON TEARDOWN: the stage builds the incoming panel BEFORE it destroys the
// outgoing one, so a save in onDestruction lands after the restore it was for (verified with
// diag_nav_survives_mode_switch.qml). Saving starts only once the restore is done (`ready`, which the
// panel sets at the end of its Component.onCompleted), so a fresh model clearing its state on build cannot
// overwrite the place it is about to be given back.
//
// A DIFFERENT SESSION STARTS AT THE DEFAULTS, so the saved place is stamped with the folder it was read
// in (the `model`'s session folder) and matches() refuses another's.
//
// A LIST'S PLACE IS KEPT PER REGISTER. The body swaps its register (the Replay glance ⇄ Analyse) in place, and
// each register names its lists alike — the glance's card list and the Analyse one are both sdCardsFlick — but
// they are different lists of different lengths. So each list's scroll is kept under its register (`scope`):
// a shot picked on the carousel takes Session diagnostics through the glance and back, and the Analyse list
// comes back where the reader left it, rather than at the top or at the glance's shorter end (#129). A list
// that comes up NEW in a swap is given its register's place, as a rebuilt panel's lists are.
//
// WHAT A "REGISTER" IS HERE is the panel's `scope`, not merely the register on show. Session diagnostics reads
// the Analyse register OVER the Replay glance (a detail, a Consistency cell, the Why of a shot) and keeps those
// lists under "Replay>Analyse" (the body's placeScope): opened from the glance they start at their own tops, and
// a scroll there leaves Analyse's own place alone. When the SAME list object carries on under another scope (a
// detail open over the glance as the stage goes to Analyse keeps the Analyse register), the list ON SHOW keeps
// what it shows and that becomes its place under the new scope; a list not on show (a tab behind the detail) is
// given the new scope's saved place — or its top, with none — when it next shows.
// A list that shows one thing at a time is keyed by that thing too (`flickKeys`): the one detail list keeps a
// place per condition ("Analyse/sdDetailFlick#<condition>"), so another condition opens at its top and the same
// one comes back where it was left. Charts reads one register at a time (no over-glance), so its scope is its
// register.
//
// THE LISTS ARE LOOKED UP AFTER THE SWAP HAS SETTLED. The body changes its register in two steps (the incoming
// register is built before the outgoing one is let go), and a lookup made between them finds the outgoing
// register's lists — the watch then sat on lists about to be destroyed, and no scroll was ever saved (#129).
// So watch() runs on the place's own zero-length timer: after the swap, and never after the place is gone (the
// panels' Qt.callLater(place.watch) ran on a panel already destroyed: "Property 'flick' … is not a function").
//
// ONLY THE READER MOVES THE PLACE. A list's scroll also changes when its content shortens under it (a shorter
// shot: the list clamps to its end), when a restore writes it, and when a list is rebuilt at the top — and
// none of those is where the reader was. Saving whatever the list showed made the clamp the place: Charts
// scrolled to 2403, a shorter shot's list clamped it to 876, and the reader came back at 876 (#129). So a
// list's place is recorded the moment its scroll changes, EXCEPT a change this place made itself (a restore,
// or the follow below), a change while the list is still waiting for its place, a change made INSIDE a resize
// (Flickable clamps and pixel-aligns the scroll before it announces the new size, _moved), and a change that
// leaves the list at its end while its place is deeper (the clamp). Everything else is the reader's, by
// whatever means — drag, flick, wheel, the scroll bar, the keyboard — which is why the place is not keyed on
// movement signals (a scroll-bar drag and a wheel turn do not all report movement). And the list FOLLOWS its
// place as its content changes: a place deeper than the list shows the list's end, and when the content
// grows back the full place returns. A reader who scrolls on the short list moves the place (up to 300
// there is 300 when the long list comes back); one who scrolls to the very end of it, past which the place
// already lies, leaves the deeper place standing.
//
// Usage (the panel's own fields in its _saveNav / _restoreNav):
//   PpReaderPlace { id: place; content: body.registerItem; scope: body.register; flickNames: [...]
//                   active: root.remembersNav; model: seriesModel; onSaveRequested: root._saveNav() }
//   (Session diagnostics: scope: body.placeScope; flickKeys: ({ sdDetailFlick: diagModel.detailConditionId }))
//   save:    const nav = place.capture({ openId: body.openId }); if (nav) SessionMode.chartsNav = nav
//   restore: if (place.matches(nav)) { body.openId = nav.openId; place.restore(nav) }
//   on build: restore, then place.watch(); then place.ready = root.remembersNav
import QtQuick
import PinPointStudio

QtObject {
    id: place

    // Where the named Flickables are looked up: the body's register (its registerItem), which the body swaps
    // when the mode or a detail changes the register.
    property Item content: null
    // The register the lists are read in (the body's `register`, or Session diagnostics' placeScope, which
    // tells Analyse over the glance apart): a list's place is kept per scope.
    property string scope: ""
    onContentChanged: place._swapped()
    onScopeChanged: place._swapped()
    // The objectNames of the Flickables whose scroll is part of the place.
    property var flickNames: []
    // What a list is showing, for a list that shows one thing at a time (objectName → id): each thing keeps
    // its own place — Session diagnostics' one detail list keeps one per condition, so another condition's
    // detail opens at its top and the same condition's comes back where it was left.
    property var flickKeys: ({})
    onFlickKeysChanged: place._rekey()
    // Whether the place is kept at all — off for a cast, which has its own lifetime and must not
    // overwrite where the reader was on the stage. Off: no list is watched and nothing is saved.
    property bool active: true
    // The model whose session folder the place is stamped with — read when the place is saved or
    // matched, never bound, so a folder the model has only just announced is the one compared.
    property QtObject model: null
    // Saving starts once the restore is done; the panel sets it at the end of its build.
    property bool ready: false

    // A watched list's scroll settled (300 ms after its last move): the panel saves its place.
    signal saveRequested()

    // Whether a saved place was read in this session's folder.
    function matches(nav) { return !!nav && nav.sessionDir === place._folder() }
    function _folder() { return SessionMode.folderKey(place.model ? place.model.sessionDir : "") }

    // The place to save: `fields` (the panel's) stamped with the folder, plus each list's scroll under its
    // register ("<register>/<objectName>"); null until `ready`. A list still waiting for its restore keeps the
    // place it is waiting for, and the other register's lists keep theirs.
    function capture(fields) {
        if (!place.ready) return null
        return Object.assign({ sessionDir: place._folder() }, fields || {}, { scroll: Object.assign({}, place._places()) })
    }

    // Put back a saved place's scroll (the panel has put back its own fields first).
    function restore(nav) {
        place._scroll = Object.assign({}, (nav && nav.scroll) || {})
        place._scrollFolder = place._folder()
        place._pendingScroll = place._wanted()
        place._scrollTries = {}
        if (Object.keys(place._pendingScroll).length > 0) restoreTimer.start()
    }

    // Every register's list places in this session ("<register>/<objectName>" → contentY): the last saved or
    // restored. A different session starts with none.
    property var _scroll: ({})
    property string _scrollFolder: ""
    function _key(name) { return place._keyIn(place.scope, name) }
    function _keyIn(scope, name) {
        const sub = String((place.flickKeys || {})[name] || "")
        return (scope !== "" ? scope + "/" + name : name) + (sub !== "" ? "#" + sub : "")
    }
    // The register the watched lists belong to; undefined while a swap settles (nothing is recorded or
    // followed then) and until the first look. _lastScope is the one they were last watched in.
    property var _watchedScope: undefined
    property var _lastScope: undefined
    // The register is swapping: stop recording now (the outgoing lists' places are already recorded, as they
    // moved — what they do while they are let go is not the reader's), then look again once it has settled.
    function _swapped() {
        place._watchedScope = undefined
        place._pendingScroll = ({})
        place.watch()
    }
    // A list started showing something else (flickKeys): it takes that thing's place, or its top.
    property var _subs: ({})
    function _rekey() {
        for (const name of place.flickNames) {
            const sub = String((place.flickKeys || {})[name] || "")
            if (sub === (place._subs[name] || "")) continue
            place._subs[name] = sub
            if (place._watchedScope === undefined) continue            // the next look seeds it
            const f = place._watchedList(name)
            if (!f) continue
            delete place._pendingScroll[name]
            delete place._scrollTries[name]
            const y = place._places()[place._key(name)] || 0
            if (y > 0) { place._pendingScroll[name] = y; place.resume() }
            else if (f.contentY !== 0) { place._adjusting = true; f.contentY = 0; place._adjusting = false }
        }
    }
    function _watchedList(name) {
        for (let i = 0; i < flickWatch.count; ++i) {
            const c = flickWatch.objectAt(i)
            if (c.modelData === name) return c.target
        }
        return null
    }

    // A watched list's scroll changed: the reader's, unless it is one of the changes the header lists.
    property bool _adjusting: false
    function _moved(name, f) {
        if (!place.ready || place._adjusting || place._watchedScope === undefined || !f) return
        if (place._pendingScroll[name] !== undefined) return            // still being given its place
        // Part of a resize: Flickable clamps (and pixel-aligns) contentY inside the size change, BEFORE it
        // announces the new size, so a size that differs from the last one announced means this move is the
        // resize's, not the reader's — unless the reader has hold of the list.
        const sz = place._sizes[name]
        if ((!sz || sz.ch !== f.contentHeight || sz.h !== f.height) && !f.moving) return
        const all = place._places(), key = place._keyIn(place._watchedScope, name)
        const most = Math.max(0, f.contentHeight - f.height)
        if ((all[key] || 0) > most + 0.5 && f.contentY >= most - 0.5) return   // the clamp: its place is deeper
        if (f.contentY > 0) all[key] = f.contentY
        else delete all[key]
        saveTimer.restart()
    }
    // A watched list's content or viewport changed size: it follows its place (the end of a list too short
    // for it, the full place when the list is long enough again). Not while the reader has hold of it.
    property var _sizes: ({})          // each watched list's last announced size, by name
    function _resized(name, f) {
        if (f) place._sizes[name] = { ch: f.contentHeight, h: f.height }
        if (!place.ready || place._watchedScope === undefined || !f || f.moving) return
        if (place._pendingScroll[name] !== undefined) return            // the restore has it
        const want = place._places()[place._keyIn(place._watchedScope, name)] || 0
        const most = Math.max(0, f.contentHeight - f.height)
        const y = Math.min(want, most)
        if (Math.abs(f.contentY - y) < 1) return
        place._adjusting = true
        f.contentY = y
        place._adjusting = false
    }
    function _places() {
        if (place._scrollFolder !== place._folder()) { place._scroll = ({}); place._scrollFolder = place._folder() }
        return place._scroll
    }
    // This register's places, by list name.
    function _wanted() {
        const all = place._places(), out = {}
        for (const name of place.flickNames) {
            const y = all[place._key(name)]
            if (y > 0) out[name] = y
        }
        return out
    }

    // Start the restore again: something the panel shows changed, and a list waiting for its place may
    // be on screen now. The first pass runs AT ONCE, so a list that is on screen and laid out has its place in the same turn as
    // the change that showed it — not a timer tick later, after whatever else that turn queued (a GUI stall
    // let the diag-nav probe read the card list between the two).
    function resume() {
        if (Object.keys(place._pendingScroll).length > 0 && place._restoreStep()) restoreTimer.start()
    }

    // The named Flickables live in the body's REGISTER, which the body swaps when the mode or the view
    // changes: forget the old ones and watch the new ones — once the swap has settled (the header says why).
    // A list that is new since the last look starts at the top, so it is given its register's place.
    function watch() { watchTimer.restart() }
    readonly property Timer _watchTimer: Timer { id: watchTimer; interval: 0; onTriggered: place._watchNow() }
    function _watchNow() {
        place._flickCache = ({})
        const want = place._wanted()
        for (let i = 0; i < flickWatch.count; ++i) {
            const c = flickWatch.objectAt(i)
            const name = c.modelData
            const f = place.flick(name)
            if (f && f !== c.target) {
                // a new list (the register swapped) starts at the top: give it its place
                if (want[name] !== undefined && Math.abs(f.contentY - want[name]) >= 1) place._pendingScroll[name] = want[name]
                else delete place._pendingScroll[name]
                delete place._scrollTries[name]
            } else if (f && place._lastScope !== undefined && place._lastScope !== place.scope) {
                // the SAME list, now read under another register (a detail open over the glance when the stage
                // goes to Analyse). The list ON SHOW keeps what it shows, which becomes its place there; a list
                // NOT on show (the tabs behind that detail) shows nothing the reader chose, so it takes the new
                // register's saved place when it shows — recording its idle top would wipe that place (#129 G5).
                if (f.visible) {
                    const all = place._places(), key = place._key(name)
                    if (f.contentY > 0) all[key] = f.contentY
                    else delete all[key]
                } else {
                    const y = want[name] !== undefined ? want[name] : 0
                    if (Math.abs(f.contentY - y) >= 1) place._pendingScroll[name] = y
                    else delete place._pendingScroll[name]
                    delete place._scrollTries[name]
                }
            }
            c.target = f
            if (f) place._sizes[name] = { ch: f.contentHeight, h: f.height }
            place._subs[name] = String((place.flickKeys || {})[name] || "")
        }
        place._watchedScope = place.scope
        place._lastScope = place.scope
        place.resume()
    }

    // Looked up once and cached — and NEVER from a binding: a walk of `children` in a binding subscribes
    // it to every child list in the body, and the body adds children by the hundred when a detail opens
    // (the probe hung on exactly that).
    function flick(name) {
        let f = place._flickCache[name]
        if (!f) { f = place._findNamed(place.content, name); if (f) place._flickCache[name] = f }
        return f
    }
    function _findNamed(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) {
            const hit = place._findNamed(kids[i], name)
            if (hit) return hit
        }
        return null
    }
    property var _flickCache: ({})

    // A list's content arrives over the next few frames (Repeaters, then layout), so the scroll is
    // re-applied until each list is tall enough to take it — for half a second of the list being ON
    // SCREEN, after which a list too short for its place shows its end (and follows its place from then on,
    // _resized: the place itself is not cut to the list). A list hidden
    // (behind an open detail, or in a view not on show) is not laid out at all, so it waits, uncounted,
    // until it shows.
    property var _pendingScroll: ({})
    property var _scrollTries: ({})
    // One pass of the restore; true while a list on screen still waits for its place (the timer runs on).
    function _restoreStep() {
        const left = {}
        for (const name in place._pendingScroll) {
            const want = place._pendingScroll[name]
            const f = place.flick(name)
            if (!f || !f.visible) { left[name] = want; continue }
            const tries = (place._scrollTries[name] || 0) + 1
            place._scrollTries[name] = tries
            const most = Math.max(0, f.contentHeight - f.height)
            f.contentY = Math.min(want, most)
            if (most < want && tries < 10) left[name] = want
        }
        place._pendingScroll = left
        // Nothing left that is on screen: stop, and start again when the body shows more.
        return !Object.keys(left).every(function (n) { const f = place.flick(n); return !f || !f.visible })
    }
    readonly property Timer _restoreTimer: Timer {
        id: restoreTimer
        interval: 50; repeat: true
        onTriggered: if (!place._restoreStep()) stop()
    }

    // The saves, on the reader's scroll (_moved): saved a moment after it settles.
    readonly property Timer _saveTimer: Timer { id: saveTimer; interval: 300; onTriggered: place.saveRequested() }
    readonly property Instantiator _flickWatch: Instantiator {
        id: flickWatch
        model: place.active ? place.flickNames : []
        delegate: Connections {
            required property string modelData
            target: null                                // set by watch()
            function onMovementStarted() { delete place._pendingScroll[modelData] }  // theirs now
            function onContentYChanged() { place._moved(modelData, target) }
            function onContentHeightChanged() { place._resized(modelData, target) }
            function onHeightChanged() { place._resized(modelData, target) }
            // a list waiting for its place has come on screen
            function onVisibleChanged() { if (place._pendingScroll[modelData] !== undefined) place.resume() }
        }
    }

    // ── a shot picked on a figure: the carousel's own click path ─────────────
    //
    // The carousel owns the selection and numbers its shots with its own counter, so the request (by
    // swing folder, the identity both sides share) is resolved against the shots the app is SHOWING —
    // the loaded session's in review, the live carousel's otherwise — and handed to SessionMode.pickShot,
    // which does exactly what tapping that swing's card does. Not gated on `active`: a cast picks too.
    readonly property Instantiator _carouselShots: Instantiator {
        id: carouselShots
        model: sessionReviewController.activeShots
        delegate: QtObject {
            required property int shotId
            required property string swingDir
        }
    }
    function pickShot(swingDir) {
        if (!swingDir) return false
        const want = SessionMode.folderKey(swingDir)
        const name = want.substring(want.lastIndexOf("/") + 1)
        for (let i = 0; i < carouselShots.count; ++i) {
            const s = carouselShots.objectAt(i)
            if (!s || !s.swingDir) continue
            const have = SessionMode.folderKey(s.swingDir)
            // The folder first; its name when the ledger was written on another host and records the
            // same swing under a different mount (shotIdForSwingDir's rule, the same reason).
            if (have === want || have.substring(have.lastIndexOf("/") + 1) === name) {
                SessionMode.pickShot(s.shotId, s.swingDir)
                return true
            }
        }
        return false
    }
}
