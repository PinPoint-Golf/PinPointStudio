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

// CalibrationGuide — the 3-D guide as the calibration routines see it (design §4.7, R9),
// and the real adapter over ONE BodyVizView.
//
// ── THE INTERFACE (what a routine may use, and all it may use) ─────────────────────
//   readonly property bool ready            the guide can be watched (bvv.fullyLoaded)
//   function pose(armQ, foreQ)              show a pose, no motion (foreQ optional)
//   function animate(armQ, foreQ, ms)       move to a pose over ms; finished() at the end
//   function cancel()                       stop any motion; NO finished()
//   signal finished()
//   hmUpperArm, hmForePose0, hmForePose1, hmRaiseDeg   — the HackMotion poses
//   property bool useGuideCamera
// The test stand-in (tests/setup/fakes/FakeGuide.qml) has the same members. A routine
// keeps its OWN stage ("introUp", "raise", …): the guide reports only "finished".
//
// ── THE ADAPTER ────────────────────────────────────────────────────────────────────
// The view binds to this object, never the other way round:
//     leadArmOverrideRotation:     guide.armTarget
//     leadForeArmOverrideRotation: guide.foreTarget   (identity for a Witmotion — the host)
//     animateLeadArm:              guide.animating
//     useGuideCamera:              guide.useGuideCamera
// and registers itself with `guide.view = <the view>` when it is created. A layout switch
// creates a new view, which registers in its turn (the old one is destroyed and `view`
// drops to null with it).
//
// ⚠ NOTHING HERE TOUCHES `visible` ON THE VIEW. The view's animation chain is what a
// routine waits on; hiding it is not how a routine is stopped. Stopping is `active`
// (below) and cancel().
import QtQuick

QtObject {
    id: guide

    // The view this adapter drives. Set by the view itself on creation.
    property Item view: null

    // R3: the host's activity. While false the guide reports nothing and any motion
    // in flight is cancelled.
    property bool active: false

    readonly property bool ready: view !== null && view.fullyLoaded

    property bool useGuideCamera: false

    // The pose the view shows — bound by the view (see the header). armTarget starts at
    // the arm hanging at the side, which is the routines' rest pose.
    property quaternion armTarget:  Qt.quaternion(0.7071, 0.7071, 0, 0)
    property quaternion foreTarget: Qt.quaternion(1, 0, 0, 0)
    property bool       animating:  false

    // ── HackMotion poses, read off the avatar (BodyVizView.qml has the derivation) ──
    readonly property quaternion hmUpperArm:  view ? view.hmCalUpperArmQuat     : Qt.quaternion(1, 0, 0, 0)
    readonly property quaternion hmForePose0: view ? view.hmCalForeArmPose0Quat : Qt.quaternion(1, 0, 0, 0)
    readonly property quaternion hmForePose1: view ? view.hmCalForeArmPose1Quat : Qt.quaternion(1, 0, 0, 0)
    readonly property real       hmRaiseDeg:  view ? view.hmCalRaiseDeg         : 30

    signal finished()

    // True between animate() and its end (or cancel()). Only a motion this adapter
    // started is reported, so a stray leadArmAnimFinished() is never a finish.
    property bool _inFlight: false

    // = BodyVizView.resetArmAnimation(armQ, foreQ), and the pose is what the view
    // shows. foreQ omitted leaves the forearm alone, as resetArmAnimation does.
    function pose(armQ, foreQ) {
        _inFlight = false
        if (view) view.resetArmAnimation(armQ, foreQ)
        animating = false
        armTarget = armQ
        if (foreQ !== undefined) foreTarget = foreQ
    }

    // Starts the stall-clamped slerp from the current pose. The view's FrameAnimation
    // starts on the TARGET CHANGE (BodyVizView onLeadArm…OverrideRotationChanged),
    // which is why animating goes true before either target is written.
    function animate(armQ, foreQ, ms) {
        if (view) view.leadArmAnimDuration = ms
        _inFlight = true
        animating = true
        armTarget = armQ
        if (foreQ !== undefined) foreTarget = foreQ
    }

    // Stops any motion where it is held (resetArmAnimation stops the FrameAnimation),
    // with no finished().
    function cancel() {
        _inFlight = false
        _swappedInFlight = false
        if (view) view.resetArmAnimation(armTarget, foreTarget)
        animating = false
    }

    onActiveChanged: if (!active) cancel()

    // ⚠ A VIEW SWAPPED MID-MOTION (F13). A layout switch destroys the view a motion was playing
    // on and creates one that binds armTarget — so it is built AT the target and never animates
    // to it, and the motion in flight would never report finished(): the routine's chain would
    // wait for ever. Once the new view can be watched, the motion it shows as complete is
    // reported complete. Nothing is replayed.
    // DECISION(stage5b): finish the motion on the new view rather than replay it there — the
    // pose it shows IS the motion's end, and a replay would restart the guide the user follows.
    property bool _swappedInFlight: false
    onViewChanged: {
        if (view !== null && _inFlight) _swappedInFlight = true
        _finishSwapped()
    }
    onReadyChanged: _finishSwapped()
    function _finishSwapped() {
        if (!_swappedInFlight || !ready) return
        _swappedInFlight = false
        if (!_inFlight || !active) return
        _inFlight = false
        animating = false
        finished()
    }

    property Connections _viewFinished: Connections {
        target:  guide.view
        enabled: guide.active && guide._inFlight
        function onLeadArmAnimFinished() {
            guide._inFlight = false
            guide.finished()
        }
    }
}
