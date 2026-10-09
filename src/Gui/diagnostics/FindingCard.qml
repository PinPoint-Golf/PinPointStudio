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

// One finding row — a fault or a watch (a target badge, colorWarn or colorAttention) or a strength
// (a check badge, colorGood), in the Swing diagnostics FAULTS idiom (home/HmWorkOns.qml): the
// badge, the name, the severity in words with the positions it was found at, and the confidence as
// four pips. Tapping it opens the row in place — the biomechanical "why", what links and
// corroborates it, the coaching (or what to protect) set in — and seeks the timeline to the
// finding's primary checkpoint. Rows are hairline-separated by the section that lists them. Pure
// binding; all values via Theme.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Column {
    id: root

    // finding = { id,name,severity,category,confidence,lowConfidence,magnitude,positions:[tags],
    //             ballFlight:[…],explanation,coaching,protect,corroboratedBy:[…],linkedTo,seekUs }
    property var finding: ({})
    property bool _open: false
    // The hairline above the row; a section's first row goes without.
    property bool divider: true
    signal seek(real us)

    readonly property string _sev: finding.severity || "watch"
    readonly property bool   _strength: _sev === "good"
    readonly property color  _tone: _sev === "fault" ? Theme.colorWarn
                                  : _strength        ? Theme.colorGood
                                  :                    Theme.colorAttention
    // Confidence on four pips: how many of them are filled.
    readonly property int    _conf: Math.round((finding.confidence || 0) * 4)
    readonly property var    _confMarks: [0, 1, 2, 3].map(function (i) { return i < root._conf })

    readonly property int _badgeSize: Theme.sp(20)
    readonly property int _indent:    _badgeSize + Theme.sp(12)
    readonly property int _rowPad:    Theme.sp(10)
    readonly property int _chevronW:  Theme.sp(22)
    readonly property int _factLabelW: Theme.sp(100)

    function _sevLabel(s) { return s === "fault" ? qsTr("fault") : s === "good" ? qsTr("keep") : qsTr("watch") }
    function _confWords(n) {
        return n >= 4 ? qsTr("high") : n >= 3 ? qsTr("good") : n >= 2 ? qsTr("moderate") : qsTr("low")
    }

    opacity: finding.lowConfidence ? 0.82 : 1.0

    Rectangle {
        visible: root.divider
        width: parent.width; height: 1
        color: Theme.colorBorder
    }

    Item {
        id: head
        width:  parent.width
        height: caption.y + caption.height + root._rowPad
        readonly property real lineH: Theme.fontSzBody * 1.3
        readonly property real lineMid: nameText.y + lineH / 2

        PpBadge {
            y: Math.round(head.lineMid - height / 2)
            size: root._badgeSize
            kind: root._strength ? "check" : "target"
            tone: root._tone
        }
        Text {
            id: nameText
            x: root._indent
            y: root._rowPad
            width: head.width - x - pips.width - Theme.sp(12) - root._chevronW
            text:  root.finding.name || ""
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Theme.fontBodyWeight
            color:          rowPress.containsMouse ? Theme.colorAccent : Theme.colorText
            wrapMode:       Text.WordWrap
            lineHeight:     1.3
        }
        PpPips {
            id: pips
            x: head.width - root._chevronW - width
            y: Math.round(head.lineMid - height / 2)
            marks: root._confMarks
            tone:  Theme.colorText2
        }
        Shape {
            id: chevron
            readonly property int s: Theme.sp(10)
            width: s; height: s
            x: head.width - s
            y: Math.round(head.lineMid - s / 2)
            rotation: root._open ? 90 : 0
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root._open ? Theme.colorText2 : Theme.colorText3
                strokeWidth: Math.max(1.2, Theme.sp(1.3)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: chevron.s * 0.32; startY: chevron.s * 0.12
                PathLine { x: chevron.s * 0.70; y: chevron.s * 0.5 }
                PathLine { x: chevron.s * 0.32; y: chevron.s * 0.88 }
            }
        }

        // The severity in words, where it was found, and the ball flight it goes with.
        Flow {
            id: caption
            x: root._indent
            y: nameText.y + nameText.height + Theme.sp(4)
            width: head.width - x - root._chevronW
            spacing: Theme.sp(6)
            Text {
                text: root._sevLabel(root._sev)
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: root._tone
            }
            Text {
                visible: (root.finding.positions || []).length > 0
                text: qsTr("at %1").arg((root.finding.positions || []).join(", "))
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText3
            }
            Repeater {
                model: root.finding.ballFlight || []
                delegate: PpChip {
                    required property var modelData
                    text:   modelData
                    tone:   Theme.colorText3
                    tinted: false
                }
            }
        }

        MouseArea {
            id: rowPress
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked: {
                root._open = !root._open
                if (root.finding.seekUs > 0) root.seek(root.finding.seekUs)
            }
        }
    }

    // ── The row, opened ─────────────────────────────────────────────────────────
    Column {
        visible: root._open
        x:       root._indent
        width:   parent.width - root._indent - root._chevronW
        spacing: Theme.sp(8)

        Text {
            width: Math.min(parent.width, Theme.sp(720))
            visible: text !== ""
            text: root.finding.explanation || ""
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
            font.weight: Theme.fontBodyWeight
            color: Theme.colorText2
            wrapMode: Text.WordWrap
            lineHeight: 1.4
        }
        PpFact {
            width: parent.width; labelWidth: root._factLabelW
            label: qsTr("LINKED"); text: root.finding.linkedTo || ""
        }
        PpFact {
            width: parent.width; labelWidth: root._factLabelW
            label: qsTr("CONFIDENCE")
            text:  qsTr("%1 · %2 of 4").arg(root._confWords(root._conf)).arg(root._conf)
            PpPips { marks: root._confMarks; tone: Theme.colorText2 }
        }
        PpFact {
            width: parent.width; labelWidth: root._factLabelW
            label: qsTr("CORROBORATED")
            text:  (root.finding.corroboratedBy || []).join(", ")
        }

        // The coaching for a fault, or what to protect for a strength, set in.
        PpInset {
            id: coachInset
            readonly property string words: root._strength ? (root.finding.protect || "")
                                                           : (root.finding.coaching || "")
            visible: words !== ""
            width:   Math.min(parent.width, Theme.sp(720))
            tone:    root._tone
            bar:     true
            PpMicro { text: root._strength ? qsTr("KEEP") : qsTr("TRY") }
            Text {
                width: parent.width
                text:  coachInset.words
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText2
                wrapMode: Text.WordWrap
                lineHeight: 1.4
            }
        }

        Item { width: 1; height: Theme.sp(4) }
    }
}
