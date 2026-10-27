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

// Session setup — Calibrate trunk (the trunk group's calibrate step; trunk_imu_sequence_grf_design.md
// §6.3 steps 1–3, §6.7; session_wizard_refactor_design.md §4.10, the recipe this is the worked
// example of).
//
// ONE guide view and ONE routine, as CalibrateArmPage: the BodyVizView the guide drives, and
// TrunkRoutine over the hip and chest sensors. The page exists only while the step is current (R2),
// so neither does the routine (R7).
//
// The avatar demonstrates each step (§6.7 "Demonstrate, then verify"): it stands tall; for a turn it
// crosses its arms, turns hips about 45° and chest about 90° away (or through), pauses, and turns
// back. A turn is TAUGHT here, facing the screen — the demo and one large instruction — and then
// run by the sensors (TrunkRoutine: armed on stillness, ended when the golfer is back facing the
// screen); nothing on the page has to be read while turned away, and the verdict appears when the
// golfer faces it again (Mark, 26 Oct). The guide (CalibrationGuide.qml) holds the pose; the view below binds it — the trunk through
// the same useTrunkOverride binding the live check draws the sensors with, the crossed arms on the
// arm overrides.
//
// GOLF TERMS AND AN UNMISTAKABLE DIRECTION (Mark, third real-sensor run, 27 Oct: "the instructions and
// the avatar need to be much much more clear — it's not clear at all which way I am supposed to turn —
// would be better to use golfing terms — backswing turn and follow-through turn make much more
// sense"). The steps are "Backswing turn" and "Follow-through turn"; the status says what to do NOW
// and never "turn back" before the turn has started. The avatar stays FACE-ON, as in every other
// calibration ceremony (Mark, 27 Oct: "all other calibration ceremonies have the avatar face on … Yes
// there is a mirroring issue, but that's why we use prompts related to the golf swing"): the golf
// terms carry the direction, not the camera. A view from behind and above was tried on review sheets
// and rejected.
//
// THE STATUS FOLLOWS THE VOICE (Mark, fourth real-sensor run, 27 Oct: "I need to know when to start the
// turn and when to come back"): "START YOUR BACKSWING TURN" the moment the step is armed, and "TURN BACK
// TO ADDRESS" only when the routine's turn-back cue fires (turnBackDue: he has turned enough, or stopped
// at the top) — not when the turn starts. With Settings → "Speak calibration instructions" on, the
// routine says the same two cues through the app's ttsController, and nothing else is ever spoken
// (TrunkRoutine.qml says why). No ttsController in this context (tests, headless) → no voice: the
// routine skips and logs it.
//
// Lifecycle (R8):
//   enter     — a recorded trunk outcome that still holds (both sensors fullyCalibrated) shows
//               complete with no re-run (restore); otherwise a fresh run (begin).
//   leave     — the routine stops (its open step is closed on the recorder; nothing fires after).
//   completed — the outcome goes into the draft: the two sensor units, checked live from then on.
// Recalibrate begins a fresh run; its done going false drops the outcome, so Continue closes at once.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

