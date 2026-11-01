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
// values a not-yet-loaded routine would show, and works out what the status column says
// for whichever routine is in play. The column itself — header, instruction, hold bar,
// status line, failure, Recalibrate — is CalibrationStatusColumn, the one drawing of it that
// the trunk's calibrate page shares; the parts only one routine has (the badge, the wG3's step
// bar and readouts, the Witmotion's warnings) sit in its slots.
import QtQuick
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

        // ── What the status column says ────────────────────────────────────────────
        // One instruction, one bar, one status line and one failure, worded for the routine in
        // play. The Witmotion words say things that are simply untrue of a HackMotion (T-pose,
        // hold-to-capture, arm-down), so each routine has its own.
        readonly property string instruction: isHackMotion ? _hmInstruction() : _wtInstruction()
        function _wtInstruction() {
            if (calibPhase === 0)
                return qsTr("Watch the guide — this shows the T-pose position you'll hold next.")
            if (calibPhase === 1) {
                if (_armDownCaptured)
                    return qsTr("Follow the guide and raise your arm out to shoulder height.")
                return qsTr("Let your lead arm hang relaxed at your side and hold still.")
            }
            return qsTr("Hold your arm at shoulder height and keep it still — the bar fills while you hold steady.")
        }
        function _hmInstruction() {
            if (hmStep === 0)
                return qsTr("The sensor calibrates itself from two positions. Rest your lead "
                            + "forearm horizontal with the wrist straight — the guide shows the "
                            + "position.")
            if (hmStep === 1)
                return qsTr("Rest your forearm horizontal, wrist straight, and hold still.")
            if (hmStep === 2)
                return qsTr("Now raise your forearm smoothly across your chest, following the "
                            + "guide. The sensor watches the whole movement, so keep it to one "
                            + "smooth motion.")
            if (hmStep === 3)
                return qsTr("The sensor is working out its own calibration.")
            if (hmStep === 4)
                return qsTr("Return to the first position — forearm horizontal, wrist straight — "
                            + "and hold still while the sensor checks itself.")
            if (hmStep === 5)
                return qsTr("Calibrated. Hold the first position a moment longer: with the wrist "
                            + "straight and at rest, the two sensor units should now read within "
                            + "about a degree of each other.")
            return qsTr("The routine stopped. Tap Recalibrate to run it again.")
        }

        // The Witmotion hold bar (the wG3 shows its discrete step bar instead).
        readonly property real holdProgress: {
            if (calibrationDone)        return 1.0
            if (calibPhase === 2)       return phaseProgress
            if (calibPhase === 1 && !_armDownCaptured)
                return Math.min(phase1AccumMs / _captureHoldMs, 1.0)
            return 0.0
        }

        // The status line: its words, whether it is set large (the athlete must act on it now),
        // and its tone.
        readonly property bool   _wtCapturing: (calibPhase === 1 && phase1AccumMs > 0 && !_armDownCaptured)
                                               || calibPhase === 2
        readonly property string status: isHackMotion ? _hmStatus() : _wtStatus()
        readonly property bool   statusBig: isHackMotion ? (hmStep >= 1 && hmStep <= 4) : _wtCapturing
        readonly property string statusTone: {
            if (calibrationDone) return "good"
            if (isHackMotion)
                return hmStep === 9 ? (hmFailKind === "warn" ? "warn" : "error")
                     : hmStep >= 1  ? "accent"
                     :                ""
            return _wtCapturing ? "accent" : ""
        }
        function _wtStatus() {
            if (calibrationDone)    return Theme.caps(qsTr("Calibration complete"))
            if (calibPhase === 0)   return Theme.caps(qsTr("Watch the guide"))
            if (calibPhase === 1) {
                if (_armDownCaptured) return Theme.caps(qsTr("Follow the guide"))
                var imu = leadImu
                if (!imu || !imu.imuConnected) return Theme.caps(qsTr("Waiting for sensor"))
                if (phase1AccumMs > 0)         return qsTr("HOLD STILL — CAPTURING")
                return Theme.caps(qsTr("Hold still"))
            }
            return qsTr("HOLD STILL — CAPTURING")
        }
        function _hmStatus() {
            if (calibrationDone) return Theme.caps(qsTr("Calibration complete"))
            if (hmStep === 9)    return Theme.caps(qsTr("Not calibrated"))
            if (hmStep === 0)    return Theme.caps(qsTr("Ready"))
            if (hmStep === 1)    return hmPhase === calpMarkingPose0
                                        ? Theme.caps(qsTr("Marking position 1")) : Theme.caps(qsTr("Hold still"))
            if (hmStep === 2)    return hmPhase === calpMarkingPose1
                                        ? Theme.caps(qsTr("Marking position 2")) : Theme.caps(qsTr("Follow the guide"))
            // ⚠ APPLYING and VERIFYING are NOT success. VERIFYING in particular means
            // the transform is already applied and the check has NOT been taken — so
            // no tick and nothing that reads as a verdict.
            if (hmStep === 3)    return qsTr("APPLYING…")
            if (hmStep === 4)    return hmAwaitingPresence ? qsTr("CHECKING…")
                                                           : Theme.caps(qsTr("Hold still"))
            return ""
        }

        // The failure. ⚠ The Witmotion's is the mount validation's, and WITMOTION ONLY: a
        // HackMotion computes its own calibration on-device; there is no A/M to solve, nothing
        // to store, and no mount check to fill the gap with. The wG3's is its routine's stop.
        readonly property string message: isHackMotion ? (hmStep === 9 ? hmFailMsg : "")
                                                       : ((mountFailed && !calibrationDone) ? mountFailMsg : "")
        readonly property string messageKind: isHackMotion ? hmFailKind : "error"

        readonly property bool showRecalibrate: calibPhase > 0 || calibrationDone
                                                || calibrationFailed || mountFailed
                                                || (isHackMotion && hmStep > 0)

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
    // ⚠ "No wrist sensor assigned" stands where it always stood for each routine: under the
    // badge for a HackMotion, under the status for a Witmotion.
    Component {
        id: fullColumn

        CalibrationStatusColumn {
            width:      armStatus.width
            showHeader: armStatus.showHeader
            stepLabel:  armStatus.stepLabel
            title:      qsTr("Calibrate Sensors")

            leading: [
                StatusBadge  { width: implicitWidth },
                NoImuWarning { width: parent.width; forHackMotion: true }
            ]
            instruction:  d.instruction
            holdShown:    !d.isHackMotion
            holdProgress: d.holdProgress
            holdDone:     d.calibrationDone
            belowBar: [
                HmStepBar { width: parent.width }
            ]
            status:      d.status
            statusBig:   d.statusBig
            statusTone:  d.statusTone
            statusTick:  !d.isHackMotion && d.calibrationDone
            belowStatus: [
                AngleWarning { width: parent.width },
                NoImuWarning { width: parent.width; forHackMotion: false },
                HmReadouts   { width: parent.width }
            ]
            message:     d.message
            messageKind: d.messageKind

            showRecalibrate: d.showRecalibrate
            onRecalibrate:   armStatus.recalibrate()
        }
    }

    // COMPACT — the toolbar panel's status, under its guide view: the same parts
    // (CalibrationStatusColumn's), laid out by the panel's ColumnLayout with no header or button.
    Component {
        id: compactColumn

        ColumnLayout {
            width:   armStatus.width
            spacing: Theme.gap(12)

            StatusBadge  { Layout.alignment: Qt.AlignLeft }
            NoImuWarning { Layout.fillWidth: true; forHackMotion: true }
            CalibrationStatusColumn.BodyText {
                Layout.fillWidth: true
                text: d.instruction
            }
            CalibrationStatusColumn.HoldBar {
                Layout.fillWidth: true
                visible:  !d.isHackMotion
                progress: d.holdProgress
                done:     d.calibrationDone
            }
            HmStepBar    { Layout.fillWidth: true }
            CalibrationStatusColumn.StatusLine {
                Layout.fillWidth: true
                text: d.status
                big:  d.statusBig
                tone: d.statusTone
                tick: !d.isHackMotion && d.calibrationDone
            }
            AngleWarning { Layout.fillWidth: true }
            NoImuWarning { Layout.fillWidth: true; forHackMotion: false }
            HmReadouts   { Layout.fillWidth: true }
            CalibrationStatusColumn.MessageText {
                Layout.fillWidth: true
                text: d.message
                kind: d.messageKind
            }
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
    component StatusBadge: PpStatusPill {
        readonly property bool _complete:    d.calibrationDone
        readonly property bool _calibrating: !d.calibrationDone && !_failed
                                             && (d.calibPhase >= 1
                                                 || (d.isHackMotion && d.hmStep >= 1 && d.hmStep <= 4))
        readonly property bool _failed:      (d.calibrationFailed || d.mountFailed) && !d.calibrationDone

        style:  "round"
        height: Theme.sp(22)
        tone: _complete    ? "good"
            : _calibrating ? "accent"
            : _failed      ? "error"
            :                ""
        text: _complete    ? qsTr("Complete")
            : _calibrating ? qsTr("Calibrating")
            : _failed      ? qsTr("Failed")
            :                qsTr("Pending")
    }

    // An invisible part is skipped by Column and ColumnLayout alike, so a part hidden for the
    // routine not in play leaves nothing holding space.

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

    // Drawn twice, each shown for one routine only (forHackMotion), so it keeps its place in
    // each routine's column.
    component NoImuWarning: Text {
        property bool forHackMotion: false
        visible:    d.leadImu === null && d.isHackMotion === forHackMotion
        wrapMode:   Text.WordWrap
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzMicro
        color:          Theme.colorWarn
        text:           qsTr("No wrist sensor assigned to slot A. Return to the Sensors step to assign one.")
    }

    // ── HackMotion status sub-components ──────────────────────────────────────
    // Every one of these is hidden unless the slot-A device is a HackMotion, so the
    // two routines' displays never overlap.

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
            tint:  (d.leadDevice && d.leadDevice.fullyCalibrated === true)
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
}
