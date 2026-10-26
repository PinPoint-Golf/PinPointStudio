// RENDER CHECK: THE TRUNK GUIDE AND THE LIVE TRUNK FIGURE (trunk_imu_sequence_grf_design.md §6.7,
// "Rendering traps": render, don't reason; §16.2 W2.3).
//
// Two modes, both WINDOWED on a visible Space (offscreen View3D draws nothing; an occluded window
// starves the render loop and every Timer):
//
//   frames (default) — the real CalibrationGuide over a real BodyVizView, bound exactly as
//     CalibrateTrunkPage binds them, driven through the routine's own calls: stand tall, crossed
//     arms, turn away (mid, end), back, turn through (mid, end), and a left-hander's turn away. Then
//     a second BodyVizView bound as CheckTrunkPage binds it, fed synthetic sensor q_anat: turned
//     away, an address bend, and a twisted sensor's side lean. Each frame is saved as a PNG and
//     logged with the nodes' scene rotations and the chest's facing direction (the sign check).
//   soak (--probe-soak-s N) — the live figure alone for N seconds under synthetic motion (a turn
//     back and through with a bend, 60 Hz), a PNG every --probe-period-s (30) s. Judge the PNGs
//     afterwards (a blank view is a uniform image).
//
//   XDG_CONFIG_HOME=<copy> PINPOINT_LOG_STDERR=1 <PinPointStudio> --probe-qml <abs path to this file> \
//     --probe-out <dir> [--probe-soak-s 390] [--probe-period-s 30]
// build/run-me/w2-renders/run.sh runs both under a memory guard.
// "PROBE:" lines; "RESULT PASS" / "RESULT FAIL (n)" at the end; exit 0 / 1.

import QtQuick
import PinPointStudio

