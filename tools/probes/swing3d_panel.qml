// swing3d_panel.qml — the 3-D swing panel's structure, WITHOUT screenshots
// (docs/design/swing_3d_viz_design.md §8.3).
//
//   QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 \
//     .../PinPointStudio.app/Contents/MacOS/PinPointStudio \
//     --probe-qml /abs/path/tools/probes/swing3d_panel.qml --probe-swing <swing dir with a skeleton3d>
//
// Every line is prefixed "S3DPROBE". It checks, on a PRIVATE host (the on-screen panel exists only
// when the user's persisted View layout turns it on, and a diagnostic must not write that setting):
//   1. the driver loads the swing and says it is available, from how many views;
//   2. the meshes load (20 mannequin segments) — ⚠ offscreen View3D renders nothing; RuntimeLoader may or may
//      not complete without a render loop, and the line says which;
//   3. each preset moves the orbit pivot to its rotation;
//   4. the tier chip's text at three instants;
//   5. REPARENTING: 50 slots created and destroyed around the host — the view is created ONCE and
//      is the same object at the end (the whole point of SwingViz3DHost).
//   7. (--probe-soak <minutes> with --probe-grab <dir>, ON-SCREEN) the View3D blanking watch
//      (view3d-disappearance-watch): scrub the playhead at 30 Hz, change preset every 3 s, move the
//      view to a NEW slot every 10 s, log a heartbeat every minute and grab a frame every 5 —
//      the grabs are judged afterwards (a blank view grabs as one flat colour).
//   6. (--probe-grab <dir>, ON-SCREEN runs only) the view renders itself to PNGs with grabToImage —
//      the app's own framebuffer, not a screen capture — for face-on / DTL / top at address, top
//      and impact. Offscreen, View3D draws nothing and the files are blank.
// Then Qt.quit().

