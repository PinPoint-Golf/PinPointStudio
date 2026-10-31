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

// A calibration ceremony's status column — the one drawing of it, beside the guide view. Used by
// ArmCalibrationStatus (both routines, Witmotion and HackMotion) and CalibrateTrunkPage.
// PRESENTATIONAL ONLY: the host works out what each line says from its routine and hands the
// words in; Recalibrate is a signal.
//
// Top to bottom, each part drawn only when it has something to show:
//   eyebrow (stepLabel) and title       — showHeader
//   leading                             — the host's own items under the title (the arm's badge,
//                                         the trunk's step list)
//   instruction                         — what to do; `large` sets it for reading from a distance
//   the hold bar                        — holdShown; fills with holdProgress, good once holdDone
//   belowBar                            — the host's items under the bar (the wG3's step bar, the
//                                         trunk's wait reason)
//   the status line                     — status, set large and bold while statusBig; statusTone
//                                         colours it ("good" | "error" | "warn" | "accent" | "" quiet);
//                                         statusTick draws the ✓ beside it
//   belowStatus                         — the host's items under the status (warnings, readouts)
//   message                             — the failure, in its messageKind's colour ("warn" | error)
//   ↺ Recalibrate                       — showRecalibrate
// `namePrefix` names the parts for tests and probes: <prefix>Instruction, <prefix>HoldBar,
// <prefix>Status, <prefix>Message, <prefix>Recalibrate ("" leaves them unnamed).
//
// The parts are also inline components (CalibrationStatusColumn.BodyText, .HoldBar, .StatusLine,
// .MessageText), for a host that lays them out itself — ArmCalibrationStatus's compact layout.
import QtQuick
import PinPointStudio

