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

// The calibration guide's view — a BodyVizView bound to a CalibrationGuide, the one place the
// adapter's bindings (CalibrationGuide.qml header, "THE ADAPTER") are written. Used by
// ImuCalibrationFlow (both layouts: the arm routines) and CalibrateTrunkPage (`trunk: true`).
//
// It IS the BodyVizView (its root type), not a wrapper around one: the view's object, its View3D
// and the order they are created in are exactly a plain BodyVizView's, so nothing here touches the
// Qt 6.11 debug Quick 3D lifetime rules (View3DKeepAlive.qml) and the test drivers find it as they
// always did (by resetArmAnimation / leadArmAnimFinished / useTrunkOverride).
//
// On creation it registers itself with its guide (`guide.view = <this view>`); a layout switch
// creates a new view, which registers in its turn.
//
//   arm (default)  the lead arm highlighted and posed by guide.armTarget, animated while the guide
//                  animates; followForearm (a HackMotion) also takes guide.foreTarget, the raise
//                  itself; the trail arm hangs; the guide's camera when it asks for one.
//   trunk          the trunk turned by the guide (hipsTarget / thoraxTarget, its own slerp); the
//                  arms posed, never animated — hanging for stand tall, folded on the chest while
//                  guide.armsCrossed.
import QtQuick
import PinPointStudio

BodyVizView {
    id: view

    property var  guide: null
    property bool trunk: false
    property bool followForearm: false

    Component.onCompleted: if (guide) guide.view = view

    poseSource:       null   // no camera input during calibration
    highlightLeadArm: !trunk
    leadArmColor:     Theme.colorAccent

    // The trunk: the guide's golfer-relative turns, as q_anat (the guide's header).
    useTrunkOverride:       trunk
    hipsOverrideRotation:   trunk && guide ? guide.hipsTarget   : Qt.quaternion(1, 0, 0, 0)
    thoraxOverrideRotation: trunk && guide ? guide.thoraxTarget : Qt.quaternion(1, 0, 0, 0)
    animateTrunk:           trunk && guide ? guide.trunkAnimating : false

    readonly property bool _crossed: trunk && guide !== null && guide.armsCrossed

    useLeadArmOverride:      true
    leadArmOverrideRotation: _crossed ? crossedLeadArmQuat
                                      : (guide ? guide.armTarget : Qt.quaternion(0.7071, 0.7071, 0, 0))
    // ⚠ THE HACKMOTION RAISE IS THIS PROPERTY. Its two poses share one upper-arm rotation (the
    // elbow must not move), so the travel the device watches is a forearm rotation and the forearm
    // target is what changes between the markers. Identity — unchanged — for the Witmotion
    // routine and the trunk. Derivation in BodyVizView.qml.
    leadForeArmOverrideRotation: _crossed ? crossedLeadForeArmQuat
                               : (followForearm && guide) ? guide.foreTarget
                               : Qt.quaternion(1, 0, 0, 0)

    useTrailArmOverride:          true
    trailArmOverrideRotation:     _crossed ? crossedTrailArmQuat     : Qt.quaternion(0.7071, 0.7071, 0, 0)
    trailForeArmOverrideRotation: _crossed ? crossedTrailForeArmQuat : Qt.quaternion(1, 0, 0, 0)

    // The arm routines' guide motion. The trunk routine moves only the trunk.
    animateLeadArm: !trunk && guide ? guide.animating : false

    // ⚠ The HackMotion routine is invisible from the default front camera (ImuCalibrationFlow's
    // CalibrationGuide declaration says why), so its guide asks for the guide camera.
    useGuideCamera: !trunk && guide ? guide.useGuideCamera : false
}