import QtQml
import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent
    // Not visible:false — a hidden window starves Timers (see plumb_bob_chart.qml).

    function _arg(name, dflt) {
        var i = Qt.application.arguments.indexOf(name)
        return (i >= 0 && i + 1 < Qt.application.arguments.length) ? Qt.application.arguments[i + 1] : dflt
    }
    readonly property string swingDir: probe._arg("--probe-swing", "")
    readonly property string grabDir: probe._arg("--probe-grab", "")
    readonly property real   soakMin: parseFloat(probe._arg("--probe-soak", "0"))
    // --probe-strip N (with --probe-grab): N frames evenly across the swing from --probe-preset
    // (default dtl) — a filmstrip for judging motion, not a pose.
    readonly property int    stripN: parseInt(probe._arg("--probe-strip", "0"))
    readonly property string stripPreset: probe._arg("--probe-preset", "dtl")

    function say(s) { console.warn("S3DPROBE " + s) }

    Item { id: holderA; width: 720; height: 540 }

    SwingViz3DHost {
        id: host
        swingDir: probe.swingDir
        positionUs: 0
    }

    Component { id: slotComp; Item { width: 720; height: 540 } }

    property int step: 0
    property var firstView: null
    property int slotsMade: 0

    Timer {
        id: tick
        interval: 250
        repeat: true
        running: true
        onTriggered: probe.advance()
    }

    function advance() {
        step += 1
        if (step === 1) {
            if (probe.swingDir === "") { say("FAIL no --probe-swing given"); Qt.quit(); return }
            var s = slotComp.createObject(holderA)
            host.attach(s)
            probe.firstView = host.view
            say("host attached, view created: " + (host.view !== null))
            return
        }
        var v = host.view
        if (!v) { say("FAIL no view"); Qt.quit(); return }
        var d = v.driver
        if (step < 40 && (d.loading || !d.available)) {
            if (step % 8 === 0) say("waiting: loading=" + d.loading + " reason=" + d.reason)
            return
        }
        if (step === 40 && !d.available) { say("FAIL not available: " + d.reason); Qt.quit(); return }
        if (probe._reported) return
        probe._reported = true
        say("available=" + d.available + " twoViews=" + d.twoViews + " joints=" + d.jointCount
            + " span=" + d.startUs + ".." + d.endUs + " club=" + d.clubLengthM.toFixed(3))
        say("hull ready=" + v.hullGeometry.ready + " shown=" + v.hullShown + " vertices=" + v.hullGeometry.vertexCount
            + " bindMismatch=" + v.hullGeometry.bindMismatchM.toFixed(6) + " m" + (v.hullGeometry.error ? " error=" + v.hullGeometry.error : ""))
        say("segments loaded=" + v.segmentsLoaded + " of 20 (offscreen: a render loop may be needed)")
        // Presets.
        var keys = ["faceOn", "dtl", "top", "target", "behind"]
        for (var i = 0; i < keys.length; ++i) {
            var want = v.presetRotation(keys[i])
            v.applyPreset(keys[i])
            say("preset " + keys[i] + " → pivot target " + want.x.toFixed(0) + "," + want.y.toFixed(0))
        }
        // The tier chip at three instants.
        var span = d.endUs - d.startUs
        var ts = [d.startUs, d.startUs + span / 2, d.endUs]
        for (var k = 0; k < ts.length; ++k) {
            host.positionUs = ts[k]
            say("t=" + Math.round(ts[k]) + " tier=" + d.frameTier + " \"" + d.frameTierText + "\" root=("
                + d.rootPosition.x.toFixed(3) + "," + d.rootPosition.y.toFixed(3) + "," + d.rootPosition.z.toFixed(3)
                + ") shaftTier=" + d.shaftTier + " ball=" + d.ballVisible)
        }
        // Reparenting stress.
        for (var n = 0; n < 50; ++n) {
            var s2 = slotComp.createObject(holderA)
            host.attach(s2)
            host.detach(s2)
            s2.destroy()
            probe.slotsMade += 1
        }
        Qt.callLater(function() {
            say("reparent x" + probe.slotsMade + ": same view=" + (host.view === probe.firstView)
                + " view parent is host=" + (host.view.parent !== null))
            if (probe.soakMin > 0 && probe.grabDir !== "") {
                probe._soakSlot = slotComp.createObject(holderA)
                host.attach(probe._soakSlot)
                probe._soakStart = Date.now()
                soakTimer.start()
                return
            }
            if (probe.grabDir === "") { say("DONE"); Qt.quit(); return }
            var s3 = slotComp.createObject(holderA)
            host.attach(s3)
            probe._grabs = []
            if (probe.stripN > 0) {
                for (var k2 = 0; k2 < probe.stripN; ++k2) {
                    var tt = d.startUs + span * k2 / (probe.stripN - 1)
                    probe._grabs.push({ preset: probe.stripPreset, name: "strip" + (k2 < 10 ? "0" : "") + k2, t: tt })
                }
                grabTimer.start()
                return
            }
            var pr = ["faceOn", "dtl", "top"]
            var at = [["address", d.startUs + 150000], ["top", d.startUs + span * 0.5], ["impact", d.startUs + span * 0.62]]
            for (var a = 0; a < at.length; ++a)
                for (var q = 0; q < pr.length; ++q) probe._grabs.push({ preset: pr[q], name: at[a][0], t: at[a][1] })
            grabTimer.start()
        })
    }
    property var _soakSlot: null
    property real _soakStart: 0
    property int _soakTick: 0
    Timer {
        id: soakTimer
        interval: 33
        repeat: true
        onTriggered: {
            var v = host.view, d = v.driver
            probe._soakTick += 1
            var span = Math.max(1, d.endUs - d.startUs)
            host.positionUs = d.startUs + ((probe._soakTick * 33000) % span)
            if (probe._soakTick % 90 === 0) {
                var keys = ["faceOn", "dtl", "top", "target", "behind"]
                v.applyPreset(keys[(probe._soakTick / 90) % keys.length])
            }
            if (probe._soakTick % 300 === 0) {
                var fresh = slotComp.createObject(holderA)
                var old = probe._soakSlot
                host.attach(fresh)
                host.detach(old)
                old.destroy()
                probe._soakSlot = fresh
            }
            var minutes = (Date.now() - probe._soakStart) / 60000
            if (probe._soakTick % 1800 === 0)
                probe.say("soak " + minutes.toFixed(1) + " min: same view=" + (host.view === probe.firstView)
                          + " available=" + d.available + " revision=" + d.revision + " ticks=" + probe._soakTick)
            if (probe._soakTick % 9000 === 1) {
                var f = probe.grabDir + "/soak_" + Math.round(minutes) + "min.png"
                v.grabToImage(function(res) { res.saveToFile(f); probe.say("soak grab " + f) })
            }
            if (minutes >= probe.soakMin) {
                stop()
                var f2 = probe.grabDir + "/soak_end.png"
                v.grabToImage(function(res) { res.saveToFile(f2); probe.say("soak grab " + f2); probe.say("DONE"); Qt.quit() })
            }
        }
    }
    property var _grabs: []
    property int _gi: 0
    property bool _armed: false
    Timer {
        id: grabTimer
        interval: 700
        repeat: true
        onTriggered: {
            var v = host.view
            if (probe._gi >= probe._grabs.length) { stop(); say("DONE"); Qt.quit(); return }
            var g = probe._grabs[probe._gi]
            if (!probe._armed) {
                host.positionUs = g.t
                v.applyPreset(g.preset)
                probe._armed = true
                return
            }
            var file = probe.grabDir + "/s3d_" + g.name + "_" + g.preset + ".png"
            v.grabToImage(function(res) { res.saveToFile(file); probe.say("grab " + file) })
            probe._armed = false
            probe._gi += 1
        }
    }
    property bool _reported: false
}