WizardPage {
    id: page

    fullBleed: true

    readonly property bool _done: page.draft.outcome("trunk").done

    canContinue: _done
    canSkip:     !_done
    hint:        _done ? qsTr("Trunk sensors calibrated")
                       : qsTr("Follow the guide to calibrate the hip and chest sensors — or skip to continue without them")
    hintTone:    _done ? "good" : "warn"

    // The recorder (trunk_ceremony_recorder.h), through the context — named as the context object
    // is, so lint W5 checks the members the routine uses.
    readonly property var trunkCeremony: page.ctx.trunkCeremony
    // The app's voice (TtsController), when this context has one — not under a name the routine's own
    // `ttsController` property would shadow.
    readonly property var _voice: typeof ttsController !== "undefined" ? ttsController : null
    // The two sensor UNITS the routine calibrates, and the draft's outcome is checked against.
    readonly property var _segments: ({ pelvis: page.ctx.roles.pelvis.unit, thorax: page.ctx.roles.thorax.unit })
    function _outcomeDevices() {
        var out = []
        if (_segments.pelvis) out.push(_segments.pelvis)
        if (_segments.thorax) out.push(_segments.thorax)
        return out
    }

    // For the hosts and the tests.
    readonly property alias routine: trunkRoutine
    readonly property alias guide:   trunkGuide

    function enter(direction) {
        if (page.draft.outcome("trunk").done) trunkRoutine.restore()
        else                                  trunkRoutine.begin()
    }
    function leave(reason) { trunkRoutine.stop(reason) }

    function recalibrate() { trunkRoutine.begin() }

    TrunkRoutine {
        id: trunkRoutine
        // R3: bound EXPLICITLY.
        active:        page.active
        guide:         trunkGuide
        segments:      page._segments
        rightHanded:   !page.ctx.leftHanded
        trunkCeremony: page.trunkCeremony
        speakInstructions: page.ctx.settings ? page.ctx.settings.experimentalSpokenInstructions === true : false
        ttsController:     page._voice

        onCompleted: page.draft.recordOutcome("trunk", "witmotion", page._outcomeDevices())
    }

    // A done that goes away while the page is up is a Recalibrate (begin() clears it): the recorded
    // outcome no longer stands.
    Connections {
        target:  trunkRoutine
        enabled: page.active
        function onDoneChanged() {
            if (!trunkRoutine.done) page.draft.clearOutcome("trunk")
        }
    }

    // The 3-D guide (R9). The view registers itself as `guide.view`.
    CalibrationGuide {
        id: trunkGuide
        active: page.active
    }

    // ── What the golfer reads, per stage (§6.3, verbatim where it gives the words) ──
    readonly property string _stage: trunkRoutine.stage
    readonly property string _sub:   trunkRoutine.substage
    readonly property bool _turnStage: _stage === "turnAway" || _stage === "turnThrough"
    // The status the golfer acts on, set large: the hold, and a turn's arming / turn / come back.
    readonly property bool _bigStatus: _sub === "hold" || (_turnStage && (_sub === "arming" || _sub === "armed"
                                                                        || _sub === "turning"))
    readonly property string _instruction: {
        switch (_stage) {
        case "intro":       return qsTr("Watch the guide. You will stand tall, then make a backswing turn and a follow-through turn, about a minute in all.")
        case "standTall":   return _sub === "ready"
                                   ? qsTr("Put the sensors on — hip on your belt, chest in the vest pocket, USB down, label out — and stand tall. It starts by itself when you're still.")
                                   : qsTr("Stand tall, toes to the stick, arms relaxed. Hold still.")
        case "turnAway":    return qsTr("Arms crossed, make a slow backswing turn — then turn back to address.")
        case "turnThrough": return qsTr("Arms crossed, make a slow follow-through turn — then turn back to address.")
        case "done":        return qsTr("Both sensors are calibrated and both turns read the right way.")
        default:            return ""
        }
    }
    readonly property string _status: {
        if (_stage === "standTall") {
            if (_sub === "hold")   return trunkRoutine.holdProgress > 0 ? qsTr("HOLD STILL — CAPTURING") : Theme.caps(qsTr("Hold still"))
            if (_sub === "passed") return Theme.caps(qsTr("Stand tall captured"))
            if (_sub === "retry")  return Theme.caps(qsTr("Once more"))
            return Theme.caps(qsTr("Get ready"))
        }
        if (_turnStage) {
            if (_sub === "demo")    return Theme.caps(qsTr("Watch the guide"))
            // What to do NOW, as the voice says it: start, then "turn back" only at the turn-back cue
            // (Mark, 27 Oct: it "tells me to turn … then before I have moved tells me to turn back").
            if (_sub === "arming")  return qsTr("HOLD STILL AT ADDRESS, FACING THE SCREEN")
            if (_sub === "armed" || (_sub === "turning" && !trunkRoutine.turnBackDue))
                return _stage === "turnAway" ? qsTr("START YOUR BACKSWING TURN")
                                             : qsTr("START YOUR FOLLOW-THROUGH TURN")
            if (_sub === "turning") return qsTr("TURN BACK TO ADDRESS")
            if (_sub === "returned" || _sub === "timeout") return Theme.caps(qsTr("Checking"))
            if (_sub === "passed")  return _stage === "turnAway" ? Theme.caps(qsTr("Backswing turn checked"))
                                                                 : Theme.caps(qsTr("Follow-through turn checked"))
            if (_sub === "retry")   return Theme.caps(qsTr("Once more"))
        }
        if (_stage === "intro") return Theme.caps(qsTr("Watch the guide"))
        if (_stage === "done")  return Theme.caps(qsTr("Calibration complete"))
        if (_stage === "failed") return Theme.caps(qsTr("Stopped"))
        return ""
    }

    RowLayout {
        anchors.fill:    parent
        anchors.margins: Theme.gap(8)
        spacing:         Theme.gap(12)

        BodyVizView {
            id: trunkBvv
            Layout.fillHeight:   true
            Layout.fillWidth:    true
            Layout.minimumWidth: Theme.sp(200)
            Component.onCompleted: trunkGuide.view = trunkBvv

            poseSource:  null   // no camera input during calibration
            rightHanded: !page.ctx.leftHanded

            // The trunk: the guide's golfer-relative turns, as q_anat (the guide's header).
            useTrunkOverride:       true
            hipsOverrideRotation:   trunkGuide.hipsTarget
            thoraxOverrideRotation: trunkGuide.thoraxTarget
            animateTrunk:           trunkGuide.trunkAnimating

            // Both arms hang at the side (the routines' rest pose) for stand tall, and fold on the
            // chest for the turns. The arms are posed, never animated, here: the trunk routine
            // moves only the trunk.
            useLeadArmOverride:           true
            leadArmOverrideRotation:      trunkGuide.armsCrossed ? trunkBvv.crossedLeadArmQuat     : trunkGuide.armTarget
            leadForeArmOverrideRotation:  trunkGuide.armsCrossed ? trunkBvv.crossedLeadForeArmQuat : Qt.quaternion(1, 0, 0, 0)
            useTrailArmOverride:          true
            trailArmOverrideRotation:     trunkGuide.armsCrossed ? trunkBvv.crossedTrailArmQuat     : Qt.quaternion(0.7071, 0.7071, 0, 0)
            trailForeArmOverrideRotation: trunkGuide.armsCrossed ? trunkBvv.crossedTrailForeArmQuat : Qt.quaternion(1, 0, 0, 0)
            animateLeadArm:               false
        }

        // ── The status column ───────────────────────────────────────────────
        Column {
            Layout.preferredWidth: Theme.sp(360)
            Layout.fillHeight:     true
            Layout.topMargin:      Theme.gap(32)
            spacing:               Theme.gap(16)

            Text {
                width:              parent.width
                text:               page.stepLabel
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:              Theme.colorText3
            }

            PpDisplayText {
                width:     parent.width
                text:      qsTr("Calibrate the trunk sensors")
                pixelSize: Math.min(Theme.sp(18), Theme.fontSzDisplay)
                wrapMode:  Text.WordWrap
            }

            // The three steps, ticked as they pass.
            Column {
                width:   parent.width
                spacing: Theme.gap(6)
                Repeater {
                    model: [{ key: "standTall",   label: qsTr("Stand tall") },
                            { key: "turnAway",    label: qsTr("Backswing turn") },
                            { key: "turnThrough", label: qsTr("Follow-through turn") }]
                    delegate: Text {
                        required property var modelData
                        readonly property int _at: ["standTall", "turnAway", "turnThrough"].indexOf(page._stage)
                        readonly property int _me: ["standTall", "turnAway", "turnThrough"].indexOf(modelData.key)
                        readonly property bool _passed: page._stage === "done" || (_at > _me)
                                                        || (_at === _me && page._sub === "passed")
                        readonly property bool _current: _at === _me && !_passed
                        objectName: "trunkStep_" + modelData.key
                        text: (_passed ? "✓  " : _current ? "›  " : "·  ") + modelData.label
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    _current ? Theme.fontBodyWeight : Font.Normal
                        color: _passed ? Theme.colorGood : _current ? Theme.colorText : Theme.colorText3
                    }
                }
            }

            Text {
                objectName: "trunkInstruction"
                width:      parent.width
                visible:    text !== ""
                text:       page._instruction
                wrapMode:   Text.WordWrap
                lineHeight: page._turnStage ? 1.25 : 1.5
                font.family:    Theme.fontBody
                // A turn's one instruction is read BEFORE turning, from where the golfer stands: large.
                font.pixelSize: page._turnStage ? Theme.fontSzData : Theme.fontSzBody2
                color:          page._turnStage ? Theme.colorText : Theme.colorText2
            }

            // The hold bar (stand tall): fills while both sensors are still.
            Item {
                objectName: "trunkHoldBar"
                readonly property real fillFraction: trunkRoutine.holdProgress
                visible: page._stage === "standTall" || page._stage === "done"
                width:   parent.width
                height:  Theme.sp(32)
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width:  parent.width
                    height: Theme.sp(4)
                    radius: Theme.sp(2)
                    color:  Theme.colorBg3
                    Rectangle {
                        width:  parent.width * parent.parent.fillFraction
                        height: parent.height
                        radius: parent.radius
                        color:  trunkRoutine.done ? Theme.colorGood : Theme.colorAccent
                        Behavior on width { NumberAnimation { duration: 150 } }
                    }
                }
            }

            // Why stand tall is waiting, or the bar keeps emptying: the sensor that is the reason.
            Text {
                objectName: "trunkWaitReason"
                width:      parent.width
                visible:    text !== ""
                text:       page._stage === "standTall" || page._turnStage ? trunkRoutine.waitReason : ""
                wrapMode:   Text.WordWrap
                lineHeight: 1.5
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                color:          Theme.colorWarn
            }

            Text {
                objectName: "trunkStatus"
                width:      parent.width
                visible:    text !== ""
                text:       page._status
                wrapMode:   Text.WordWrap
                font.family:        Theme.fontData
                font.pixelSize:     page._bigStatus ? Theme.fontSzHeading : Theme.fontSzMicro
                font.bold:          page._bigStatus
                font.letterSpacing: Theme.trackingData
                color: page._stage === "done"   ? Theme.colorGood
                     : page._stage === "failed" ? Theme.colorError
                     : page._bigStatus ? Theme.colorAccent
                     : Theme.colorText3
            }

            // The failure, naming the sensor and the cause (the C++ records' words). A turn's verdict is
            // read when the golfer is back facing the screen: large.
            Text {
                objectName: "trunkMessage"
                width:      parent.width
                visible:    text !== ""
                text:       trunkRoutine.message
                wrapMode:   Text.WordWrap
                lineHeight: page._turnStage ? 1.25 : 1.5
                font.family:    Theme.fontBody
                font.pixelSize: page._turnStage ? Theme.fontSzData : Theme.fontSzBody2
                color: trunkRoutine.messageKind === "warn" ? Theme.colorWarn : Theme.colorError
            }

            PpButton {
                objectName: "trunkRecalibrate"
                visible: page._stage !== "idle" && page._stage !== "intro"
                label:   qsTr("↺  Recalibrate")
                primary: false
                onClicked: page.recalibrate()
            }
        }
    }
}
