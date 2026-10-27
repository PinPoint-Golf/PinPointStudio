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

// END-TO-END: THE DIAGNOSTICS PANEL KEEPS THE READER'S PLACE ACROSS A MODE SWITCH.
//
// PpModeStage rebuilds its panels on every mode switch — twice per auto-replayed shot — and the
// session diagnostics panel used to come back on SESSION, filter ALL, Watching folded and no
// characteristic open. This drives the real app: put the panel in a place (SESSION tab with a
// shot picked — the case the readingShot edge would otherwise drag to THIS SHOT — Watching open,
// a characteristic's detail open, the card list scrolled), flip Replay → Analyse → Replay, and
// require the place back on a panel that WAS rebuilt. Then a place stamped with another session
// must not be restored. Also checks the ball-ready ting is off on the stage's Replay/Analyse
// and on again on Capture.
//
// TYPESET REGISTERS (11 Oct): the body hosts a register — PpDiagReplay in Replay, PpDiagAnalyse in
// Analyse (`sdRegister`, `register`). An open detail is read in PpDiagAnalyse in either mode, so the
// detail (`sdDetailBody`) is drawn on both sides of the flip, with the stage still in Replay on the way
// back; closing it there returns to PpDiagReplay, whose card list `sdCardsFlick` must come back where
// it was. The checks below assert both, beside the place.
//
// Writes the View layout — run on a COPY of the settings (XDG_CONFIG_HOME):
//   build/run-me/verify-diag-nav.sh [session dir] [ordinal]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string sessionDir: arg("--probe-session", "")
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "8"))

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }

    function walk(item, name, out) {
        if (!item) return
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], name, out)
    }
    function theBody() {
        const bodies = []
        walk(probe.Window.window.contentItem, "sdBody", bodies)
        const shown = bodies.filter(function (b) { return b.visible && b.width > 0 })
        return shown.length === 1 ? shown[0] : null
    }
    function flick(body, name) { const o = []; walk(body, name, o); return o.length ? o[0] : null }
    // On screen only if every ancestor up to `top` is visible.
    function shown(item, top) {
        let i = item
        while (i && i !== top) { if (!i.visible) return false; i = i.parent }
        return !!i
    }
    function register(body) { const o = []; walk(body, "sdRegister", o); return o.length ? o[0].register : "" }
    function anyShown(body, name) { const o = []; walk(body, name, o); return o.some(x => shown(x, body)) }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property var    before: null
    property var    want: ({})
    property int    step: 0
    function expectPlace(what, reg) {
        const b = theBody()
        check(!!b, what + ": one Session diagnostics panel is up")
        if (!b) return
        check(b !== before, what + ": the panel WAS rebuilt (else this proves nothing)")
        check(b.tab === want.tab, what + ": tab " + b.tab + " (want " + want.tab + ")")
        check(b.watchingExpanded === want.watchingExpanded, what + ": Watching fold kept")
        check(b.cardFilter === want.cardFilter, what + ": filter " + b.cardFilter)
        check(b.detailConditionId === want.detail,
              what + ": detail '" + b.detailConditionId + "' (want '" + want.detail + "')")
        check(register(b) === reg, what + ": the " + reg + " register is loaded (" + register(b) + ")")
        check(anyShown(b, "sdDetailBody"), what + ": the open detail is drawn")
        before = b
    }
    // Checked after closing the detail the way a reader would: in Analyse the card list sits behind the
    // open detail (and is built only on first show), so the Replay list is the one that must come back.
    function expectScroll(what) {
        const b = theBody()
        const f = b ? flick(b, "sdCardsFlick") : null
        check(!!f && f.visible && Math.abs(f.contentY - want.scroll) < 1,
              what + ": card list scroll " + (f ? f.contentY : "-") + " (want " + want.scroll + ") contentHeight " + (f ? f.contentHeight + " height " + f.height : "-"))
    }

    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const s = probe.step
            if (s === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                // Split in both stage modes, diagnostics on: the arrangement that rebuilds.
                for (const m of [SessionMode.replay, SessionMode.analyse]) {
                    ViewLayout.setArrangement(m, "split")
                    if (!ViewLayout.isPanelOn(m, "sessionDiagnostics"))
                        ViewLayout.setPanel(m, "sessionDiagnostics", true)
                }
                navController.navigate(AppScreen.Session)                 // the session screen
                SessionMode.enterCapture()
                probe.check(cameraManager.ballCueEnabled === true, "ball ting ON on Capture")
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (s === 2) {
                probe.check(sessionReviewController.reviewActive, "the session loaded for review")
                let picked = false
                for (let i = 0; i < shots.count; ++i) {
                    const sh = shots.objectAt(i)
                    if (sh.ordinal === probe.ordinal) { SessionMode.enterReplay(sh.shotId, sh.swingDir); picked = true }
                }
                probe.check(picked, "shot " + probe.ordinal + " is in the carousel")
                probe.check(cameraManager.ballCueEnabled === false, "ball ting OFF on Replay")
            } else if (s === 3) {
                const b = theBody()
                probe.check(!!b, "the panel is up in Replay")
                if (!b) { probe.finish(); return }
                probe.check(b.readingShot === true, "reading the picked shot (the edge that forces THIS SHOT)")
                const cards = b.cards || []
                probe.check(cards.length > 0, cards.length + " characteristic cards")
                if (cards.length === 0) { probe.finish(); return }
                b.tab = "session"
                b.watchingExpanded = true
                b.cardFilter = "all"
            } else if (s === 4) {
                // A step after the place is set, so the list is measured as it is laid out with
                // Watching open — not the geometry from before it unfolded.
                const b = theBody()
                const cards = b.cards || []
                const f = probe.flick(b, "sdCardsFlick")
                const most = f ? Math.max(0, f.contentHeight - f.height) : 0
                probe.log("card list: contentHeight " + (f ? f.contentHeight : "-") + " height " + (f ? f.height : "-"))
                if (f && most > 0) f.contentY = Math.min(most, Math.round(most / 2))
                b._openDetail(cards[0].id)
                probe.want = { tab: "session", watchingExpanded: true, cardFilter: "all",
                               detail: b.detailConditionId, scroll: f ? f.contentY : 0 }
                probe.check(probe.want.detail === cards[0].id, "opened the detail of '" + cards[0].id + "'")
                probe.before = b
            } else if (s === 5) {
                SessionMode.enterAnalyse()
                probe.check(cameraManager.ballCueEnabled === false, "ball ting OFF on Analyse")
            } else if (s === 6) {
                probe.expectPlace("Replay → Analyse", "Analyse")
                SessionMode.showReplay()
            } else if (s === 7) {
                probe.expectPlace("Analyse → Replay", "Analyse")
                probe.check(SessionMode.mode === SessionMode.replay, "Analyse → Replay: the stage stays in Replay under the detail")
                probe.theBody()._closeDetail()
            } else if (s === 8) {
                probe.expectScroll("Analyse → Replay, detail closed")
                // A place saved in another session is not this one's: take the panel down,
                // re-stamp the save with another folder, then bring a panel up.
                probe.before = probe.theBody()
                ViewLayout.setPanel(SessionMode.replay, "sessionDiagnostics", false)
            } else if (s === 9) {
                probe.check(probe.theBody() === null, "panel taken down")
                const nav = SessionMode.diagnosticsNav
                probe.check(!!nav && nav.tab === "session" && nav.watchingExpanded === true,
                            "the place is saved on SessionMode")
                // Re-open a detail in the save so the other-session check below has one to refuse.
                nav.detailConditionId = probe.want.detail
                SessionMode.diagnosticsNav = Object.assign({}, nav, { sessionDir: "/somewhere/else" })
                ViewLayout.setPanel(SessionMode.replay, "sessionDiagnostics", true)
            } else if (s === 10) {
                const b = probe.theBody()
                probe.check(!!b && b !== probe.before, "rebuilt for the other-session case")
                probe.check(!!b && b.detailConditionId === "", "another session's open detail is NOT restored")
                probe.check(!!b && b.watchingExpanded === false, "…nor its Watching fold")
                SessionMode.enterCapture()
            } else if (s === 11) {
                probe.check(cameraManager.ballCueEnabled === true, "ball ting back ON on Capture")
                probe.finish()
            }
        }
    }
}
