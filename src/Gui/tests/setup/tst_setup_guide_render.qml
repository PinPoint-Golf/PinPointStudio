// Stage 5e — render checks of the calibration guide and the Check view (design §7.4 RS1/RS2),
// in the TEST BINARY: the real BodyVizView / ArmVizView, rendered offscreen through the RHI
// backend (QT_QUICK_BACKEND=rhi), grabbed from the window (harness.grabItem — grabToImage never
// called back for a View3D, here or in the real app), and measured in C++ by the harness
// (imageStats / imageDiff — not a QML Canvas readback, which never called back for a grab url in
// the real app). No window, no hardware.
//
// Why: the HackMotion guide once shipped with the WRONG motion (the upper arm swung) while its
// numeric self-check passed; and the six-minute soak runs with no sensors, so it only ever shows
// the Witmotion intro. These cases look at the frames AND at the scene.
//
//   RS2  HackMotion, real pace, through session setup over a fake wG3 (autoScript): grabs at pose
//        0, mid-raise, end of raise and after the return. Rendered (std ≫ a blank control); the
//        forearm moved (pose 0 vs end of raise, in the forearm region and as the forearm node's
//        rotation — by the guide's own hmRaiseDeg); the upper arm did not (its override rotation
//        and its node's scene rotation constant across all four); the return matches pose 0; the
//        guide camera is the HackMotion one.
//   RS1  Witmotion, real pace, fake Witmotions held still: arm-down, T-pose, done. Rendered;
//        arm-down vs T-pose differ in the arm region; the override rotation is the routine's
//        arm-down then T-pose target; the frontal camera.
//   RC   The Check view over a calibrated wG3: rendered; turning the palm unit by a known angle
//        changes the frame (the live arm follows the sensor); liveWrist active only on Check.
// Every grab also asserts ONE View3D alive under the shell; cleanup() fails on any unexpected
// warning.
//
// To keep the grabs: SETUP_GUIDE_GRABS=<dir> (harness.grabDir()); by default nothing is saved.
import QtQuick
import QtTest
import PinPointStudio
import "support"

