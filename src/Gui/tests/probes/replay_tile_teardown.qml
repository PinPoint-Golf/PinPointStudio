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

// #209 (refactor 5.3b): A REPLAY TILE TORN DOWN WITH ITS SINK BIND STILL QUEUED. Each replay tile (PpCameraFrame with
// replayStreamIndex >= 0) binds its video sink to the replay player one Qt.callLater after it is built. A tile released
// by its Repeater or Loader loses its QML context at once and is deleted later, so a bind still queued then ran with no
// `app` in scope: "PpCameraFrame.qml: ReferenceError: app is not defined". This drives, with the default layout:
//   A  the race, made certain: a reviewed shot in Replay, then Analyse (its tiles are rebuilt, each with a bind queued),
//      and leaving the review in the SAME delayed-call pass, before those binds run (what a click handled while the
//      GUI thread is catching up does), then Start;
//   B  the golfer's path, one event per step: review, Replay, Analyse, back to live, Start.
// The probe says PASS or FAIL on what it can see; the warning itself is in the app log, so the run passes only with
// "PROBE: RESULT PASS" AND no QML warning in the log (grep 'qml:[0-9]*:' and 'ReferenceError|TypeError').
//
//   PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 HOME=<scratch> CFFIXED_USER_HOME=<scratch> \
//     XDG_CONFIG_HOME=<scratch>/.config PINPOINT_DIAGNOSTICS_DIR=<scratch>/diag \
//     <PinPointStudio> --probe-qml <abs path to this file> --probe-session <a session folder with video>
// (a scratch copy: loading a session regrades and writes into it). Exit 0 = PASS, 1 = FAIL.

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
    function walk(obj, pred, out, seen) {
        if (!obj || seen.has(obj)) return
        seen.add(obj)
        if (pred(obj)) out.push(obj)
        const kids = obj.data || obj.children || []
        for (let i = 0; i < kids.length; ++i) walk(kids[i], pred, out, seen)
    }
    // The replay tiles on screen: PpCameraFrame (it alone has _bindReplaySink) in replay-stream mode.
    function replayTiles() {
        const out = []
        walk(probe.Window.window.contentItem,
             o => o._bindReplaySink !== undefined && o.replayStreamIndex !== undefined && o.replayStreamIndex >= 0,
             out, new Set())
        return out
    }

    Instantiator {
        id: reviewed
        model: app.sessionReviewController.shots
        delegate: QtObject { required property int shotId; required property string swingDir }
    }

    function reviewFirstShot() {
        const s = reviewed.objectAt(0)
        app.state.enterReplay(app.sessionReviewController.activeShots.shotRefOf(s.shotId), s.swingDir)
    }

    property int step: 0
    property int raceTiles: -1
    Timer {
        id: tick
        interval: 2500; repeat: true; running: true
        onTriggered: probe.next()
    }

    function next() {
        step += 1
        if (step === 1) {
            if (!check(reviewDir !== "", "--probe-session given")) { finish(); return }
            app.navController.navigate(AppScreen.Session)
            app.state.enterCapture()
            app.sessionReviewController.loadSession(reviewDir)
        } else if (step === 2) {
            if (!check(app.sessionReviewController.reviewActive && reviewed.count > 0,
                       "the session loaded for review (" + reviewed.count + " shots)")) { finish(); return }
            reviewFirstShot()
        } else if (step === 3) {
            const n = replayTiles().length
            if (!check(app.state.replay.active && n > 0, "A: Replay shows the reviewed shot's video (" + n + " tile(s))")) {
                finish(); return
            }
            // A: Analyse rebuilds the stage, and its tiles are built one delayed-call pass later, each queueing its bind.
            // Leaving the review is queued behind that pass, so it runs after the tiles are built and before they bind.
            app.state.enterAnalyse()
            Qt.callLater(function() {
                probe.raceTiles = probe.replayTiles().length
                app.sessionReviewController.resumeLive()
            })
        } else if (step === 4) {
            check(raceTiles > 0, "A: the race was set up: Analyse had built " + raceTiles + " replay tile(s) when the review was left")
            check(app.state.mode === app.state.captureMode && !app.state.replay.active && replayTiles().length === 0,
                  "A: back to live: Capture, no replay, no replay tile")
            app.state.startSession(false)
        } else if (step === 5) {
            check(app.sessionController.running, "A: the session started")
            app.state.endSession(true)
        } else if (step === 6) {
            check(!app.sessionController.running, "A: the session ended")
            app.sessionReviewController.loadSession(reviewDir)
        } else if (step === 7) {
            check(app.sessionReviewController.reviewActive, "B: the session loaded for review again")
            reviewFirstShot()
        } else if (step === 8) {
            check(app.state.replay.active && replayTiles().length > 0, "B: Replay shows the video")
            app.state.enterAnalyse()
        } else if (step === 9) {
            check(app.state.mode === app.state.analyseMode && replayTiles().length > 0, "B: Analyse shows the video")
            app.sessionReviewController.resumeLive()
        } else if (step === 10) {
            check(app.state.mode === app.state.captureMode && replayTiles().length === 0, "B: back to live")
            app.state.startSession(false)
        } else if (step === 11) {
            check(app.sessionController.running, "B: the session started")
            app.state.endSession(true)
        } else if (step === 12) {
            check(!app.sessionController.running, "B: the session ended")
            app.navController.navigate(AppScreen.Home)
        } else {
            finish()
        }
    }
}
