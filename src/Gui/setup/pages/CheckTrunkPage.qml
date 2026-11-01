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
// each the D13 turn of the sensor's live anatomical orientation RELATIVE TO THE MOMENT THIS PAGE OPENED,
// golfer-relative: + is AWAY from the target, so turning away must read positive — the sign check made
// visible. X is chest − hips. The headings are the C++ ones (ImuInstance.trunkHeadingDeg, trunk_angles.h
// liveHeadingDeg: the heading of q_anat·x̂), so where C++ has no reading — not calibrated, a filter
// fault, the medio-lateral axis near vertical — the page shows the missing mark "-", never a number. A unit with no
// reading when the page opens is zeroed at its first reading.
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
import "../../viz/VizMath.js" as VizMath
import "../../typeset/figure_format.js" as Figures

WizardPage {
    id: page

    fullBleed: true

    readonly property var _pelvis: page.ctx.roles.pelvis.unit
    readonly property var _thorax: page.ctx.roles.thorax.unit
    // Golfer-relative: a right-hander turns away about −Z (pinpoint_sign_conventions.md "Trunk IMU
    // readings"), so the heading change is negated for one and kept for the other.
    readonly property real _awaySign: page.ctx.leftHanded ? 1 : -1

    // A unit's live heading (°, imu_instance.h trunkHeadingDeg): a finite number, or NaN — no reading.
    function _headingOf(u) {
        var h = u ? Number(u.trunkHeadingDeg) : NaN
        return isFinite(h) ? h : NaN
    }
    readonly property real livePelvisHeading: _headingOf(_pelvis)
    readonly property real liveThoraxHeading: _headingOf(_thorax)

    // Each unit's zero: its heading when the page opened (enter), or — when it had no reading then —
    // its first reading after, so a reading that appears late is shown, never held back as "-".
    property real _refPelvis: NaN
    property real _refThorax: NaN
    function _takeRefs() {
        if (!page.active) return
        if (isNaN(_refPelvis) && !isNaN(livePelvisHeading)) _refPelvis = livePelvisHeading
        if (isNaN(_refThorax) && !isNaN(liveThoraxHeading)) _refThorax = liveThoraxHeading
    }
    onLivePelvisHeadingChanged: _takeRefs()
    onLiveThoraxHeadingChanged: _takeRefs()

    function enter(direction) {
        _refPelvis = NaN
        _refThorax = NaN
        _takeRefs()
    }

    // The signed turn from heading `ref` to heading `h`, golfer-relative (+ away); NaN when either
    // is no reading.
    function _turn(h, ref) {
        return isNaN(h) || isNaN(ref) ? NaN : VizMath.wrap180(h - ref) * _awaySign
    }
    readonly property real hipsDeg:  _turn(livePelvisHeading, _refPelvis)
    readonly property real chestDeg: _turn(liveThoraxHeading, _refThorax)
    readonly property real xDeg:     isNaN(hipsDeg) || isNaN(chestDeg) ? NaN : chestDeg - hipsDeg
    // Set by the house rules (typeset/figure_format.js): "-" when missing, a true minus, whole degrees.
    function _deg(v) { return Figures.figureUnit(v, "°") }
    readonly property string readingLine: qsTr("hips %1 · chest %2 · X %3")
                                              .arg(_deg(hipsDeg)).arg(_deg(chestDeg)).arg(_deg(xDeg))

    ColumnLayout {
        anchors.fill:    parent
        anchors.margins: Theme.gap(8)
        spacing:         Theme.gap(12)

        PpPageHeader {
            style:               "setup"
            Layout.fillWidth:    true
            Layout.maximumWidth: Theme.contentWidth(page.width)
            Layout.topMargin:    Theme.gap(32)
            eyebrow:             page.stepLabel
            title:               qsTr("Check your trunk sensors")
            description:         qsTr("Make a few backswing and follow-through turns. A backswing turn must read positive, and the chest must turn further than the hips. If it doesn't, go back and recalibrate.")
            titlePixelSize:      Math.min(Theme.sp(22), Theme.fontSzDisplay)
            rule:                false
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
            SetupReadingsBox {
                padX:       Theme.sp(24)
                padY:       Theme.sp(18)
                rowSpacing: Theme.gap(8)
                rowsWidth:  Math.min(Theme.sp(320), Math.max(readings.implicitWidth, Theme.sp(240)))

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
                    text:           qsTr("Take your address: the figure should bend straight forward. If it leans to one side, a sensor is twisted in its pocket. If it leans back, a sensor is on the front or facing in — wear both on your back, label out.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                }
            }
        }

        SetupRecalibrateLink {
            Layout.fillWidth:    true
            Layout.bottomMargin: Theme.gap(8)
            prompt:              qsTr("Not following your turn?")
            linkObjectName:      "trunkCheckRecalibrate"
            onActivated:         page.redoCalibration("trunk", "calibrateTrunk")
        }
    }
}