Item {
    id: root
    width: 1400
    height: 900

    CalibDriver { id: drv; anchors.fill: parent; tc: tcase }

    // The blank control, grabbed in the same pass as each view: a flat fill shows no structure.
    // ON TOP, in the bottom-left corner over the footer's hint: a window grab sees what is drawn
    // there, and anything under it would not be blank (no case clicks the footer).
    Rectangle { id: blank; width: 40; height: 40; x: 0; y: root.height - 40; color: Theme.colorBg; z: 1000 }

    TestCase {
        id: tcase
        name: "SetupGuideRender"
        when: windowShown

        // HackMotion library phases (FakeHmDevice).
        readonly property int pAwaitHorizontal: 1
        readonly property int pVerifying: 6

        // Thresholds — see the report for the measured margins.
        readonly property real minRenderStd: 8.0     // a rendered avatar view; the control is ~0
        readonly property real minMoved: 1.5         // mean |Δluma| in the arm region: it moved
        readonly property real maxSame: 0.6          // … and two frames of the same pose match

        // ⚠ NEEDS A REAL WINDOW (needsRender, design §7.1). Measured at Stage 5e: under the offscreen
        // QPA the RHI backend (Metal) builds its QRhi but the window NEVER presents a frame —
        // frameSwapped 0 after 500 ms — so an ItemGrabResult never calls back, and
        // QQuickWindow::grabWindow() crashes (SIGSEGV at 0x30) for want of a drawable. The 3-D views
        // load and their animations run on the animation clock (which is all the calibration
        // cases need), but nothing is drawn. These cases therefore skip offscreen, saying so, and
        // run windowed:  QT_QPA_PLATFORM=cocoa … -input tst_setup_guide_render.qml
        function needsRender() {
            if (Qt.platform.pluginName === "offscreen")
                skip("needsRender: offscreen RHI presents no frame here (frameSwapped 0; grabToImage never calls "
                     + "back; grabWindow crashes) — run windowed: QT_QPA_PLATFORM=cocoa")
        }

        function init() {
            testLog.reset()
            if (Qt.platform.pluginName === "offscreen")
                testLog.expect("^Populating font family aliases took \\d+ ms\\. Replace uses of missing font family \"Sans Serif\"")
            appSettings.imuRoles = ({})
        }
        function cleanup() {
            drv.teardown()
            var w = testLog.takeWarnings()
            compare(w.length, 0, "unexpected warnings (" + w.length + "):\n" + w.join("\n"))
        }

        // ── Helpers ─────────────────────────────────────────────────────────
        function typeName(o) { return String(o).split("(")[0].split("_QML")[0] }
        function view3Ds(under) {
            return drv.findAll(under, function(o) { var n = typeName(o); return n === "View3D" || n === "QQuick3DViewport" })
        }
        // The item as the window shows it (harness.grabItem: grabWindow, cropped). Not
        // grabToImage: its callback never came for a View3D here (Stage 5e).
        function grabOf(item) {
            var img = harness.grabItem(item)
            verify(img !== undefined && img !== null, "the window grab failed")
            return img
        }
        function save(img, name) {
            var dir = harness.grabDir()
            if (dir !== "") verify(harness.saveImage(img, dir + "/" + name + ".png"), "could not save " + name)
        }
        // Grabs the one View3D under the shell, and the blank control; checks both.
        function grabView(tag) {
            var vs = view3Ds(drv.setup)
            compare(vs.length, 1, tag + ": View3Ds alive under the shell")
            var v = grabOf(vs[0])
            var c = grabOf(blank)
            var sv = harness.imageStats(v), sc = harness.imageStats(c)
            console.info("[render] " + tag + " view " + sv.width + "×" + sv.height + " mean " + sv.mean.toFixed(1)
                         + " std " + sv.std.toFixed(2) + " | control std " + sc.std.toFixed(2))
            verify(sv.std > minRenderStd && sv.std > sc.std + minRenderStd,
                   tag + ": the view is not rendered (std " + sv.std.toFixed(2) + ", control " + sc.std.toFixed(2) + ")")
            save(v, tag)
            return v
        }
        function q(x) { return Qt.quaternion(x.scalar, x.x, x.y, x.z) }    // detach a value reference
        // The rotation between two orientations, in degrees. Through the relative quaternion and
        // atan2, not 2·acos(|a·b|): for float quaternions a hair apart acos reads ~0.7° (measured).
        function angleDeg(a, b) {
            var w = a.scalar * b.scalar + a.x * b.x + a.y * b.y + a.z * b.z          // conj(a)·b
            var x = a.scalar * b.x - a.x * b.scalar - a.y * b.z + a.z * b.y
            var y = a.scalar * b.y + a.x * b.z - a.y * b.scalar - a.z * b.x
            var z = a.scalar * b.z - a.x * b.y + a.y * b.x - a.z * b.scalar
            return 2 * Math.atan2(Math.sqrt(x * x + y * y + z * z), Math.abs(w)) * 180 / Math.PI
        }
        function fmtQ(x) { return "(" + [x.scalar, x.x, x.y, x.z].map(function(v) { return v.toFixed(4) }).join(", ") + ")" }
        // The lead arm's two scene nodes (a right-handed golfer leads with the LEFT arm).
        function nodes(bvv) {
            return { upper: harness.segmentNode(bvv, "arm_LeftArm.glb"),
                     fore:  harness.segmentNode(bvv, "arm_LeftForeArm.glb") }
        }
        function diff(a, b, r) { return harness.imageDiff(a, b, r) }

        // The screen regions, as fractions of the guide view (read off saved grabs at Stage 5e:
        // the guide camera frames the lead arm right of centre; the frontal camera the whole body).
        //   hmFore   — where the forearm sweeps (across the chest, hand from the left of the torso
        //              up to the shoulder);
        //   hmUpper  — the lead upper arm alone (the figure's left arm, on the right of the frame,
        //              above the elbow): it must NOT change.
        readonly property rect hmFore:  Qt.rect(0.27, 0.28, 0.30, 0.34)
        readonly property rect hmUpper: Qt.rect(0.545, 0.34, 0.055, 0.11)
        //   wtArm    — the frontal view: where the lead arm is held out at the T-pose (it hangs at
        //              the side, below and left of this, at arm-down);
        //   rcHand   — the Check view (ArmVizView): the lead hand.
        readonly property rect wtArm:   Qt.rect(0.52, 0.26, 0.20, 0.20)
        readonly property rect rcHand:  Qt.rect(0.34, 0.30, 0.12, 0.28)
        readonly property rect whole:   Qt.rect(0, 0, 1, 1)

        // ── RS2 — HackMotion ─────────────────────────────────────────────────
        function test_RS2_hackMotionGuide() {
            needsRender()
            var t0 = drv.now()
            drv.openAtCalibrate("hackmotion")          // the real pace
            var dev = drv.hm
            var bvv = drv.flow._guide.view
            verify(bvv.useGuideCamera, "the HackMotion guide is not on the guide camera")
            var g = drv.flow._guide
            var raiseDeg = g.hmRaiseDeg
            var shots = {}, arm = {}, fore = {}, nd = {}
            function take(tag) {
                arm[tag]  = q(bvv.leadArmOverrideRotation)
                fore[tag] = q(bvv.leadForeArmOverrideRotation)
                nd[tag]   = nodes(bvv)
                shots[tag] = grabView("rs2_" + tag)
                console.info("[render] " + tag + " hmStep " + drv.hmStep() + " phase " + dev.calibrationPhase
                             + " | arm override " + fmtQ(arm[tag]) + " fore override " + fmtQ(fore[tag])
                             + " | upper node scene " + fmtQ(nd[tag].upper.sceneRotation)
                             + " fore node " + fmtQ(nd[tag].fore.rotation) + " t+" + Math.round(drv.now() - t0))
            }
            // Pose 0: the device waits for the horizontal; the guide holds pose 0.
            tryVerify(function() { return dev.calibrationPhase === pAwaitHorizontal }, 10000, "never AWAIT_HORIZONTAL")
            wait(300)
            take("pose0")
            // Mid-raise: about half way through the 3000 ms raise.
            tryVerify(function() { return drv.animStage() === "hmRaise" && bvv._leadArmP >= 0.45 }, 10000, "no raise")
            take("midRaise")
            console.info("[render] midRaise progress " + bvv._leadArmP.toFixed(2))
            // End of the raise: the guide reported finished.
            tryVerify(function() { return drv.animStage() !== "hmRaise" && bvv._leadArmP >= 1 }, 6000, "the raise never finished")
            take("endRaise")
            // After the return (1500 ms), at VERIFYING.
            tryVerify(function() { return drv.animStage() === "hmReturn" }, 10000, "no return")
            tryVerify(function() { return drv.animStage() !== "hmReturn" && bvv._leadArmP >= 1 }, 6000, "the return never finished")
            take("afterReturn")
            verify(dev.calibrationPhase >= pVerifying, "the return ended before VERIFYING (" + dev.calibrationPhase + ")")

            verify(nd.pose0.upper.found && nd.pose0.fore.found, "the lead arm's scene nodes were not found")
            // (c) The upper arm did not move — override and rendered node, all four grabs.
            var tags = ["pose0", "midRaise", "endRaise", "afterReturn"]
            for (var i = 1; i < tags.length; ++i) {
                var da = angleDeg(arm.pose0, arm[tags[i]]), dn = angleDeg(nd.pose0.upper.sceneRotation, nd[tags[i]].upper.sceneRotation)
                console.info("[render] upper arm vs pose0 at " + tags[i] + ": override " + da.toFixed(3) + "°, node " + dn.toFixed(3) + "°")
                verify(da < 0.01, "the upper-arm override moved at " + tags[i] + " by " + da.toFixed(3) + "°")
                verify(dn < 0.05, "the upper-arm node moved at " + tags[i] + " by " + dn.toFixed(3) + "°")
            }
            // (b) The forearm moved by the guide's raise angle — override, node and pixels.
            var fo = angleDeg(fore.pose0, fore.endRaise), fn = angleDeg(nd.pose0.fore.rotation, nd.endRaise.fore.rotation)
            var fm = angleDeg(nd.pose0.fore.rotation, nd.midRaise.fore.rotation)
            console.info("[render] forearm pose0 → endRaise: override " + fo.toFixed(2) + "°, node " + fn.toFixed(2)
                         + "° (mid-raise node " + fm.toFixed(2) + "°); hmRaiseDeg " + raiseDeg)
            verify(Math.abs(fo - raiseDeg) < 3, "the forearm override moved " + fo.toFixed(2) + "°, not ~" + raiseDeg + "°")
            verify(Math.abs(fn - raiseDeg) < 3, "the forearm node moved " + fn.toFixed(2) + "°, not ~" + raiseDeg + "°")
            verify(fm > 3 && fm < fn - 3, "mid-raise is not between the two poses (" + fm.toFixed(2) + "°)")
            var dRaise = diff(shots.pose0, shots.endRaise, hmFore), dRaiseAll = diff(shots.pose0, shots.endRaise, whole)
            var dMid   = diff(shots.pose0, shots.midRaise, hmFore)
            var dBack  = diff(shots.pose0, shots.afterReturn, hmFore)
            var dUpper = Math.max(diff(shots.pose0, shots.midRaise, hmUpper), diff(shots.pose0, shots.endRaise, hmUpper),
                                  diff(shots.pose0, shots.afterReturn, hmUpper))
            console.info("[render] pixels: forearm region pose0↔endRaise " + dRaise.toFixed(2) + " (whole frame "
                         + dRaiseAll.toFixed(2) + "), pose0↔midRaise " + dMid.toFixed(2) + ", pose0↔afterReturn "
                         + dBack.toFixed(2) + "; upper-arm region, worst of three " + dUpper.toFixed(2))
            verify(dRaise > minMoved, "the forearm did not visibly move (" + dRaise.toFixed(2) + ")")
            verify(dUpper < maxSame, "the upper arm visibly moved (" + dUpper.toFixed(2) + ")")
            // (d) It came back.
            var bo = angleDeg(fore.pose0, fore.afterReturn)
            verify(bo < 0.5, "the forearm override did not return (" + bo.toFixed(2) + "°)")
            verify(dBack < maxSame, "the return frame differs from pose 0 (" + dBack.toFixed(2) + ")")
            console.info("[render] RS2 took " + Math.round(drv.now() - t0) + " ms")
        }

        // ── RS1 — Witmotion ──────────────────────────────────────────────────
        function test_RS1_witmotionGuide() {
            needsRender()
            var t0 = drv.now()
            drv.openAtCalibrate("witmotion2")         // the real pace; the fakes are held still
            var bvv = drv.flow._guide.view
            verify(!bvv.useGuideCamera, "the Witmotion guide is not on the frontal camera")
            var shots = {}, arm = {}
            function take(tag) {
                arm[tag] = q(bvv.leadArmOverrideRotation)
                shots[tag] = grabView("rs1_" + tag)
                console.info("[render] " + tag + " stage " + drv.stage() + " arm override " + fmtQ(arm[tag])
                             + " t+" + Math.round(drv.now() - t0))
            }
            tryVerify(function() { return drv.stage() === "phase1" }, 20000, "never phase 1")
            wait(200)
            take("armDown")
            var r = drv.d()
            var downQ = q(r.leadArmDownQuat), tQ = q(r.tPoseQuat)
            tryVerify(function() { return drv.stage() === "phase2" }, 20000, "never phase 2")
            wait(200)
            take("tPose")
            tryVerify(function() { return drv.done() }, 20000, "never done")
            wait(200)
            take("done")
            var a1 = angleDeg(arm.armDown, downQ), a2 = angleDeg(arm.tPose, tQ), between = angleDeg(downQ, tQ)
            console.info("[render] override vs target: armDown " + a1.toFixed(3) + "°, tPose " + a2.toFixed(3)
                         + "°; arm-down ↔ T-pose " + between.toFixed(1) + "°")
            verify(a1 < 0.5, "the arm-down grab is not at the arm-down target (" + a1.toFixed(2) + "°)")
            verify(a2 < 0.5, "the T-pose grab is not at the T-pose target (" + a2.toFixed(2) + "°)")
            var d = diff(shots.armDown, shots.tPose, wtArm), dAll = diff(shots.armDown, shots.tPose, whole)
            console.info("[render] pixels (arm region): armDown↔tPose " + d.toFixed(2) + " (whole frame " + dAll.toFixed(2) + ")")
            verify(d > minMoved, "arm-down and T-pose look the same (" + d.toFixed(2) + ")")
            console.info("[render] RS1 took " + Math.round(drv.now() - t0) + " ms")
        }

        // ── RC — the Check view follows the sensor ───────────────────────────
        function test_RC_checkViewFollowsTheSensor() {
            needsRender()
            var t0 = drv.now()
            drv.openAtCalibrate("hackmotion", { scale: 0.05 })
            tryVerify(function() { return drv.done() }, 15000, "the wG3 calibration never completed")
            var lw = drv.fx.liveWrist
            verify(!lw.active, "liveWrist active on Calibrate")
            verify(drv.continueToCheck(), "Continue did not reach Check")
            verify(lw.active, "liveWrist not active on Check")
            var avv = drv.findAll(drv.setup, function(o) { return typeName(o) === "ArmVizView" })
            compare(avv.length, 1)
            var dev = drv.hm
            // The calibrated units at their reference (identity), then the palm turned 40° about X.
            dev.unitLowerArm.anatCalibrated = true
            dev.unitPalm.anatCalibrated = true
            dev.unitLowerArm.anatQuat = Qt.quaternion(1, 0, 0, 0)
            dev.unitPalm.anatQuat = Qt.quaternion(1, 0, 0, 0)
            wait(1500)                                  // the arm view's segments load and draw
            var before = grabView("rc_reference")
            var a = 20 * Math.PI / 180                  // half of 40°
            dev.unitPalm.anatQuat = Qt.quaternion(Math.cos(a), Math.sin(a), 0, 0)
            wait(400)
            var after = grabView("rc_palm40")
            var d = diff(before, after, rcHand), dAll = diff(before, after, whole)
            console.info("[render] Check view: palm turned 40° → mean |Δluma| " + d.toFixed(2) + " in the hand region ("
                         + dAll.toFixed(2) + " whole frame)")
            verify(d > minMoved, "the Check view did not follow the palm (" + d.toFixed(2) + ")")
            drv.backFromCheck()
            verify(!lw.active, "liveWrist still active after leaving Check")
            console.info("[render] RC took " + Math.round(drv.now() - t0) + " ms")
        }
    }
}
