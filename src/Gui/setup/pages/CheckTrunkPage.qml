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

// Session setup — Check trunk (the trunk group's check step; trunk_imu_sequence_grf_design.md §6.7
// "Demonstrate, then verify", page 2: the live check).
//
// The golfer turns freely and watches the readings follow. One line of numbers, per the tile rule:
//     hips 45° · chest 88° · X 43°
// each the D13 turn (trunk_angles.h turnFromRad: the heading of q_anat·x̂) of the sensor's live
// anatomical orientation RELATIVE TO THE MOMENT THIS PAGE OPENED, golfer-relative: + is AWAY from the
// target, so turning away must read positive — the sign check made visible. X is chest − hips.
// Nothing here is gated or recorded.
//
// The live trunk FIGURE beside it: BodyVizView in IMU mode, whole-body framing — the hips from the
// pelvis q_anat, the spine chain from the thorax relative to the pelvis (useTrunkOverride, the same
// binding the guide demonstrates with). It draws the q_anat AS IS, not zeroed at page open: a forward
// bend at address reads as a bend, which is the point of the optional "take your address" look (§6.7).
// Turning away must turn the model away — the sign check made visible.
// The lead upper arm is NOT drawn from its sensor: a Witmotion arm's q_anat lives in the limb world
// (zeroed at its own arm-down pose, imu_frame_contract.md §5), not the trunk's stance world, and no
// solve here relates the two. Both arms hang.
//
// Recalibrate drops the trunk outcome and jumps back to Calibrate trunk with both steps pending; that
// page then begins a fresh run, because nothing valid is recorded.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    fullBleed: true

    readonly property var _pelvis: page.ctx.roles.pelvis.unit
    readonly property var _thorax: page.ctx.roles.thorax.unit
    // Golfer-relative: a right-hander turns away about −Z (pinpoint_sign_conventions.md "Trunk IMU
    // readings"), so the heading change is negated for one and kept for the other.
    readonly property real _awaySign: page.ctx.leftHanded ? 1 : -1

    // The orientations at the moment the page opened (enter), the zero of every reading.
    property var _refPelvis: null
    property var _refThorax: null

    function enter(direction) {
        _refPelvis = _pelvis ? _pelvis.anatQuat : null
        _refThorax = _thorax ? _thorax.anatQuat : null
    }

    function recalibrate() {
        page.draft.clearOutcome("trunk")
        page.flow.goTo("calibrateTrunk", true)
    }

    // Heading (deg) of q·x̂ in the horizontal plane (trunk_angles.h turnHeadingRad).
    function _heading(q) {
        var ml = q.times(Qt.vector3d(1, 0, 0))
        return Math.atan2(ml.y, ml.x) * 180 / Math.PI
    }
    // The signed turn from `ref` to `q`, wrapped to (−180, 180], golfer-relative (+ away).
    function _turn(q, ref) {
        if (!q || !ref) return NaN
        var d = _heading(q) - _heading(ref)
        while (d > 180)   d -= 360
        while (d <= -180) d += 360
        return d * _awaySign
    }
    readonly property real hipsDeg:  _pelvis && _refPelvis ? _turn(_pelvis.anatQuat, _refPelvis) : NaN
    readonly property real chestDeg: _thorax && _refThorax ? _turn(_thorax.anatQuat, _refThorax) : NaN
    readonly property real xDeg:     isNaN(hipsDeg) || isNaN(chestDeg) ? NaN : chestDeg - hipsDeg
    function _deg(v) { return isNaN(v) ? "—" : Math.round(v) + "°" }
    readonly property string readingLine: qsTr("hips %1 · chest %2 · X %3")
                                              .arg(_deg(hipsDeg)).arg(_deg(chestDeg)).arg(_deg(xDeg))

    ColumnLayout {
        anchors.fill:    parent
        anchors.margins: Theme.gap(8)
        spacing:         Theme.gap(12)

        Column {
            Layout.fillWidth:    true
            Layout.maximumWidth: Theme.contentWidth(page.width)
            Layout.topMargin:    Theme.gap(32)
            spacing:             Theme.gap(8)

            Text {
                text:               page.stepLabel
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
            }

            PpDisplayText {
                width:     parent.width
                text:      qsTr("Check your trunk sensors")
                pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                wrapMode:  Text.WordWrap
            }

            Text {
                width:          parent.width
                text:           qsTr("Turn freely, as if to the top and through. Turning away must read positive, and the chest must turn further than the hips. If it doesn't, go back and recalibrate.")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText2
                lineHeight:     1.65
                wrapMode:       Text.WordWrap
            }
        }

        // ── The live trunk figure (§6.7 page 2) ─────────────────────────────────
        // It exists only while this page does (R2; L2: one View3D at a time). Sized as Check arm sizes
        // its ArmVizView: it takes ALL the page's height between the text and the Recalibrate row, and
        // the readings ride on it in a corner box (Check arm's live-wrist box), not in lines above and
        // below it that would take height from a standing figure.
        Item {
            objectName:          "trunkLiveFigureSlot"
            Layout.fillWidth:    true
            Layout.fillHeight:   true
            Layout.minimumHeight: Theme.sp(220)

            BodyVizView {
                id: liveFigure
                objectName:   "trunkLiveFigure"
                anchors.fill: parent
                poseSource:   null          // IMU mode: no camera input
                rightHanded:  !page.ctx.leftHanded

                useTrunkOverride:       true
                hipsOverrideRotation:   page._pelvis ? page._pelvis.anatQuat : Qt.quaternion(1, 0, 0, 0)
                thoraxOverrideRotation: page._thorax ? page._thorax.anatQuat : Qt.quaternion(1, 0, 0, 0)

                useLeadArmOverride:           true
                leadArmOverrideRotation:      Qt.quaternion(0.7071, 0.7071, 0, 0)
                leadForeArmOverrideRotation:  Qt.quaternion(1, 0, 0, 0)
                useTrailArmOverride:          true
                trailArmOverrideRotation:     Qt.quaternion(0.7071, 0.7071, 0, 0)
                trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)
            }

            // The readings box, top right over the figure (as Check arm's live-wrist box).
            Rectangle {
                anchors { top: parent.top; right: parent.right; margins: Theme.gap(10) }
                width:   readingsCol.width  + Theme.sp(24)
                height:  readingsCol.height + Theme.sp(18)
                radius:  Theme.radius
                color:   Qt.alpha(Theme.colorBg2, 0.85)
                border.width: 1
                border.color: Theme.colorBorderMid

                Column {
                    id: readingsCol
                    anchors.centerIn: parent
                    width:   Math.min(Theme.sp(320), Math.max(readings.implicitWidth, Theme.sp(240)))
                    spacing: Theme.gap(8)

                    // The one line of numbers (the tile rule), zeroed when the page opened.
                    Text {
                        id: readings
                        objectName:     "trunkReadings"
                        text:           page.readingLine
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzHeading
                        color:          isNaN(page.hipsDeg) || isNaN(page.chestDeg) ? Theme.colorText3 : Theme.colorText
                    }

                    // The optional look (§6.7): never gated, nothing recorded.
                    Text {
                        objectName:     "trunkAddressHint"
                        width:          parent.width
                        text:           qsTr("Take your address: the figure should bend straight forward. If it leans to one side, a sensor is twisted in its pocket.")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText3
                        wrapMode:       Text.WordWrap
                    }
                }
            }
        }

        Row {
            Layout.fillWidth:    true
            Layout.bottomMargin: Theme.gap(8)
            spacing:             Theme.gap(6)

            Text {
                text:           qsTr("Not following your turn?")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText3
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                objectName:     "trunkCheckRecalibrate"
                text:           qsTr("Recalibrate")
                font.family:    Theme.fontBody
                font.weight:    Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorAccent
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    page.recalibrate()
                }
            }
        }
    }
}
