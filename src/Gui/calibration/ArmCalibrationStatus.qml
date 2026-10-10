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

// The arm-calibration status column (design §4.7) — PRESENTATIONAL ONLY. It reads the
// routine's outputs and the host's resolved lead unit/device, and says what they mean.
// It calls nothing and changes nothing; Recalibrate is a signal the host acts on.
//
//   mode "full"    — the wizard: eyebrow, title, the status, a Recalibrate button
//   mode "compact" — the toolbar panel: the status only (the host pins its own buttons)
//
// Moved out of ImuCalibrationFlow.qml with every string, colour and layout unchanged.
// The `d` object below mirrors the names the sub-components always read, with the
// values a not-yet-loaded routine would show.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: armStatus

    property string   mode:            "full"     // "full" | "compact"
    property var      routine:         null       // WitmotionArmRoutine | HackMotionArmRoutine | null
    property bool     isHackMotion:    false
    property QtObject leadImu:         null       // the slot-A / leadForearm UNIT
    property QtObject leadDevice:      null       // its PERIPHERAL
    property bool     calibrationDone: false
    property bool     showHeader:      true
    property string   stepLabel:       ""

    signal recalibrate()

    implicitWidth:  statusLoader.implicitWidth
    implicitHeight: statusLoader.implicitHeight

    QtObject {
        id: d
        readonly property var  _r:   armStatus.routine
        readonly property bool _wt:  _r !== null && _r !== undefined && _r.calibPhase !== undefined
        readonly property bool _hmr: _r !== null && _r !== undefined && _r.hmStep !== undefined

        readonly property bool     isHackMotion:      armStatus.isHackMotion
        readonly property QtObject leadImu:           armStatus.leadImu
        readonly property QtObject leadDevice:        armStatus.leadDevice
        readonly property bool     calibrationDone:   armStatus.calibrationDone
        readonly property bool     calibrationFailed: (_wt || _hmr) ? _r.calibrationFailed : false

        // Witmotion
        readonly property int    calibPhase:       _wt ? _r.calibPhase       : 0
        readonly property real   phaseProgress:    _wt ? _r.phaseProgress    : 0.0
        readonly property real   phase1AccumMs:    _wt ? _r.phase1AccumMs    : 0.0
        readonly property real   _captureHoldMs:   _wt ? _r._captureHoldMs   : 1
        readonly property bool   _armDownCaptured: _wt ? _r._armDownCaptured : false
        readonly property bool   mountFailed:      _wt ? _r.mountFailed      : false
        readonly property string mountFailMsg:     _wt ? _r.mountFailMsg     : ""

        // HackMotion
        readonly property int    hmStep:               _hmr ? _r.hmStep               : 0
        readonly property int    hmPhase:              _hmr ? _r.hmPhase              : 0
        readonly property bool   hmAwaitingPresence:   _hmr ? _r.hmAwaitingPresence   : false
        readonly property string hmFailMsg:            _hmr ? _r.hmFailMsg            : ""
        readonly property string hmFailKind:           _hmr ? _r.hmFailKind           : "error"
        readonly property real   _hmRaiseTravelDeg:    _hmr ? _r._hmRaiseTravelDeg    : Number.NaN
        readonly property real   _hmMinRaiseTravelDeg: _hmr ? _r._hmMinRaiseTravelDeg : 0
        readonly property int    calpMarkingPose0:     _hmr ? _r.calpMarkingPose0     : -1
        readonly property int    calpMarkingPose1:     _hmr ? _r.calpMarkingPose1     : -1
        readonly property int    calUncalibrated:      _hmr ? _r.calUncalibrated      : -1
        readonly property int    calCalibrated:        _hmr ? _r.calCalibrated        : -1
        readonly property int    calLost:              _hmr ? _r.calLost              : -1

        // Presentation helpers — every one of these is STATE, never a score.
        function _hmDeg(v) { return (v === undefined || isNaN(v)) ? "—" : v.toFixed(2) + "°" }
        function _hmStateText(s) {
            if (s === calCalibrated)   return Theme.caps(qsTr("Calibrated"))
            if (s === calUncalibrated) return Theme.caps(qsTr("Uncalibrated"))
            if (s === calLost)         return Theme.caps(qsTr("Lost"))
            return Theme.caps(qsTr("Not checked"))
        }
    }

    Loader {
        id: statusLoader
        width: parent.width
        sourceComponent: armStatus.mode === "compact" ? compactColumn : fullColumn
    }

    // FULL — the wizard's status column.
    Component {
        id: fullColumn

        Column {
            width:   armStatus.width
            spacing: Theme.gap(16)

            Text {
                visible:            armStatus.showHeader
                width:              parent.width
                text:               armStatus.stepLabel
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
            }

            PpDisplayText {
                visible:        armStatus.showHeader
                width:          parent.width
                text:           qsTr("Calibrate Sensors")
                pixelSize:      Math.min(Theme.sp(18), Theme.fontSzDisplay)
                wrapMode:       Text.WordWrap
            }

            StatusBadge   { width: implicitWidth }
            PhaseText      { width: parent.width }
            ProgressBar    { width: parent.width }
            StatusLabel    { width: parent.width }
            AngleWarning   { width: parent.width }
            NoImuWarning   { width: parent.width }
            MountFailText  { width: parent.width }

            // Device-native routine — mutually exclusive with the four above.
            HmPhaseText    { width: parent.width }
            HmStepBar      { width: parent.width }
            HmStatusLabel  { width: parent.width }
            HmReadouts     { width: parent.width }
            HmFailText     { width: parent.width }

            PpButton {
                visible: d.calibPhase > 0 || d.calibrationDone
                         || d.calibrationFailed || d.mountFailed
                         || (d.isHackMotion && d.hmStep > 0)
                label:   qsTr("↺  Recalibrate")
                primary: false
                onClicked: armStatus.recalibrate()
            }
        }
    }

    // COMPACT — the toolbar panel's status, under its guide view.
    Component {
        id: compactColumn

        ColumnLayout {
            width:   armStatus.width
            spacing: Theme.gap(12)

            StatusBadge  { Layout.alignment: Qt.AlignLeft }
            PhaseText     { Layout.fillWidth: true }
            ProgressBar   { Layout.fillWidth: true }
            StatusLabel   { Layout.fillWidth: true }
            AngleWarning  { Layout.fillWidth: true }
            NoImuWarning  { Layout.fillWidth: true }
            MountFailText { Layout.fillWidth: true }

            // Device-native routine — mutually exclusive with the four above.
            HmPhaseText   { Layout.fillWidth: true }
            HmStepBar     { Layout.fillWidth: true }
            HmStatusLabel { Layout.fillWidth: true }
            HmReadouts    { Layout.fillWidth: true }
            HmFailText    { Layout.fillWidth: true }
        }
    }

    // ── Shared status sub-components (used by both layouts) ────────────────────
    // Status uses the wizard's existing colours: colorAccent (calibrating),
    // colorGood (done), colorWarn/colorError (issues). The compact layout
    // additionally frames its pinned action bar with the attention colour to draw
    // the eye to the controls; the status indicators themselves do not.

    // ⚠ The badge is SHARED by both routines, so "calibrating" has to be true for a
    // HackMotion too — its calibPhase never leaves 0, and a routine mid-flight
    // reading "Pending" is the kind of quietly wrong state this flow exists to
    // avoid. `calibrationFailed` is set by both routines, so _failed needs nothing.
    // ⚠ FAILED TAKES PRECEDENCE OVER CALIBRATING (CW4): a lead sensor that drops mid-run leaves
    // calibPhase ≥ 1, and testing that first kept the badge on "Calibrating" over a dead run.
    component StatusBadge: Rectangle {
        readonly property bool _complete:    d.calibrationDone
        readonly property bool _calibrating: !d.calibrationDone && !_failed
                                             && (d.calibPhase >= 1
                                                 || (d.isHackMotion && d.hmStep >= 1 && d.hmStep <= 4))
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

    // ⚠ The next four are the WITMOTION routine's status display and say things that
    // are simply untrue of a HackMotion (T-pose, hold-to-capture, arm-down). Hidden
    // rather than reworded, because the device-native routine has its own set below.
    // An invisible child is skipped by both Column and ColumnLayout, so nothing is
    // left holding space.
    component PhaseText: Text {
        visible:    !d.isHackMotion
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
        visible: !d.isHackMotion
        height:  Theme.sp(32)
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
        visible: !d.isHackMotion
        spacing: Theme.gap(6)
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
                if (d.calibrationDone)    return Theme.caps(qsTr("Calibration complete"))
                if (d.calibPhase === 0)   return Theme.caps(qsTr("Watch the guide"))
                if (d.calibPhase === 1) {
                    if (d._armDownCaptured) return Theme.caps(qsTr("Follow the guide"))
                    var imu = d.leadImu
                    if (!imu || !imu.imuConnected) return Theme.caps(qsTr("Waiting for sensor"))
                    if (d.phase1AccumMs > 0)       return qsTr("HOLD STILL — CAPTURING")
                    return Theme.caps(qsTr("Hold still"))
                }
                return qsTr("HOLD STILL — CAPTURING")
            }
            Behavior on font.pixelSize { NumberAnimation { duration: Theme.durationNormal } }
            Behavior on color           { ColorAnimation  { duration: Theme.durationNormal } }
        }
    }

    // Calibration angle warning — shown when arm-down→T-pose angle was outside
    // the expected ~90° range. ⚠ WITMOTION ONLY: it grades OUR two-pose capture,
    // and a HackMotion computes its calibration on-device from a different routine
    // with nothing host-side to grade. There is no equivalent to invent.
    component AngleWarning: Text {
        readonly property bool _show: {
            if (d.isHackMotion) return false
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

    // ⚠ WITMOTION ONLY, like the mount validation that sets it. A HackMotion
    // computes its own calibration on-device; there is no A/M to solve, nothing to
    // store, and no mount check to fill the gap with.
    component MountFailText: Text {
        visible:    d.mountFailed && !d.calibrationDone && !d.isHackMotion
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorError
        text:           d.mountFailMsg
    }

    // ── HackMotion status sub-components ──────────────────────────────────────
    // Every one of these is hidden unless the slot-A device is a HackMotion, so the
    // two routines' displays never overlap.

    component HmPhaseText: Text {
        visible:    d.isHackMotion
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        color:          Theme.colorText2
        text: {
            if (d.hmStep === 0)
                return qsTr("The sensor calibrates itself from two positions. Rest your lead "
                            + "forearm horizontal with the wrist straight — the guide shows the "
                            + "position.")
            if (d.hmStep === 1)
                return qsTr("Rest your forearm horizontal, wrist straight, and hold still.")
            if (d.hmStep === 2)
                return qsTr("Now raise your forearm smoothly across your chest, following the "
                            + "guide. The sensor watches the whole movement, so keep it to one "
                            + "smooth motion.")
            if (d.hmStep === 3)
                return qsTr("The sensor is working out its own calibration.")
            if (d.hmStep === 4)
                return qsTr("Return to the first position — forearm horizontal, wrist straight — "
                            + "and hold still while the sensor checks itself.")
            if (d.hmStep === 5)
                return qsTr("Calibrated. Hold the first position a moment longer: with the wrist "
                            + "straight and at rest, the two sensor units should now read within "
                            + "about a degree of each other.")
            return qsTr("The routine stopped. Tap Recalibrate to run it again.")
        }
    }

    // Four steps, discrete. No indeterminate progress and no elapsed-time bar: the
    // pacing that matters is the guide animation's, and a second clock next to it
    // invites the coach to hurry a motion the device is measuring.
    component HmStepBar: Row {
        visible: d.isHackMotion && d.hmStep >= 1 && d.hmStep <= 5
        spacing: Theme.gap(4)
        Repeater {
            model: 4
            Rectangle {
                width:  Theme.sp(38)
                height: Theme.sp(4)
                radius: Theme.sp(2)
                color: d.calibrationDone            ? Theme.colorGood
                     : (d.hmStep > index + 1)       ? Theme.colorAccent
                     : (d.hmStep === index + 1)     ? Theme.colorAccentLight
                     :                                Theme.colorBg3
            }
        }
    }

    component HmStatusLabel: Text {
        visible:            d.isHackMotion
        wrapMode:           Text.WordWrap
        font.family:        Theme.fontData
        font.pixelSize:     (d.hmStep >= 1 && d.hmStep <= 4) ? Theme.fontSzHeading : Theme.fontSzMicro
        font.bold:          d.hmStep >= 1 && d.hmStep <= 4
        font.letterSpacing: Theme.trackingData
        color: d.calibrationDone   ? Theme.colorGood
             : d.hmStep === 9      ? (d.hmFailKind === "warn" ? Theme.colorWarn : Theme.colorError)
             : d.hmStep >= 1       ? Theme.colorAccent
             :                       Theme.colorText3
        text: {
            if (d.calibrationDone) return Theme.caps(qsTr("Calibration complete"))
            if (d.hmStep === 9)    return Theme.caps(qsTr("Not calibrated"))
            if (d.hmStep === 0)    return Theme.caps(qsTr("Ready"))
            if (d.hmStep === 1)    return d.hmPhase === d.calpMarkingPose0
                                          ? Theme.caps(qsTr("Marking position 1")) : Theme.caps(qsTr("Hold still"))
            if (d.hmStep === 2)    return d.hmPhase === d.calpMarkingPose1
                                          ? Theme.caps(qsTr("Marking position 2")) : Theme.caps(qsTr("Follow the guide"))
            // ⚠ APPLYING and VERIFYING are NOT success. VERIFYING in particular means
            // the transform is already applied and the check has NOT been taken — so
            // no tick and nothing that reads as a verdict.
            if (d.hmStep === 3)    return qsTr("APPLYING…")
            if (d.hmStep === 4)    return d.hmAwaitingPresence ? qsTr("CHECKING…")
                                                              : Theme.caps(qsTr("Hold still"))
            return ""
        }
        Behavior on font.pixelSize { NumberAnimation { duration: Theme.durationNormal } }
        Behavior on color           { ColorAnimation  { duration: Theme.durationNormal } }
    }

    // One label/value line of the confirmation readout. Data font on the value so it
    // reads as an instrument reading rather than as a grade.
    component HmReadout: Row {
        id: hmReadoutRow
        property string label: ""
        property string value: ""
        property color  tint:  Theme.colorText2
        spacing: Theme.gap(8)
        Text {
            width:          Theme.sp(150)
            text:           hmReadoutRow.label
            wrapMode:       Text.WordWrap
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
        }
        Text {
            text:               hmReadoutRow.value
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              hmReadoutRow.tint
        }
    }

    // The confirmation readout. ⚠ Nothing here is a score and nothing here ranks
    // attempts: the relative-angle COLLAPSE is what proves the calibration took,
    // the calibration STATE is the only value that says a check was taken and
    // passed, and the presence angle is shown as state with the hold's own evidence
    // beside it.
    component HmReadouts: Column {
        visible: d.isHackMotion && (d.hmStep === 4 || d.hmStep === 5)
        spacing: Theme.gap(6)

        // ⚠ ONLY INTERPRETABLE AT REST WITH A STRAIGHT WRIST — the same stream reads
        // 170-180° mid-motion — which is why it is shown only while the athlete is
        // being asked to hold the reference pose, and labelled that way.
        HmReadout {
            label: qsTr("Both units, at rest, wrist straight")
            value: d._hmDeg(d.leadDevice ? d.leadDevice.relativeAngleDeg : NaN)
            tint:  Theme.colorText
        }
        Text {
            width:          parent.width
            wrapMode:       Text.WordWrap
            lineHeight:     1.4
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
            text: qsTr("About 15° before calibration, 0.4–0.8° after. The collapse is what shows "
                       + "the calibration took — while the arm is moving the same figure reads "
                       + "170–180° and means nothing.")
        }

        // ⚠ THE ONE FIGURE THAT SEES THE HALF THE PRESENCE CHECK CANNOT. The device
        // reports a calibration even when nothing moved (§8.2: a no-raise attempt
        // scored the BEST presence angle of three), so this is the evidence that the
        // routine was actually performed. Shown as measured travel, never as a score:
        // more than the routine's ~30° is not "better", it is a different motion.
        HmReadout {
            label: qsTr("Forearm movement we saw")
            value: d._hmDeg(d._hmRaiseTravelDeg)
            tint:  isNaN(d._hmRaiseTravelDeg) ? Theme.colorText3
                 : d._hmRaiseTravelDeg >= d._hmMinRaiseTravelDeg ? Theme.colorGood
                 : Theme.colorWarn
        }

        HmReadout {
            label: qsTr("Sensor calibration state")
            value: d._hmStateText(d.leadDevice ? d.leadDevice.calibrationState : 0)
            tint:  (d.leadDevice && d.leadDevice.calibrationState === d.calCalibrated)
                       ? Theme.colorGood : Theme.colorWarn
        }
        HmReadout {
            label: qsTr("Reference-pose check")
            value: d._hmDeg(d.leadDevice ? d.leadDevice.presenceAngleDeg : NaN)
        }
        HmReadout {
            label: qsTr("Pose hold (max spread)")
            value: d._hmDeg(d.leadDevice ? d.leadDevice.poseSpreadMaxDeg : NaN)
        }
        // ⚠ The count means different things on the two paths — samples USED when a
        // measurement was taken, samples merely COLLECTED when the library warned it
        // could not take one — so presenceNotMeasured is what says which, and the
        // number is never shown without it.
        HmReadout {
            label: qsTr("Readings averaged")
            value: {
                var dev = d.leadDevice
                if (!dev) return "—"
                if (dev.presenceNotMeasured)
                    return qsTr("%1 — too few to measure").arg(dev.presenceSamplesUsed)
                return dev.presenceSamplesUsed > 0 ? String(dev.presenceSamplesUsed) : "—"
            }
            tint: (d.leadDevice && d.leadDevice.presenceNotMeasured) ? Theme.colorError
                                                                    : Theme.colorText2
        }
        Text {
            width:          parent.width
            wrapMode:       Text.WordWrap
            lineHeight:     1.4
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
            // ⚠ Said in the UI, not only in a comment, because the temptation to
            // recalibrate until the number drops is exactly what §8.2 measured as
            // choosing the WORST attempt: no raise at all scored 0.70°, the correct
            // routine 1.96°, a raise about the wrong axis 6.10°.
            text: qsTr("The check confirms the sensor is calibrated — it is not a quality score "
                       + "and a smaller number is not a better calibration, so do not re-run to "
                       + "chase it down.")
        }
    }

    component HmFailText: Text {
        visible:    d.isHackMotion && d.hmStep === 9 && d.hmFailMsg !== ""
        wrapMode:   Text.WordWrap
        lineHeight: 1.5
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          d.hmFailKind === "warn" ? Theme.colorWarn : Theme.colorError
        text:           d.hmFailMsg
    }
}
