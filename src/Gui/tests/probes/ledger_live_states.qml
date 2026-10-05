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

// END-TO-END: THE SESSION LEDGER FOLLOWS TODAY'S SESSION THROUGH EVERY STATE IT CAN BE IN.
//
// The panel showed an earlier day's session opened for review and nothing at all for today's:
// not after a relaunch, not once the session was ended, and a shot landing in an extended
// session was dropped or filed under the wrong number. This walks the real app through
//
//   relaunched (today's swings on the carousel, no session running) → extended (running) →
//   a new shot lands → ended
//
// and fails unless the panel's ledger holds every swing of the session in each state.
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <this file> \
//       --probe-session <today's session dir, in a SCRATCH library> --probe-new-swing <swing dir>
// The swing at --probe-new-swing must NOT exist at launch: the runner
// (build/run-me/verify-ledger-live.sh) copies it into place when this file logs ADD-SWING, and
// this file then announces it the way ShotProcessor does. Exit 0 = PASS; "PROBE:" lines say why.

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
    readonly property string newSwingDir: arg("--probe-new-swing", "")
    readonly property int    newOrdinal: parseInt(newSwingDir.slice(newSwingDir.lastIndexOf("_") + 1))

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
    function model() {
        const bodies = []
        walk(probe.Window.window.contentItem, "sdBody", bodies)
        return bodies.length === 1 ? bodies[0].source : null
    }
    function same(a, b) { return a.replace(/\/+$/, "") === b.replace(/\/+$/, "") }
    function state(tag, wantShots) {
        const m = model()
        check(!!m, tag + ": one Session diagnostics panel is up")
        if (!m) return
        m.waitForIdle(60000)
        check(same(m.sessionDir, probe.sessionDir),
              tag + ": the panel is on today's session (it says '" + m.sessionDir + "')")
        check(m.shotCount === wantShots,
              tag + ": the ledger holds " + wantShots + " shots (it holds " + m.shotCount + ")")
        check(m.cards.length + m.watching.length > 0,
              tag + ": the SESSION / WATCHING tabs have rows (" + m.cards.length + " + " + m.watching.length + ")")
    }

    Instantiator {
        id: live
        model: shotModel
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }
    function maxLiveId() {
        let m = 0
        for (let i = 0; i < live.count; ++i) m = Math.max(m, live.objectAt(i).shotId)
        return m
    }

    property int step: 0
    property int base: 0
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const s = probe.step
            if (s === 1) {
                navController.navigate(2)                 // the Wrist screen: today's swings load
                SessionMode.enterCapture()
            } else if (s === 2) {
                probe.base = live.count
                probe.check(live.count > 0, "relaunched: today's swings are on the carousel (" + live.count + ")")
                const newest = live.objectAt(0)
                SessionMode.enterReplay(newest.shotId, newest.swingDir)   // the carousel's click path
            } else if (s === 3) {
                SessionMode.enterAnalyse()
            } else if (s === 5) {
                probe.state("relaunched", probe.base)
            } else if (s === 6) {
                // PpSessionToolbar._beginSession(true), less the clock and the cameras.
                shotProcessor.beginSessionFolder(SessionController.Wrist, true)
                shotModel.loadSessionDir(shotProcessor.activeSessionDir)
            } else if (s === 8) {
                probe.check(probe.same(shotProcessor.activeSessionDir, probe.sessionDir),
                            "extended: the session resumed in today's folder")
                probe.state("extended", probe.base)
                probe.log("ADD-SWING")                    // the runner copies the new swing in now
            } else if (s === 11) {
                // What ShotProcessor says when a shot's document is on disk: the carousel's own
                // id (its next counter value) and the swing folder.
                const id = probe.maxLiveId() + 1
                probe.log("announcing swing " + probe.newOrdinal + " as carousel id " + id)
                shotProcessor.shotProcessed(id, probe.newSwingDir)
            } else if (s === 13) {
                probe.state("new shot", probe.base + 1)
                const m = probe.model()
                if (m) probe.check(m.shotIdForSwingDir(probe.newSwingDir) === probe.newOrdinal,
                                   "new shot: filed under its swing number " + probe.newOrdinal
                                   + " (it is " + m.shotIdForSwingDir(probe.newSwingDir) + ")")
            } else if (s === 14) {
                // The End Session button, less the devices.
                shotProcessor.endSessionFolder()
                shotModel.loadSessionDir(shotProcessor.todaySessionDir(SessionController.Wrist))
            } else if (s === 16) {
                probe.state("ended", probe.base + 1)
                probe.finish()
            }
        }
    }
}
