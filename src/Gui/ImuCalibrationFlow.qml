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

// Wrist/arm positional calibration flow. Relocated verbatim from the session
// wizard's Panel 3 so the SAME state machine runs in two hosts:
//   • the start-session wizard  (layoutMode "full")
//   • the Wrist toolbar IMU panel (layoutMode "compact")
//
// The state machine (phases 0→1→2, the timer chain, stillness-gated capture,
// mount validation) lives on the internal `d` object; the BodyVizView guide and
// the status display live in the active layout Component (full or compact). The
// timers reach the loaded BodyVizView through `flow._bvv`.
//
// Auto-start is gated on `flow.visible && flow._autoStartGate` (replacing the
// wizard's `currentStep === stepCalibrate` guard) so the flow runs whether hosted
// by a wizard step or a panel that toggles it visible.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: flow

    // ── Config ────────────────────────────────────────────────────────────────
    property string layoutMode: "full"     // "full" (wizard) | "compact" (toolbar panel)
    property bool   showHeader: true        // step eyebrow + "Calibrate Sensors" title
    property string stepLabel:  qsTr("CALIBRATE")   // host-supplied eyebrow text

    // ── Outputs (read-only) — hosts bind to these ─────────────────────────────
    readonly property alias calibrationDone:   d.calibrationDone
    readonly property alias calibrationFailed: d.calibrationFailed
    readonly property alias mountFailed:       d.mountFailed
    readonly property alias phase:             d.calibPhase
    readonly property alias leadImu:           d.leadImu   // slot-A instance (or null)

    // ── Signals ─────────────────────────────────────────────────────────────
    signal completed()
    signal cancelled()      // compact-mode "Back/Cancel"

    // ── Internal wiring ───────────────────────────────────────────────────────
    // Auto-start gate: true between begin() and reset(). Combined with visibility
    // it replaces the wizard's currentStep guard.
    property bool _autoStartGate: false
    // The active layout's BodyVizView assigns itself here on load so the
    // flow-level timers can drive its guide animation across the Loader boundary.
    property var _bvv: null

    // ── API (host-driven entry points) ────────────────────────────────────────
    function begin()         { d._reset(); _autoStartGate = true }
    function reset()         { d._reset(); _autoStartGate = false }
    // Restore the completed state WITHOUT re-running — used on backward nav into
    // the wizard step, or when the lead IMU is already calibrated this session.
    // Consolidates the wizard's two former restore branches; both ended in the
    // same visible state (phase 2, progress full, done).
    function showCompleted() {
        var imu = d.leadImu
        if (imu !== null && imu.calibrated) {
            d.calibArmDownQuat  = imu.calibArmDown
            d.calibArmTPoseQuat = imu.calibArmTPose
        }
        d.calibPhase         = 2
        d.phase1AccumMs      = 0
        d.stableAccumMs      = d._captureHoldMs
        d.phaseProgress      = 1.0
        d._animateLeadArm    = false
        d._leadArmTarget     = d.leadArmDownQuat
        d.calibrationFailed  = false
        d.mountFailed        = false
        d.mountFailMsg       = ""
        d._armDownCaptured   = true
        d._phase1MinHoldDone = true
        d.calibrationDone    = true
        _autoStartGate       = false
    }

    onCalibrationDoneChanged: if (calibrationDone) { calibCompleteTing.play(); flow.completed() }

    TingPlayer { id: calibCompleteTing; frequency: 4186.0 }  // C8 — two octaves above the ball ting

    // ── State machine + helpers (relocated from the wizard's calibPanel) ───────
    QtObject {
        id: d

        // phase 0 — user holds lead arm straight down; wait for IMU stable
        // phase 1 — animated guide: arm moves from down → T-pose
        // phase 2 — user raises arm to T-pose; hold to capture
        property int  calibPhase:    0
        property real phaseProgress: 0.0   // 0–1 for phase 2 hold timer

        // Captured reference quaternions from the lead-arm IMU.
        property var  calibArmDownQuat:  null   // phase 0 — arm relaxed at side
        property var  calibArmTPoseQuat: null   // phase 2 — arm raised to T-pose
        property bool calibrationDone:   false
        // Diagnostic — Euler angles at each calibration capture (cleared by _reset)
        property var calibArmDownEuler:  null   // { roll, pitch, yaw } at arm-down capture
        property var calibArmTPoseEuler: null   // { roll, pitch, yaw } at T-pose capture

        readonly property bool rightHanded: athleteController.currentHandedness !== "Left"

        // T-pose seed quaternions (match BodyPoseAdapter pre-seed values).
        readonly property quaternion tPoseQuat: d.rightHanded
            ? Qt.quaternion( 0.9948, -0.0105, -0.0011,  0.1012)   // left arm
            : Qt.quaternion( 0.9948, -0.0105,  0.0011, -0.1011)   // right arm

        // Arm-down: shoulder-local +Z = world -Y (verified by computing
        // shoulder.rotation * (0,0,1) for both left and right shoulders).
        // Rotating armNode's +Y to shoulder-local +Z = R_x(+90°) for both arms.
        // The mirror symmetry of the shoulder nodes means both use the same value.
        readonly property quaternion leadArmDownQuat:  Qt.quaternion(0.7071, 0.7071, 0, 0)
        readonly property quaternion trailArmDownQuat: Qt.quaternion(0.7071, 0.7071, 0, 0)

        // The lead-arm IMU: slot A = Wrist for Wrist Motion.
        // imuManager.instances is a required reactive dependency:
        // instanceFor() is a Q_INVOKABLE (not a property), so without it
        // this binding would not re-evaluate when the instance is created
        // by the Connect button on the previous step.
        readonly property QtObject leadImu: {
            var _dep      = imuManager.instances
            var placement = appSettings.imuPlacement
            var list      = imuManager.imuDeviceList
            for (var i = 0; i < list.length; ++i)
                if (placement[list[i].id] === "A")
                    return imuManager.instanceFor(list[i].id)
            return null
        }
        // Hand (B) and upper-arm (C) are OPTIONAL for the Wrist session. The precise
        // calibration + mount check run on whatever is connected (A is the anchor).
        readonly property QtObject slotB: {
            var _dep = imuManager.instances; var p = appSettings.imuPlacement
            var list = imuManager.imuDeviceList
            for (var i = 0; i < list.length; ++i)
                if (p[list[i].id] === "B") return imuManager.instanceFor(list[i].id)
            return null
        }
        readonly property QtObject slotC: {
            var _dep = imuManager.instances; var p = appSettings.imuPlacement
            var list = imuManager.imuDeviceList
            for (var i = 0; i < list.length; ++i)
                if (p[list[i].id] === "C") return imuManager.instanceFor(list[i].id)
            return null
        }
        function _connectedSegs() {
            // [instance, armDownRef] for every connected segment; A always first.
            var out = []
            if (leadImu && leadImu.imuConnected) out.push([leadImu, _refA])
            if (slotB   && slotB.imuConnected)   out.push([slotB,   _refB])
            if (slotC   && slotC.imuConnected)   out.push([slotC,   _refC])
            return out
        }
        function _curQuat(i) { return i ? Qt.quaternion(i.quatW, i.quatX, i.quatY, i.quatZ) : null }
        // Small Y-rotation bringing the abducted-pose anatomical axis onto Z (= φ).
        function _phiFromAbduction(inst) {
            var q = inst.anatQuat
            var w = Math.min(1, Math.abs(q.scalar))
            var s = Math.sqrt(Math.max(0, 1 - w*w))
            if (s < 1e-4) return 0
            var sg = q.scalar >= 0 ? 1 : -1
            var mx = sg*q.x/s, mz = sg*q.z/s
            var pp = Math.atan2(mx, mz), pm = Math.atan2(-mx, -mz)
            var phi = Math.abs(pp) <= Math.abs(pm) ? pp : pm
            return phi * 180 / Math.PI
        }
        // Per-sensor arm-down reference quaternions (captured at phase-1 completion).
        property var _refA: null
        property var _refB: null
        property var _refC: null
        // Mount validation outcome (set at phase-2 completion).
        property bool   mountFailed: false
        property string mountFailMsg: ""

        // Phase 2: accumulate stable hold duration (target _captureHoldMs).
        property real stableAccumMs: 0.0

        // Stillness-gated capture tuning. Both capture phases (arm-down and
        // abduction) watch the IMU's instantaneous angular velocity and only
        // accumulate samples while the arm is held still; any motion above the
        // threshold resets the hold. The threshold is deliberately forgiving:
        // an arm held out at shoulder height sways/tremors more than a few °/s
        // (an earlier, tighter gate never settled → capture stalled), but stays
        // comfortably below mid-motion (30–100°/s+).
        readonly property real _stillThreshDps: 15.0   // deg/s — held-still ceiling
        readonly property real _captureHoldMs:  2000   // ms of continuous stillness

        // Quaternion samples accumulated during each stillness-held capture window.
        property var _phase1Samples: []
        property var _phase2Samples: []

        // Phase 1 accumulator.
        property real phase1AccumMs: 0.0

        function _quatSlerp(a, b, t) {
            var dot = a.scalar * b.scalar + a.x * b.x + a.y * b.y + a.z * b.z
            if (dot < 0) { b = Qt.quaternion(-b.scalar, -b.x, -b.y, -b.z); dot = -dot }
            if (dot > 0.9995) {
                var r = Qt.quaternion(a.scalar + t * (b.scalar - a.scalar),
                                      a.x     + t * (b.x     - a.x),
                                      a.y     + t * (b.y     - a.y),
                                      a.z     + t * (b.z     - a.z))
                var len = Math.sqrt(r.scalar*r.scalar + r.x*r.x + r.y*r.y + r.z*r.z)
                return Qt.quaternion(r.scalar/len, r.x/len, r.y/len, r.z/len)
            }
            var theta0    = Math.acos(dot)
            var sinTheta0 = Math.sin(theta0)
            var s0 = Math.sin((1 - t) * theta0) / sinTheta0
            var s1 = Math.sin(      t * theta0) / sinTheta0
            return Qt.quaternion(s0 * a.scalar + s1 * b.scalar,
                                 s0 * a.x     + s1 * b.x,
                                 s0 * a.y     + s1 * b.y,
                                 s0 * a.z     + s1 * b.z)
        }

        // Iterative slerp mean: slerp(acc, samples[i], 1/(i+1)) converges to
        // the uniform spherical mean when all samples cluster near each other.
        function _slerpAverage(samples) {
            if (samples.length === 0) return Qt.quaternion(1, 0, 0, 0)
            var acc = samples[0]
            for (var i = 1; i < samples.length; i++)
                acc = _quatSlerp(acc, samples[i], 1.0 / (i + 1))
            return acc
        }

        // Animation targets — driven imperatively by the phase timers below.
        property quaternion _leadArmTarget:  d.leadArmDownQuat
        property bool       _animateLeadArm: false
        // Set when arm-down is captured; prevents the phase-1 timer from
        // re-triggering captureTransitionTimer during the raise animation.
        property bool _armDownCaptured: false
        // True only after phase1MinHoldTimer fires — gives the user a 2s settle
        // window after phase 1 begins before the stillness-gated capture starts.
        property bool _phase1MinHoldDone: false

        // Set when the lead IMU disconnects mid-calibration.
        property bool calibrationFailed: false

        function _reset() {
            if (leadImu) leadImu.clearCalibration()

            introDoneTimer.stop()
            introReadyTimer.stop()
            phase1MinHoldTimer.stop()
            phase1HoldTimer.stop()
            captureTransitionTimer.stop()
            raiseReadyTimer.stop()
            calibPhase         = 0
            phase1AccumMs      = 0
            stableAccumMs      = 0
            _phase1Samples     = []
            _phase2Samples     = []
            phaseProgress      = 0.0
            _animateLeadArm    = false
            _leadArmTarget     = leadArmDownQuat
            calibrationFailed  = false
            _armDownCaptured   = false
            _phase1MinHoldDone = false
            mountFailed        = false
            mountFailMsg       = ""
            _refA              = null
            _refB              = null
            _refC              = null
            calibArmDownQuat   = null
            calibArmTPoseQuat  = null
            calibrationDone    = false
            calibArmDownEuler  = null
            calibArmTPoseEuler = null
        }
    }

    Connections {
        target:  d.leadImu
        enabled: d.calibPhase > 0 && !d.calibrationDone
        function onImuConnectedChanged() {
            var imu = d.leadImu
            if (imu && !imu.imuConnected)
                d.calibrationFailed = true
        }
    }

    // Phase 2 hold timer.
    Timer {
        id: stabilityHoldTimer
        interval: 100
        repeat:   true
        running:  flow.visible && flow._autoStartGate
                  && d.calibPhase === 2
                  && !d.calibrationDone
                  && !d.mountFailed
                  && d.leadImu !== null
        onTriggered: {
            var imu = d.leadImu
            if (!imu) return
            // Stillness-gated: only accumulate while the arm is held still;
            // motion resets the hold so the captured pose is genuinely static.
            if (imu.angularVelocityDps > d._stillThreshDps) {
                d._phase2Samples = []
                d.stableAccumMs  = 0
                d.phaseProgress  = 0.0
                return
            }
            d._phase2Samples = d._phase2Samples.concat(
                [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
            d.stableAccumMs += interval
            d.phaseProgress = Math.min(d.stableAccumMs / d._captureHoldMs, 1.0)
            if (d.stableAccumMs >= d._captureHoldMs) {
                d.calibArmTPoseQuat = d._slerpAverage(d._phase2Samples)

                // Abduction refinement + mount validation for EVERY connected segment.
                // Each sensor: refine its mounting about the long axis by φ (from the
                // abducted-pose anatomical orientation), then evaluate the two-part gate
                //   gravity check (gravΔ ≤ 25°, catches flip/upside-down) AND
                //   long-axis deviation (φ/strapΔ ≤ 15°, catches strap rotation).
                // PASS requires ALL connected segments to pass; any FAIL → re-seat.
                var segs = d._connectedSegs()
                var allPass = segs.length > 0
                var failNames = []
                var nameFor = function(inst) {
                    return inst === d.leadImu ? qsTr("forearm")
                         : inst === d.slotB    ? qsTr("hand")
                         : qsTr("upper arm")
                }
                for (var k = 0; k < segs.length; ++k) {
                    var s2 = segs[k][0], ref = segs[k][1]
                    if (!ref) continue
                    s2.refineMountAboutLongAxis(ref, d._phiFromAbduction(s2), false)
                    var ok = s2.mountDeviationDeg <= 15.0 && s2.mountGravityErrorDeg <= 25.0
                    if (!ok) { allPass = false; failNames.push(nameFor(s2)) }
                }

                if (allPass) {
                    d.mountFailed = false
                    d.mountFailMsg = ""
                    d.calibrationDone = true
                } else {
                    d.mountFailed = true
                    d.mountFailMsg = qsTr("Sensor mounted incorrectly (%1) — re-seat per the strap guide and tap Recalibrate.")
                        .arg(failNames.join(", "))
                    // Leave calibrationDone false; user must Recalibrate.
                }
            }
        }
    }

    // Phase 0: 3s after the flow becomes active, play 3s rest→T-pose animation.
    // Guard on visible+gate: this flow lives in a StackLayout/Popup so it is
    // instantiated up front — without the guard the intro fires immediately and
    // the whole capture chain runs in the background.
    Timer {
        id: introStartTimer
        interval: 3000
        repeat:   false
        running:  d.calibPhase === 0 && flow.visible && flow._autoStartGate
        onTriggered: {
            flow._bvv.resetArmAnimation(d.leadArmDownQuat)
            flow._bvv.leadArmAnimDuration = 3000
            d._animateLeadArm = true
            d._leadArmTarget  = d.tPoseQuat
            introDoneTimer.start()   // wait for up-animation
        }
    }

    // Up-animation done: lower arm back to rest at the same speed.
    Timer {
        id: introDoneTimer
        interval: 3000
        repeat:   false
        onTriggered: {
            // _leadArmFrom is still leadArmDownQuat from resetArmAnimation —
            // reset it to tPoseQuat so the return animation starts from the top.
            flow._bvv.resetArmAnimation(d.tPoseQuat)
            flow._bvv.leadArmAnimDuration = 3000
            d._leadArmTarget = d.leadArmDownQuat
            introReadyTimer.start()   // 3s anim + 2s pause
        }
    }

    // Return animation + 2s pause complete: start phase 1.
    Timer {
        id: introReadyTimer
        interval: 5000   // 3000ms return anim + 2000ms pause
        repeat:   false
        onTriggered: {
            d._animateLeadArm    = false
            d._phase1MinHoldDone = false
            d.calibPhase         = 1
            phase1MinHoldTimer.start()
            // No hardware zeroing — orientation comes from our own Madgwick
            // fusion (device angle-zeroing is vestigial); the arm-down pose is
            // captured directly by phase1HoldTimer once the IMU is stable.
        }
    }

    Timer {
        id: phase1MinHoldTimer
        interval: 2000
        repeat:   false
        onTriggered: d._phase1MinHoldDone = true
        // No capture here — phase1HoldTimer handles it reactively.
    }

    // Phase 1 accumulator — same stillness-gated pattern as stabilityHoldTimer
    // (phase 2). The timer runs for the whole phase; each tick decides whether
    // to accumulate (arm held still) or reset the hold (arm moving), watching
    // imu.angularVelocityDps. _armDownCaptured gates it off once captured so it
    // can't re-trigger during the raise animation. _phase1MinHoldDone (set 2s
    // after phase 1 begins via phase1MinHoldTimer) gives the user a settle
    // window before the still-watch starts.
    Timer {
        id: phase1HoldTimer
        interval: 100
        repeat:   true
        running:  flow.visible && flow._autoStartGate
                  && d.calibPhase === 1
                  && d._phase1MinHoldDone
                  && !d._armDownCaptured
                  && d.leadImu !== null
        onTriggered: {
            var imu = d.leadImu
            if (!imu) return
            // Stillness-gated: only accumulate while the arm is held still;
            // motion resets the hold so the captured pose is genuinely static.
            if (imu.angularVelocityDps > d._stillThreshDps) {
                d._phase1Samples = []
                d.phase1AccumMs  = 0
                return
            }
            d._phase1Samples = d._phase1Samples.concat(
                [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
            d.phase1AccumMs += interval
            if (d.phase1AccumMs >= d._captureHoldMs) {
                d.calibArmDownQuat = d._slerpAverage(d._phase1Samples)
                // Quick-calibrate EVERY connected segment at arm-down: sets A so
                // anatQuat=identity here, with the fixed nominal mounting M, and runs
                // the gravity (flip) check. Each sensor's arm-down reference is stored
                // for the phase-2 abduction refinement. The hand, forearm and upper-arm
                // sensors are mounted COPLANAR (same strap orientation), so all three share
                // the arm nominal mount → handMount=false for all. (nominalHandMount() is a
                // non-coplanar dorsal placement we do not use.)
                d._refA = d._curQuat(d.leadImu)
                d._refB = d._curQuat(d.slotB)
                d._refC = d._curQuat(d.slotC)
                if (d.leadImu && d.leadImu.imuConnected)
                    d.leadImu.setNominalCalibration(d._refA, false)
                if (d.slotB && d.slotB.imuConnected)
                    d.slotB.setNominalCalibration(d._refB, false)
                if (d.slotC && d.slotC.imuConnected)
                    d.slotC.setNominalCalibration(d._refC, false)
                d._armDownCaptured = true  // stops timer via binding; must be after quat capture
                captureTransitionTimer.start()
            }
        }
    }

    // Phase 1 → raise: brief pause after arm-down captured, then play
    // the raise guide animation before starting T-pose capture.
    Timer {
        id: captureTransitionTimer
        interval: 800
        repeat:   false
        onTriggered: {
            flow._bvv.resetArmAnimation(d.leadArmDownQuat)
            flow._bvv.leadArmAnimDuration = 1500
            d._animateLeadArm = true
            d._leadArmTarget  = d.tPoseQuat
            raiseReadyTimer.start()
        }
    }

    // Raise animation (1.5s) + 2s pause, then start T-pose capture.
    Timer {
        id: raiseReadyTimer
        interval: 3500   // 1500ms raise anim + 2000ms pause
        repeat:   false
        onTriggered: {
            d._animateLeadArm = false
            d.calibPhase      = 2
        }
    }

    // ── Layout: full (wizard) vs compact (toolbar panel) ───────────────────────
    Loader {
        anchors.fill: parent
        sourceComponent: flow.layoutMode === "compact" ? compactLayout : fullLayout
    }

    // FULL — RowLayout: BodyVizView (fill) | status Column (sp(180), right).
    Component {
        id: fullLayout

        RowLayout {
            anchors.fill:    parent
            anchors.margins: Theme.sp(8)
            spacing:         Theme.sp(12)

            BodyVizView {
                id: calibBvvFull
                Layout.fillHeight:   true
                Layout.fillWidth:    true
                Layout.minimumWidth: Theme.sp(200)
                Component.onCompleted: flow._bvv = calibBvvFull

                poseSource:       null   // no camera input during calibration
                rightHanded:      d.rightHanded
                highlightLeadArm: true
                leadArmColor:     Theme.colorAccent

                useLeadArmOverride:          true
                leadArmOverrideRotation:     d._leadArmTarget
                leadForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                useTrailArmOverride:          true
                trailArmOverrideRotation:     d.trailArmDownQuat
                trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                animateLeadArm: d._animateLeadArm
            }

            Column {
                Layout.preferredWidth: Theme.sp(360)
                Layout.fillHeight:     true
                spacing:               Theme.sp(16)
                Layout.topMargin:      Theme.sp(32)

                Text {
                    visible:            flow.showHeader
                    width:              parent.width
                    text:               flow.stepLabel
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro
                    color:              Theme.colorText3
                }

                Text {
                    visible:        flow.showHeader
                    width:          parent.width
                    text:           qsTr("Calibrate Sensors")
                    font.family:    Theme.fontDisplay
                    font.italic:    Theme.fontDisplayItalic
                    font.weight:    Theme.fontDisplayWeight
                    font.pixelSize: Math.min(Theme.sp(18), Theme.fontSzDisplay)
                    color:          Theme.colorText
                    wrapMode:       Text.WordWrap
                }

                StatusBadge   { width: implicitWidth }
                PhaseText      { width: parent.width }
                ProgressBar    { width: parent.width }
                StatusLabel    { width: parent.width }
                AngleWarning   { width: parent.width }
                NoImuWarning   { width: parent.width }
                MountFailText  { width: parent.width }

                PpButton {
                    visible: d.calibPhase > 0 || d.calibrationDone
                             || d.calibrationFailed || d.mountFailed
                    label:   qsTr("↺  Recalibrate")
                    primary: false
                    onClicked: flow.begin()
                }
            }
        }
    }

    // COMPACT — stacked BodyVizView (~sp(220)) + status, scrolling in a Flickable,
    // with the action bar PINNED at the bottom so it is always visible even when a
    // short window clamps the popup. The whole-panel attention frame is drawn by the
    // host panel; here the action bar is a plain container whose main action (Cancel)
    // uses the attention colour.
    Component {
        id: compactLayout

        Item {
            anchors.fill: parent

            // Pinned, always-visible action bar.
            Item {
                id: actionBar
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.margins: Theme.sp(12)
                height: Theme.sp(54)

                RowLayout {
                    anchors { fill: parent; leftMargin: Theme.sp(10); rightMargin: Theme.sp(10) }
                    spacing: Theme.sp(8)
                    PpButton {
                        visible: d.calibPhase > 0 || d.calibrationDone
                                 || d.calibrationFailed || d.mountFailed
                        label:   qsTr("↺  Recalibrate")
                        primary: false
                        onClicked: flow.begin()
                    }
                    Item { Layout.fillWidth: true }
                    PpButton {
                        label:     qsTr("Cancel")
                        attention: true
                        onClicked: flow.cancelled()
                    }
                }
            }

            Flickable {
                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: actionBar.top }
                contentWidth: width
                contentHeight: compactCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: compactCol
                    width: parent.width
                    spacing: Theme.sp(12)

                    BodyVizView {
                        id: calibBvvCompact
                        Layout.fillWidth:       true
                        Layout.preferredHeight: Theme.sp(220)
                        Layout.leftMargin:      Theme.sp(12)
                        Layout.rightMargin:     Theme.sp(12)
                        Layout.topMargin:       Theme.sp(12)
                        Component.onCompleted: flow._bvv = calibBvvCompact

                        poseSource:       null
                        rightHanded:      d.rightHanded
                        highlightLeadArm: true
                        leadArmColor:     Theme.colorAccent

                        useLeadArmOverride:          true
                        leadArmOverrideRotation:     d._leadArmTarget
                        leadForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                        useTrailArmOverride:          true
                        trailArmOverrideRotation:     d.trailArmDownQuat
                        trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                        animateLeadArm: d._animateLeadArm
                    }

                    ColumnLayout {
                        Layout.fillWidth:    true
                        Layout.leftMargin:   Theme.sp(15)
                        Layout.rightMargin:  Theme.sp(15)
                        Layout.bottomMargin: Theme.sp(12)
                        spacing: Theme.sp(12)

                        StatusBadge  { Layout.alignment: Qt.AlignLeft }
                        PhaseText     { Layout.fillWidth: true }
                        ProgressBar   { Layout.fillWidth: true }
                        StatusLabel   { Layout.fillWidth: true }
                        AngleWarning  { Layout.fillWidth: true }
                        NoImuWarning  { Layout.fillWidth: true }
                        MountFailText { Layout.fillWidth: true }
                    }
                }
            }
        }
    }

    // ── Shared status sub-components (used by both layouts) ────────────────────
    // Status uses the wizard's existing colours: colorAccent (calibrating),
    // colorGood (done), colorWarn/colorError (issues). The compact layout
    // additionally frames its pinned action bar with the attention colour to draw
    // the eye to the controls; the status indicators themselves do not.

    component StatusBadge: Rectangle {
        readonly property bool _complete:    d.calibrationDone
        readonly property bool _calibrating: d.calibPhase >= 1 && !d.calibrationDone
        readonly property bool _failed:      (d.calibrationFailed || d.mountFailed) && !d.calibrationDone

        implicitWidth:  statusBadgeLbl.implicitWidth + Theme.sp(16)
        implicitHeight: Theme.sp(22)
        height:         Theme.sp(22)
        radius:         Theme.sp(11)
        color: _complete    ? Theme.colorGoodLight
             : _calibrating ? Theme.colorAccentLight
             : _failed      ? Theme.colorErrorLight
             :                Theme.colorBg3
        border.color: _complete    ? Theme.colorGood
                    : _calibrating ? Theme.colorAccent
                    : _failed      ? Theme.colorError
                    :                Theme.colorBorderMid

        Text {
            id: statusBadgeLbl
            anchors.centerIn: parent
            text: parent._complete    ? qsTr("Complete")
                : parent._calibrating ? qsTr("Calibrating")
                : parent._failed      ? qsTr("Failed")
                :                       qsTr("Pending")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color: parent._complete    ? Theme.colorGood
                 : parent._calibrating ? Theme.colorAccent
                 : parent._failed      ? Theme.colorError
                 :                       Theme.colorText3
        }
    }

    component PhaseText: Text {
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        color:          Theme.colorText2
        text: {
            if (d.calibPhase === 0)
                return qsTr("Watch the guide — this shows the T-pose position you'll hold next.")
            if (d.calibPhase === 1) {
                if (d._armDownCaptured)
                    return qsTr("Follow the guide and raise your arm out to shoulder height.")
                return qsTr("Let your lead arm hang relaxed at your side and hold still.")
            }
            return qsTr("Hold your arm at shoulder height and keep it still — the bar fills while you hold steady.")
        }
    }

    component ProgressBar: Item {
        height: Theme.sp(32)
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width:  parent.width
            height: Theme.sp(4)
            radius: Theme.sp(2)
            color:  Theme.colorBg3
            Rectangle {
                readonly property real fillFraction: {
                    if (d.calibrationDone)        return 1.0
                    if (d.calibPhase === 2)       return d.phaseProgress
                    if (d.calibPhase === 1 && !d._armDownCaptured)
                        return Math.min(d.phase1AccumMs / d._captureHoldMs, 1.0)
                    return 0.0
                }
                width:  parent.width * fillFraction
                height: parent.height
                radius: parent.radius
                color:  d.calibrationDone ? Theme.colorGood : Theme.colorAccent
                Behavior on width { NumberAnimation { duration: 150 } }
            }
        }
    }

    component StatusLabel: Row {
        spacing: Theme.sp(6)
        Rectangle {
            visible:      d.calibrationDone
            width:        Theme.sp(16)
            height:       Theme.sp(16)
            radius:       width / 2
            color:        "transparent"
            border.color: Theme.colorGood
            border.width: Theme.sp(1.5)
            y:            (statusLbl.implicitHeight - height) / 2
            Text {
                anchors.centerIn: parent
                text:           "✓"
                color:          Theme.colorGood
                font.pixelSize: Theme.sp(9)
                font.bold:      true
            }
        }
        Text {
            id: statusLbl
            readonly property bool _capturing:
                (d.calibPhase === 1 && d.phase1AccumMs > 0 && !d._armDownCaptured)
                || d.calibPhase === 2
            width:              parent.width - (d.calibrationDone ? Theme.sp(22) : 0)
            wrapMode:           Text.WordWrap
            font.family:        Theme.fontData
            font.pixelSize:     _capturing ? Theme.fontSzHeading : Theme.fontSzMicro
            font.bold:          _capturing
            font.letterSpacing: Theme.trackingData
            color: d.calibrationDone ? Theme.colorGood
                 : _capturing         ? Theme.colorAccent
                 :                       Theme.colorText3
            text: {
                if (d.calibrationDone)    return qsTr("CALIBRATION COMPLETE")
                if (d.calibPhase === 0)   return qsTr("WATCH THE GUIDE")
                if (d.calibPhase === 1) {
                    if (d._armDownCaptured) return qsTr("FOLLOW THE GUIDE")
                    var imu = d.leadImu
                    if (!imu || !imu.imuConnected) return qsTr("WAITING FOR SENSOR")
                    if (d.phase1AccumMs > 0)       return qsTr("HOLD STILL — CAPTURING")
                    return qsTr("HOLD STILL")
                }
                return qsTr("HOLD STILL — CAPTURING")
            }
            Behavior on font.pixelSize { NumberAnimation { duration: Theme.durationNormal } }
            Behavior on color           { ColorAnimation  { duration: Theme.durationNormal } }
        }
    }

    // Calibration angle warning — shown when arm-down→T-pose angle was outside
    // the expected ~90° range.
    component AngleWarning: Text {
        readonly property bool _show: {
            var imu = d.leadImu
            return d.calibrationDone && imu !== null && imu.calibrated && !imu.calibrationAngleValid
        }
        visible:    _show
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorWarn
        text:           qsTr("The arm positions didn't look quite right — the angle between arm-down and T-pose was much less than expected. For best results, make sure your arm is fully raised to shoulder height during the T-pose step, then tap Recalibrate.")
    }

    component NoImuWarning: Text {
        visible:    d.leadImu === null
        wrapMode:   Text.WordWrap
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorWarn
        text:           qsTr("No wrist sensor assigned to slot A. Return to the IMUs step to assign one.")
    }

    component MountFailText: Text {
        visible:    d.mountFailed && !d.calibrationDone
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorError
        text:           d.mountFailMsg
    }
}
