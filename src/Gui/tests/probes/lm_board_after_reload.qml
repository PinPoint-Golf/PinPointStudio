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

// END-TO-END: THE LAUNCH MONITOR BOARD AFTER THE CAROUSEL'S CHEAP RELOAD (#52).
//
// End Session, app start, an athlete change and entering the Wrist screen all reload the live
// carousel with ShotListModel::loadSessionDir — the summary-only read, which leaves every row's
// analysisDetail empty. The board read its readings from nothing else, so after any of them it
// said there were none, though every shot's document held them. This drives that path and fails
// if the board does not show what is on disk:
//
//   shotModel.loadSessionDir(<session>) → focus shot N → the real PpLaunchMonitorPanel on the
//   live carousel → the board's shots must carry readings for exactly the shots that have them.
//
// --probe-path review does the same through sessionReviewController.loadSession (the full read
// the sessions drawer uses) — the control: that path was never broken.
//
// Run through --probe-qml (dev builds only), offscreen:
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 13 [--probe-expect 13-19] \
//       (an ordinal outside --probe-expect is a shot no device measured: it must carry none)
//       [--probe-path cheap|review] [--probe-png <abs path>]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-lm-board.sh wraps it.

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
    readonly property int ordinal: parseInt(arg("--probe-ordinal", "13"))
    readonly property string path: arg("--probe-path", "cheap")
    readonly property string png: arg("--probe-png", "")
    // The ordinals that carry readings on disk, "13-19" — every other shot must carry none.
    readonly property var expected: {
        const r = arg("--probe-expect", "13-19").split("-").map(x => parseInt(x))
        const out = []
        for (let n = r[0]; n <= (r.length > 1 ? r[1] : r[0]); ++n) out.push(n)
        return out
    }

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

    readonly property var carousel: probe.path === "review" ? sessionReviewController.shots : shotModel
    Instantiator {
        id: shots
        model: probe.carousel
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }

    // The real panel, on whichever list the stage would hand it (sessionReviewController.activeShots).
    Rectangle {
        id: frame
        z: 1000
        width: 1180; height: 760
        color: Theme.colorBg
        Loader {
            id: panelLoader
            anchors.fill: parent
            anchors.margins: 16
            active: false
            sourceComponent: PpLaunchMonitorPanel { }
        }
    }

    property int step: 0
    Timer {
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            if (probe.step === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                if (probe.path === "review")
                    sessionReviewController.loadSession(probe.sessionDir)
                else
                    shotModel.loadSessionDir(probe.sessionDir)   // End Session's reload, verbatim
            } else if (probe.step === 2) {
                probe.check(shots.count > 0, probe.path + " load: " + shots.count + " shots in the carousel")
                probe.check((probe.path === "review") === sessionReviewController.reviewActive,
                            "the panel boards the " + (probe.path === "review" ? "reviewed" : "live") + " list")
                let picked = false
                for (let i = 0; i < shots.count; ++i) {
                    const s = shots.objectAt(i)
                    if (s.ordinal !== probe.ordinal) continue
                    SessionMode.focusedShotId   = s.shotId
                    SessionMode.focusedSwingDir = s.swingDir
                    picked = true
                }
                probe.check(picked, "shot " + probe.ordinal + " is in the carousel")
                panelLoader.active = true
            } else if (probe.step === 3) {
                const panel = panelLoader.item
                probe.check(!!panel, "the launch monitor panel is up")
                if (!panel) { probe.finish(); return }
                const body = panel.graphicsBody
                const g = body.g || {}
                const boardShots = g.shots || []
                log("emptyText: '" + body.emptyText + "'  scope: '" + body.scopeText + "'")
                probe.check(boardShots.length === shots.count,
                            "the board lists every shot (" + boardShots.length + " of " + shots.count + ")")
                const withReadings = []
                for (const s of boardShots)
                    if (Object.keys(s.values || {}).length > 0) withReadings.push(s.number)
                withReadings.sort((a, b) => a - b)
                log("shots with readings: [" + withReadings.join(", ") + "]")
                probe.check(JSON.stringify(withReadings) === JSON.stringify(probe.expected),
                            "readings on exactly shots " + probe.expected[0] + "–" + probe.expected[probe.expected.length - 1])
                const focus = boardShots.filter(s => s.number === probe.ordinal)[0]
                const n = focus ? Object.keys(focus.values || {}).length : 0
                const measured = probe.expected.indexOf(probe.ordinal) >= 0
                probe.check(measured ? n > 0 : n === 0, "shot " + probe.ordinal + " carries " + n + " readings"
                            + (focus && focus.values["lm.ballSpeed"] !== undefined
                               ? " (ball speed " + focus.values["lm.ballSpeed"].toFixed(1) + ")" : ""))
                probe.check(body.emptyText.indexOf("No readings") < 0
                            && body.emptyText.indexOf("No launch monitor readings") < 0,
                            "the board does not say there are no readings")
                if (probe.png !== "") {
                    frame.grabToImage(function (res) { res.saveToFile(probe.png); probe.finish() })
                    return
                }
                probe.finish()
            }
        }
    }
}
