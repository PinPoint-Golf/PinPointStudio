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
// Usage (the panel's own fields in its _saveNav / _restoreNav):
//   PpReaderPlace { id: place; content: body; flickNames: [...]; active: root.remembersNav
//                   model: seriesModel; onSaveRequested: root._saveNav() }
//   save:    const nav = place.capture({ openId: body.openId }); if (nav) SessionMode.chartsNav = nav
//   restore: if (place.matches(nav)) { body.openId = nav.openId; place.restore(nav) }
//   on build: restore, then place.watch(); then place.ready = root.remembersNav
import QtQuick
import PinPointStudio

QtObject {
    id: place

    // Where the named Flickables are looked up (the panel's body).
    property Item content: null
    // The objectNames of the Flickables whose scroll is part of the place.
    property var flickNames: []
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

    // The place to save: `fields` (the panel's) stamped with the folder, plus each list's scroll; null
    // until `ready`. A list still waiting for its restore keeps the place it is waiting for.
    function capture(fields) {
        if (!place.ready) return null
        const scroll = {}
        for (const name of place.flickNames) {
            if (place._pendingScroll[name] !== undefined) { scroll[name] = place._pendingScroll[name]; continue }
            const f = place.flick(name)
            if (f && f.contentY > 0) scroll[name] = f.contentY
        }
        return Object.assign({ sessionDir: place._folder() }, fields || {}, { scroll: scroll })
    }

    // Put back a saved place's scroll (the panel has put back its own fields first).
    function restore(nav) {
        place._pendingScroll = Object.assign({}, (nav && nav.scroll) || {})
        place._scrollTries = {}
        if (Object.keys(place._pendingScroll).length > 0) restoreTimer.start()
    }

    // Start the restore again: something the panel shows changed, and a list waiting for its place may
    // be on screen now.
    function resume() {
        if (Object.keys(place._pendingScroll).length > 0) restoreTimer.start()
    }

    // The named Flickables live in the body's REGISTER, which the body swaps when the mode or the view
    // changes: forget the old ones and watch the new ones.
    function watch() {
        place._flickCache = ({})
        for (let i = 0; i < flickWatch.count; ++i) {
            const c = flickWatch.objectAt(i)
            c.target = place.flick(c.modelData)
        }
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
    // SCREEN, after which a list that has shrunk keeps the furthest it can scroll to. A list hidden
    // (behind an open detail, or in a view not on show) is not laid out at all, so it waits, uncounted,
    // until it shows.
    property var _pendingScroll: ({})
    property var _scrollTries: ({})
    readonly property Timer _restoreTimer: Timer {
        id: restoreTimer
        interval: 50; repeat: true
        onTriggered: {
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
            if (Object.keys(left).every(function (n) {
                    const f = place.flick(n); return !f || !f.visible }))
                stop()
        }
    }

    // The saves, on scroll. A scroll is saved a moment after it settles; the restore's own writes do not
    // count, because a list is only written while it is still pending.
    readonly property Timer _saveTimer: Timer { id: saveTimer; interval: 300; onTriggered: place.saveRequested() }
    readonly property Instantiator _flickWatch: Instantiator {
        id: flickWatch
        model: place.active ? place.flickNames : []
        delegate: Connections {
            required property string modelData
            target: null                                // set by watch()
            function onMovementStarted() { delete place._pendingScroll[modelData] }  // theirs now
            function onContentYChanged() {
                if (place._pendingScroll[modelData] === undefined) saveTimer.restart()
            }
            // a list waiting for its place has come on screen
            function onVisibleChanged() { if (place._pendingScroll[modelData] !== undefined) restoreTimer.start() }
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
