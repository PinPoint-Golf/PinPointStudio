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

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPoint

Item {
    id: root

    // ── Public API ────────────────────────────────────────────────────────────

    property int sessionType: 0

    signal cancelled()
    signal sessionStartRequested(int sessionType, var goals)
    signal recalibrateRequested()
    signal navigateToSettings(int panelIndex)   // 3 = Cameras, 4 = IMUs

    function reopenAtCameras() {
        currentStep = root.stepCameras
        stepStates  = ["done", "pending", "pending", "pending", "pending", "pending"]
    }

    // ── Step state ────────────────────────────────────────────────────────────

    // Named step indices (panels live in a StackLayout at these indices). Use
    // these everywhere instead of hardcoded numbers so the wizard stays robust.
    //   Goals(0) Cameras(1) IMUs(2) Calibrate(3) Confirm(4) Ready(5)
    // Calibrate + Confirm are wrist-only (hasCalibrateStep); all others run every
    // session. (Zero-G / gyro-bias is performed in the WitMotion app, not here.)
    readonly property int stepGoals:     0
    readonly property int stepCameras:   1
    readonly property int stepImus:      2
    readonly property int stepCalibrate: 3
    readonly property int stepConfirm:   4
    readonly property int stepReady:     5

    property int currentStep: 0
    // 6 entries, indexed by the named step constants above.
    property var stepStates: ["pending", "pending", "pending", "pending", "pending", "pending"]

    // Per-session IMU exclusion (device ids that won't be connected this session).
    // Seeded from appSettings.imuExcluded each time the wizard opens (see
    // onVisibleChanged), but the wizard toggle edits THIS list only — it is a
    // per-session choice and must not write back to appSettings.
    property var sessionImuExcluded: []

    // True only for Wrist Motion (index 1) — enables the Calibrate + Confirm steps.
    readonly property bool hasCalibrateStep: sessionType === 1
    // Terminal step index: Ready for wrist; for non-wrist Calibrate/Confirm are
    // skipped so the display count is 4.
    readonly property int lastStep: hasCalibrateStep ? stepReady : 4
    // Total visible steps for the "STEP x OF N" eyebrows.
    readonly property int totalSteps: hasCalibrateStep ? 5 : 4

    // ── Positional calibration state (Panel 3) ────────────────────────────────
    // Captured reference quaternions from the lead-arm IMU.
    property var  calibArmDownQuat:  null   // phase 0 — arm relaxed at side
    property var  calibArmTPoseQuat: null   // phase 2 — arm raised to T-pose
    property bool calibrationDone:   false
    // Diagnostic — Euler angles at each calibration capture (cleared by _resetCalibration)
    property var calibArmDownEuler:  null   // { roll, pitch, yaw } at arm-down capture
    property var calibArmTPoseEuler: null   // { roll, pitch, yaw } at T-pose capture

    onCalibrationDoneChanged: if (calibrationDone) calibCompleteTing.play()

    TingPlayer { id: calibCompleteTing; frequency: 4186.0 }  // C8 — two octaves above the ball ting

    function _resetCalibration() {
        if (calibPanel.leadImu) calibPanel.leadImu.clearCalibration()

        introDoneTimer.stop()
        introReadyTimer.stop()
        phase1MinHoldTimer.stop()
        phase1HoldTimer.stop()
        captureTransitionTimer.stop()
        raiseReadyTimer.stop()
        calibPanel.calibPhase         = 0
        calibPanel.phase1AccumMs      = 0
        calibPanel.stableAccumMs      = 0
        calibPanel._phase1Samples     = []
        calibPanel._phase2Samples     = []
        calibPanel.phaseProgress      = 0.0
        calibPanel._animateLeadArm    = false
        calibPanel._leadArmTarget     = calibPanel.leadArmDownQuat
        calibPanel.calibrationFailed  = false
        calibPanel._armDownCaptured   = false
        calibPanel._phase1MinHoldDone = false
        calibPanel.mountFailed        = false
        calibPanel.mountFailMsg       = ""
        calibPanel._refA              = null
        calibPanel._refB              = null
        calibPanel._refC              = null
        calibArmDownQuat              = null
        calibArmTPoseQuat             = null
        calibrationDone               = false
        calibArmDownEuler             = null
        calibArmTPoseEuler            = null
    }

    // Track the previous step so we know the direction of navigation.
    property int _prevStep: -1

    onCurrentStepChanged: {
        var prev    = _prevStep
        _prevStep   = currentStep

        if (currentStep === root.stepCalibrate) {
            introDoneTimer.stop()
            introReadyTimer.stop()
            phase1MinHoldTimer.stop()
            phase1HoldTimer.stop()
            captureTransitionTimer.stop()
            raiseReadyTimer.stop()

            if (prev >= root.stepCalibrate) {
                // Navigating backward from a later step — retain completed state.
                calibPanel.calibPhase         = 2
                calibPanel.phaseProgress      = 1.0
                calibPanel._animateLeadArm    = false
                calibPanel._leadArmTarget     = calibPanel.leadArmDownQuat
                calibPanel._armDownCaptured   = true
                calibPanel._phase1MinHoldDone = true
            } else {
                // Going forward to the Calibrate step — if the lead IMU instance
                // already has calibration from this session, restore; else start fresh.
                var imu = calibPanel.leadImu
                if (imu !== null && imu.calibrated) {
                    calibArmDownQuat              = imu.calibArmDown
                    calibArmTPoseQuat             = imu.calibArmTPose
                    calibPanel.calibPhase         = 2
                    calibPanel.phase1AccumMs      = 0
                    calibPanel.stableAccumMs      = calibPanel._captureHoldMs
                    calibPanel.phaseProgress      = 1.0
                    calibPanel._animateLeadArm    = false
                    calibPanel._leadArmTarget     = calibPanel.leadArmDownQuat
                    calibPanel.calibrationFailed  = false
                    calibPanel._armDownCaptured   = true
                    calibPanel._phase1MinHoldDone = true
                    calibrationDone               = true
                } else {
                    _resetCalibration()
                }

            }
        }
    }

    // ── Session type data ─────────────────────────────────────────────────────

    readonly property var sessionTypes: [
        { icon: "◑", name: qsTr("Swing Analysis"),
          requiredCameras: 2, optionalCamera: false, requiredImus: 3, railIndex: 1 },
        { icon: "⌖", name: qsTr("Wrist Motion"),
          requiredCameras: 0, optionalCamera: true,  requiredImus: 2, railIndex: 2 },
        { icon: "⇅", name: qsTr("Ground Forces"),
          requiredCameras: 2, optionalCamera: false, requiredImus: 3, railIndex: 3 },
        { icon: "✦", name: qsTr("AI Coach"),
          requiredCameras: 2, optionalCamera: false, requiredImus: 3, railIndex: 4 }
    ]

    // ── IMU placement requirements per session type ───────────────────────────
    // Indexed 0–3 to match sessionTypes above. Edit here to change requirements.
    // slot: label shown in the row ("A"/"B"/"C")
    // placement: where on the body the sensor is mounted
    // required: false = optional slot, shown with muted styling

    readonly property var imuRequirements: [
        // 0 — Swing Analysis
        [
            { slot: "A", placement: qsTr("Thorax"),       required: true  },
            { slot: "B", placement: qsTr("Lumbar spine"), required: true  },
            { slot: "C", placement: qsTr("T12 junction"), required: true  }
        ],
        // 1 — Wrist Motion
        [
            { slot: "A", placement: qsTr("Wrist"),        required: true  },
            { slot: "B", placement: qsTr("Hand"),         required: true  },
            { slot: "C", placement: qsTr("Upper arm"),    required: false }
        ],
        // 2 — Ground Forces
        [
            { slot: "A", placement: qsTr("Lead thigh"),   required: true  },
            { slot: "B", placement: qsTr("Trail thigh"),  required: true  },
            { slot: "C", placement: qsTr("Lumbar spine"), required: true  }
        ],
        // 3 — AI Coach (same placements as Swing Analysis)
        [
            { slot: "A", placement: qsTr("Thorax"),       required: true  },
            { slot: "B", placement: qsTr("Lumbar spine"), required: true  },
            { slot: "C", placement: qsTr("T12 junction"), required: true  }
        ]
    ]

    // ── Goal chip data — indexed 0–3 to match sessionTypes ──────────────────
    // Edit here to change goals per session type without touching UI code.

    readonly property var goalDefsByType: [
        // 0 — Swing Analysis
        [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: qsTr("CHECK WHERE I'M AT · IDENTIFY AREAS TO IMPROVE") },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: qsTr("SEGMENT VELOCITY ORDER")    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: qsTr("HIP SWAY DETECTION")        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: qsTr("IN-OUT TREND")              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: qsTr("FLEXION AT IMPACT")         }
        ],
        // 1 — Wrist Motion
        [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: qsTr("CHECK WHERE I'M AT · IDENTIFY AREAS TO IMPROVE") },
            { key: "wristAngleTop",       name: qsTr("Wrist angle at the top"),    sub: qsTr("FLAT / BOWED / CUPPED AT TOP")     },
            { key: "impactConditions",    name: qsTr("Impact conditions"),         sub: qsTr("FLEXION / EXTENSION AT CONTACT")   },
            { key: "wristAngleSequence",  name: qsTr("Wrist angle sequence"),      sub: qsTr("TRANSITION & ARC PROFILE")         },
            { key: "trailWristExtension", name: qsTr("Trail wrist extension"),     sub: qsTr("SCOOP / FLIP DETECTION")           }
        ],
        // 2 — Ground Forces
        [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: qsTr("CHECK WHERE I'M AT · IDENTIFY AREAS TO IMPROVE") },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: qsTr("SEGMENT VELOCITY ORDER")    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: qsTr("HIP SWAY DETECTION")        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: qsTr("IN-OUT TREND")              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: qsTr("FLEXION AT IMPACT")         }
        ],
        // 3 — AI Coach
        [
            { key: "generalAssessment", name: qsTr("General assessment"),  sub: qsTr("CHECK WHERE I'M AT · IDENTIFY AREAS TO IMPROVE") },
            { key: "kinematicSequence", name: qsTr("Kinematic sequence"),  sub: qsTr("SEGMENT VELOCITY ORDER")    },
            { key: "xFactor",           name: qsTr("X-factor"),            sub: qsTr("HIP–SHOULDER SEPARATION")   },
            { key: "swingTempo",        name: qsTr("Swing tempo"),          sub: qsTr("BACK : DOWN RATIO")         },
            { key: "earlyExtension",    name: qsTr("Early extension"),      sub: qsTr("HIP SWAY DETECTION")        },
            { key: "clubPath",          name: qsTr("Club path"),            sub: qsTr("IN-OUT TREND")              },
            { key: "wristAngles",       name: qsTr("Wrist angles"),         sub: qsTr("FLEXION AT IMPACT")         }
        ]
    ]

    readonly property var curGoalDefs: goalDefsByType[sessionType]

    property var  selectedGoals:   []
    property bool goalsInteracted: false

    // ── TODO stubs for camera properties not yet on cameraManager ───────────────

    property bool _todo_stereoCalibrationValid: false  // TODO: bind to real property
    property bool _todo_triangulationValid:     false  // TODO: bind to real property

    // ── IMU detail string when connected ──────────────────────────────────────

    readonly property string imuDetail: {
        var insts = imuManager.instances // rebind on connection changes
        if (insts.length === 0) return ""
        // Show detail from the first connected instance.
        var inst = null
        for (var i = 0; i < insts.length; ++i) {
            if (insts[i].imuConnected) { inst = insts[i]; break }
        }
        if (!inst) return ""
        var parts = []
        var hz = inst.dataRateHz
        if (hz > 0) parts.push(Math.round(hz) + " Hz")
        var rate = inst.outputRateHz
        if (rate > 0) parts.push(qsTr("configured %1 Hz").arg(rate))
        var bat = inst.batteryPercent
        if (bat >= 0) parts.push(qsTr("battery %1%").arg(bat))
        if (insts.length > 1) parts.push(qsTr("%1 IMUs").arg(insts.length))
        return parts.join(" · ")
    }

    // ── Convenience ───────────────────────────────────────────────────────────

    readonly property var curType:    sessionTypes[sessionType]
    readonly property var curImuReqs: imuRequirements[sessionType]

    // Find the cameraList entry assigned to a given perspective.
    // perspective: 2 = face-on, 1 = down-the-line (matches VideoController convention).
    // Reads directly from cameraList which now includes the persisted perspective field,
    // so this works whether or not cameras are currently selected.
    readonly property var faceOnData: {
        var list = cameraManager.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].perspective === 2) return list[i]
        return null
    }

    readonly property var dtlData: {
        var list = cameraManager.cameraList
        for (var i = 0; i < list.length; ++i)
            if (list[i].perspective === 1) return list[i]
        return null
    }

    function camDetail(d) {
        if (!d) return ""
        var sn  = d.serialNumber ? qsTr("SN: ") + d.serialNumber : ""
        var res = (d.maxWidth && d.maxHeight) ? d.maxWidth + " × " + d.maxHeight : ""
        var ifc = d.interface || ""
        return [d.alias || d.description, sn, ifc, res].filter(function(s){ return s !== "" }).join(" · ")
    }

    // True when at least one of the session's cameras is marked fixed in place —
    // in that case stereo calibration and triangulation become optional steps.
    readonly property bool anyFixedCamera: {
        var fixed = appSettings.cameraFixedInPlace
        if (faceOnData && fixed[faceOnData.cameraKey]) return true
        if (dtlData    && fixed[dtlData.cameraKey])    return true
        return false
    }

    readonly property bool camsOk: {
        if (curType.optionalCamera) return true
        if (!faceOnData || !dtlData) return false
        if (!anyFixedCamera && (!_todo_stereoCalibrationValid || !_todo_triangulationValid)) return false
        return true
    }

    // True when a device in imuDeviceList has been assigned to the given slot letter.
    // Reactive: re-evaluates when imuDeviceList or imuPlacement changes.
    function deviceForSlot(slotLetter) {
        var list      = imuManager.imuDeviceList
        var placement = appSettings.imuPlacement
        for (var i = 0; i < list.length; ++i)
            if (placement[list[i].id] === slotLetter) return list[i]
        return null
    }

    // All required IMU slots assigned.
    readonly property bool imusOk: {
        var reqs      = root.curImuReqs
        var list      = imuManager.imuDeviceList
        var placement = appSettings.imuPlacement
        for (var i = 0; i < reqs.length; ++i) {
            if (!reqs[i].required) continue
            var found = false
            for (var j = 0; j < list.length; ++j)
                if (placement[list[j].id] === reqs[i].slot) { found = true; break }
            if (!found) return false
        }
        return reqs.some(function(r) { return r.required })
    }

    // True when every required IMU slot has a connected instance.
    // Reactive: imuManager.instances fires instancesChanged() whenever any
    // ImuInstance.imuConnected changes (see imu_manager.cpp lambda at createInstance).
    readonly property bool imusAllConnected: {
        var _dep = imuManager.instances   // reactive dependency
        var reqs      = root.curImuReqs
        var list      = imuManager.imuDeviceList
        var placement = appSettings.imuPlacement
        for (var i = 0; i < reqs.length; ++i) {
            if (!reqs[i].required) continue
            var found = false
            for (var j = 0; j < list.length; ++j) {
                if (placement[list[j].id] === reqs[i].slot) {
                    var inst = imuManager.instanceFor(list[j].id)
                    if (inst && inst.imuConnected) { found = true; break }
                }
            }
            if (!found) return false
        }
        return reqs.some(function(r) { return r.required })
    }

    readonly property bool anySkipped: stepStates[1] === "skipped" || stepStates[2] === "skipped"

    // Every unmet requirement — drives the Ready panel content.
    // Reactive: re-evaluates when hardware state, step states, or placements change.
    // Each entry: { text: string, panel: int }
    // panel >= 0 renders a clickable "→ Open … settings" link that navigates
    // to that Settings sub-panel (3 = Cameras, 4 = IMUs) and returns here via ‹.
    // panel = -1 means the issue has no actionable settings link.
    readonly property var readinessIssues: {
        var issues = []

        // Camera requirements
        if (stepStates[1] === "skipped") {
            issues.push({ text: qsTr("Cameras skipped — no video will be captured this session"), panel: -1 })
        } else if (!curType.optionalCamera) {
            if (!faceOnData)
                issues.push({ text: qsTr("Face-on camera not assigned"), panel: 3 })
            if (!dtlData)
                issues.push({ text: qsTr("Down-the-line camera not assigned"), panel: 3 })
            // Calibration and triangulation are only required when cameras are not fixed in place
            if (!anyFixedCamera) {
                if (!_todo_stereoCalibrationValid)
                    issues.push({ text: qsTr("Stereo calibration not confirmed — use Recalibrate in the cameras step"), panel: -1 })
                if (!_todo_triangulationValid)
                    issues.push({ text: qsTr("Triangulation not confirmed"), panel: -1 })
            }
        }

        // IMU requirements — one entry per unassigned required slot, plus calibration
        if (stepStates[2] === "skipped") {
            issues.push({ text: qsTr("Motion sensors skipped — no movement data will be captured"), panel: -1 })
        } else {
            var reqs      = curImuReqs
            var list      = imuManager.imuDeviceList
            var placement = appSettings.imuPlacement
            for (var i = 0; i < reqs.length; ++i) {
                if (!reqs[i].required) continue
                var found = false
                for (var j = 0; j < list.length; ++j)
                    if (placement[list[j].id] === reqs[i].slot) { found = true; break }
                if (!found)
                    issues.push({ text: qsTr("IMU %1 — %2 not assigned")
                                            .arg(reqs[i].slot).arg(reqs[i].placement),
                                  panel: 4 })
            }
            if (root.hasCalibrateStep && !root.calibrationDone)
                issues.push({ text: qsTr("Sensor position calibration not completed — return to the Calibrate step"), panel: -1 })
        }

        return issues
    }

    readonly property bool fullyReady: readinessIssues.length === 0

    readonly property int contentWidth: Theme.contentWidth(width)

    onVisibleChanged: {
        if (visible) {
            var saved = appSettings.sessionGoalsByType[sessionType.toString()]
            selectedGoals   = (saved && saved.length > 0) ? saved.slice() : []
            goalsInteracted = false
            // Snapshot the persisted exclusions as this session's starting point.
            sessionImuExcluded = appSettings.imuExcluded.slice()
        }
    }

    // ── Page layout ───────────────────────────────────────────────────────────

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ────────────────────────────────────────────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: hdrInner.implicitHeight + Theme.sp(40)

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width:  root.contentWidth
                height: parent.height

                Column {
                    id: hdrInner
                    anchors {
                        left:           parent.left
                        right:          closeBtn.left
                        verticalCenter: parent.verticalCenter
                        rightMargin:    Theme.sp(16)
                    }
                    spacing: Theme.sp(6)

                    Row {
                        spacing: Theme.sp(8)
                        Text {
                            text:           root.curType.icon
                            font.pixelSize: Theme.sp(20)
                            color:          Theme.colorText2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text:           root.curType.name
                            font.family:    Theme.fontDisplay
                            font.italic:    Theme.fontDisplayItalic
                            font.weight: Theme.fontDisplayWeight
                            font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                            color:          Theme.colorText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        width:          parent.width
                        text:           qsTr("Before you step up, let's make sure everything is ready. We'll confirm your goals, check your cameras, and connect your sensors — it only takes a moment.")
                        font.family:    Theme.fontBody
                        font.weight:    Theme.fontBodyWeight
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText2
                        wrapMode:       Text.WordWrap
                        lineHeight:     1.65
                    }

                    Text {
                        text:               athleteController.currentName !== ""
                                                ? athleteController.currentName.toUpperCase()
                                                : qsTr("NO ATHLETE")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color:              Theme.colorText3
                    }
                }

                Rectangle {
                    id: closeBtn
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    width: Theme.sp(28); height: Theme.sp(28)
                    radius: Theme.sp(14)
                    color: closeMa.containsMouse ? Theme.colorBg2 : "transparent"
                    border.width: 1
                    border.color: closeMa.containsMouse ? Theme.colorBorderStrong : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text:           "✕"
                        font.pixelSize: Theme.sp(13)
                        color:          closeMa.containsMouse ? Theme.colorText : Theme.colorText3
                    }

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.cancelled()
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Tab strip ─────────────────────────────────────────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(56)

            Row {
                anchors.centerIn: parent
                spacing: 0

                Repeater {
                    // 6 tabs always present; the Calibrate + Confirm tabs are hidden
                    // for non-wrist sessions. Indices match the named step constants.
                    model: [qsTr("Goals"), qsTr("Cameras"), qsTr("IMUs"), qsTr("Calibrate"), qsTr("Confirm"), qsTr("Ready")]

                    delegate: Row {
                        id: tabRow
                        required property string modelData
                        required property int    index
                        spacing: 0
                        visible: (tabRow.index !== root.stepCalibrate && tabRow.index !== root.stepConfirm)
                                 || root.hasCalibrateStep

                        Item {
                            visible: tabRow.index > 0
                            width:   Theme.sp(28); height: Theme.sp(32)

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left:  parent.left
                                anchors.right: parent.right
                                height: 1
                                color: root.stepStates[tabRow.index - 1] === "done"
                                           ? Theme.colorGood : Theme.colorBorderMid
                                Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                            }
                        }

                        Column {
                            spacing: Theme.sp(4)

                            readonly property string pipState: {
                                if (tabRow.index === root.currentStep) return "current"
                                var s = root.stepStates[tabRow.index]
                                if (s === "done")    return "done"
                                if (s === "skipped") return "skipped"
                                return "future"
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Theme.sp(16); height: Theme.sp(16)
                                radius: Theme.sp(8)
                                color: {
                                    var s = parent.pipState
                                    if (s === "current") return Theme.colorAccent
                                    if (s === "done")    return Theme.colorGoodLight
                                    if (s === "skipped") return Theme.colorWarnLight
                                    return Theme.colorBg3
                                }
                                border.width: 1
                                border.color: {
                                    var s = parent.pipState
                                    if (s === "current") return Theme.colorAccent
                                    if (s === "done")    return Theme.colorGood
                                    if (s === "skipped") return Theme.colorWarn
                                    return Theme.colorBorderMid
                                }
                                Behavior on color        { ColorAnimation { duration: Theme.durationNormal } }
                                Behavior on border.color { ColorAnimation { duration: Theme.durationNormal } }

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        var s = parent.parent.pipState
                                        if (s === "done")    return "✓"
                                        if (s === "skipped") return "⚠"
                                        return (tabRow.index + 1).toString()
                                    }
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: {
                                        var s = parent.parent.pipState
                                        if (s === "current") return Theme.dark ? Theme.colorBg : "#FFFFFF"
                                        if (s === "done")    return Theme.colorGood
                                        if (s === "skipped") return Theme.colorWarn
                                        return Theme.colorText3
                                    }
                                }
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text:           tabRow.modelData
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzMicro
                                color: {
                                    var s = parent.pipState
                                    if (s === "current") return Theme.colorAccent
                                    if (s === "done")    return Theme.colorGood
                                    if (s === "skipped") return Theme.colorWarn
                                    return Theme.colorText3
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Body — Flickable so tall panels don't clip ────────────────────────

        Flickable {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            contentWidth:  width
            contentHeight: bodyPanel.implicitHeight + Theme.sp(40)
            clip:          true

            Item {
                id: bodyPanel
                anchors.horizontalCenter: parent.horizontalCenter
                width:          root.contentWidth
                implicitHeight: bodyStack.implicitHeight

                StackLayout {
                    id: bodyStack
                    width:        parent.width
                    currentIndex: root.currentStep

                    // ── Panel 0: Goals ────────────────────────────────────────

                    Item {
                        implicitHeight: goalsCol.implicitHeight + Theme.sp(32)

                        Column {
                            id: goalsCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
                            spacing: Theme.sp(16)

                            StepIntro {
                                width:   parent.width
                                eyebrow: qsTr("STEP 1 OF %1 · GOALS").arg(root.totalSteps)
                                heading: qsTr("What are you working on today?")
                                body:    qsTr("Pick the areas you want to focus on this session. Pinpoint will highlight these metrics in the live view and lead with them in your summary. We've picked up where you left off — change anything you like and we'll remember it next time.")
                            }

                            GridLayout {
                                width:         parent.width
                                columns:       2
                                columnSpacing: Theme.sp(8)
                                rowSpacing:    Theme.sp(8)

                                Repeater {
                                    model: root.curGoalDefs

                                    delegate: Rectangle {
                                        id: chip
                                        required property var modelData
                                        required property int index

                                        Layout.fillWidth:    true
                                        Layout.preferredHeight: chipCol.implicitHeight + Theme.sp(36)
                                        radius: Theme.radius

                                        readonly property bool sel: root.selectedGoals.indexOf(modelData.key) !== -1
                                        readonly property bool showLast: !root.goalsInteracted
                                                                         && sel
                                                                         && root.selectedGoals.length > 0
                                                                         && root.selectedGoals[0] === modelData.key

                                        color:        sel ? Theme.colorAccentLight : "transparent"
                                        border.width: 1
                                        border.color: sel ? Theme.colorAccent : Theme.colorBorderMid

                                        Column {
                                            id: chipCol
                                            anchors {
                                                left: parent.left; right: parent.right
                                                leftMargin: Theme.sp(14); rightMargin: Theme.sp(14)
                                                verticalCenter: parent.verticalCenter
                                            }
                                            spacing: Theme.sp(5)

                                            Row {
                                                spacing: Theme.sp(6)
                                                Text {
                                                    text:           chip.modelData.name
                                                    font.family:    Theme.fontBody
                                                    font.pixelSize: Theme.fontSzBody2
                                                    color:          chip.sel ? Theme.colorAccent : Theme.colorText
                                                }
                                                Text {
                                                    visible:        chip.showLast
                                                    text:           qsTr("↩ last session")
                                                    font.family:    Theme.fontData
                                                    font.pixelSize: Theme.fontSzMicro
                                                    color:          Theme.colorText3
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }
                                            Text {
                                                width:              parent.width
                                                text:               chip.modelData.sub
                                                font.family:        Theme.fontData
                                                font.pixelSize:     Theme.fontSzMicro
                                                font.letterSpacing: Theme.trackingData
                                                color:              Theme.colorText3
                                                elide:              Text.ElideRight
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape:  Qt.PointingHandCursor
                                            onClicked: {
                                                root.goalsInteracted = true
                                                var k   = chip.modelData.key
                                                var arr = root.selectedGoals.slice()
                                                var i   = arr.indexOf(k)
                                                if (i === -1) arr.push(k)
                                                else          arr.splice(i, 1)
                                                root.selectedGoals = arr
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Panel 1: Cameras ──────────────────────────────────────

                    Item {
                        implicitHeight: camsCol.implicitHeight + Theme.sp(32)

                        Column {
                            id: camsCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
                            spacing: Theme.sp(16)

                            StepIntro {
                                width:   parent.width
                                eyebrow: qsTr("STEP 2 OF %1 · CAMERAS").arg(root.totalSteps)
                                heading: qsTr("Checking your cameras")
                                body:    root.curType.optionalCamera
                                    ? qsTr("A face-on camera is optional for this session type. If you have one set up it will capture video alongside your IMU data, but you can start without it.")
                                    : qsTr("This session uses two cameras — face-on and down the line — to reconstruct your movement in 3D. Both need to be detected and stereo-calibrated before we start. If your setup hasn't moved since last time, you're good to go.")
                            }

                            Column {
                                width: parent.width
                                spacing: 0

                                // Face-on: always shown; optional for Wrist Motion
                                CheckRow {
                                    width:    parent.width
                                    ok:       root.faceOnData !== null
                                    optional: root.curType.optionalCamera
                                    label:    qsTr("Face-on camera")
                                    subOk:    root.camDetail(root.faceOnData)
                                    subFail:  root.curType.optionalCamera
                                                  ? qsTr("OPTIONAL — ASSIGN FACE-ON PERSPECTIVE IN SETTINGS → CAMERAS")
                                                  : qsTr("NOT FOUND — ASSIGN FACE-ON PERSPECTIVE IN SETTINGS → CAMERAS")
                                }

                                // Down-the-line: only needed when two cameras are required
                                CheckRow {
                                    visible: !root.curType.optionalCamera
                                    width:   parent.width
                                    ok:      root.dtlData !== null
                                    label:   qsTr("Down-the-line camera")
                                    subOk:   root.camDetail(root.dtlData)
                                    subFail: qsTr("NOT FOUND — ASSIGN DOWN-THE-LINE PERSPECTIVE IN SETTINGS → CAMERAS")
                                }

                                // Stereo calibration and triangulation: two-camera sessions only
                                CheckRow {
                                    visible:      !root.curType.optionalCamera
                                    width:        parent.width
                                    ok:           root._todo_stereoCalibrationValid
                                    optional:     root.anyFixedCamera
                                    label:        qsTr("Stereo calibration")
                                    subOk:        qsTr("Calibration valid")
                                    subFail:      root.anyFixedCamera
                                                      ? qsTr("OPTIONAL — CAMERAS ARE FIXED IN PLACE")
                                                      : qsTr("CALIBRATION NEEDED")
                                    showRecal:    true
                                    recalEnabled: root.faceOnData !== null && root.dtlData !== null
                                    onRecalibrate: root.recalibrateRequested()
                                }
                                CheckRow {
                                    visible:  !root.curType.optionalCamera
                                    width:    parent.width
                                    ok:       root._todo_triangulationValid
                                    optional: root.anyFixedCamera
                                    label:    qsTr("Triangulation")
                                    subOk:    qsTr("Baseline confirmed")
                                    subFail:  root.anyFixedCamera
                                                  ? qsTr("OPTIONAL — CAMERAS ARE FIXED IN PLACE")
                                                  : qsTr("NOT CONFIRMED")
                                }
                            }

                            // Settings deep-link
                            Text {
                                text:           qsTr("→ Open camera settings")
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          Theme.colorAccent
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape:  Qt.PointingHandCursor
                                    onClicked:    root.navigateToSettings(3)
                                }
                            }
                        }
                    }

                    // ── Panel 2: IMUs ─────────────────────────────────────────

                    Item {
                        implicitHeight: imusCol.implicitHeight + Theme.sp(32)

                        Column {
                            id: imusCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
                            spacing: Theme.sp(16)

                            // ── Connect queue state ───────────────────────────
                            property var  _connectQueue: []
                            property int  _connectIdx:   0
                            property bool _connecting:   false

                            // Build the list of assigned-but-unconnected device indices
                            // for the current session's required slots, then connect them
                            // sequentially with a 2-second gap between each to give BlueZ
                            // time to reset its GATT state.
                            function startConnect() {
                                var queue = []
                                var reqs = root.curImuReqs
                                var list = imuManager.imuDeviceList
                                var placement = appSettings.imuPlacement
                                var excluded  = root.sessionImuExcluded
                                for (var i = 0; i < reqs.length; ++i) {
                                    for (var j = 0; j < list.length; ++j) {
                                        if (placement[list[j].id] === reqs[i].slot
                                                && excluded.indexOf(list[j].id) < 0) {
                                            var inst = imuManager.instanceFor(list[j].id)
                                            if (!inst || !inst.imuConnected)
                                                queue.push(list[j].index)
                                        }
                                    }
                                }
                                if (queue.length === 0) return
                                _connectQueue = queue
                                _connectIdx   = 0
                                _connecting   = true
                                imuManager.setSelected(queue[0], true)
                                _connectIdx   = 1
                                if (_connectIdx < queue.length)
                                    imuConnectTimer.start()
                                else
                                    _connecting = false
                            }

                            Timer {
                                id: imuConnectTimer
                                interval: 2000
                                repeat:   false
                                onTriggered: {
                                    var queue = imusCol._connectQueue
                                    var idx   = imusCol._connectIdx
                                    if (idx < queue.length) {
                                        imuManager.setSelected(queue[idx], true)
                                        imusCol._connectIdx = idx + 1
                                        if (imusCol._connectIdx < queue.length)
                                            imuConnectTimer.restart()
                                        else
                                            imusCol._connecting = false
                                    } else {
                                        imusCol._connecting = false
                                    }
                                }
                            }

                            StepIntro {
                                width:   parent.width
                                eyebrow: qsTr("STEP 3 OF %1 · MOTION SENSORS").arg(root.totalSteps)
                                heading: qsTr("Attaching your motion sensors")
                                body:    qsTr("Switch each sensor on before stepping up. The positions below are specific to this session type — attach them in the right place and pair each one. Assign placements in Settings → IMUs if you haven't already.")
                            }

                            // ── Scan / Connect header ─────────────────────────
                            RowLayout {
                                width: parent.width
                                spacing: Theme.sp(8)

                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var n = imuManager.imuDeviceList.length
                                        return n === 0
                                            ? qsTr("NO DEVICES FOUND")
                                            : n === 1 ? qsTr("1 DEVICE FOUND")
                                                      : qsTr("%1 DEVICES FOUND").arg(n)
                                    }
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color: imuManager.imuDeviceList.length > 0
                                               ? Theme.colorText3 : Theme.colorWarn
                                }

                                // Scan button — mirrors ImusPanel.qml exactly
                                Rectangle {
                                    id: imuWizScanBtn
                                    property bool scanning: false

                                    implicitWidth:  imuWizScanMeasure.implicitWidth + Theme.sp(24)
                                    implicitHeight: Theme.sp(28)
                                    radius: Theme.radius
                                    color:  scanning ? Theme.colorAccentLight : "transparent"
                                    border.width: 1
                                    border.color: scanning ? Theme.colorAccent : Theme.colorBorderStrong
                                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                                    Text {
                                        id: imuWizScanMeasure
                                        visible: false
                                        text: qsTr("Scanning…")
                                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text:           imuWizScanBtn.scanning ? qsTr("Scanning…") : qsTr("Scan")
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          imuWizScanBtn.scanning ? Theme.colorAccent : Theme.colorText2
                                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                    }
                                    Timer {
                                        id: imuWizScanTimer
                                        interval: 30000
                                        onTriggered: imuWizScanBtn.scanning = false
                                    }
                                    Connections {
                                        target: imuManager
                                        function onImuEnumeratedCountChanged() {
                                            imuWizScanTimer.stop()
                                            imuWizScanBtn.scanning = false
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape:  Qt.PointingHandCursor
                                        onClicked: {
                                            imuWizScanBtn.scanning = true
                                            imuManager.rescanImu()
                                            imuWizScanTimer.restart()
                                        }
                                    }
                                }

                                // Connect button — works through each found-but-unconnected
                                // required sensor; 2-second gap between each to let BlueZ reset.
                                Rectangle {
                                    id: imuWizConnectBtn

                                    readonly property bool canConnect: {
                                        var _dep = imuManager.instances   // reactive
                                        if (imusCol._connecting) return false
                                        var reqs = root.curImuReqs
                                        var list = imuManager.imuDeviceList
                                        var placement = appSettings.imuPlacement
                                        var excluded  = root.sessionImuExcluded
                                        for (var i = 0; i < reqs.length; ++i) {
                                            for (var j = 0; j < list.length; ++j) {
                                                if (placement[list[j].id] === reqs[i].slot
                                                        && excluded.indexOf(list[j].id) < 0) {
                                                    var inst = imuManager.instanceFor(list[j].id)
                                                    if (!inst || !inst.imuConnected) return true
                                                }
                                            }
                                        }
                                        return false
                                    }

                                    implicitWidth:  imuWizConnMeasure.implicitWidth + Theme.sp(24)
                                    implicitHeight: Theme.sp(28)
                                    radius: Theme.radius
                                    color:  imusCol._connecting ? Theme.colorAccentLight : "transparent"
                                    border.width: 1
                                    border.color: imusCol._connecting ? Theme.colorAccent : Theme.colorBorderStrong
                                    opacity: canConnect ? 1.0 : 0.4
                                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                                    Behavior on opacity      { NumberAnimation { duration: Theme.durationFast } }

                                    Text {
                                        id: imuWizConnMeasure
                                        visible: false
                                        text: qsTr("Connecting…")
                                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Theme.fontBodyWeight
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text:           imusCol._connecting ? qsTr("Connecting…") : qsTr("Connect")
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          imusCol._connecting ? Theme.colorAccent : Theme.colorText2
                                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled:      imuWizConnectBtn.canConnect
                                        cursorShape:  Qt.PointingHandCursor
                                        onClicked:    imusCol.startConnect()
                                    }
                                }
                            }

                            Column {
                                width: parent.width
                                spacing: 0

                                // Per-slot rows driven by the imuRequirements table.
                                // A slot is "found" when a device in imuDeviceList has been
                                // assigned to that slot letter in appSettings.imuPlacement.
                                Repeater {
                                    model: root.curImuReqs

                                    delegate: CheckRow {
                                        required property var modelData
                                        required property int index

                                        width:    parent.width
                                        optional: !modelData.required
                                        label:    qsTr("IMU %1 — %2").arg(modelData.slot).arg(modelData.placement)

                                        // Resolved device for this slot (null = unassigned).
                                        // imuManager.instances is a reactive dependency: any
                                        // imuConnected change fires instancesChanged() which
                                        // re-evaluates these bindings.
                                        readonly property var _dev: {
                                            var _dep = imuManager.instances
                                            var list      = imuManager.imuDeviceList
                                            var placement = appSettings.imuPlacement
                                            for (var i = 0; i < list.length; ++i)
                                                if (placement[list[i].id] === modelData.slot) return list[i]
                                            return null
                                        }

                                        // Live instance — re-evaluated when imuManager.instances
                                        // changes. Once non-null, QML tracks _inst.stateLabel and
                                        // _inst.imuConnected directly for fine-grained reactivity.
                                        property QtObject _inst: {
                                            var _dep = imuManager.instances
                                            return _dev ? imuManager.instanceFor(_dev.id) : null
                                        }

                                        // QML tracks these on _inst directly: stateLabelChanged()
                                        // and imuConnectedChanged() on the instance update them.
                                        readonly property string _stateLabel: _inst ? _inst.stateLabel    : ""
                                        readonly property bool   _connected:  _inst ? _inst.imuConnected  : false

                                        // Excluded = disabled for connection THIS SESSION only.
                                        // Seeded from appSettings.imuExcluded at wizard open, but the
                                        // toggle edits root.sessionImuExcluded — it never writes back
                                        // to settings (per-session choice).
                                        readonly property bool _excluded:
                                            _dev !== null && root.sessionImuExcluded.indexOf(_dev.id) >= 0

                                        disabled:      _excluded
                                        subDisabled:   _dev
                                            ? qsTr("%1 · DISABLED — WON'T CONNECT").arg(_dev.alias || _dev.description)
                                            : qsTr("DISABLED — WON'T CONNECT")
                                        // Toggle shown once a device is assigned to this slot.
                                        showToggle:    _dev !== null
                                        toggleChecked: !_excluded
                                        onToggled: (v) => {
                                            if (!_dev) return
                                            var list = root.sessionImuExcluded.slice()
                                            var idx  = list.indexOf(_dev.id)
                                            if (!v && idx < 0)  list.push(_dev.id)     // disable → exclude
                                            if ( v && idx >= 0) list.splice(idx, 1)    // enable  → include
                                            root.sessionImuExcluded = list
                                            // Disabling a device that is selected/connected must
                                            // actually disconnect it so it leaves the session.
                                            if (!v) imuManager.setSelected(_dev.index, false)
                                        }

                                        ok:   !_excluded && _connected
                                        warn: !_excluded && _dev !== null && !_connected

                                        // RHS chip: appears as soon as an instance exists and
                                        // updates in real-time as the connection progresses.
                                        // Suppressed while excluded (the dimmed row + toggle say it all).
                                        chipText: {
                                            if (_excluded || !_inst) return ""
                                            if (_connected) return qsTr("Connected")
                                            if (_stateLabel === "Error" || _stateLabel === "Not found")
                                                return qsTr("Connection Failed")
                                            return qsTr("Connecting")
                                        }
                                        chipColor: {
                                            if (!_inst) return "transparent"
                                            if (_connected) return Theme.colorGoodLight
                                            if (_stateLabel === "Error" || _stateLabel === "Not found")
                                                return Theme.colorErrorLight
                                            return Theme.colorWarnLight
                                        }
                                        chipTextColor: {
                                            if (!_inst) return Theme.colorText3
                                            if (_connected) return Theme.colorGood
                                            if (_stateLabel === "Error" || _stateLabel === "Not found")
                                                return Theme.colorError
                                            return Theme.colorWarn
                                        }

                                        subOk: {
                                            if (!_dev) return ""
                                            return [_dev.alias || _dev.description, _dev.transport, _dev.id]
                                                       .filter(function(s){ return s && s !== "" })
                                                       .join(" · ")
                                        }

                                        subWarn: {
                                            if (!_dev) return ""
                                            return (_dev.alias || _dev.description) + qsTr(" — PRESS CONNECT")
                                        }

                                        subFail: {
                                            var noDevices = imuManager.imuDeviceList.length === 0
                                            if (noDevices)
                                                return modelData.required
                                                    ? qsTr("NO DEVICES FOUND — CLICK SCAN")
                                                    : qsTr("OPTIONAL — NO DEVICES FOUND")
                                            return modelData.required
                                                ? qsTr("NOT ASSIGNED — OPEN SETTINGS → IMUS")
                                                : qsTr("OPTIONAL — NOT ASSIGNED")
                                        }
                                    }
                                }

                            }

                            // Settings deep-link
                            Text {
                                text:           qsTr("→ Open IMU settings")
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                color:          Theme.colorAccent
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape:  Qt.PointingHandCursor
                                    onClicked:    root.navigateToSettings(4)
                                }
                            }
                        }
                    }

                    // ── Panel 3: Calibrate Sensors ───────────────────────────
                    // Only reachable for Wrist Motion (hasCalibrateStep).
                    // The panel fills the available height — no Flickable wrapper.

                    Item {
                        id: calibPanel

                        // Fill the visible area of the Flickable viewport.
                        implicitHeight: Math.max(Theme.sp(480), bodyStack.parent.parent.height - Theme.sp(80))

                        // ── Calibration state machine ─────────────────────────
                        // phase 0 — user holds lead arm straight down; wait for IMU stable
                        // phase 1 — animated guide: arm moves from down → T-pose
                        // phase 2 — user raises arm to T-pose; hold 3 s to capture

                        property int  calibPhase:    0
                        property real phaseProgress: 0.0   // 0–1 for phase 2 hold timer

                        readonly property bool rightHanded: athleteController.currentHandedness !== "Left"

                        // T-pose seed quaternions (match BodyPoseAdapter pre-seed values).
                        readonly property quaternion tPoseQuat: calibPanel.rightHanded
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
                        property quaternion _leadArmTarget:  calibPanel.leadArmDownQuat
                        property bool       _animateLeadArm: false
                        // Set when arm-down is captured; prevents the phase-1 Connections
                        // from re-triggering captureTransitionTimer during the raise animation.
                        property bool _armDownCaptured: false
                        // True only after phase1MinHoldTimer fires — gives the user a 2s settle
                        // window after phase 1 begins before the stillness-gated capture starts.
                        property bool _phase1MinHoldDone: false

                        // Set when the lead IMU disconnects mid-calibration.
                        property bool   calibrationFailed: false

                        Connections {
                            target:  calibPanel.leadImu
                            enabled: calibPanel.calibPhase > 0 && !root.calibrationDone
                            function onImuConnectedChanged() {
                                var imu = calibPanel.leadImu
                                if (imu && !imu.imuConnected)
                                    calibPanel.calibrationFailed = true
                            }
                        }

                        // Phase 2 hold timer.
                        Timer {
                            id: stabilityHoldTimer
                            interval: 100
                            repeat:   true
                            running:  root.currentStep === root.stepCalibrate
                                      && calibPanel.calibPhase === 2
                                      && !root.calibrationDone
                                      && !calibPanel.mountFailed
                                      && calibPanel.leadImu !== null
                            onTriggered: {
                                var imu = calibPanel.leadImu
                                if (!imu) return
                                // Stillness-gated: only accumulate while the arm is held still;
                                // motion resets the hold so the captured pose is genuinely static.
                                if (imu.angularVelocityDps > calibPanel._stillThreshDps) {
                                    calibPanel._phase2Samples = []
                                    calibPanel.stableAccumMs  = 0
                                    calibPanel.phaseProgress  = 0.0
                                    return
                                }
                                calibPanel._phase2Samples = calibPanel._phase2Samples.concat(
                                    [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
                                calibPanel.stableAccumMs += interval
                                calibPanel.phaseProgress = Math.min(calibPanel.stableAccumMs / calibPanel._captureHoldMs, 1.0)
                                if (calibPanel.stableAccumMs >= calibPanel._captureHoldMs) {
                                    root.calibArmTPoseQuat = calibPanel._slerpAverage(calibPanel._phase2Samples)

                                    // Abduction refinement + mount validation for EVERY connected segment.
                                    // Each sensor: refine its mounting about the long axis by φ (from the
                                    // abducted-pose anatomical orientation), then evaluate the two-part gate
                                    //   gravity check (gravΔ ≤ 25°, catches flip/upside-down) AND
                                    //   long-axis deviation (φ/strapΔ ≤ 15°, catches strap rotation).
                                    // PASS requires ALL connected segments to pass; any FAIL → re-seat.
                                    var segs = calibPanel._connectedSegs()
                                    var allPass = segs.length > 0
                                    var failNames = []
                                    var nameFor = function(inst) {
                                        return inst === calibPanel.leadImu ? qsTr("forearm")
                                             : inst === calibPanel.slotB    ? qsTr("hand")
                                             : qsTr("upper arm")
                                    }
                                    for (var k = 0; k < segs.length; ++k) {
                                        var s2 = segs[k][0], ref = segs[k][1]
                                        if (!ref) continue
                                        s2.refineMountAboutLongAxis(ref, calibPanel._phiFromAbduction(s2), false)
                                        var ok = s2.mountDeviationDeg <= 15.0 && s2.mountGravityErrorDeg <= 25.0
                                        if (!ok) { allPass = false; failNames.push(nameFor(s2)) }
                                    }

                                    if (allPass) {
                                        calibPanel.mountFailed = false
                                        calibPanel.mountFailMsg = ""
                                        root.calibrationDone = true
                                    } else {
                                        calibPanel.mountFailed = true
                                        calibPanel.mountFailMsg = qsTr("Sensor mounted incorrectly (%1) — re-seat per the strap guide and tap Recalibrate.")
                                            .arg(failNames.join(", "))
                                        // Leave calibrationDone false; user must Recalibrate.
                                    }
                                }
                            }
                        }

                        // Phase 0: 3s after page load, play 3s rest→T-pose animation.
                        // Guard on the Calibrate step: calibPanel lives in a StackLayout so it
                        // is instantiated at app startup — without this guard the intro fires
                        // immediately and the entire capture chain runs in the background.
                        Timer {
                            id: introStartTimer
                            interval: 3000
                            repeat:   false
                            running:  calibPanel.calibPhase === 0 && root.currentStep === root.stepCalibrate
                            onTriggered: {
                                calibBvv.resetArmAnimation(calibPanel.leadArmDownQuat)
                                calibBvv.leadArmAnimDuration = 3000
                                calibPanel._animateLeadArm   = true
                                calibPanel._leadArmTarget    = calibPanel.tPoseQuat
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
                                calibBvv.resetArmAnimation(calibPanel.tPoseQuat)
                                calibBvv.leadArmAnimDuration = 3000
                                calibPanel._leadArmTarget    = calibPanel.leadArmDownQuat
                                introReadyTimer.start()   // 3s anim + 2s pause
                            }
                        }

                        // Return animation + 2s pause complete: start phase 1.
                        Timer {
                            id: introReadyTimer
                            interval: 5000   // 3000ms return anim + 2000ms pause
                            repeat:   false
                            onTriggered: {
                                calibPanel._animateLeadArm    = false
                                calibPanel._phase1MinHoldDone = false
                                calibPanel.calibPhase         = 1
                                phase1MinHoldTimer.start()
                                // No hardware zeroing — orientation comes from our own Madgwick
                                // fusion (device angle-zeroing is vestigial); the arm-down pose is
                                // captured directly by phase1HoldTimer once the IMU is stable.
                            }
                        }

                        // Phase 1 accumulator — same stillness-gated pattern as stabilityHoldTimer
                        // (phase 2). The timer runs for the whole phase; each tick decides whether
                        // to accumulate (arm held still) or reset the hold (arm moving), watching
                        // imu.angularVelocityDps. _armDownCaptured gates it off once captured so it
                        // can't re-trigger during the raise animation. _phase1MinHoldDone (set 2s
                        // after phase 1 begins via phase1MinHoldTimer) gives the user a settle
                        // window before the still-watch starts.
                        property real phase1AccumMs: 0.0

                        Timer {
                            id: phase1MinHoldTimer
                            interval: 2000
                            repeat:   false
                            onTriggered: calibPanel._phase1MinHoldDone = true
                            // No capture here — phase1HoldTimer handles it reactively.
                        }

                        Timer {
                            id: phase1HoldTimer
                            interval: 100
                            repeat:   true
                            running:  root.currentStep === root.stepCalibrate
                                      && calibPanel.calibPhase === 1
                                      && calibPanel._phase1MinHoldDone
                                      && !calibPanel._armDownCaptured
                                      && calibPanel.leadImu !== null
                            onTriggered: {
                                var imu = calibPanel.leadImu
                                if (!imu) return
                                // Stillness-gated: only accumulate while the arm is held still;
                                // motion resets the hold so the captured pose is genuinely static.
                                if (imu.angularVelocityDps > calibPanel._stillThreshDps) {
                                    calibPanel._phase1Samples = []
                                    calibPanel.phase1AccumMs  = 0
                                    return
                                }
                                calibPanel._phase1Samples = calibPanel._phase1Samples.concat(
                                    [Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)])
                                calibPanel.phase1AccumMs += interval
                                if (calibPanel.phase1AccumMs >= calibPanel._captureHoldMs) {
                                    root.calibArmDownQuat = calibPanel._slerpAverage(calibPanel._phase1Samples)
                                    // Quick-calibrate EVERY connected segment at arm-down: sets A so
                                    // anatQuat=identity here, with the fixed nominal mounting M, and runs
                                    // the gravity (flip) check. Each sensor's arm-down reference is stored
                                    // for the phase-2 abduction refinement. All segments share the strap
                                    // convention → handMount=false for all.
                                    calibPanel._refA = calibPanel._curQuat(calibPanel.leadImu)
                                    calibPanel._refB = calibPanel._curQuat(calibPanel.slotB)
                                    calibPanel._refC = calibPanel._curQuat(calibPanel.slotC)
                                    if (calibPanel.leadImu && calibPanel.leadImu.imuConnected)
                                        calibPanel.leadImu.setNominalCalibration(calibPanel._refA, false)
                                    if (calibPanel.slotB && calibPanel.slotB.imuConnected)
                                        calibPanel.slotB.setNominalCalibration(calibPanel._refB, false)
                                    if (calibPanel.slotC && calibPanel.slotC.imuConnected)
                                        calibPanel.slotC.setNominalCalibration(calibPanel._refC, false)
                                    calibPanel._armDownCaptured = true  // stops timer via binding; must be after quat capture
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
                                calibBvv.resetArmAnimation(calibPanel.leadArmDownQuat)
                                calibBvv.leadArmAnimDuration = 1500
                                calibPanel._animateLeadArm   = true
                                calibPanel._leadArmTarget    = calibPanel.tPoseQuat
                                raiseReadyTimer.start()
                            }
                        }

                        // Raise animation (1.5s) + 2s pause, then start T-pose capture.
                        Timer {
                            id: raiseReadyTimer
                            interval: 3500   // 1500ms raise anim + 2000ms pause
                            repeat:   false
                            onTriggered: {
                                calibPanel._animateLeadArm = false
                                calibPanel.calibPhase      = 2
                            }
                        }

                        RowLayout {
                            anchors.fill:    parent
                            anchors.margins: Theme.sp(8)
                            spacing:         Theme.sp(12)

                            // ── Left: full body 3D view ────────────────────────
                            BodyVizView {
                                id: calibBvv
                                Layout.fillHeight:   true
                                Layout.fillWidth:    true
                                Layout.minimumWidth: Theme.sp(200)

                                poseSource:       null   // no camera input during calibration
                                rightHanded:      calibPanel.rightHanded
                                highlightLeadArm: true
                                leadArmColor:     Theme.colorAccent

                                useLeadArmOverride:          true
                                leadArmOverrideRotation:     calibPanel._leadArmTarget
                                leadForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                                useTrailArmOverride:          true
                                trailArmOverrideRotation:     calibPanel.trailArmDownQuat
                                trailForeArmOverrideRotation: Qt.quaternion(1, 0, 0, 0)

                                animateLeadArm: calibPanel._animateLeadArm
                            }

                            // ── Right: status column ───────────────────────────
                            Column {
                                Layout.preferredWidth: Theme.sp(180)
                                Layout.fillHeight:     true
                                spacing:               Theme.sp(16)
                                Layout.topMargin:      Theme.sp(32)

                                Text {
                                    width:              parent.width
                                    text:               qsTr("STEP 4 OF %1 · CALIBRATE").arg(root.totalSteps)
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:              Theme.colorText3
                                }

                                Text {
                                    width:          parent.width
                                    text:           qsTr("Calibrate Sensors")
                                    font.family:    Theme.fontDisplay
                                    font.italic:    Theme.fontDisplayItalic
                                    font.weight: Theme.fontDisplayWeight
                                    font.pixelSize: Math.min(Theme.sp(18), Theme.fontSzDisplay)
                                    color:          Theme.colorText
                                    wrapMode:       Text.WordWrap
                                }

                                // Calibration status badge
                                // Priority (highest first): Complete > Calibrating > Failed > Pending
                                Rectangle {
                                    readonly property bool _complete:    root.calibrationDone
                                    // Active during capture phases (arm-down + T-pose)
                                    readonly property bool _calibrating: calibPanel.calibPhase >= 1
                                                                         && !root.calibrationDone
                                    readonly property bool _failed:      (calibPanel.calibrationFailed || calibPanel.mountFailed) && !root.calibrationDone

                                    width:  statusBadgeLbl.implicitWidth + Theme.sp(16)
                                    height: Theme.sp(22)
                                    radius: Theme.sp(11)
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

                                // Phase instruction
                                Text {
                                    width:      parent.width
                                    wrapMode:   Text.WordWrap
                                    lineHeight: 1.5
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText2
                                    text: {
                                        if (calibPanel.calibPhase === 0)
                                            return qsTr("Watch the guide — this shows the T-pose position you'll hold next.")
                                        if (calibPanel.calibPhase === 1) {
                                            if (calibPanel._armDownCaptured)
                                                return qsTr("Follow the guide and raise your arm out to shoulder height.")
                                            return qsTr("Let your lead arm hang relaxed at your side and hold still.")
                                        }
                                        return qsTr("Hold your arm at shoulder height and keep it still — the bar fills while you hold steady.")
                                    }
                                }

                                // Stability progress bar
                                Item {
                                    width:  parent.width
                                    height: Theme.sp(32)

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width:  parent.width
                                        height: Theme.sp(4)
                                        radius: Theme.sp(2)
                                        color:  Theme.colorBg3

                                        Rectangle {
                                            id: progressFill
                                            readonly property real fillFraction: {
                                                if (root.calibrationDone)         return 1.0
                                                if (calibPanel.calibPhase === 2)  return calibPanel.phaseProgress
                                                if (calibPanel.calibPhase === 1 && !calibPanel._armDownCaptured)
                                                    return Math.min(calibPanel.phase1AccumMs / calibPanel._captureHoldMs, 1.0)
                                                return 0.0
                                            }
                                            width:  parent.width * fillFraction
                                            height: parent.height
                                            radius: parent.radius
                                            color:  root.calibrationDone ? Theme.colorGood : Theme.colorAccent
                                            Behavior on width { NumberAnimation { duration: 150 } }
                                        }
                                    }
                                }

                                // Status label (checkmark circle + text)
                                Row {
                                    width:   parent.width
                                    spacing: Theme.sp(6)

                                    Rectangle {
                                        visible:              root.calibrationDone
                                        width:                Theme.sp(16)
                                        height:               Theme.sp(16)
                                        radius:               width / 2
                                        color:                "transparent"
                                        border.color:         Theme.colorGood
                                        border.width:         Theme.sp(1.5)
                                        y:                    (statusLbl.implicitHeight - height) / 2

                                        Text {
                                            anchors.centerIn: parent
                                            text:             "✓"
                                            color:            Theme.colorGood
                                            font.pixelSize:   Theme.sp(9)
                                            font.bold:        true
                                        }
                                    }

                                    Text {
                                        id: statusLbl
                                        readonly property bool _capturing:
                                            (calibPanel.calibPhase === 1 && calibPanel.phase1AccumMs > 0 && !calibPanel._armDownCaptured)
                                            || calibPanel.calibPhase === 2

                                        width:              parent.width - (root.calibrationDone ? Theme.sp(22) : 0)
                                        wrapMode:           Text.WordWrap
                                        font.family:        Theme.fontData
                                        font.pixelSize:     _capturing ? Theme.fontSzHeading : Theme.fontSzMicro
                                        font.bold:          _capturing
                                        font.letterSpacing: Theme.trackingData
                                        color: root.calibrationDone ? Theme.colorGood
                                             : _capturing            ? Theme.colorAccent
                                             :                         Theme.colorText3
                                        text: {
                                            if (root.calibrationDone)        return qsTr("CALIBRATION COMPLETE")
                                            if (calibPanel.calibPhase === 0)     return qsTr("WATCH THE GUIDE")
                                            if (calibPanel.calibPhase === 1) {
                                                if (calibPanel._armDownCaptured) return qsTr("FOLLOW THE GUIDE")
                                                var imu = calibPanel.leadImu
                                                if (!imu || !imu.imuConnected)   return qsTr("WAITING FOR SENSOR")
                                                if (calibPanel.phase1AccumMs > 0) return qsTr("HOLD STILL — CAPTURING")
                                                return qsTr("HOLD STILL")
                                            }
                                            return qsTr("HOLD STILL — CAPTURING")
                                        }
                                        Behavior on font.pixelSize { NumberAnimation { duration: Theme.durationNormal } }
                                        Behavior on color           { ColorAnimation  { duration: Theme.durationNormal } }
                                    }
                                }

                                // Calibration angle warning — shown when arm-down→T-pose
                                // angle was outside the expected ~90° range
                                Text {
                                    readonly property bool _show: {
                                        var imu = calibPanel.leadImu
                                        return root.calibrationDone
                                            && imu !== null
                                            && imu.calibrated
                                            && !imu.calibrationAngleValid
                                    }
                                    visible:    _show
                                    width:      parent.width
                                    wrapMode:   Text.WordWrap
                                    lineHeight: 1.5
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorWarn
                                    text:           qsTr("The arm positions didn't look quite right — the angle between arm-down and T-pose was much less than expected. For best results, make sure your arm is fully raised to shoulder height during the T-pose step, then tap Recalibrate.")
                                }

                                // No-IMU warning
                                Text {
                                    width:      parent.width
                                    visible:    calibPanel.leadImu === null
                                    wrapMode:   Text.WordWrap
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorWarn
                                    text:           qsTr("No wrist sensor assigned to slot A. Return to the IMUs step to assign one.")
                                }

                                // Mount-validation failure — a sensor is mounted wrong (flip / bad seat).
                                Text {
                                    width:      parent.width
                                    visible:    calibPanel.mountFailed && !root.calibrationDone
                                    wrapMode:   Text.WordWrap
                                    lineHeight: 1.5
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorError
                                    text:           calibPanel.mountFailMsg
                                }

                                // Recalibrate button — hidden while still in the initial Pending state
                                PpButton {
                                    visible: calibPanel.calibPhase > 0
                                             || root.calibrationDone
                                             || calibPanel.calibrationFailed
                                             || calibPanel.mountFailed
                                    label:   qsTr("↺  Recalibrate")
                                    primary: false
                                    onClicked: root._resetCalibration()
                                }

                                // ── Calibration diagnostic display ───────────────────────────────────
                                // Visible only after calibration completes. Remove once axis mapping is confirmed.
                                Column {
                                    visible: root.calibrationDone
                                    width:   parent.width
                                    spacing: Theme.sp(6)

                                    Rectangle {
                                        width:  parent.width
                                        height: diagContent.implicitHeight + Theme.sp(16)
                                        color:  Theme.colorBg2
                                        radius: Theme.radius
                                        border.width: 1
                                        border.color: Theme.colorBorderMid

                                        Column {
                                            id: diagContent
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(10) }
                                            spacing: Theme.sp(4)

                                            Text {
                                                text:               "CALIBRATION DIAGNOSTIC"
                                                font.family:        Theme.fontData
                                                font.pixelSize:     Theme.fontSzMicro
                                                font.letterSpacing: Theme.trackingMicro
                                                color:              Theme.colorText3
                                            }

                                            component DiagRow: Row {
                                                required property string label
                                                required property string value
                                                width: parent.width
                                                spacing: Theme.sp(8)
                                                Text {
                                                    text:           label
                                                    font.family:    Theme.fontData
                                                    font.pixelSize: Theme.fontSzMicro
                                                    color:          Theme.colorText3
                                                    width:          Theme.sp(80)
                                                }
                                                Text {
                                                    text:           value
                                                    font.family:    Theme.fontData
                                                    font.pixelSize: Theme.fontSzMicro
                                                    color:          Theme.colorText2
                                                    wrapMode:       Text.WordWrap
                                                    width:          parent.width - Theme.sp(80) - Theme.sp(8)
                                                }
                                            }

                                            DiagRow {
                                                label: "ARM-DOWN E"
                                                value: root.calibArmDownEuler
                                                    ? ("R " + root.calibArmDownEuler.roll.toFixed(1)
                                                     + "  P " + root.calibArmDownEuler.pitch.toFixed(1)
                                                     + "  Y " + root.calibArmDownEuler.yaw.toFixed(1))
                                                    : "—"
                                            }
                                            DiagRow {
                                                label: "ARM-DOWN Q"
                                                value: root.calibArmDownQuat
                                                    ? ("w" + root.calibArmDownQuat.scalar.toFixed(3)
                                                     + " x" + root.calibArmDownQuat.x.toFixed(3)
                                                     + " y" + root.calibArmDownQuat.y.toFixed(3)
                                                     + " z" + root.calibArmDownQuat.z.toFixed(3))
                                                    : "—"
                                            }
                                            DiagRow {
                                                label: "T-POSE E"
                                                value: root.calibArmTPoseEuler
                                                    ? ("R " + root.calibArmTPoseEuler.roll.toFixed(1)
                                                     + "  P " + root.calibArmTPoseEuler.pitch.toFixed(1)
                                                     + "  Y " + root.calibArmTPoseEuler.yaw.toFixed(1))
                                                    : "—"
                                            }
                                            DiagRow {
                                                label: "T-POSE Q"
                                                value: root.calibArmTPoseQuat
                                                    ? ("w" + root.calibArmTPoseQuat.scalar.toFixed(3)
                                                     + " x" + root.calibArmTPoseQuat.x.toFixed(3)
                                                     + " y" + root.calibArmTPoseQuat.y.toFixed(3)
                                                     + " z" + root.calibArmTPoseQuat.z.toFixed(3))
                                                    : "—"
                                            }
                                            DiagRow {
                                                label: "CAL XFORM"
                                                value: {
                                                    var imu = calibPanel.leadImu
                                                    if (!imu || !imu.calibrated) return "—"
                                                    var ct = imu.calibTransform
                                                    return ("w" + ct.scalar.toFixed(3)
                                                          + " x" + ct.x.toFixed(3)
                                                          + " y" + ct.y.toFixed(3)
                                                          + " z" + ct.z.toFixed(3))
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Panel 4: Confirm Tracking ─────────────────────────────
                    // Only reachable for wrist motion sessions (hasCalibrateStep).

                    Item {
                        implicitHeight: Math.max(Theme.sp(480), bodyStack.parent.parent.height - Theme.sp(80))

                        ColumnLayout {
                            anchors.fill:    parent
                            anchors.margins: Theme.sp(8)
                            spacing:         Theme.sp(12)

                            Column {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.sp(32)
                                spacing:          Theme.sp(8)

                                Text {
                                    text:               qsTr("STEP 4 OF %1 · CONFIRM TRACKING").arg(root.totalSteps)
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:              Theme.colorText3
                                }

                                Text {
                                    width:          parent.width
                                    text:           qsTr("Check your sensor")
                                    font.family:    Theme.fontDisplay
                                    font.italic:    Theme.fontDisplayItalic
                                    font.weight:    Theme.fontDisplayWeight
                                    font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                                    color:          Theme.colorText
                                    wrapMode:       Text.WordWrap
                                }

                                Text {
                                    width:          parent.width
                                    text:           qsTr("Move your arm slowly in all directions. The 3D model should track your movement precisely. If it doesn't, go back and recalibrate.")
                                    font.family:    Theme.fontBody
                                    font.weight:    Theme.fontBodyWeight
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText2
                                    lineHeight:     1.65
                                    wrapMode:       Text.WordWrap
                                }
                            }

                            // ── TEMP calibration diagnostic capture (remove before commit) ──
                            // Code-led, log-driven, user-paced. One static REFERENCE hold (gravity)
                            // then FUNCTIONAL swings: the joint axis is extracted by PCA over the
                            // incremental quaternion rotations across the whole movement (robust to
                            // imprecise angles). Results written to ~/pinpoint_imu_diag.log via
                            // ImuInstance::logDiag(). Slot A (calibPanel.leadImu) only.
                            Item {
                                id: diagWrap
                                Layout.fillWidth:  true
                                Layout.fillHeight: true

                                // Per-segment calibration. REFERENCE captures every sensor's arm-down
                                // pose at once; each functional swing isolates one joint axis for one
                                // sensor. The long axis comes from gravity at the reference (the segment
                                // hangs vertical), and one perpendicular flex swing completes the frame.
                                // Steps whose sensor isn't present are skipped automatically.
                                readonly property var diagSteps: [
                                    { tag: "REFERENCE", slot: "ALL", kind: "static", dur: 3000,
                                      prompt: qsTr("Whole arm hanging straight down, relaxed, palm toward your thigh. Stand still while it captures.") },
                                    { tag: "FOREARM TWIST", slot: "A", kind: "func", dur: 6000,
                                      prompt: qsTr("Wrist sensor: twist your forearm palm-in ↔ palm-out, repeatedly. Keep your elbow tucked and still.") },
                                    { tag: "ELBOW BEND", slot: "A", kind: "func", dur: 6000,
                                      prompt: qsTr("Wrist sensor: bend and straighten your elbow repeatedly. Keep your upper arm still at your side.") },
                                    { tag: "SHOULDER RAISE", slot: "C", kind: "func", dur: 6000,
                                      prompt: qsTr("Upper-arm sensor: keeping your arm straight, raise it forward and lower it, repeatedly. Don't twist the forearm.") },
                                    { tag: "WRIST WAVE", slot: "B", kind: "func", dur: 6000,
                                      prompt: qsTr("Hand sensor: bend your wrist up and down (wave) repeatedly, keeping your forearm still.") }
                                ]
                                property bool   diagActive: false
                                property int    stepIndex:  0
                                property string phase:      "idle"   // idle | ready | capture | done
                                property real   elapsedMs:  0
                                property var    samples:    []
                                property var    _scat:      [0,0,0,0,0,0]  // symmetric 3x3: 00,01,02,11,12,22
                                property var    _funcPrev:  null
                                property var    _funcStart: null
                                property int    _funcCount: 0
                                // Per-segment captured inputs (sensor frame), fed to the solver.
                                property var    refA: null;  property var refB: null;  property var refC: null
                                property var    twistA: null; property var elbowA: null
                                property var    flexC: null;  property var flexB: null

                                // Slot B/C instance resolvers (slot A is calibPanel.leadImu).
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
                                function _instForSlot(slot) {
                                    if (slot === "B") return slotB
                                    if (slot === "C") return slotC
                                    return calibPanel.leadImu   // "A" and "ALL"
                                }
                                // The sensor sampled during the current step.
                                readonly property QtObject activeInst: _instForSlot(diagSteps[stepIndex].slot)

                                function _qMul(a, b) {
                                    return Qt.quaternion(
                                        a.scalar*b.scalar - a.x*b.x - a.y*b.y - a.z*b.z,
                                        a.scalar*b.x + a.x*b.scalar + a.y*b.z - a.z*b.y,
                                        a.scalar*b.y - a.x*b.z + a.y*b.scalar + a.z*b.x,
                                        a.scalar*b.z + a.x*b.y - a.y*b.x + a.z*b.scalar)
                                }
                                function _qConj(q) { return Qt.quaternion(q.scalar, -q.x, -q.y, -q.z) }
                                function _fmtQ(q) {
                                    return "(" + q.scalar.toFixed(6) + "," + q.x.toFixed(6)
                                         + "," + q.y.toFixed(6) + "," + q.z.toFixed(6) + ")"
                                }
                                function _axisAngle(q) {
                                    var w = Math.max(-1, Math.min(1, q.scalar))
                                    var ang = 2 * Math.acos(w) * 180 / Math.PI
                                    var s = Math.sqrt(Math.max(0, 1 - w*w))
                                    if (s < 1e-6) return { ax: 1, ay: 0, az: 0, ang: ang }
                                    return { ax: q.x/s, ay: q.y/s, az: q.z/s, ang: ang }
                                }
                                function _vnorm(v) {
                                    var n = Math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
                                    return n > 1e-9 ? [v[0]/n, v[1]/n, v[2]/n] : [1,0,0]
                                }
                                function _rotate(q, v) {
                                    var r = _qMul(_qMul(q, Qt.quaternion(0, v[0], v[1], v[2])), _qConj(q))
                                    return [r.x, r.y, r.z]
                                }
                                function _scatAdd(a, w) {
                                    var s = _scat
                                    s[0]+=w*a[0]*a[0]; s[1]+=w*a[0]*a[1]; s[2]+=w*a[0]*a[2]
                                    s[3]+=w*a[1]*a[1]; s[4]+=w*a[1]*a[2]; s[5]+=w*a[2]*a[2]
                                    _scat = s
                                }
                                function _dominantEig() {
                                    var s = _scat
                                    function mv(v) {
                                        return [ s[0]*v[0]+s[1]*v[1]+s[2]*v[2],
                                                 s[1]*v[0]+s[3]*v[1]+s[4]*v[2],
                                                 s[2]*v[0]+s[4]*v[1]+s[5]*v[2] ]
                                    }
                                    var starts = [[1,0,0],[0,1,0],[0,0,1]]
                                    var bestV = [1,0,0], bestL = -1
                                    for (var k = 0; k < starts.length; ++k) {
                                        var v = _vnorm(starts[k])
                                        for (var i = 0; i < 80; ++i) v = _vnorm(mv(v))
                                        var m = mv(v)
                                        var l = v[0]*m[0] + v[1]*m[1] + v[2]*m[2]
                                        if (l > bestL) { bestL = l; bestV = v }
                                    }
                                    var trace = s[0] + s[3] + s[5]
                                    return { axis: bestV, quality: trace > 1e-9 ? bestL/trace : 0 }
                                }

                                function _startDiag() {
                                    diagActive = true
                                    refA = refB = refC = twistA = elbowA = flexC = flexB = null
                                    stepIndex = 0; _beginReady()
                                }

                                // Quick (mandated-mount) calibration: with the strap enforcing the sensor
                                // orientation, M is the known nominal — one arm-down reference per sensor
                                // is enough. Hold the whole arm straight down and still, then tap.
                                // Numeric verification: log every sensor's anatQuat (and the relative
                                // wrist/elbow joint rotations) for a held pose, so calibration can be
                                // assessed from the log rather than by eye.
                                function _logAnat(tag) {
                                    function fq(i) {
                                        if (!i) return "—"
                                        var q = i.anatQuat
                                        return "(" + q.scalar.toFixed(5) + "," + q.x.toFixed(5)
                                             + "," + q.y.toFixed(5) + "," + q.z.toFixed(5) + ")"
                                    }
                                    var imu = calibPanel.leadImu
                                    if (!imu) return
                                    imu.logDiag("ANAT_" + tag,
                                        "fa=" + fq(calibPanel.leadImu) + " ha=" + fq(slotB) + " ua=" + fq(slotC))
                                }

                                function _quickCalibrate() {
                                    // All three sensors share the SAME strap convention, so they share the
                                    // SAME nominal mounting (handMount=false for all). The earlier separate
                                    // hand nominal was derived circularly from a possibly-unclean wrist
                                    // swing; the independent abduction test exposed it (82° inconsistency).
                                    var segs = [[calibPanel.leadImu, false], [slotB, false], [slotC, false]]
                                    for (var i = 0; i < segs.length; ++i) {
                                        var inst = segs[i][0]
                                        if (inst && inst.imuConnected)
                                            inst.setNominalCalibration(
                                                Qt.quaternion(inst.quatW, inst.quatX, inst.quatY, inst.quatZ),
                                                segs[i][1])
                                    }
                                }

                                // Precise (δM fine-tune) — small bounded correction, keeps the validated
                                // nominal (does NOT re-solve M). Two poses: (1) arm down → Quick calibrate
                                // baseline; (2) arm abducted to the side → the anatomical orientation
                                // should be a pure rotation about anatomical Z (anterior). The observed
                                // axis's deviation about the long axis (Y) gives phi = atan2(mx, mz),
                                // applied via refineMountAboutLongAxis. Bounded → cannot flip the frame.
                                property string preciseStage: "idle"   // idle | got-down
                                property bool   preciseComplete: false
                                property var    pdA: null; property var pdB: null; property var pdC: null

                                // Small Y-rotation that brings the abducted-pose rotation axis onto Z.
                                function _phiFromAbduction(inst) {
                                    var q = inst.anatQuat
                                    var w = Math.min(1, Math.abs(q.scalar))
                                    var s = Math.sqrt(Math.max(0, 1 - w*w))
                                    if (s < 1e-4) return 0                       // ~no rotation captured
                                    var sg = q.scalar >= 0 ? 1 : -1
                                    var mx = sg*q.x/s, mz = sg*q.z/s             // axis components
                                    var pp = Math.atan2(mx, mz)                  // → +Z
                                    var pm = Math.atan2(-mx, -mz)                // → -Z
                                    var phi = Math.abs(pp) <= Math.abs(pm) ? pp : pm   // smallest correction
                                    return phi * 180 / Math.PI
                                }
                                function _preciseCapture() {
                                    var segs = [[calibPanel.leadImu, false], [slotB, false], [slotC, false]]
                                    if (preciseStage === "idle") {
                                        // Pose 1: quick-calibrate baseline + remember the down reference.
                                        preciseComplete = false
                                        pdA = _curQuat(calibPanel.leadImu); pdB = _curQuat(slotB); pdC = _curQuat(slotC)
                                        for (var i = 0; i < segs.length; ++i) {
                                            var inst = segs[i][0]
                                            if (inst && inst.imuConnected)
                                                inst.setNominalCalibration(_curQuat(inst), segs[i][1])
                                        }
                                        preciseStage = "got-down"
                                        return
                                    }
                                    // Pose 2 (abducted): DIAGNOSTIC FIRST (assumption-free), then refine.
                                    // For each sensor log: the raw down→abduction rotation axis IN THE SENSOR
                                    // FRAME (ground truth — which sensor axis abduction is physically about,
                                    // no anatomical/nominal assumption), the abduction angle, the nominal-frame
                                    // anatQuat axis, and the φ the old method derives. This shows directly
                                    // whether the assumed orientation matches reality.
                                    var refs = [pdA, pdB, pdC]
                                    var names = ["forearm(A)", "hand(B)", "upper(C)"]
                                    for (var j = 0; j < segs.length; ++j) {
                                        var s2 = segs[j][0]
                                        if (!(s2 && s2.imuConnected && refs[j])) continue
                                        // raw abduction axis in the sensor frame
                                        var abdRaw = _curQuat(s2)
                                        var relRaw = _qMul(_qConj(refs[j]), abdRaw)
                                        var ar = _axisAngle(relRaw)
                                        // nominal-frame anatQuat axis (pre-refine) + the φ it yields
                                        var aq = s2.anatQuat
                                        var aw = Math.min(1, Math.abs(aq.scalar)); var as_ = Math.sqrt(Math.max(0,1-aw*aw))
                                        var asg = aq.scalar >= 0 ? 1 : -1
                                        var ax = as_>1e-4 ? asg*aq.x/as_ : 0, ay = as_>1e-4 ? asg*aq.y/as_ : 0, az = as_>1e-4 ? asg*aq.z/as_ : 0
                                        var phi = _phiFromAbduction(s2)
                                        s2.logDiag("PRECISE_DBG",
                                            names[j] + " rawAbdAxis_sensor=(" + ar.ax.toFixed(3) + "," + ar.ay.toFixed(3) + "," + ar.az.toFixed(3) + ")"
                                            + " ang=" + ar.ang.toFixed(1)
                                            + " anatAxis=(" + ax.toFixed(3) + "," + ay.toFixed(3) + "," + az.toFixed(3) + ")"
                                            + " phi=" + phi.toFixed(1))
                                        s2.refineMountAboutLongAxis(refs[j], phi, segs[j][1])
                                    }
                                    preciseStage = "idle"
                                    preciseComplete = true
                                    // Write the gate result to the log (no screenshot needed). PASS requires
                                    // BOTH the long-axis fine-tune (φ) small AND the gravity/flip check small.
                                    var imuL = calibPanel.leadImu
                                    if (imuL) {
                                        function d(i){ return i ? i.mountDeviationDeg.toFixed(1) : "—" }
                                        function g(i){ return i ? i.mountGravityErrorDeg.toFixed(1) : "—" }
                                        function verdict(i){
                                            if (!i || !i.imuConnected) return "—"
                                            return (i.mountDeviationDeg <= 15 && i.mountGravityErrorDeg <= 25) ? "PASS" : "FAIL"
                                        }
                                        imuL.logDiag("PRECISE_DONE",
                                            "forearm[strapΔ=" + d(calibPanel.leadImu) + " gravΔ=" + g(calibPanel.leadImu) + " " + verdict(calibPanel.leadImu) + "]"
                                            + " hand[strapΔ=" + d(slotB) + " gravΔ=" + g(slotB) + " " + verdict(slotB) + "]"
                                            + " upper[strapΔ=" + d(slotC) + " gravΔ=" + g(slotC) + " " + verdict(slotC) + "]")
                                    }
                                }
                                function _beginReady() {
                                    // Skip steps whose sensor isn't connected.
                                    while (stepIndex < diagSteps.length && diagSteps[stepIndex].slot !== "ALL"
                                           && !_instForSlot(diagSteps[stepIndex].slot))
                                        stepIndex += 1
                                    if (stepIndex >= diagSteps.length) { phase = "done"; diagActive = false; return }
                                    phase = "ready"
                                }
                                function _beginCapture() {
                                    phase = "capture"; elapsedMs = 0; samples = []
                                    _scat = [0,0,0,0,0,0]; _funcPrev = null; _funcStart = null; _funcCount = 0
                                    if (activeInst) activeInst.beginRawDump(diagSteps[stepIndex].tag)
                                    captureClock.restart()
                                }
                                function _sample() {
                                    var imu = activeInst
                                    if (!imu || !imu.imuConnected) return
                                    var kind = diagSteps[stepIndex].kind
                                    var cur = Qt.quaternion(imu.quatW, imu.quatX, imu.quatY, imu.quatZ)
                                    if (kind === "static") {
                                        var s = samples; s.push(cur); samples = s
                                        return
                                    }
                                    // functional: PCA over incremental rotation axes, referenced to start frame
                                    if (!_funcPrev) { _funcStart = cur; _funcPrev = cur; return }
                                    var dq = _qMul(_qConj(_funcPrev), cur)
                                    var aa = _axisAngle(dq)
                                    if (aa.ang > 0.05 && aa.ang < 30) {
                                        var rref = _qMul(_qConj(_funcStart), _funcPrev)
                                        var axb  = _rotate(rref, [aa.ax, aa.ay, aa.az])
                                        _scatAdd(axb, aa.ang)
                                        _funcCount += 1
                                    }
                                    _funcPrev = cur
                                }
                                function _curQuat(inst) {
                                    return inst ? Qt.quaternion(inst.quatW, inst.quatX, inst.quatY, inst.quatZ) : null
                                }
                                function _finish() {
                                    var imu = activeInst
                                    var st  = diagSteps[stepIndex]
                                    if (imu) imu.endRawDump()
                                    var payload
                                    if (st.kind === "static") {
                                        // REFERENCE: snapshot every present sensor's arm-down quaternion,
                                        // plus its raw accel (gravity) so each segment's frame can be
                                        // verified offline.
                                        refA = _curQuat(calibPanel.leadImu)
                                        refB = _curQuat(slotB)
                                        refC = _curQuat(slotC)
                                        payload = "refA=" + (refA ? _fmtQ(refA) : "—")
                                                + " refB=" + (refB ? _fmtQ(refB) : "—")
                                                + " refC=" + (refC ? _fmtQ(refC) : "—")
                                    } else {
                                        var e = _dominantEig()
                                        if (st.tag === "FOREARM TWIST") twistA = e.axis
                                        if (st.tag === "ELBOW BEND")   { elbowA = e.axis; _applySegment("A") }
                                        if (st.tag === "SHOULDER RAISE") { flexC = e.axis; _applySegment("C") }
                                        if (st.tag === "WRIST WAVE")     { flexB = e.axis; _applySegment("B") }
                                        payload = "funcaxis=(" + e.axis[0].toFixed(4) + "," + e.axis[1].toFixed(4)
                                                + "," + e.axis[2].toFixed(4) + ") quality=" + e.quality.toFixed(3)
                                                + " samples=" + _funcCount
                                    }
                                    if (imu) imu.logDiag(st.tag, payload)
                                    if (stepIndex + 1 < diagSteps.length) { stepIndex += 1; _beginReady() }
                                    else { phase = "done"; diagActive = false }
                                }

                                // Feed a segment's reference + functional axes to its sensor's solver
                                // (ImuInstance::setFunctionalCalibration), making its anatQuat live.
                                // Long axis: forearm uses the twist axis; hand/upper-arm use gravity at
                                // the reference (the segment hangs vertical). Madgwick world up is +Z, so
                                // world DOWN is -Z expressed in the sensor frame at the reference pose.
                                function _applySegment(slot) {
                                    var inst, ref, longAx, flexAx
                                    if (slot === "A") { inst = calibPanel.leadImu; ref = refA; flexAx = elbowA; }
                                    else if (slot === "C") { inst = slotC; ref = refC; flexAx = flexC; }
                                    else if (slot === "B") { inst = slotB; ref = refB; flexAx = flexB; }
                                    if (!inst || !ref || !flexAx) return
                                    var down = _rotate(_qConj(ref), [0, 0, -1])
                                    longAx = (slot === "A" && twistA) ? twistA : down   // gravity for B/C
                                    inst.setFunctionalCalibration(ref,
                                        Qt.vector3d(down[0], down[1], down[2]),
                                        Qt.vector3d(longAx[0], longAx[1], longAx[2]),
                                        Qt.vector3d(flexAx[0], flexAx[1], flexAx[2]))
                                }

                                Timer {
                                    id: captureClock
                                    interval: 50; repeat: true
                                    running: diagWrap.phase === "capture" && root.currentStep === root.stepConfirm
                                    onTriggered: {
                                        diagWrap.elapsedMs += 50
                                        if (diagWrap.elapsedMs >= diagWrap.diagSteps[diagWrap.stepIndex].dur) {
                                            stop(); diagWrap._finish()
                                        }
                                    }
                                }
                                Connections {
                                    target:  diagWrap.activeInst
                                    enabled: diagWrap.phase === "capture" && root.currentStep === root.stepConfirm
                                    function onQuatChanged() { diagWrap._sample() }
                                }

                                ArmVizView {
                                    id: confirmArmViz
                                    anchors.fill: parent
                                }

                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height:  diagCol.implicitHeight + Theme.sp(24)
                                    color:   "#CC000000"

                                    Column {
                                        id: diagCol
                                        anchors { left: parent.left; right: parent.right
                                                  verticalCenter: parent.verticalCenter; margins: Theme.sp(16) }
                                        spacing: Theme.sp(10)

                                        Text {
                                            width:    parent.width
                                            wrapMode: Text.WordWrap
                                            color:    "white"
                                            font.family:    Theme.fontBody
                                            font.pixelSize: Theme.fontSzBody2
                                            lineHeight:     1.4
                                            text: {
                                                if (diagWrap.phase === "idle")
                                                    return qsTr("Mount sensors per the strap convention. Hold your whole arm straight down and still, then tap Quick calibrate. (Characterise runs the full swing capture — only needed to set the nominal.)")
                                                if (diagWrap.phase === "done")
                                                    return qsTr("Calibration applied — move your arm; the model should now track it. Tap to recapture.")
                                                var st = diagWrap.diagSteps[diagWrap.stepIndex]
                                                var head = qsTr("Step %1 of %2 · %3").arg(diagWrap.stepIndex + 1).arg(diagWrap.diagSteps.length).arg(st.tag)
                                                if (diagWrap.phase === "ready")
                                                    return head + "\n" + st.prompt + "\n" + qsTr("Tap Start when you're in position, then begin moving.")
                                                var pct = Math.round(diagWrap.elapsedMs / st.dur * 100)
                                                var verb = st.kind === "static" ? qsTr("Hold still")
                                                         : st.kind === "tumble" ? qsTr("Keep rotating through orientations")
                                                                                : qsTr("Keep moving")
                                                return head + "\n" + st.prompt + "\n" + qsTr("%1 — capturing… %2%").arg(verb).arg(pct)
                                            }
                                        }

                                        Row {
                                            spacing: Theme.sp(10)

                                            // Quick (mandated-mount) calibration — the primary path.
                                            Rectangle {
                                                visible: diagWrap.phase === "idle" || diagWrap.phase === "done"
                                                width:   quickTxt.implicitWidth + Theme.sp(24)
                                                height:  quickTxt.implicitHeight + Theme.sp(12)
                                                radius:  Theme.sp(4)
                                                color:   Theme.colorAccent
                                                Text {
                                                    id: quickTxt
                                                    anchors.centerIn: parent
                                                    color: "white"
                                                    font.family:    Theme.fontBody
                                                    font.pixelSize: Theme.fontSzLabel
                                                    text: qsTr("Quick calibrate (arm down)")
                                                }
                                                TapHandler { onTapped: diagWrap._quickCalibrate() }
                                            }

                                            // Precise (δM fine-tune) — two poses: arm down, then abduct.
                                            Rectangle {
                                                visible: diagWrap.phase === "idle" || diagWrap.phase === "done"
                                                width:   precTxt.implicitWidth + Theme.sp(24)
                                                height:  precTxt.implicitHeight + Theme.sp(12)
                                                radius:  Theme.sp(4)
                                                color:   diagWrap.preciseStage === "got-down" ? Theme.colorGood : "#55FFFFFF"
                                                Text {
                                                    id: precTxt
                                                    anchors.centerIn: parent
                                                    color: "white"
                                                    font.family:    Theme.fontBody
                                                    font.pixelSize: Theme.fontSzLabel
                                                    text: diagWrap.preciseStage === "got-down"
                                                              ? qsTr("…now raise arm to the SIDE, tap")
                                                              : qsTr("Precise: 1) arm down, tap")
                                                }
                                                TapHandler { onTapped: diagWrap._preciseCapture() }
                                            }

                                            // Full swing capture — only for characterising the nominal.
                                            Rectangle {
                                                visible: diagWrap.phase !== "capture"
                                                width:   startTxt.implicitWidth + Theme.sp(24)
                                                height:  startTxt.implicitHeight + Theme.sp(12)
                                                radius:  Theme.sp(4)
                                                color:   diagWrap.phase === "ready" ? Theme.colorAccent : "#44FFFFFF"
                                                Text {
                                                    id: startTxt
                                                    anchors.centerIn: parent
                                                    color: "white"
                                                    font.family:    Theme.fontBody
                                                    font.pixelSize: Theme.fontSzLabel
                                                    text: diagWrap.phase === "ready"
                                                              ? qsTr("Start: %1").arg(diagWrap.diagSteps[diagWrap.stepIndex].tag)
                                                              : (diagWrap.phase === "done" ? qsTr("Characterise again")
                                                                                           : qsTr("Characterise (swings)"))
                                                }
                                                TapHandler {
                                                    onTapped: {
                                                        if (diagWrap.phase === "ready") diagWrap._beginCapture()
                                                        else                            diagWrap._startDiag()
                                                    }
                                                }
                                            }
                                        }

                                        // Precise-calibration completion + per-sensor strap-slop readout.
                                        Text {
                                            visible: diagWrap.preciseComplete
                                                     && (diagWrap.phase === "idle" || diagWrap.phase === "done")
                                            color: {
                                                function d(i){ return i ? i.mountDeviationDeg : 0 }
                                                var mx = Math.max(d(calibPanel.leadImu), d(diagWrap.slotB), d(diagWrap.slotC))
                                                return mx > 15 ? Theme.colorWarn : Theme.colorGood
                                            }
                                            font.family:    Theme.fontBody
                                            font.pixelSize: Theme.fontSzLabel
                                            text: {
                                                function d(i){ return i ? i.mountDeviationDeg.toFixed(1) : "—" }
                                                return qsTr("✓ Precise calibration complete — strap Δ: forearm %1° · hand %2° · upper %3°")
                                                    .arg(d(calibPanel.leadImu)).arg(d(diagWrap.slotB)).arg(d(diagWrap.slotC))
                                            }
                                        }

                                        // Numeric verification — log all sensors' anatQuat for a held pose.
                                        Row {
                                            spacing: Theme.sp(8)
                                            visible: diagWrap.phase === "idle" || diagWrap.phase === "done"
                                            Repeater {
                                                model: ["NEUTRAL", "ABDUCT", "WRIST_FLEX", "WRIST_EXT"]
                                                delegate: Rectangle {
                                                    required property string modelData
                                                    width:  vTxt.implicitWidth + Theme.sp(16)
                                                    height: vTxt.implicitHeight + Theme.sp(10)
                                                    radius: Theme.sp(4)
                                                    color:  "#33FFFFFF"
                                                    Text {
                                                        id: vTxt
                                                        anchors.centerIn: parent
                                                        color: "white"; font.family: Theme.fontBody
                                                        font.pixelSize: Theme.fontSzLabel
                                                        text: qsTr("Log ") + modelData
                                                    }
                                                    TapHandler { onTapped: diagWrap._logAnat(modelData) }
                                                }
                                            }
                                        }

                                    }
                                }
                            }
                        }
                    }

                    // ── Panel 5: Ready ────────────────────────────────────────


                    Item {
                        implicitHeight: readyCol.implicitHeight + Theme.sp(32)

                        Column {
                            id: readyCol
                            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.sp(32) }
                            spacing: Theme.sp(20)

                            Column {
                                width: parent.width
                                spacing: Theme.sp(8)

                                Text {
                                    text:               qsTr("STEP %1 OF %1 · READY").arg(root.totalSteps)
                                    font.family:        Theme.fontData
                                    font.pixelSize:     Theme.fontSzMicro
                                    font.letterSpacing: Theme.trackingMicro
                                    color:              Theme.colorText3
                                }
                                Text {
                                    width:          parent.width
                                    text:           root.fullyReady
                                                        ? qsTr("You're good to go")
                                                        : qsTr("Not quite ready")
                                    font.family:    Theme.fontDisplay
                                    font.italic:    Theme.fontDisplayItalic
                                    font.weight: Theme.fontDisplayWeight
                                    font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                                    color:          root.fullyReady ? Theme.colorText : Theme.colorWarn
                                    wrapMode:       Text.WordWrap
                                }
                                Text {
                                    width:          parent.width
                                    text:           root.fullyReady
                                        ? qsTr("Everything checked out. The moment you take your address and make your first swing, Pinpoint starts capturing. There's nothing else to press.")
                                        : qsTr("A few things couldn't be confirmed before this session. You can use ← Back to sort them out, or start anyway — Pinpoint will capture what it can, though some analysis may be limited or missing from your results.")
                                    font.family:    Theme.fontBody
                                    font.weight:    Theme.fontBodyWeight
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText2
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.65
                                }
                            }

                            // Issue list — visible only when something is unmet
                            Column {
                                visible: !root.fullyReady
                                width:   parent.width
                                spacing: Theme.sp(6)

                                Repeater {
                                    model: root.readinessIssues
                                    delegate: Row {
                                        required property var modelData
                                        required property int index
                                        width:   parent.width
                                        spacing: Theme.sp(8)

                                        Text {
                                            text:           "⚠"
                                            font.family:    Theme.fontBody
                                            font.pixelSize: Theme.fontSzBody2
                                            color:          Theme.colorWarn
                                            anchors.top:    parent.top
                                            anchors.topMargin: Theme.sp(1)
                                        }

                                        Column {
                                            width:   parent.width - Theme.sp(8) - Theme.sp(16)
                                            spacing: Theme.sp(3)

                                            Text {
                                                width:          parent.width
                                                text:           modelData.text
                                                font.family:    Theme.fontBody
                                                font.weight:    Theme.fontBodyWeight
                                                font.pixelSize: Theme.fontSzBody2
                                                color:          Theme.colorText2
                                                wrapMode:       Text.WordWrap
                                                lineHeight:     1.4
                                            }

                                            Text {
                                                visible:        modelData.panel >= 0
                                                text:           modelData.panel === 3
                                                                    ? qsTr("→ Open camera settings")
                                                                    : qsTr("→ Open IMU settings")
                                                font.family:    Theme.fontBody
                                                font.pixelSize: Theme.fontSzBody2
                                                color:          linkMa.containsMouse
                                                                    ? Theme.colorText : Theme.colorAccent
                                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                                                MouseArea {
                                                    id:           linkMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    cursorShape:  Qt.PointingHandCursor
                                                    onClicked:    root.navigateToSettings(modelData.panel)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Summary table
                            Rectangle {
                                width: parent.width
                                height: summaryRows.implicitHeight
                                radius: Theme.radius
                                color:  "transparent"
                                border.width: 1
                                border.color: Theme.colorBorderMid

                                Column {
                                    id: summaryRows
                                    width: parent.width

                                    SummaryRow {
                                        width: parent.width
                                        rowLabel: qsTr("Goals")
                                        rowValue: root.selectedGoals.length > 0
                                                      ? qsTr("%1 selected · saved to profile").arg(root.selectedGoals.length)
                                                      : qsTr("None — defaulting to %1").arg(root.curGoalDefs[0].name)
                                        good: true
                                    }
                                    Rectangle { width: parent.width; height: 1; color: Theme.colorBorderMid }
                                    SummaryRow {
                                        width: parent.width
                                        rowLabel: qsTr("Cameras")
                                        good: root.stepStates[1] !== "skipped"
                                                  && (root.curType.optionalCamera || root.camsOk)
                                        rowValue: {
                                            if (root.stepStates[1] === "skipped")
                                                return qsTr("Skipped — no video capture")
                                            if (root.curType.optionalCamera)
                                                return root.faceOnData !== null
                                                    ? qsTr("Face-on detected · optional")
                                                    : qsTr("Not assigned — optional for this session")
                                            if (!root.faceOnData && !root.dtlData)
                                                return qsTr("Neither camera assigned")
                                            if (!root.faceOnData)
                                                return qsTr("Face-on camera not assigned")
                                            if (!root.dtlData)
                                                return qsTr("Down-the-line camera not assigned")
                                            return qsTr("Face-on + down-the-line detected")
                                        }
                                    }
                                    Rectangle { width: parent.width; height: 1; color: Theme.colorBorderMid }
                                    SummaryRow {
                                        width: parent.width
                                        rowLabel: qsTr("IMUs")
                                        good: root.stepStates[2] !== "skipped" && root.imusOk
                                        rowValue: root.stepStates[2] === "skipped"
                                                      ? qsTr("Skipped — no motion data")
                                                      : root.imusOk
                                                          ? qsTr("%1 sensors assigned").arg(root.curImuReqs.filter(function(r){ return r.required }).length)
                                                          : qsTr("Some sensors not assigned")
                                    }
                                }
                            }

                            // Notice box
                            Rectangle {
                                width: parent.width
                                height: noticeText.implicitHeight + Theme.sp(20)
                                radius: Theme.radius
                                color:  root.fullyReady ? Theme.colorGoodLight : Theme.colorWarnLight
                                border.width: 1
                                border.color: root.fullyReady ? Theme.colorGood : Theme.colorWarn

                                Text {
                                    id: noticeText
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.sp(12) }
                                    text: root.fullyReady
                                              ? qsTr("Everything's set up and ready to go. Step up when you like — Pinpoint will start capturing the moment you take your address.")
                                              : qsTr("Starting with an incomplete setup is fine — partial data is often still useful. For the full picture though, it's worth coming back once the hardware is sorted. Your results will thank you for it.")
                                    font.family:    Theme.fontBody
                                    font.weight:    Theme.fontBodyWeight
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          root.fullyReady ? Theme.colorGood : Theme.colorWarn
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.5
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid }

        // ── Footer ────────────────────────────────────────────────────────────

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(64)

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width:  root.contentWidth
                height: parent.height

                RowLayout {
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: Theme.sp(8)

                    // Hint text
                    Text {
                        Layout.fillWidth: true
                        text: {
                            var s = root.currentStep
                            if (s === 0) {
                                var n = root.selectedGoals.length
                                if (n === 0) return qsTr("No goals selected — defaulting to %1").arg(root.curGoalDefs[0].name)
                                return n === 1 ? qsTr("1 goal selected") : qsTr("%1 goals selected").arg(n)
                            }
                            if (s === 1) {
                                if (root.curType.optionalCamera)
                                    return root.faceOnData !== null
                                        ? qsTr("Face-on camera detected · optional for this session")
                                        : qsTr("No camera detected — optional for this session")
                                return root.camsOk
                                    ? qsTr("Face-on and down-the-line cameras detected · calibration valid")
                                    : (!root.faceOnData ? qsTr("Face-on camera not assigned")
                                                        : qsTr("Down-the-line camera not assigned"))
                            }
                            if (s === root.stepImus) return root.imusAllConnected
                                ? qsTr("All required sensors connected")
                                : root.imusOk
                                    ? qsTr("All required sensors assigned — tap Connect to pair them")
                                    : qsTr("Some sensors not assigned — or skip to start without motion data")
                            if (s === root.stepCalibrate) return root.calibrationDone
                                ? qsTr("Calibration complete")
                                : qsTr("Follow the guide to calibrate your sensors — or skip to continue without calibration")
                            return ""
                        }
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color: {
                            var s = root.currentStep
                            if (s === root.stepCameras) return (root.camsOk || root.curType.optionalCamera) ? Theme.colorGood : Theme.colorWarn
                            if (s === root.stepImus)    return root.imusAllConnected ? Theme.colorGood : Theme.colorWarn
                            if (s === root.stepCalibrate) return root.calibrationDone ? Theme.colorGood : Theme.colorWarn
                            return Theme.colorText3
                        }
                        elide: Text.ElideRight
                    }

                    // ← Back
                    PpButton {
                        visible: root.currentStep > 0
                        label:   qsTr("← Back")
                        primary: false
                        onClicked: {
                            var arr = root.stepStates.slice()
                            arr[root.currentStep] = "pending"
                            root.stepStates = arr
                            // Skip back over Calibrate + Confirm (Ready → IMUs) for non-wrist sessions.
                            if (root.currentStep === root.stepReady && !root.hasCalibrateStep)
                                root.currentStep = root.stepImus
                            else
                                root.currentStep--
                        }
                    }

                    // Skip →
                    PpButton {
                        visible: (root.currentStep === root.stepCameras && !root.camsOk && !root.curType.optionalCamera)
                                 || (root.currentStep === root.stepImus && !root.imusAllConnected)
                                 || (root.currentStep === root.stepCalibrate && !root.calibrationDone)
                        label:   qsTr("Skip →")
                        primary: false
                        onClicked: {
                            var arr = root.stepStates.slice()
                            arr[root.currentStep] = "skipped"
                            root.stepStates = arr
                            // Skipping IMUs on non-wrist sessions jumps over Calibrate + Confirm to Ready.
                            if (root.currentStep === root.stepImus && !root.hasCalibrateStep)
                                root.currentStep = root.stepReady
                            else
                                root.currentStep++
                        }
                    }

                    // Primary action
                    Rectangle {
                        implicitWidth:  primaryLbl.implicitWidth + Theme.sp(28)
                        implicitHeight: Theme.sp(38)
                        radius: Theme.radius
                        color: root.currentStep < root.lastStep
                                   ? Theme.colorAccent
                                   : (root.fullyReady ? Theme.colorGood : Theme.colorWarn)
                        // IMU step: dim until all required IMUs are connected (Skip handles bypass).
                        // Calibrate step: dim until done (Skip handles bypass).
                        // Uses primaryArea.pressed for press feedback — imperative opacity
                        // assignments would destroy this binding on first press.
                        opacity: {
                            var blocked = (root.currentStep === root.stepImus && !root.imusAllConnected)
                                       || (root.currentStep === root.stepCalibrate && !root.calibrationDone)
                            if (blocked) return 0.4
                            return primaryArea.pressed ? 0.8 : 1.0
                        }
                        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

                        Text {
                            id: primaryLbl
                            anchors.centerIn: parent
                            text: root.currentStep < root.lastStep
                                      ? qsTr("Continue →")
                                      : (root.fullyReady ? qsTr("▶  Start session") : qsTr("▶  Start anyway"))
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                        }

                        MouseArea {
                            id: primaryArea
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onClicked: {
                                if (root.currentStep < root.lastStep) {
                                    if (root.currentStep === root.stepImus && !root.imusAllConnected) return
                                    if (root.currentStep === root.stepCalibrate && !root.calibrationDone)  return
                                    var arr = root.stepStates.slice()
                                    arr[root.currentStep] = "done"
                                    root.stepStates = arr
                                    // Non-wrist sessions skip Calibrate + Confirm (IMUs → Ready).
                                    if (root.currentStep === root.stepImus && !root.hasCalibrateStep)
                                        root.currentStep = root.stepReady
                                    else
                                        root.currentStep++
                                } else {
                                    var goals = root.selectedGoals.length > 0
                                                    ? root.selectedGoals
                                                    : [root.curGoalDefs[0].key]
                                    root.sessionStartRequested(root.sessionType, goals)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Inline helper components ──────────────────────────────────────────────

    component StepIntro: Column {
        property string eyebrow: ""
        property string heading: ""
        property string body:    ""
        spacing: Theme.sp(8)

        Text {
            text:               eyebrow
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }
        Text {
            width:          parent.width
            text:           heading
            font.family:    Theme.fontDisplay
            font.italic:    Theme.fontDisplayItalic
            font.weight: Theme.fontDisplayWeight
            font.pixelSize: Math.min(Theme.sp(20), Theme.fontSzDisplay)
            color:          Theme.colorText
            wrapMode:       Text.WordWrap
        }
        Text {
            width:          parent.width
            text:           body
            font.family:    Theme.fontBody
            font.weight:    Theme.fontBodyWeight
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText2
            wrapMode:       Text.WordWrap
            lineHeight:     1.65
        }
        Rectangle {
            width: parent.width; height: 1
            color: Theme.colorBorderMid
        }
    }

    // Small on/off toggle — mirrors the TogglePill in ImusPanel.qml so the
    // wizard's per-IMU enable switch looks and behaves identically to Settings.
    component TogglePill: Rectangle {
        id: pill
        property bool checked: false
        signal toggled(bool value)

        width:  Theme.sp(34)
        height: Theme.sp(18)
        radius: Theme.sp(9)
        color:  pill.checked ? Theme.colorAccent : Theme.colorBg3
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            width:  Theme.sp(12)
            height: Theme.sp(12)
            radius: Theme.sp(6)
            color:  "white"
            anchors.verticalCenter: parent.verticalCenter
            x: pill.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
            Behavior on x { NumberAnimation { duration: 120 } }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape:  Qt.PointingHandCursor
            onClicked:    pill.toggled(!pill.checked)
        }
    }

    component StatusCircle: Rectangle {
        property bool ok:    false
        property bool warn:  false   // amber — found but not ready (e.g. assigned but not connected)
        property bool error: false   // red   — hard failure (not found / not assigned)
        width: Theme.sp(17); height: Theme.sp(17); radius: Theme.sp(9)
        color:        ok    ? Theme.colorGoodLight  :
                      warn  ? Theme.colorWarnLight  :
                      error ? Theme.colorErrorLight : "transparent"
        border.width: 1
        border.color: ok    ? Theme.colorGood  :
                      warn  ? Theme.colorWarn  :
                      error ? Theme.colorError : Theme.colorBorderMid
        Text {
            anchors.centerIn: parent
            visible:        parent.ok
            text:           "✓"
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorGood
        }
    }

    component CheckRow: Item {
        id: cr
        property bool   ok:            false
        property bool   warn:          false  // amber — found but not connected/ready
        property bool   optional:      false  // true = muted colour when hard-failing
        property string label:         ""
        property string subOk:         ""
        property string subWarn:       ""    // shown when warn && !ok
        property string subFail:       ""
        property bool   showRecal:     false
        property bool   recalEnabled:  false
        // Disabled state — muted/greyed, neutral circle, shows subDisabled.
        property bool   disabled:      false
        property string subDisabled:   ""
        // Optional on/off toggle on the right (e.g. enable an IMU for connection).
        property bool   showToggle:    false
        property bool   toggleChecked: true
        signal toggled(bool value)
        // RHS status chip — shown when chipText is non-empty
        property string chipText:      ""
        property color  chipColor:     "transparent"
        property color  chipTextColor: Theme.colorText3
        signal recalibrate()

        height: Theme.sp(52)

        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1; color: Theme.colorBorder
        }

        RowLayout {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: Theme.sp(10)

            StatusCircle {
                opacity: cr.disabled ? 0.45 : 1.0
                ok:    !cr.disabled && cr.ok
                warn:  !cr.disabled && !cr.ok && cr.warn
                error: !cr.disabled && !cr.ok && !cr.warn && !cr.optional
            }

            Column {
                Layout.fillWidth: true
                spacing: Theme.sp(2)
                opacity: cr.disabled ? 0.45 : 1.0
                Text {
                    text:           cr.label
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText
                }
                Text {
                    width:              parent.width
                    text:               cr.disabled ? cr.subDisabled
                                                     : (cr.ok ? cr.subOk : (cr.warn ? cr.subWarn : cr.subFail))
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color:              cr.disabled ? Theme.colorText3
                                               : (cr.ok   ? Theme.colorGood
                                               : (cr.warn ? Theme.colorWarn
                                                          : (cr.optional ? Theme.colorText3
                                                                         : Theme.colorError)))
                    elide:              Text.ElideRight
                }
            }

            // Per-row enable toggle (e.g. include/exclude an IMU for connection).
            Row {
                visible:          cr.showToggle
                spacing:          Theme.sp(6)
                Layout.alignment: Qt.AlignVCenter
                Text {
                    text:           qsTr("Enable")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color:          Theme.colorText3
                    anchors.verticalCenter: parent.verticalCenter
                }
                TogglePill {
                    checked:                cr.toggleChecked
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled:              (v) => cr.toggled(v)
                }
            }

            PpButton {
                visible:   cr.showRecal
                label:     qsTr("Recalibrate")
                primary:   false
                enabled:   cr.recalEnabled
                onClicked: cr.recalibrate()
            }

            // Status chip — e.g. "Connecting", "Connection Failed", "Connected"
            Rectangle {
                visible:        cr.chipText !== ""
                implicitWidth:  chipLbl.implicitWidth + Theme.sp(14)
                implicitHeight: Theme.sp(20)
                radius:         Theme.sp(4)
                color:          cr.chipColor
                border.width:   1
                border.color:   cr.chipTextColor
                Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    id: chipLbl
                    anchors.centerIn:   parent
                    text:               cr.chipText
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color:              cr.chipTextColor
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
        }
    }

    component SummaryRow: Item {
        id: sr
        property string rowLabel: ""
        property string rowValue: ""
        property bool   good:     true
        height: Theme.sp(44)

        RowLayout {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.sp(14) }
            spacing: Theme.sp(10)

            Rectangle {
                width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                color: sr.good ? Theme.colorGood : Theme.colorWarn
            }
            Text {
                Layout.fillWidth: true
                text:           sr.rowLabel
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorText
            }
            Text {
                text:               sr.rowValue
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              sr.good ? Theme.colorGood : Theme.colorWarn
            }
        }
    }
}
