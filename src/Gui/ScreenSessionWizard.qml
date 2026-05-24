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
        currentStep = 1
        stepStates  = ["done", "pending", "pending", "pending"]
    }

    // ── Step state ────────────────────────────────────────────────────────────

    property int currentStep: 0
    property var stepStates:  ["pending", "pending", "pending", "pending"]

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

    // ── TODO stubs for IMU calibration (not yet implemented) ─────────────────

    property bool _todo_calibrationComplete: false  // TODO: bind to real property
    property bool _todo_calibrationRunning:  false  // TODO: bind to real property

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

    // All required slots assigned and zero-g calibration complete.
    readonly property bool imusOk: {
        if (!_todo_calibrationComplete) return false
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
            if (!_todo_calibrationComplete)
                issues.push({ text: qsTr("Zero-g calibration not completed — place all sensors flat and still"), panel: -1 })
        }

        return issues
    }

    readonly property bool fullyReady: readinessIssues.length === 0

    // Max content width — centred on wider screens like other pages
    readonly property int contentWidth: Math.min(width - Theme.sp(80), Theme.sp(600))

    onVisibleChanged: {
        if (visible) {
            var saved = appSettings.sessionGoalsByType[sessionType.toString()]
            selectedGoals   = (saved && saved.length > 0) ? saved.slice() : []
            goalsInteracted = false
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
                            font.pixelSize: Math.min(Theme.sp(22), Theme.fontSzDisplay)
                            color:          Theme.colorText
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Text {
                        width:          parent.width
                        text:           qsTr("Before you step up, let's make sure everything is ready. We'll confirm your goals, check your cameras, and connect your sensors — it only takes a moment.")
                        font.family:    Theme.fontBody
                        font.weight:    Font.Light
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
                    model: [qsTr("Goals"), qsTr("Cameras"), qsTr("IMUs"), qsTr("Ready")]

                    delegate: Row {
                        id: tabRow
                        required property string modelData
                        required property int    index
                        spacing: 0

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
                                eyebrow: qsTr("STEP 1 OF 4 · GOALS")
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

                                        Layout.fillWidth: true
                                        height: chipCol.implicitHeight + Theme.sp(36)
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
                                                wrapMode:           Text.WordWrap
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
                                eyebrow: qsTr("STEP 2 OF 4 · CAMERAS")
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

                            StepIntro {
                                width:   parent.width
                                eyebrow: qsTr("STEP 3 OF 4 · MOTION SENSORS")
                                heading: qsTr("Attaching your motion sensors")
                                body:    qsTr("Switch each sensor on before stepping up. The positions below are specific to this session type — attach them in the right place and pair each one. Assign placements in Settings → IMUs if you haven't already.")
                            }

                            // ── Scan header ───────────────────────────────────
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
                                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Font.Light
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        text:           imuWizScanBtn.scanning ? qsTr("Scanning…") : qsTr("Scan")
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Font.Light
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

                                        ok: {
                                            var list      = imuManager.imuDeviceList
                                            var placement = appSettings.imuPlacement
                                            for (var i = 0; i < list.length; ++i)
                                                if (placement[list[i].id] === modelData.slot) return true
                                            return false
                                        }

                                        subOk: {
                                            var list      = imuManager.imuDeviceList
                                            var placement = appSettings.imuPlacement
                                            for (var i = 0; i < list.length; ++i) {
                                                if (placement[list[i].id] === modelData.slot) {
                                                    var d = list[i]
                                                    return [d.alias || d.description, d.transport, d.id]
                                                               .filter(function(s){ return s && s !== "" })
                                                               .join(" · ")
                                                }
                                            }
                                            return ""
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

                                // ── Calibration row (TODO: not yet implemented) ─
                                Item {
                                    width: parent.width; height: Theme.sp(52)
                                    Rectangle {
                                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                                        height: 1; color: Theme.colorBorder
                                    }
                                    RowLayout {
                                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                                        spacing: Theme.sp(10)
                                        StatusCircle { ok: root._todo_calibrationComplete }
                                        Column {
                                            Layout.fillWidth: true
                                            spacing: Theme.sp(2)
                                            Text {
                                                text:           qsTr("Zero-g calibration")
                                                font.family:    Theme.fontBody
                                                font.pixelSize: Theme.fontSzBody2
                                                color:          Theme.colorText
                                            }
                                            Text {
                                                width: parent.width
                                                text:  root._todo_calibrationComplete
                                                           ? qsTr("Calibrated")
                                                           : qsTr("PLACE SENSOR FLAT AND STILL")  // TODO: trigger from controller
                                                font.family:        Theme.fontData
                                                font.pixelSize:     Theme.fontSzMicro
                                                font.letterSpacing: Theme.trackingData
                                                color: root._todo_calibrationComplete ? Theme.colorGood : Theme.colorWarn
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Item {
                                            width: Theme.sp(20); height: Theme.sp(20)
                                            visible: root._todo_calibrationRunning && !root._todo_calibrationComplete
                                            Text {
                                                id: calSpinner
                                                anchors.centerIn: parent
                                                text: "↺"; font.pixelSize: Theme.sp(14); color: Theme.colorText3
                                            }
                                            RotationAnimator {
                                                target: calSpinner; from: 0; to: 360
                                                duration: 1200; loops: Animation.Infinite
                                                running: root._todo_calibrationRunning && !root._todo_calibrationComplete && root.visible
                                            }
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

                    // ── Panel 3: Ready ────────────────────────────────────────

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
                                    text:               qsTr("STEP 4 OF 4 · READY")
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
                                    font.weight:    Font.Light
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
                                                font.weight:    Font.Light
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
                                    font.weight:    Font.Light
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
                            if (s === 2) return root.imusOk
                                ? qsTr("All required sensors assigned")
                                : qsTr("Some sensors not assigned — or skip to start without motion data")
                            return ""
                        }
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingData
                        color: {
                            var s = root.currentStep
                            if (s === 1) return (root.camsOk || root.curType.optionalCamera) ? Theme.colorGood : Theme.colorWarn
                            if (s === 2) return root.imusOk ? Theme.colorGood : Theme.colorWarn
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
                            root.currentStep--
                        }
                    }

                    // Skip →
                    PpButton {
                        visible: (root.currentStep === 1 && !root.camsOk && !root.curType.optionalCamera)
                                 || (root.currentStep === 2 && !root.imusOk)
                        label:   qsTr("Skip →")
                        primary: false
                        onClicked: {
                            var arr = root.stepStates.slice()
                            arr[root.currentStep] = "skipped"
                            root.stepStates = arr
                            root.currentStep++
                        }
                    }

                    // Primary action
                    Rectangle {
                        implicitWidth:  primaryLbl.implicitWidth + Theme.sp(28)
                        implicitHeight: Theme.sp(38)
                        radius: Theme.radius
                        color: root.currentStep < 3
                                   ? Theme.colorAccent
                                   : (root.fullyReady ? Theme.colorGood : Theme.colorWarn)

                        Text {
                            id: primaryLbl
                            anchors.centerIn: parent
                            text: root.currentStep < 3
                                      ? qsTr("Continue →")
                                      : (root.fullyReady ? qsTr("▶  Start session") : qsTr("▶  Start anyway"))
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color: Theme.dark ? Theme.colorBg : "#FFFFFF"
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape:  Qt.PointingHandCursor
                            onPressed:    parent.opacity = 0.8
                            onReleased:   parent.opacity = 1.0
                            onClicked: {
                                if (root.currentStep < 3) {
                                    var arr = root.stepStates.slice()
                                    arr[root.currentStep] = "done"
                                    root.stepStates = arr
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
            font.pixelSize: Math.min(Theme.sp(20), Theme.fontSzDisplay)
            color:          Theme.colorText
            wrapMode:       Text.WordWrap
        }
        Text {
            width:          parent.width
            text:           body
            font.family:    Theme.fontBody
            font.weight:    Font.Light
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

    component StatusCircle: Rectangle {
        property bool ok: false
        width: Theme.sp(17); height: Theme.sp(17); radius: Theme.sp(9)
        color:        ok ? Theme.colorGoodLight  : "transparent"
        border.width: 1
        border.color: ok ? Theme.colorGood : Theme.colorBorderMid
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
        property bool   ok:           false
        property bool   optional:     false  // true = muted warn colour when not ok
        property string label:        ""
        property string subOk:        ""
        property string subFail:      ""
        property bool   showRecal:    false
        property bool   recalEnabled: false
        signal recalibrate()

        height: Theme.sp(52)

        Rectangle {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 1; color: Theme.colorBorder
        }

        RowLayout {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: Theme.sp(10)

            StatusCircle { ok: cr.ok }

            Column {
                Layout.fillWidth: true
                spacing: Theme.sp(2)
                Text {
                    text:           cr.label
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText
                }
                Text {
                    width:              parent.width
                    text:               cr.ok ? cr.subOk : cr.subFail
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color:              cr.ok ? Theme.colorGood : (cr.optional ? Theme.colorText3 : Theme.colorWarn)
                    elide:              Text.ElideRight
                }
            }

            PpButton {
                visible:   cr.showRecal
                label:     qsTr("Recalibrate")
                primary:   false
                enabled:   cr.recalEnabled
                onClicked: cr.recalibrate()
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
