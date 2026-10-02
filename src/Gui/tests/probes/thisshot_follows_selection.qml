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

// END-TO-END: THIS SHOT FOLLOWS THE CAROUSEL INTO ANALYSE.
//
// The qml_ui suite presses PpSessionDiagnosticsBody with fixtures; it cannot see the PANEL's
// wiring, and that is where this bug lived three times over: the selection passed by the wrong
// id, the readout fetched only in review, and finally the readout's first fetch lost because
// the panel is built on entering Analyse and resolves the pick before its Connections exist.
// So this drives the real app the way a golfer does and fails if THIS SHOT is not reading the
// picked swing:
//
//   load a session → pick shot N with the carousel's own click path → enter Analyse →
//   the Session diagnostics panel must be on THIS SHOT, reading shot N, with cells drawn.
//
// Run through --probe-qml (dev builds only), offscreen:
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 8 [--probe-png <abs path>]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-thisshot.sh wraps it.

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
    readonly property string png: arg("--probe-png", "")

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

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    property int step: 0
    property string pickedDir: ""
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            if (probe.step === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                navController.navigate(2)                 // the Wrist screen
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                probe.check(sessionReviewController.reviewActive, "the session loaded for review")
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal === probe.ordinal) {
                        probe.pickedDir = s.swingDir
                        SessionMode.enterReplay(s.shotId, s.swingDir)   // the carousel's click path
                    }
                }
                probe.check(probe.pickedDir !== "", "shot " + probe.ordinal + " is in the carousel")
            } else if (probe.step === 3) {
                SessionMode.enterAnalyse()
            } else if (probe.step === 4) {
                const bodies = []
                probe.walk(probe.Window.window.contentItem, "sdBody", bodies)
                probe.check(bodies.length === 1, "one Session diagnostics panel is up in Analyse")
                if (bodies.length !== 1) { probe.finish(); return }
                const body = bodies[0]
                const r = body.readout
                probe.check(!!r && !!r.conditions && r.conditions.length > 0,
                            "the panel holds a readout for the picked shot")
                probe.check(!!r && r.shotIndex === probe.ordinal - 1,
                            "…for shot " + probe.ordinal + " (shotIndex " + (r ? r.shotIndex : "-") + ")")
                probe.check(body._tab === "shot", "THIS SHOT is the tab on screen")
                probe.check(body.readingShot === true, "the body is reading a shot")
                const strip = [], cells = [], empty = [], label = []
                probe.walk(body, "sdReviewStrip", strip)
                probe.walk(body, "sdReviewCell", cells)
                probe.walk(body, "sdShotEmpty", empty)
                probe.walk(body, "sdReviewStripLabel", label)
                probe.check(strip.length === 1 && strip[0].visible, "the shot strip is drawn")
                probe.check(cells.length > 0, "with " + cells.length + " condition cells")
                probe.check(empty.length === 1 && !empty[0].visible, "and no 'pick a shot' message")
                probe.check(label.length === 1 && label[0].text.indexOf("SHOT " + probe.ordinal + " OF") === 0,
                            "the strip names the shot: '" + (label[0] ? label[0].text : "") + "'")
                if (probe.png !== "") {
                    body.grabToImage(function (res) { res.saveToFile(probe.png); probe.finish() })
                    return
                }
                probe.finish()
            }
        }
    }
}
