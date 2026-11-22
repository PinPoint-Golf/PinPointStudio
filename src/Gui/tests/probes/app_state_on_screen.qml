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

// 4-a ON SCREEN (refactor 4-c, deliverable 3: build/refactor/step-4/REVIEW-a.md "Delta check"). One application state
// (app.state) holds the mode, the selection, the replay and the session on screen; this drives the real app through
// it, as the carousel, the toolbar and the rail do, and reports what the screens hold:
//   B3  a pick survives a reload of the same session in the live carousel, and leaving the session screen and coming
//       back (its reload of today's, which is nothing here);
//   B2  the Consistency glance follows the selection;
//   B1  the Motion popup's Customise page goes back to Presets on a switch to Capture;
//       a reviewed session in Analyse draws its stage panel;
//       an empty live session's End posts no "not being saved" notice, with the Analyse panels up.
// End of a session WITH shots needs a capture source, which a headless probe has none of: not driven here.
//
//   PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 HOME=<scratch> CFFIXED_USER_HOME=<scratch> \
//     XDG_CONFIG_HOME=<scratch>/.config PINPOINT_DIAGNOSTICS_DIR=<scratch>/diag \
//     <PinPointStudio> --probe-qml <abs path to this file> --probe-live <a session folder> --probe-session <another>
// (both of the scratch library). --probe-live is shown in the live carousel the way the session screen shows today's.
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
    readonly property string reviewDir: arg("--probe-session", "")
    readonly property string liveDir: arg("--probe-live", "")

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
        return ok
    }
    function finish() {
        tick.running = false
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    // Every object under the window, popups' content included (a Popup is not an Item: walk its contentItem).
    function walk(obj, pred, out, seen) {
        if (!obj || seen.has(obj)) return
        seen.add(obj)
        if (pred(obj)) out.push(obj)
        const kids = obj.data || obj.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out, seen)
        if (obj.contentItem && obj.contentItem !== obj && obj.opened !== undefined) walk(obj.contentItem, pred, out, seen)
    }
    function find(pred) { const out = []; walk(probe.Window.window.contentItem, pred, out, new Set()); return out }
    function named(n) { return find(o => o.objectName === n) }
    function shown(item) {
        let i = item
        while (i) { if (i.visible === false) return false; i = i.parent }
        return true
    }

    Instantiator {
        id: live
        model: shotModel
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    Instantiator {
        id: reviewed
        model: sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property int ordinal; required property string swingDir }
    }
    Instantiator {
        id: notes
        model: notifications
        delegate: QtObject { required property string notificationId; required property string title }
    }

    property int step: 0
    property string pickedDir: ""
    property bool replayBefore: false
    property var panel: null
    property var popup: null

    function liveShot(i) { return i < live.count ? live.objectAt(i) : null }
    function glanceDirs() {
        return find(o => (o.objectName === "sdConsistencyGlanceView" || o.objectName === "sdConsistencyView")
                         && o._swingDir !== undefined)
            .map(o => o.objectName + (shown(o) ? "" : " (hidden)") + "=" + o._swingDir)
    }

    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: probe.next()
    }

    function body() { const b = named("sdBody").filter(shown); return b.length === 1 ? b[0] : null }

    function next() {
        step += 1
        if (step === 1) {
            navController.navigate(AppScreen.Session)
            app.state.enterCapture()
        } else if (step === 2) {
            // The live carousel showing a session, as it shows today's (the scratch library has no session of today):
            // the session screen's own call, ScreenWrist._syncTodayCarousel → app.state.setLiveFolder.
            if (!check(liveDir !== "", "--probe-live given")) { finish(); return }
            app.state.setLiveFolder(liveDir)
        } else if (step === 3) {
            log("on screen " + app.state.sessionOnScreenFolder + ", live carousel " + live.count + " shot(s)")
            if (!check(live.count >= 2, "the live carousel holds the session's shots")) { finish(); return }
            const s = liveShot(1)
            pickedDir = s.swingDir
            app.state.enterReplay(shotModel.shotRefOf(s.shotId), s.swingDir)           // the carousel's click path
        } else if (step === 4) {
            check(app.state.selection.valid && app.state.selection.swingDir === pickedDir, "B3: the pick is the selection")
            replayBefore = !!app.state.replay && app.state.replay.active
            check(replayBefore, "B3: the pick started a replay (mode " + app.state.mode + ")")   // else the replay checks below prove nothing
            app.state.setLiveFolder(liveDir)                                         // B3: a reload of the same session
        } else if (step === 5) {
            check(app.state.selection.valid && app.state.selection.swingDir === pickedDir,
                  "B3: a reload of the same session keeps the selection (" + app.state.selection.swingDir + ")")
            check((!!app.state.replay && app.state.replay.active) === replayBefore,
                  "B3: …and the replay (" + (app.state.replay ? app.state.replay.active : "-") + ")")
            navController.navigate(AppScreen.Home)
        } else if (step === 6) {
            navController.navigate(AppScreen.Session)       // the carousel reloads today's: none here, so "" on screen
        } else if (step === 7) {
            log("after Home and back: on screen '" + app.state.sessionOnScreenFolder + "', carousel " + live.count)
            check(app.state.selection.valid && app.state.selection.swingDir === pickedDir,
                  "B3: entering the session screen keeps the selection (nothing on screen is not another session)")
            app.state.setLiveFolder(liveDir)               // the session back on screen
        } else if (step === 8) {
            check(app.state.selection.valid && app.state.selection.swingDir === pickedDir,
                  "B3: the selection is the same session's when it is back on screen")
            check((!!app.state.replay && app.state.replay.active) === replayBefore,
                  "B3: …and the replay is as it was (" + (app.state.replay ? app.state.replay.active : "-") + ")")
            // B2: the stage panel's Consistency tab (the glance is built on its first show)
            const b = body()
            if (check(!!b, "the Replay stage shows the Session diagnostics panel (the layout's)")) b.tab = "consistency"
        } else if (step === 9) {
            const g = glanceDirs()
            log("B2 glance: " + JSON.stringify(g))
            check(g.length > 0 && g.some(t => t.endsWith("=" + pickedDir)), "B2: the Consistency glance holds the pick")
            const s = liveShot(0)
            pickedDir = s.swingDir
            app.state.pickShot(shotModel.shotRefOf(s.shotId), s.swingDir)              // the selection moves
        } else if (step === 10) {
            const g = glanceDirs()
            log("B2 glance after the move: " + JSON.stringify(g))
            check(g.length > 0 && g.every(t => t.endsWith("=" + pickedDir) || t.indexOf("(hidden)") >= 0),
                  "B2: every shown Consistency glance follows the selection to " + pickedDir)
            app.state.showReplay()
        } else if (step === 11) {
            // B1: the Motion popup, in a mode that is not Capture
            const p = named("motionPopup")
            if (!check(p.length === 1, "B1: the Motion popup exists")) { step = 13; return }
            popup = p[0]
            popup.open()
            panel = popup.contentItem
            check(app.state.mode !== app.state.captureMode, "B1: the mode is not Capture (" + app.state.mode + ")")
            panel.page = 1                                                            // Customise
        } else if (step === 12) {
            check(panel.page === 1 && popup.visible, "B1: Customise is showing in the open popup")
            app.state.enterCapture()
        } else if (step === 13) {
            if (panel) {
                check(panel.page === 0, "B1: a switch to Capture puts the popup back on Presets (page " + panel.page
                      + ", popup " + (popup.visible ? "still open" : "closed") + ")")
                popup.close()
            }
            // a reviewed session
            if (!check(reviewDir !== "", "--probe-session given")) { step = 16; return }
            sessionReviewController.loadSession(reviewDir)
        } else if (step === 14) {
            check(sessionReviewController.reviewActive, "the session loaded for review (" + reviewed.count + " shots)")
            if (reviewed.count > 0) {
                const s = reviewed.objectAt(0)
                app.state.enterReplay(sessionReviewController.activeShots.shotRefOf(s.shotId), s.swingDir)
                pickedDir = s.swingDir
            }
        } else if (step === 15) {
            const b = body()
            if (check(!!b, "Replay of the reviewed session shows its stage panel")) {
                b.tab = "shot"
                const r = b.readout
                check(!!r && !!r.conditions && r.conditions.length > 0, "…not blank: it holds the picked shot's readout")
            }
            log("B2 glance in review: " + JSON.stringify(glanceDirs()))
            app.state.enterAnalyse()
        } else if (step === 16) {
            log("Analyse: " + named("sdBody").filter(shown).length + " stage panel(s) in this layout")
            // an empty live session, ended with the Analyse panels up
            sessionReviewController.resumeLive()
            app.state.startSession(false)
        } else if (step === 17) {
            check(sessionController.running, "the empty live session started (on screen " + app.state.sessionOnScreenFolder + ")")
            app.state.enterAnalyse()
        } else if (step === 18) {
            app.state.endSession(true)
        } else if (step === 19) {
            const titles = []
            for (let i = 0; i < notes.count; ++i) titles.push(notes.objectAt(i).title)
            log("notices after End: " + JSON.stringify(titles))
            check(!sessionController.running, "the empty session ended")
            check(!titles.some(t => t.indexOf("not being saved") >= 0), "no \"not being saved\" notice after an empty End")
            navController.navigate(AppScreen.Home)
        } else {
            finish()
        }
    }
}
