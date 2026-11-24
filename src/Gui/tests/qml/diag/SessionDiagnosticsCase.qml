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

import QtQuick
import QtTest
import PinPointStudio

// THE SESSION DIAGNOSTICS TEST CASE: the helpers every tst_session_diagnostics_<part>.qml file presses the body
// with (tree walking, the register and its views, sources, text, pointer), the one init() each test starts from,
// and the theme put back at the end. A file declares it inside its SessionDiagnosticsProbe, names it, and adds its
// tests; `probe` is that harness and `body` the panel body under test. Split from tst_session_diagnostics.qml (#254).
TestCase {
    when: windowShown
    readonly property Item probe: parent
    readonly property Item body: probe ? probe.body : null

    // ── tree walking ─────────────────────────────────────────────────────
    function findAll(item, name, out) {
        out = out || []
        if (!item)
            return out
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].objectName === name)
                out.push(kids[i])
            findAll(kids[i], name, out)
        }
        return out
    }
    function visibleAll(item, name) { return findAll(item, name).filter(function (i) { return i.visible }) }
    // `visible` is an item's own flag and says nothing about its ancestors: a view behind another
    // view is hidden by ONE invisible parent and everything in it is still visible: true.
    function shownAll(item, name) { return findAll(item, name).filter(function (i) { return shown(i) }) }
    function one(item, name) {
        const all = findAll(item, name)
        verify(all.length >= 1, "expected a '" + name + "', found none")
        return all[0]
    }
    function oneShown(item, name) {
        const all = shownAll(item, name)
        verify(all.length >= 1, "expected a '" + name + "' on screen, found none (" + findAll(item, name).length + " hidden)")
        return all[0]
    }
    // An item is only really on screen if every ancestor up to the body is too.
    function shown(item) {
        let i = item
        while (i && i !== body) {
            if (!i.visible)
                return false
            i = i.parent
        }
        return !!i
    }

    // ── the register and its views ───────────────────────────────────────
    function reg() { return one(body, "sdRegister") }
    // Which register the body loads is SessionMode's (or the cast's) choice, never the test's: the
    // test sets the mode the app would be in, and checks the body followed it.
    function useRegister(name) {
        app.state.mode = name === "Analyse" ? 2 /* Analyse */ : 1 /* Replay */
        laidOut()
        compare(reg().register, name, "the body loaded the " + name + " register")
    }
    readonly property var viewNames: ({ shot: "sdShotView", session: "sdSessionView",
                                        watching: "sdWatchingView", detail: "sdDetailBody" })
    function showTab(key) { body.tab = key; laidOut() }
    function viewOf(key) { return oneShown(body, viewNames[key]) }
    function tabByKey(key) {
        const ts = shownAll(body, "sdTab").filter(function (t) { return t.key === key })
        verify(ts.length === 1, "one '" + key + "' tab on screen")
        return ts[0]
    }
    function tabCount(key) { return one(tabByKey(key), "sdTabCount") }
    // Replay's session view draws a sparkline row per pattern, or its small multiples when they fit.
    function replaySessionItems(v) { return shownAll(v, "sdSparkRow").concat(shownAll(v, "sdSmallMultiple")) }

    // ── sources ──────────────────────────────────────────────────────────
    function laidOut() {
        // A Row/Column positions on POLISH, not on the binding that sized it: read geometry, or
        // press anything, only after every polish the change scheduled has run. Wait on the WINDOW,
        // since an item's own flag says nothing about the positioners inside it, and spin the event
        // loop around it, so a queued signal, a Loader or a Qt.callLater lands and any polish it
        // schedules is waited on too. (It was waitForRendering, which returns on a NEW frame or after
        // 5 s: when a change drew nothing new, each call slept 5 s, 400 s of this file's 427, #254.)
        const w = body.Window.window
        wait(0)
        for (let i = 0; i < 20 && isPolishScheduled(w); ++i) {
            verify(waitForPolish(w), "the panel's layout settles")
            wait(0)
        }
        verify(!isPolishScheduled(w), "the panel's layout settles")
    }
    function setSource(s) {
        probe.src = null
        wait(0)
        probe.src = s
        laidOut()
    }
    // Review is TWO inputs — the surface and the selected shot's readout.
    function setReview(s, r) {
        probe.readout = null
        setSource(s)
        probe.readout = r
        laidOut()
    }
    // A real-session state ({ source, readout }), with the declarations spied.
    function applyState(st) { setReview(st.source ? probe.spied(st.source) : null, st.readout) }
    // A republish: the source REPLACED, never nulled in between, exactly as the model's arrives.
    function republish(s, r) {
        probe.src = s
        if (r !== undefined)
            probe.readout = r
        laidOut()
    }

    // ── text ─────────────────────────────────────────────────────────────
    function isText(i) { return !!i && i.elide !== undefined && i.truncated !== undefined && typeof i.text === "string" }
    function textsIn(item, out) {
        out = out || []
        if (!item) return out
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            if (isText(kids[i])) out.push(kids[i])
            textsIn(kids[i], out)
        }
        return out
    }
    function plain(s) {
        return String(s === undefined || s === null ? "" : s).replace(/<[^>]*>/g, "").replace(/&nbsp;/g, " ")
                   .replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/\s+/g, " ").trim()
    }
    function shownTexts(item) { return textsIn(item).filter(function (t) { return shown(t) && t.text !== "" }) }
    function allText(item) { return shownTexts(item).map(function (t) { return plain(t.text) }).join(" | ") }
    // A view head: Replay's Head (title, sub) or Analyse's PpStandfirst (kicker, head, standfirst).
    function headText(h) {
        const parts = []
        for (const k of ["title", "sub", "kicker", "head", "standfirst", "text"])
            if (h[k] !== undefined && typeof h[k] === "string") parts.push(plain(h[k]))
        return parts.join(" · ")
    }
    // A ledger cell (or its folded twin): an Item carrying `text`, its Text child drawing it.
    function inkOf(cell) {
        const kids = cell.children
        for (let i = 0; i < kids.length; ++i) if (isText(kids[i])) return kids[i].color
        return undefined
    }

    // ── pointer ──────────────────────────────────────────────────────────
    // A press can only land on what is on screen: scroll the item's view so it is inside it.
    function flickOf(item) {
        let f = item ? item.parent : null
        while (f && f !== body && !(f.contentY !== undefined && f.contentItem !== undefined && /Flick$/.test(f.objectName)))
            f = f.parent
        return f && f !== body ? f : null
    }
    function intoView(item) {
        const f = flickOf(item)
        if (!f) return
        const y = item.mapToItem(f.contentItem, 0, 0).y
        const h = Math.max(item.height, 1)
        if (y < f.contentY || y + h > f.contentY + f.height)
            f.contentY = Math.max(0, Math.min(y - Theme.sp(20), f.contentHeight - f.height))
        laidOut()
    }
    // A ledger row opens its condition from anywhere but its own controls: press it on its name.
    function clickRow(row, cellName) {
        intoView(row)
        const c = one(row, cellName || "sdSessionRowName")
        const p = c.mapToItem(row, Math.min(Theme.sp(6), c.width / 2), Math.min(Theme.sp(6), c.height / 2))
        mouseClick(row, p.x, p.y)
        laidOut()
    }
    function park() { mouseMove(body, 1, 1); laidOut() }
    // Hover a ledger row with the real pointer, over its NAME — where a reader points. (The first run
    // found the rich-text name kept the hover from the row's MouseArea; the row hovers by HoverHandler
    // now.) The brush is the row's hover state (rw.hot).
    function hoverRow(row) {
        intoView(row)
        const br = one(row, "sdShotStrip").brush
        const p = one(row, "sdSessionRowName").mapToItem(row, Theme.sp(4), Theme.sp(4))
        mouseMove(row, p.x, p.y)
        laidOut()
        compare(br.hoveredCondition, row.cardId, "the pointer on the row's name hovers the row")
    }

    function init() {
        probe.width = 1168; probe.height = 560
        Theme.themeIndex = 5          // studio dark
        Theme.fontScale = 1.0
        Theme.reduceMotion = false
        app.state.mode = 0 /* Capture */
        body.surface = "panel"
        body.forceRegister = ""
        body.interactive = true
        body.watchingExpanded = false
        body.cardFilter = "all"
        body.tab = "session"
        body.pulseCue = null
        probe.readout = null
        probe.src = null
        probe.pipFixture = []
        probe.pipFired = 0
        probe.lastScreenRef = ""
        probe.lastScreenCondition = ""
        probe.screenCalls = 0
        probe.lastShotDir = ""
        probe.lastShotId = -1
        probe.shotCalls = 0
        probe.focusCalls = 0
        probe.lastFocusId = ""
        probe.clearFocusCalls = 0
        probe.correctCalls = 0
        probe.lastCorrectShot = -1
        probe.lastCorrectFamily = ""
        probe.lastCorrectTo = "\u0000"
        probe.lastIntentShot = -1
        probe.lastIntentShape = "\u0000"
        probe.lastIntentFromShot = -1
        probe.lastIntentFromShape = "\u0000"
        probe.flagCalls = 0
        probe.lastFlagShot = -1
        probe.detailCalls = 0
        probe.lastDetailId = ""
        probe.closeDetailCalls = 0
        probe.measureCalls = 0
        probe.lastMeasureId = ""
        laidOut()
    }
    // The theme this file sets, put back as it found it when it ends (issue #171): Theme
    // outlives one test, and a file that leaves it changed hands the next file a theme it never
    // chose. init() sets the theme every test here is written against (studio dark).
    property var was: ({})
    function initTestCase() {
        was = { themeIndex: Theme.themeIndex, fontScale: Theme.fontScale, reduceMotion: Theme.reduceMotion }
    }
    function cleanupTestCase() {
        app.state.mode = 0 /* Capture */
        Theme.themeIndex = was.themeIndex
        Theme.fontScale = was.fontScale
        Theme.reduceMotion = was.reduceMotion
    }

    function marksOf(cell) {
        const stub = one(cell, "sdShotStub")
        const kids = stub.parent.children
        return { fired: kids[0], clean: kids[1], stub: stub }
    }
}
