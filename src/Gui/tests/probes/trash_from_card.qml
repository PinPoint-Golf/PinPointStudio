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

// END-TO-END: DELETING SWINGS FROM THE CAROUSEL (#56).
//
// Drives the real app the way a golfer does, on a scratch copy of a session:
//
//   load a session → pick swing N (it goes on the stage, its videos open in the replay) →
//   delete it with the action bar → the stage moves to the next older swing, the replay with it,
//   and the row goes → pick two OTHER swings with their ticks (the stage does not move) → delete
//   them from the bar → they go, and the stage is still where it was → a card's own trash can
//   deletes a swing that was never on the stage.
//
// The runner checks the folders on disk afterwards (QML cannot stat a file). Optional grabs of the
// dock for the review sheets: --probe-png-hover (a card under the pointer) and --probe-png-picked
// (two swings picked, the bar in its selection state).
//
//   QT_QPA_PLATFORM=offscreen PinPointStudio --probe-qml <abs path to this file> \
//       --probe-session <session dir> --probe-ordinal 8 [--probe-png-hover <png>] [--probe-png-picked <png>]
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. build/run-me/verify-trash.sh wraps it.

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
    readonly property string pngHover: arg("--probe-png-hover", "")
    readonly property string pngPicked: arg("--probe-png-picked", "")

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

    // Every item under `item` passing `pred`, through children (visual tree).
    function find(item, pred, out) {
        if (!item) return out
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) find(kids[i], pred, out)
        return out
    }
    function shown(item) {
        let i = item
        while (i) { if (!i.visible) return false; i = i.parent }
        return true
    }
    // The carousel on screen (each mode screen has one; only the current one is visible).
    function carousel() {
        const all = find(probe.Window.window.contentItem,
                         i => typeof i._trash === "function" && i.markedIds !== undefined, [])
        return all.filter(shown)[0] || null
    }
    function cards(c) {
        return find(c, i => i.markToggled !== undefined && i.trashRequested !== undefined, [])
    }
    function cardFor(c, shotId) { return cards(c).filter(k => k.shotId === shotId)[0] || null }

    Instantiator {
        id: shots
        model: sessionReviewController.shots
        delegate: QtObject {
            required property int shotId
            required property int ordinal
            required property string swingDir
        }
    }
    // A COPY: the Instantiator's object dies with its row, and the rows here are being deleted.
    function byOrdinal(n) {
        for (let i = 0; i < shots.count; ++i) {
            const o = shots.objectAt(i)
            if (o && o.ordinal === n) return { shotId: o.shotId, ordinal: o.ordinal, swingDir: o.swingDir }
        }
        return null
    }

    function grab(item, path, then) {
        if (path === "") { then(); return }
        item.grabToImage(function (res) { res.saveToFile(path); probe.log("grabbed " + path); then() })
    }

    property int step: 0
    property var picked: null        // the swing put on the stage, then deleted
    property var older: null         // the swing the stage should move to
    property var marks: []           // two swings picked with their ticks, then deleted
    property var lone: null          // a swing deleted from its own card, never on the stage
    property int countBefore: 0
    Timer {
        id: ticker
        interval: 2500; repeat: true; running: true
        onTriggered: {
            probe.step += 1
            const c = probe.carousel()
            if (probe.step === 1) {
                if (probe.sessionDir === "") { probe.check(false, "--probe-session given"); probe.finish(); return }
                navController.navigate(2)                 // the Wrist screen
                SessionMode.enterCapture()
                sessionReviewController.loadSession(probe.sessionDir)
            } else if (probe.step === 2) {
                probe.check(sessionReviewController.reviewActive, "the session loaded for review")
                probe.check(!!c, "a carousel is on screen")
                probe.picked = probe.byOrdinal(probe.ordinal)
                probe.older  = probe.byOrdinal(probe.ordinal - 1)
                probe.check(!!probe.picked && !!probe.older,
                            "swings " + probe.ordinal + " and " + (probe.ordinal - 1) + " are in the carousel")
                if (!c || !probe.picked || !probe.older) { probe.finish(); return }
                probe.countBefore = shots.count
                c.expanded = true                          // the strip may be folded (settings are a copy)
                c._toggleShot(probe.picked.shotId, probe.picked.swingDir, null)   // the card's click
            } else if (probe.step === 3) {
                probe.check(SessionMode.focusedShotId === probe.picked.shotId, "the picked swing is on the stage")
                probe.check(shotReplay.active && shotReplay.swingDir === probe.picked.swingDir,
                            "…and its videos are open in the replay")
                if (probe.pngHover !== "") {
                    // A card under the pointer: its pick tick and trash can show.
                    const k = probe.cardFor(c, probe.older.shotId)
                    if (k) k.hovered = true
                }
                probe.grab(c, probe.pngHover, function () {
                    const k = probe.cardFor(c, probe.older.shotId)
                    if (k) k.hovered = false
                    c._trash([SessionMode.focusedShotId])                    // the bar's Move to trash
                })
            } else if (probe.step === 4) {
                probe.check(shots.count === probe.countBefore - 1,
                            "the swing on the stage was deleted (" + probe.countBefore + " → " + shots.count + ")")
                probe.check(sessionReviewController.shots.shotSummary(probe.picked.shotId).valid === false,
                            "…its row is gone")
                probe.check(SessionMode.focusedShotId === probe.older.shotId,
                            "the stage moved to the next older swing (#" + probe.older.ordinal + ")")
                probe.check(SessionMode.mode === SessionMode.replay, "…without leaving Replay")
                probe.check(shotReplay.swingDir === probe.older.swingDir, "…and the replay went with it")
                probe.log("TRASHED " + probe.picked.swingDir)
                // Pick two others with their ticks: neither goes on the stage.
                const a = probe.byOrdinal(probe.ordinal - 2), b = probe.byOrdinal(probe.ordinal - 3)
                probe.check(!!a && !!b, "two more swings to pick")
                if (!a || !b || !probe.cardFor(c, a.shotId) || !probe.cardFor(c, b.shotId)) {
                    probe.check(false, "their cards are drawn (" + probe.cards(c).length + " cards)")
                    probe.finish(); return
                }
                probe.marks = [a, b]
                probe.cardFor(c, a.shotId).markToggled()
                probe.cardFor(c, b.shotId).markToggled()
            } else if (probe.step === 5) {
                probe.check(c.markedIds.length === 2, "two swings are picked")
                probe.check(SessionMode.focusedShotId === probe.older.shotId, "…and the stage did not move")
                const rows = probe.find(c, i => i.objectName === "markRow", [])
                probe.check(rows.length === 1 && probe.shown(rows[0]), "the bar shows the selection")
                const btn = probe.find(c, i => i.objectName === "trashMarkedButton", [])[0]
                probe.check(!!btn && probe.shown(btn) && btn.label === qsTr("Move %1 to trash").arg(2),
                            "…with 'Move 2 to trash' (" + (btn ? btn.label : "none") + ")")
                probe.grab(c, probe.pngPicked, function () {
                    probe.countBefore = shots.count
                    btn.clicked()
                })
            } else if (probe.step === 6) {
                probe.check(shots.count === probe.countBefore - 2, "both picked swings were deleted")
                probe.check(c.markedIds.length === 0, "…and the selection is empty")
                probe.check(SessionMode.focusedShotId === probe.older.shotId, "…and the stage is still where it was")
                for (const m of probe.marks) probe.log("TRASHED " + m.swingDir)
                // A swing deleted from its own card's trash can, never on the stage.
                probe.lone = probe.byOrdinal(probe.ordinal + 1)       // the newest: first in the strip
                probe.check(!!probe.lone, "a swing for the card's trash can")
                if (!probe.lone || !probe.cardFor(c, probe.lone.shotId)) {
                    probe.check(false, "its card is drawn"); probe.finish(); return
                }
                probe.countBefore = shots.count
                probe.cardFor(c, probe.lone.shotId).trashRequested()
            } else if (probe.step === 7) {
                probe.check(shots.count === probe.countBefore - 1, "the card's trash can deleted its swing")
                probe.check(SessionMode.focusedShotId === probe.older.shotId, "…and the stage did not move")
                probe.log("TRASHED " + probe.lone.swingDir)
                probe.log("KEPT " + probe.older.swingDir)
                probe.finish()
            }
        }
    }
}