Column {
    id: col

    property bool   showHeader: true
    property string stepLabel:  ""
    property string title:      ""
    property string namePrefix: ""

    property alias  leading:     leadingSlot.data
    property string instruction: ""
    // A step read from where the golfer stands (the trunk's turns): the instruction and the message
    // set large.
    property bool   large:       false

    property bool   holdShown:    false
    property real   holdProgress: 0
    property bool   holdDone:     false
    property alias  belowBar:     belowBarSlot.data

    property string status:         ""
    property bool   statusBig:      false
    property string statusTone:     ""
    property bool   statusTick:     false
    property bool   statusAnimated: true
    property alias  belowStatus:    belowStatusSlot.data

    property string message:     ""
    property string messageKind: ""
    property real   messageSize: Theme.fontSzMicro

    property bool   showRecalibrate: false
    signal recalibrate()

    spacing: Theme.gap(16)

    function _name(part) { return namePrefix !== "" ? namePrefix + part : "" }

    Text {
        visible:            col.showHeader
        width:              parent.width
        text:               col.stepLabel
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }

    PpDisplayText {
        visible:   col.showHeader
        width:     parent.width
        text:      col.title
        pixelSize: Math.min(Theme.sp(18), Theme.fontSzDisplay)
        wrapMode:  Text.WordWrap
    }

    // The slots are Columns at the column's own spacing: an empty one has no height and is skipped,
    // so the host's items sit exactly as if they were the column's own.
    Column { id: leadingSlot; width: parent.width; spacing: col.spacing }

    BodyText {
        objectName: col._name("Instruction")
        width:      parent.width
        text:       col.instruction
        large:      col.large
    }

    HoldBar {
        objectName: col._name("HoldBar")
        visible:    col.holdShown
        width:      parent.width
        progress:   col.holdProgress
        done:       col.holdDone
    }

    Column { id: belowBarSlot; width: parent.width; spacing: col.spacing }

    StatusLine {
        objectName: col._name("Status")
        width:      parent.width
        text:       col.status
        big:        col.statusBig
        tone:       col.statusTone
        tick:       col.statusTick
        animated:   col.statusAnimated
    }

    Column { id: belowStatusSlot; width: parent.width; spacing: col.spacing }

    MessageText {
        objectName: col._name("Message")
        width:      parent.width
        text:       col.message
        kind:       col.messageKind
        large:      col.large
        size:       col.messageSize
    }

    PpButton {
        objectName: col._name("Recalibrate")
        visible:    col.showRecalibrate
        label:      qsTr("↺  Recalibrate")
        primary:    false
        onClicked:  col.recalibrate()
    }

    // ── The parts ─────────────────────────────────────────────────────────────

    // What to do now. `large`: read from a distance (Data size, tighter leading, full-strength ink).
    component BodyText: Text {
        property bool large: false
        visible:    text !== ""
        wrapMode:   Text.WordWrap
        lineHeight: large ? 1.25 : 1.5
        font.family:    Theme.fontBody
        font.pixelSize: large ? Theme.fontSzData : Theme.fontSzBody2
        color:          large ? Theme.colorText : Theme.colorText2
    }

    // The hold bar: a 4 px track that fills while the sensors are held still; good once done.
    component HoldBar: Item {
        id: bar
        property real progress: 0
        property bool done:     false
        readonly property real fillFraction: progress
        height: Theme.sp(32)
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width:  parent.width
            height: Theme.sp(4)
            radius: Theme.sp(2)
            color:  Theme.colorBg3
            Rectangle {
                width:  parent.width * bar.fillFraction
                height: parent.height
                radius: parent.radius
                color:  bar.done ? Theme.colorGood : Theme.colorAccent
                Behavior on width { NumberAnimation { duration: 150 } }
            }
        }
    }

    // The status line, in the data face: small and quiet, or — while the golfer must act on it
    // (`big`) — heading size and bold. `tick` draws the ✓ ring before it.
    component StatusLine: Row {
        id: line
        property string text:     ""
        property bool   big:      false
        property string tone:     ""      // "good" | "error" | "warn" | "accent" | "" (quiet)
        property bool   tick:     false
        property bool   animated: true
        visible: text !== ""
        spacing: Theme.gap(6)
        Rectangle {
            visible:      line.tick
            width:        Theme.sp(16)
            height:       Theme.sp(16)
            radius:       width / 2
            color:        "transparent"
            border.color: Theme.colorGood
            border.width: Theme.sp(1.5)
            y:            (lineText.implicitHeight - height) / 2
            Text {
                anchors.centerIn: parent
                text:           "✓"
                color:          Theme.colorGood
                font.pixelSize: Theme.sp(9)
                font.bold:      true
            }
        }
        Text {
            id: lineText
            width:              line.width - (line.tick ? Theme.sp(22) : 0)
            text:               line.text
            wrapMode:           Text.WordWrap
            font.family:        Theme.fontData
            font.pixelSize:     line.big ? Theme.fontSzHeading : Theme.fontSzMicro
            font.bold:          line.big
            font.letterSpacing: Theme.trackingData
            color: line.tone === "good"   ? Theme.colorGood
                 : line.tone === "error"  ? Theme.colorError
                 : line.tone === "warn"   ? Theme.colorWarn
                 : line.tone === "accent" ? Theme.colorAccent
                 :                          Theme.colorText3
            Behavior on font.pixelSize { enabled: line.animated; NumberAnimation { duration: Theme.durationNormal } }
            Behavior on color           { enabled: line.animated; ColorAnimation  { duration: Theme.durationNormal } }
        }
    }

    // The failure, in the warning colour for a "warn" and the error colour otherwise.
    component MessageText: Text {
        property string kind:  ""
        property bool   large: false
        property real   size:  Theme.fontSzMicro
        visible:    text !== ""
        wrapMode:   Text.WordWrap
        lineHeight: large ? 1.25 : 1.5
        font.family:    Theme.fontBody
        font.pixelSize: large ? Theme.fontSzData : size
        color:          kind === "warn" ? Theme.colorWarn : Theme.colorError
    }
}
