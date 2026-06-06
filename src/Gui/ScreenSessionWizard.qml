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
import PinPointStudio

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

    // Set by the Confirm-Tracking "Recalibrate" link before navigating back to
    // the Calibrate step, so onCurrentStepChanged forces a fresh calibration
    // instead of retaining the previous "complete" state (backward navigation).
    property bool _forceRecalibrate: false

    // True only for Wrist Motion (index 1) — enables the Calibrate + Confirm steps.
    readonly property bool hasCalibrateStep: sessionType === 1
    // Terminal step index: Ready for wrist; for non-wrist Calibrate/Confirm are
    // skipped so the display count is 4.
    readonly property int lastStep: hasCalibrateStep ? stepReady : 4
    // Total visible steps for the "STEP x OF N" eyebrows.
    readonly property int totalSteps: hasCalibrateStep ? 5 : 4

    // The Calibrate / Confirm steps host a full 3D visualisation that should use
    // the entire viewport width, unlike the text-led steps which are clamped to a
    // comfortable reading column (Theme.contentWidth).
    readonly property bool isVizStep: currentStep === stepCalibrate || currentStep === stepConfirm

    // ── Positional calibration (Panel 3) ──────────────────────────────────────
    // The calibration state machine, timers, helpers, BodyVizView guide, status
    // column and completion ting now live in ImuCalibrationFlow.qml (calibFlow,
    // hosted by Panel 3). The wizard reads calibFlow.calibrationDone for gating
    // and drives it via begin() / showCompleted() from onCurrentStepChanged.

    // Track the previous step so we know the direction of navigation.
    property int _prevStep: -1

    onCurrentStepChanged: {
        var prev    = _prevStep
        _prevStep   = currentStep

        if (currentStep === root.stepCalibrate) {
            // "Recalibrate" from Confirm Tracking: force a fresh run regardless
            // of navigation direction (otherwise the restore branch below would
            // restore the previous "complete" state).
            if (root._forceRecalibrate) {
                root._forceRecalibrate = false
                calibFlow.begin()
                return
            }

            if (prev >= root.stepCalibrate) {
                // Navigating backward from a later step — retain completed state.
                calibFlow.showCompleted()
            } else {
                // Going forward to the Calibrate step — if the lead IMU instance
                // already has calibration from this session, restore; else start fresh.
                var imu = calibFlow.leadImu
                if (imu !== null && imu.calibrated)
                    calibFlow.showCompleted()
                else
                    calibFlow.begin()
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
    // perspective: 2 = face-on, 1 = down-the-line (matches CameraInstance convention).
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
            if (root.hasCalibrateStep && !calibFlow.calibrationDone)
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
            // Viz steps fill the viewport exactly (no scroll padding); text-led
            // steps add a little bottom breathing room. The Math.max keeps content
            // scrollable if it ever exceeds the viewport (e.g. a very short window).
            contentHeight: Math.max(height, bodyPanel.implicitHeight + (root.isVizStep ? 0 : Theme.sp(40)))
            clip:          true

            Item {
                id: bodyPanel
                anchors.horizontalCenter: parent.horizontalCenter
                // Viz steps span the full viewport; text-led steps stay in the
                // centred reading column.
                width:          root.isVizStep ? parent.width : root.contentWidth
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

                            // True when at least one enabled, assigned sensor is not yet
                            // connected — i.e. there is something for "Connect" to do.
                            // Drives the wizard footer button (Connect ↔ Continue).
                            readonly property bool canConnect: {
                                var _dep = imuManager.instances   // reactive
                                if (_connecting) return false
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
                        // Panel 3 hosts the extracted calibration flow (full
                        // layout). Fill the visible viewport height, gated on the
                        // active step so the full-viewport height does not inflate
                        // the StackLayout on the text-led steps.
                        implicitHeight: root.isVizStep ? Math.max(Theme.sp(480), bodyStack.parent.parent.height) : 0

                        ImuCalibrationFlow {
                            id: calibFlow
                            anchors.fill: parent
                            layoutMode:   "full"
                            showHeader:   true
                            stepLabel:    qsTr("STEP 4 OF %1 · CALIBRATE").arg(root.totalSteps)
                            // The wizard advances via the footer; nothing required here.
                            onCompleted: {}
                        }
                    }

                    // ── Panel 4: Confirm Tracking ─────────────────────────────
                    // Only reachable for wrist motion sessions (hasCalibrateStep).

                    Item {
                        // Gated on the active step so this panel's full-viewport
                        // height does not inflate the StackLayout on other steps.
                        implicitHeight: root.isVizStep ? Math.max(Theme.sp(480), bodyStack.parent.parent.height) : 0

                        ColumnLayout {
                            anchors.fill:    parent
                            anchors.margins: Theme.sp(8)
                            spacing:         Theme.sp(12)

                            Column {
                                Layout.fillWidth:    true
                                Layout.maximumWidth: root.contentWidth
                                Layout.topMargin:    Theme.sp(32)
                                spacing:             Theme.sp(8)

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

                            // Live tracking — the arm avatar is driven entirely by the
                            // calibrated sensors (anatQuat), resolved per slot inside
                            // ArmVizView. Move your arm and confirm the model follows.
                            ArmVizView {
                                id: confirmArmViz
                                Layout.fillWidth:  true
                                Layout.fillHeight: true
                            }

                            // Recalibrate affordance — returns to the Calibrate step and forces
                            // a fresh capture (root._forceRecalibrate), used when tracking looks
                            // wrong. Calibration itself happens there, not on this screen.
                            Row {
                                Layout.fillWidth:    true
                                Layout.bottomMargin: Theme.sp(8)
                                spacing:             Theme.sp(6)

                                Text {
                                    text:           qsTr("Not tracking your movement?")
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText3
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    id:             recalLink
                                    text:           qsTr("Recalibrate")
                                    font.family:    Theme.fontBody
                                    font.weight:    Theme.fontBodyWeight
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorAccent
                                    anchors.verticalCenter: parent.verticalCenter
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape:  Qt.PointingHandCursor
                                        onClicked: {
                                            var arr = root.stepStates.slice()
                                            arr[root.stepCalibrate] = "pending"
                                            arr[root.stepConfirm]   = "pending"
                                            root.stepStates = arr
                                            root._forceRecalibrate = true
                                            root.currentStep = root.stepCalibrate
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
                            if (s === root.stepCalibrate) return calibFlow.calibrationDone
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
                            if (s === root.stepCalibrate) return calibFlow.calibrationDone ? Theme.colorGood : Theme.colorWarn
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
                                 || (root.currentStep === root.stepCalibrate && !calibFlow.calibrationDone)
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
                        id: primaryBtn
                        // IMU step: the primary button doubles as "Connect" until every
                        // ENABLED sensor is connected, then becomes "Continue". Keeps the
                        // bottom-right button the single "keep progressing" control.
                        // "Ready to continue" = required slots all connected AND nothing
                        // enabled left to connect (imusCol.canConnect false). Using
                        // canConnect — not just imusAllConnected — means re-enabling an
                        // optional sensor mid-page correctly reverts the button to "Connect".
                        readonly property bool imuConnectMode:
                            root.currentStep === root.stepImus
                            && !(root.imusAllConnected && !imusCol.canConnect)

                        implicitWidth:  primaryLbl.implicitWidth + Theme.sp(28)
                        implicitHeight: Theme.sp(38)
                        radius: Theme.radius
                        color: root.currentStep < root.lastStep
                                   ? Theme.colorAccent
                                   : (root.fullyReady ? Theme.colorGood : Theme.colorWarn)
                        // Dim only when there's nothing actionable: Calibrate not done, or
                        // IMU connect-mode with nothing left to connect (and not mid-connect).
                        // Uses primaryArea.pressed for press feedback — imperative opacity
                        // assignments would destroy this binding on first press.
                        opacity: {
                            var blocked = (root.currentStep === root.stepCalibrate && !calibFlow.calibrationDone)
                                       || (primaryBtn.imuConnectMode && !imusCol.canConnect && !imusCol._connecting)
                            if (blocked) return 0.4
                            return primaryArea.pressed ? 0.8 : 1.0
                        }
                        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

                        Text {
                            id: primaryLbl
                            anchors.centerIn: parent
                            text: primaryBtn.imuConnectMode
                                      ? (imusCol._connecting ? qsTr("Connecting…") : qsTr("Connect"))
                                      : (root.currentStep < root.lastStep
                                            ? qsTr("Continue →")
                                            : (root.fullyReady ? qsTr("▶  Start session") : qsTr("▶  Start anyway")))
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                        }

                        MouseArea {
                            id: primaryArea
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onClicked: {
                                // IMU connect-mode: trigger connection instead of advancing.
                                if (primaryBtn.imuConnectMode) {
                                    if (!imusCol._connecting && imusCol.canConnect) imusCol.startConnect()
                                    return
                                }
                                if (root.currentStep < root.lastStep) {
                                    if (root.currentStep === root.stepCalibrate && !calibFlow.calibrationDone)  return
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
