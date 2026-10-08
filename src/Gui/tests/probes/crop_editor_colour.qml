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

// END-TO-END: THE CROP EDITOR SHOWS A RAW-BAYER CAMERA IN COLOUR, AND THE CROP BOX SITS ON
// THE PICTURE.
//
// Opens Settings ▸ Cameras, opens the crop editor on the first raw-Bayer (Spinnaker) camera the
// way its button does (capture stopped, camera disconnected, openRoiIndex set), waits for the
// preview to stream, and saves the preview rectangle as a PNG next to --probe-out. Checks the
// preview instance demosaics (needsDebayer) and that a colour item, not the greyscale video
// output, is what shows; and that the picture fills the rectangle the crop box is drawn in.
//
//   PinPointStudio --probe-qml <this file> --probe-out <dir>
// Exit 0 = PASS, 1 = FAIL; "PROBE:" lines say why. The PNG is for a human (or Claude) to look at.

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
    readonly property string outDir: arg("--probe-out", ".")
    readonly property int waitMs: 15000
    property int failures: 0

    function log(s) { console.log("PROBE: " + s) }
    function check(ok, what) {
        log((ok ? "PASS " : "FAIL ") + what)
        if (!ok) failures += 1
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }
    function findAll(item, pred, out) {
        out = out || []
        if (!item) return out
        if (pred(item)) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) findAll(kids[i], pred, out)
        return out
    }
    function rootItem() { let t = probe; while (t.parent) t = t.parent; return t }

    property double startedMs: Date.now()
    property double streamSinceMs: 0
    property int stage: 0
    property var panel: null
    property var cam: null
    property var preview: null

    Timer {
        id: tick
        interval: 250; running: true; repeat: true
        onTriggered: probe.step()
    }

    function fail(what) { check(false, what); tick.running = false; finish() }

    function step() {
        const elapsed = Date.now() - startedMs
        if (stage === 0) {
            const list = cameraManager.cameraList
            for (let i = 0; i < list.length; ++i)
                if (list[i].impactCapable) { cam = list[i]; break }
            if (!cam) { if (elapsed < waitMs) return; fail("a raw-Bayer (impact-capable) camera is enumerated"); return }
            log("camera " + cam.description + " " + cam.serialNumber + " index " + cam.index)
            navController.navigateRail(9)   // Main.qml screenSettings
            stage = 1
            return
        }
        if (stage === 1) {
            const s = findAll(rootItem(), function(o) {
                return o.activeNavIndex !== undefined && o.navigateToResult !== undefined
            })
            if (s.length === 0) { if (elapsed < waitMs + 5000) return; fail("the Settings screen exists"); return }
            s[0].activeNavIndex = 3   // Cameras
            stage = 2
            return
        }
        if (stage === 2) {
            const p = findAll(rootItem(), function(o) {
                return o.openRoiIndex !== undefined && o.applyImpactMode !== undefined
            })
            if (p.length === 0) { if (elapsed < waitMs + 8000) return; fail("Settings ▸ Cameras is shown"); return }
            panel = p[0]
            // What the crop button does: stop capture, disconnect, open.
            if (cameraManager.isRecording) cameraManager.stopAll()
            if (cam.selected) cameraManager.setSelected(cam.index, false)
            panel.openRoiIndex = cam.index
            stage = 3
            return
        }
        if (stage === 3) {
            const r = findAll(panel, function(o) {
                return o.roiDragMode !== undefined && o.syncPreview !== undefined && o.visible
            })
            if (r.length === 0) { if (elapsed < waitMs + 12000) return; fail("the crop editor opened"); return }
            preview = r[0]
            stage = 4
            return
        }
        if (stage === 4) {
            const inst = findAll(panel, function(o) { return o.localPreviewInstance !== undefined && o.roiOpen === true })
            const pi = inst.length ? inst[0].localPreviewInstance : null
            if (!pi || pi.cameraFps <= 0) { if (elapsed < waitMs + 20000) return; fail("the preview streams"); return }
            if (streamSinceMs === 0) { streamSinceMs = Date.now(); return }
            if (Date.now() - streamSinceMs < 3000) return   // auto exposure settles
            tick.running = false
            inspect(pi)
        }
    }

    function inspect(pi) {
        log("preview instance fps " + pi.cameraFps.toFixed(1) + ", frame " + pi.frameWidth + "x" + pi.frameHeight
            + ", needsDebayer " + pi.needsDebayer)
        check(pi.needsDebayer, "the preview instance delivers raw Bayer (needsDebayer)")
        const colour = findAll(preview, function(o) { return o.visible && o.toString().indexOf("BayerVideoItem") === 0 })
        check(colour.length === 1, "a BayerVideoItem (GPU demosaic) is what shows (" + colour.length + ")")
        if (colour.length === 1) {
            const c = colour[0]
            log("colour item " + c.width.toFixed(1) + "x" + c.height.toFixed(1) + " in preview "
                + preview.width.toFixed(1) + "x" + preview.height.toFixed(1))
            check(Math.abs(c.width - preview.width) < 2 && Math.abs(c.height - preview.height) < 2,
                  "the picture fills the rectangle the crop box is drawn in")
            const frameAspect = pi.frameWidth / pi.frameHeight, rectAspect = preview.width / preview.height
            check(Math.abs(frameAspect - rectAspect) < 0.02,
                  "the rectangle has the frame's aspect (" + rectAspect.toFixed(3) + " vs " + frameAspect.toFixed(3) + ")")
        }
        const path = outDir + "/crop_editor_" + cam.serialNumber + ".png"
        preview.grabToImage(function(result) {
            const ok = result.saveToFile(path)
            check(ok, "saved " + path)
            finish()
        })
    }
}