Item {
    id: probe
    anchors.fill: parent
    z: 10000

    function arg(name, dflt) {
        const a = Qt.application.arguments
        const i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string outDir:   arg("--probe-out", "")
    readonly property int    soakMs:   parseInt(arg("--probe-soak-s", "0")) * 1000
    readonly property int    periodMs: parseInt(arg("--probe-period-s", "30")) * 1000
    // Iteration aid for the crossed-arm constants: 16 numbers (left arm, left forearm, right arm,
    // right forearm, w x y z each) used in place of BodyVizView's; the run stops after the turn away.
    readonly property var    armsArg:  arg("--probe-arms", "").split(",").filter(function(x) { return x !== "" }).map(Number)
    readonly property bool   armsTry:  armsArg.length === 16
    function armQ(i) { return Qt.quaternion(armsArg[4 * i], armsArg[4 * i + 1], armsArg[4 * i + 2], armsArg[4 * i + 3]) }

    function log(s) { console.warn("PROBE: " + s) }
    property int failures: 0
    function check(ok, what) {
        if (!ok) { log("FAIL " + what); failures += 1 }
        return ok
    }
    function finish() {
        log(failures === 0 ? "RESULT PASS" : ("RESULT FAIL (" + failures + ")"))
        Qt.exit(failures === 0 ? 0 : 1)
    }

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    // ── The guide, as CalibrateTrunkPage binds it ───────────────────────────
    CalibrationGuide { id: guide; active: true }
    property bool rightHanded: true

    Component {
        id: guideViewComp
        BodyVizView {
            id: bvv
            poseSource:  null
            rightHanded: probe.rightHanded
            Component.onCompleted: guide.view = bvv
            useTrunkOverride:       true
            hipsOverrideRotation:   guide.hipsTarget
            thoraxOverrideRotation: guide.thoraxTarget
            animateTrunk:           guide.trunkAnimating
            useLeadArmOverride:           true
            leadArmOverrideRotation:      guide.armsCrossed ? (probe.armsTry ? probe.armQ(0) : bvv.crossedLeadArmQuat)     : guide.armTarget
            leadForeArmOverrideRotation:  guide.armsCrossed ? (probe.armsTry ? probe.armQ(1) : bvv.crossedLeadForeArmQuat) : Qt.quaternion(1, 0, 0, 0)
            useTrailArmOverride:          true
            trailArmOverrideRotation:     guide.armsCrossed ? (probe.armsTry ? probe.armQ(2) : bvv.crossedTrailArmQuat)     : Qt.quaternion(0.7071, 0.7071, 0, 0)
            trailForeArmOverrideRotation: guide.armsCrossed ? (probe.armsTry ? probe.armQ(3) : bvv.crossedTrailForeArmQuat) : Qt.quaternion(1, 0, 0, 0)
            animateLeadArm:               false
        }
    }

    // ── The live figure, as CheckTrunkPage binds it, over synthetic sensors ──
    QtObject {
        id: sensors
        property quaternion pelvis: Qt.quaternion(1, 0, 0, 0)
        property quaternion thorax: Qt.quaternion(1, 0, 0, 0)
    }
    Component {
        id: liveViewComp
        BodyVizView {
            poseSource:  null
            rightHanded: probe.rightHanded
            useTrunkOverride:       true
            hipsOverrideRotation:   sensors.pelvis
            thoraxOverrideRotation: sensors.thorax
            useLeadArmOverride:           true
            leadArmOverrideRotation:      Qt.quaternion(0.7071, 0.7071, 0, 0)
            leadForeArmOverrideRotation:  Qt.quaternion(1, 0, 0, 0)
            useTrailArmOverride:          true
            trailArmOverrideRotation:     Qt.quaternion(0.7071, 0.7071, 0, 0)
            trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)
        }
    }

    Rectangle {
        id: frame
        width: 640; height: 760
        anchors.centerIn: parent
        color: Theme.colorBg
        Loader {
            id: stage
            width: 640; height: 720
            sourceComponent: probe.soakMs > 0 ? liveViewComp : guideViewComp
        }
        Text {
            anchors { top: stage.bottom; horizontalCenter: stage.horizontalCenter; topMargin: 6 }
            text: probe.caption
            color: Theme.colorText
            font.pixelSize: 16
        }
    }
    property string caption: ""

    // q about a trunk-world axis, degrees.
    function qa(ax, ay, az, deg) {
        const h = deg * Math.PI / 360, s = Math.sin(h)
        return Qt.quaternion(Math.cos(h), ax * s, ay * s, az * s)
    }
    function fmt(v) { return "(" + [v.x, v.y, v.z].map(function(x) { return x.toFixed(3) }).join(", ") + ")" }
    function fmtQ(q) { return "(" + [q.scalar, q.x, q.y, q.z].map(function(x) { return x.toFixed(4) }).join(", ") + ")" }
    // The nodes, found by their GLB.
    function node(view, glb) {
        const stack = [view], seen = new Set()
        while (stack.length) {
            const o = stack.pop()
            if (!o || seen.has(o)) continue
            seen.add(o)
            if (o.source !== undefined && String(o.source).endsWith(glb) && o.parent && o.parent.sceneRotation !== undefined)
                return o.parent
            for (const list of [o.children, o.data, o.resources])
                if (list) for (let i = 0; i < list.length; ++i) stack.push(list[i])
            if (o.scene !== undefined && o.scene !== null && typeof o.scene === "object") stack.push(o.scene)
        }
        return null
    }
    // The chest's facing (Spine2's scene rotation applied to its rest-frame forward) and the head.
    function describe(view, tag) {
        const sp2 = node(view, "body_Spine2.glb"), hips = node(view, "body_Hips.glb"), head = node(view, "body_Head.glb")
        if (!sp2 || !hips || !head) { check(false, tag + ": nodes not found"); return {} }
        const restSp2 = Qt.quaternion(0.9983, 0.0577, 0, 0).times(Qt.quaternion(0.9982, -0.0607, 0, 0))  // ≈ its rest world
        const chestFwd = sp2.sceneRotation.times(restSp2.conjugated()).times(Qt.vector3d(0, 0, 1))
        const hipsFwd  = hips.sceneRotation.times(Qt.vector3d(0, 0, 1))
        const hp = head.scenePosition
        const out = { chestFwd: Qt.vector3d(chestFwd.x, chestFwd.y, chestFwd.z), hipsFwd: Qt.vector3d(hipsFwd.x, hipsFwd.y, hipsFwd.z),
                      head: Qt.vector3d(hp.x, hp.y, hp.z) }
        log(tag + " | hips fwd " + fmt(hipsFwd) + " | chest fwd " + fmt(chestFwd) + " | head " + fmt(head.scenePosition)
            + " | hips q " + fmtQ(hips.sceneRotation))
        return out
    }
    // Heading of a scene forward vector, degrees, 0 = facing the camera, + = toward scene +X.
    function heading(v) { return Math.atan2(v.x, v.z) * 180 / Math.PI }

    property var shots: []
    property int pendingGrabs: 0
    function shoot(name, then) {
        caption = name
        pendingGrabs += 1
        // Let the caption and the pose reach a frame before the grab.
        Qt.callLater(function() {
            frame.grabToImage(function(r) {
                if (outDir !== "") r.saveToFile(outDir + "/" + name + ".png")
                log("saved " + name + ".png")
                pendingGrabs -= 1
                if (then) then()
            })
        })
    }

    // ── The frames sequence (a step list, one step at a time) ──────────────
    property int  stepIx: 0
    property var  v: stage.item
    property double t0: Date.now()
    property var  rec: ({})
    property bool waiting: false

    Connections {
        target: guide
        function onFinished() { probe.guideFinished = true }
    }
    property bool guideFinished: false

    function framesStep() {
        if (waiting) return
        const now = Date.now()
        const view = stage.item
        switch (stepIx) {
        case 0:
            if (!view || !view.fullyLoaded) { if (now - t0 > 60000) { check(false, "the guide never loaded"); finish() } return }
            if (now - t0 < 2500) return
            guide.trunkPose(0, 0, false)
            stepIx = 1; hold(800); return
        case 1:
            rec.stand = describe(view, "stand tall")
            check(Math.abs(heading(rec.stand.chestFwd)) < 2, "stand tall: the chest faces the camera")
            waiting = true; shoot("01_stand_tall", function() { waiting = false; stepIx = 2 }); return
        case 2:
            guide.trunkPose(0, 0, true)
            stepIx = 3; hold(800); return
        case 3:
            waiting = true; shoot("02_crossed_arms", function() { waiting = false; stepIx = 4 }); return
        case 4:
            guideFinished = false
            guide.trunkAnimate(45, 90, true, 3000)
            stepIx = 5; return
        case 5:
            if (view._trunkP < 0.5) return
            rec.awayMid = describe(view, "away mid (p " + view._trunkP.toFixed(2) + ")")
            waiting = true; shoot("03_turn_away_mid", function() { waiting = false; stepIx = 6 }); return
        case 6:
            if (!guideFinished) return
            stepIx = 7; hold(500); return
        case 7:
            rec.awayEnd = describe(view, "away end")
            // Right-hander turned away: the chest turns to face the golfer's RIGHT — scene −X.
            check(Math.abs(heading(rec.awayEnd.chestFwd) + 90) < 3, "away end: chest heading −90° (" + heading(rec.awayEnd.chestFwd).toFixed(1) + ")")
            check(Math.abs(heading(rec.awayEnd.hipsFwd) + 45) < 3, "away end: hips heading −45° (" + heading(rec.awayEnd.hipsFwd).toFixed(1) + ")")
            check(heading(rec.awayMid.chestFwd) < -10 && heading(rec.awayMid.chestFwd) > -80, "away mid: between")
            waiting = true; shoot("04_turn_away_end", function() { waiting = false; stepIx = 8 }); return
        case 8:
            if (armsTry) { finish(); return }
            guideFinished = false
            guide.trunkAnimate(0, 0, true, 3000)
            stepIx = 9; return
        case 9:
            if (!guideFinished) return
            rec.back = describe(view, "back")
            check(Math.abs(heading(rec.back.chestFwd)) < 2, "back: the chest faces the camera again")
            guideFinished = false
            guide.trunkPose(0, 0, true)
            guide.trunkAnimate(-45, -90, true, 3000)
            stepIx = 10; return
        case 10:
            if (view._trunkP < 0.5) return
            rec.thrMid = describe(view, "through mid (p " + view._trunkP.toFixed(2) + ")")
            waiting = true; shoot("05_turn_through_mid", function() { waiting = false; stepIx = 11 }); return
        case 11:
            if (!guideFinished) return
            stepIx = 12; hold(500); return
        case 12:
            rec.thrEnd = describe(view, "through end")
            check(Math.abs(heading(rec.thrEnd.chestFwd) - 90) < 3, "through end: chest heading +90° (" + heading(rec.thrEnd.chestFwd).toFixed(1) + ")")
            waiting = true; shoot("06_turn_through_end", function() { waiting = false; stepIx = 13 }); return
        case 13:
            // A left-hander turned away: the mirror.
            probe.rightHanded = false
            guide.trunkPose(0, 0, true)
            guideFinished = false
            guide.trunkAnimate(45, 90, true, 1500)
            stepIx = 14; return
        case 14:
            if (!guideFinished) return
            stepIx = 15; hold(500); return
        case 15:
            rec.lh = describe(view, "left-hander away end")
            check(Math.abs(heading(rec.lh.chestFwd) - 90) < 3, "LH away end: chest heading +90° (" + heading(rec.lh.chestFwd).toFixed(1) + ")")
            waiting = true; shoot("07_left_hander_turn_away_end", function() { waiting = false; stepIx = 16 }); return
        case 16:
            // cancel() mid-motion: held, no finished().
            probe.rightHanded = true
            guide.trunkPose(0, 0, true)
            guideFinished = false
            guide.trunkAnimate(45, 90, true, 2000)
            stepIx = 17; return
        case 17:
            if (view._trunkP < 0.3) return
            guide.cancel()
            rec.cancelP = view._trunkP
            stepIx = 18; hold(2600); return
        case 18:
            check(!guideFinished, "cancel(): no finished()")
            check(view._trunkP === rec.cancelP, "cancel(): the motion held (" + view._trunkP + " vs " + rec.cancelP + ")")
            log("cancel held at p " + rec.cancelP.toFixed(2) + ", finished " + guideFinished)
            // ── The live figure ──
            stage.sourceComponent = liveViewComp
            t0 = now
            stepIx = 19; return
        case 19:
            if (!view || !view.fullyLoaded || view.useTrunkOverride === undefined || view.animateTrunk === undefined) return
            if (now - t0 < 2500) return
            sensors.pelvis = qa(0, 0, 1, -45)            // a right-hander turned away: hips 45°, chest 88°
            sensors.thorax = qa(0, 0, 1, -88)
            stepIx = 20; hold(600); return
        case 20:
            rec.live = describe(view, "live away 45/88")
            check(Math.abs(heading(rec.live.chestFwd) + 88) < 3, "live: the chest turned away (" + heading(rec.live.chestFwd).toFixed(1) + ")")
            waiting = true; shoot("08_live_turned_away", function() { waiting = false; stepIx = 21 }); return
        case 21:
            sensors.pelvis = qa(1, 0, 0, -25)            // address: a forward bend at the hips
            sensors.thorax = qa(1, 0, 0, -35)
            stepIx = 22; hold(600); return
        case 22:
            rec.bend = describe(view, "live address bend")
            check(rec.bend.head.z > rec.stand.head.z + 0.2, "live bend: the head goes FORWARD (toward the camera)")
            waiting = true; shoot("09_live_address_bend", function() { waiting = false; stepIx = 23 }); return
        case 23:
            // The golfer bends straight forward; the chest sensor, twisted 25° in its pocket, reads a
            // side lean as part of it.
            sensors.pelvis = qa(1, 0, 0, -25)
            sensors.thorax = qa(1, 0, 0, -35).times(qa(0, 1, 0, 25))
            stepIx = 24; hold(600); return
        case 24:
            rec.lean = describe(view, "live twisted sensor")
            waiting = true; shoot("10_live_twisted_sensor_side_lean", function() { waiting = false; stepIx = 25 }); return
        case 25:
            finish(); return
        }
    }
    property double holdUntil: 0
    function hold(ms) { holdUntil = Date.now() + ms }

    // ── The soak ─────────────────────────────────────────────────────────────
    property double soakStart: 0
    property double lastGrab: 0
    property int    cycle: 0
    function soakStep() {
        const now = Date.now(), view = stage.item
        if (soakStart === 0) {
            if (!view || !view.fullyLoaded) { if (now - t0 > 60000) { check(false, "never loaded"); finish() } return }
            soakStart = now; lastGrab = now
            log("soaking " + soakMs / 1000 + " s, a grab every " + periodMs / 1000 + " s")
            return
        }
        // Synthetic motion: address bend, a turn back and through (period 6 s).
        const t = (now - soakStart) / 1000
        const turn = Math.sin(2 * Math.PI * t / 6)
        sensors.pelvis = qa(0, 0, 1, -45 * turn).times(qa(1, 0, 0, -25))
        sensors.thorax = qa(0, 0, 1, -90 * turn).times(qa(1, 0, 0, -35))
        if (waiting) return
        if (now - lastGrab >= periodMs || now - soakStart >= soakMs) {
            // A hidden or occluded window starves the render loop and every Timer: the grab comes
            // late and the soak measured nothing in between. Say so rather than pass.
            check(now - lastGrab < periodMs * 1.5,
                  "cycle " + (cycle + 1) + ": " + Math.round((now - lastGrab) / 1000) + " s since the last grab — "
                  + "the render loop was starved (window hidden or occluded?); this soak is not valid")
            cycle += 1
            lastGrab = now
            const done = now - soakStart >= soakMs
            waiting = true
            shoot("soak_" + ("0" + cycle).slice(-2), function() {
                waiting = false
                log("cycle " + cycle + " t+" + Math.round((Date.now() - soakStart) / 1000) + " s, turn " + turn.toFixed(2))
                if (done) finish()
            })
        }
    }

    Timer {
        interval: 16; running: true; repeat: true
        onTriggered: {
            if (Date.now() < probe.holdUntil) return
            if (probe.soakMs > 0) probe.soakStep()
            else probe.framesStep()
        }
    }
}
