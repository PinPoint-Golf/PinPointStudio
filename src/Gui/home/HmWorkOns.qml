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

// WORK ONS — the faults the current athlete keeps producing, by their technical names: the
// detailed layer under YOUR SWING (docs/design/work_ons_design.md), shown as FAULTS in the Swing
// diagnostics screen (ScreenSwingDiagnostics.qml). One card in the summary's own family
// (HmSwingSummary.qml): surface, hairline border, a 3 px rule in the card's tone, a mono heading in
// that tone, round badges, ten-segment meters, session pips.
//
// The card's tone is colorWarn, the coral between the summary's amber and colorError: these are
// the faults themselves, one level more technical than the home screen's golfer words, so they
// read as their own card without the alarm a red would raise. Nothing here is an alarm.
//
// A row, closed, is
//   a status badge   active (a target, in the tone) · easing (a down arrow, colorGood) ·
//                    unconfirmed (a dashed ring, grey: placed on evidence no recent session
//                    could renew) — a shape each, so colour is never the only channel
//   the name         the pack's label, the headline
//   a chip           "↓ easing", or "last seen 4 Jul" for an unconfirmed one
//   a meter          swingsFired of swingsTotal in ten segments, the count in words beside it
//   session pips     one per session, oldest first, in PpTickRun's vocabulary: a dot where it
//                    was a pattern, a green dash where the session was clean, an outlined ring
//                    where the session could not tell — present, never a gap
// Clicking it opens the row in place: what the fault costs, the readings behind the row, the
// drill where the pack authors one, and a way into the session that last showed it. One row open
// at a time. Wide, a row is one line with its meters and pips in columns; narrow, two lines.
//
// Every string about a fault is WorkOnsController's. This file positions and paints.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Column {
    id: root

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null
    // Rows drawn before "show all".
    property int collapsedCount: 5
    // The card's heading: "FAULTS" in the Swing diagnostics screen.
    property string title: qsTr("WORK ONS")

    signal reviewSessionRequested(string sessionDir)

    readonly property var  items:     controller ? controller.items : []
    readonly property bool updating:  controller ? controller.updating : false
    readonly property int  cleared:   controller ? controller.clearedCount : 0
    property string openId: ""
    property bool   showAll: false

    // The catch-up, said once and quietly: sessions recorded before this existed are being read,
    // and the list below may still grow.
    readonly property bool catchingUp: root.updating && root.controller !== null
                                       && root.controller.sessionCount < root.controller.sessionsFound

    // ── Palette ──────────────────────────────────────────────────────────────
    readonly property color tone:       Theme.colorWarn
    readonly property color toneEasing: Theme.colorGood
    readonly property color toneQuiet:  Theme.colorText3

    // ── Geometry, the summary's ──────────────────────────────────────────────
    readonly property int cardPad:    Theme.sp(20)
    readonly property int badgeSize:  Theme.sp(20)
    readonly property int textIndent: badgeSize + Theme.sp(12)
    readonly property int rowPad:     Theme.sp(14)
    readonly property int innerWidth: width - 2 * cardPad
    // One line per row down to this card width; below it the meter and pips go under the name.
    readonly property bool oneLine:   innerWidth >= Theme.sp(620)

    // At most this many pips in a row: the latest sessions, as in the summary.
    readonly property int maxPips: 8
    function latestTicks(ticks) {
        const t = ticks || []
        return t.length > root.maxPips ? t.slice(t.length - root.maxPips) : t
    }
    readonly property int pipCount: {
        let n = 1
        for (const it of root.items) n = Math.max(n, Math.min(root.maxPips, (it.ticks || []).length))
        return n
    }
    readonly property int pipSize: Theme.sp(6)
    readonly property int pipGap:  Theme.sp(4)
    readonly property int pipsWidth: root.pipCount * root.pipSize + (root.pipCount - 1) * root.pipGap
    readonly property int chevronW:  Theme.sp(22)
    readonly property int meterW:    10 * Theme.sp(9) + 9 * Theme.sp(3)
    // The count column: as wide as the widest count, so every row's pips share a right edge.
    TextMetrics {
        id: countMetrics
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        text: {
            let w = ""
            for (const it of root.items) if (String(it.countText).length > w.length) w = it.countText
            return w
        }
    }
    readonly property int countW: Math.ceil(countMetrics.advanceWidth)
    FontMetrics { id: body2Metrics; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2 }
    // Meter, count and pips, right of the name on one line.
    readonly property int rightBlockW: root.meterW + Theme.sp(10) + root.countW + Theme.sp(28)
                                       + root.pipsWidth + root.chevronW

    width:   parent ? parent.width : 0
    spacing: 0

    function share(it) { return it.swingsTotal > 0 ? it.swingsFired / it.swingsTotal : 0 }
    function tickCount(ticks, state) {
        let n = 0
        for (const t of ticks || []) if (t.state === state) ++n
        return n
    }

    // ════════════════════════════════════════════════════════════════════════
    // Pieces — the summary's, redrawn here (inline components are file-local)
    // ════════════════════════════════════════════════════════════════════════

    component Micro: Text {
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }

    // Active: a target, as "What needs work" draws its items.
    component AimBadge: Rectangle {
        id: badge
        property color tone: root.tone
        width: root.badgeSize; height: width; radius: width / 2
        color: Qt.alpha(badge.tone, Theme.dark ? 0.16 : 0.12)
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(badge.width * 0.56); height: width; radius: width / 2
            color: "transparent"; border.width: Math.max(1.5, Theme.sp(1.4)); border.color: badge.tone
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(badge.width * 0.2); height: width; radius: width / 2
            color: badge.tone
        }
    }
    // Easing: a down arrow, the trend chip's own glyph.
    component EasingBadge: Rectangle {
        id: badge
        readonly property int size: root.badgeSize
        width: size; height: width; radius: width / 2
        color: Qt.alpha(root.toneEasing, Theme.dark ? 0.16 : 0.12)
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.toneEasing; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.5; startY: badge.size * 0.27
                PathLine { x: badge.size * 0.5; y: badge.size * 0.72 }
            }
            ShapePath {
                strokeColor: root.toneEasing; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.31; startY: badge.size * 0.53
                PathLine { x: badge.size * 0.5;  y: badge.size * 0.72 }
                PathLine { x: badge.size * 0.69; y: badge.size * 0.53 }
            }
        }
    }
    // Unconfirmed: a dashed ring, untinted — something was here, and nothing recent says so.
    component UnconfirmedBadge: Item {
        id: badge
        readonly property int size: root.badgeSize
        width: size; height: size
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.toneQuiet; strokeWidth: Math.max(1.2, Theme.sp(1.2)); fillColor: "transparent"
                strokeStyle: ShapePath.DashLine; dashPattern: [2, 2]
                PathAngleArc {
                    centerX: badge.size / 2; centerY: badge.size / 2
                    radiusX: badge.size / 2 - 1; radiusY: radiusX
                    startAngle: 0; sweepAngle: 360
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(badge.size * 0.2); height: width; radius: width / 2
            color: root.toneQuiet
        }
    }

    // "↓ easing" / "last seen 4 Jul": a pill, tiny mono.
    component Chip: Rectangle {
        id: chip
        property string text: ""
        property color  tone: root.toneEasing
        property bool   tinted: true
        visible: text !== ""
        width:  visible ? chipText.implicitWidth + Theme.sp(12) : 0
        height: Theme.sp(18)
        radius: height / 2
        color:  chip.tinted ? Qt.alpha(chip.tone, Theme.dark ? 0.12 : 0.09) : "transparent"
        border.width: 1
        border.color: chip.tinted ? Qt.alpha(chip.tone, 0.45) : Theme.colorBorderStrong
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              chip.tone
        }
    }

    // How many of the golfer's swings, as ten rounded segments.
    component Meter: Row {
        id: meter
        property real  share: 0
        property color tone: root.tone
        readonly property int filled: Math.max(0, Math.min(10, Math.round(share * 10)))
        spacing: Theme.sp(3)
        Repeater {
            model: 10
            Rectangle {
                required property int index
                width: Theme.sp(9); height: Theme.sp(6); radius: height / 2
                color: index < meter.filled ? meter.tone : Theme.colorBorderMid
            }
        }
    }

    // One pip, in PpTickRun's vocabulary: fired is a dot in the tone, clean a green dash, and a
    // session that could not tell an outlined ring.
    component Pip: Item {
        id: pip
        property string kind: "fired"
        width: root.pipSize; height: root.pipSize
        Rectangle {
            readonly property bool clean: pip.kind === "clean"
            anchors.centerIn: parent
            width:  parent.width
            height: clean ? Math.max(2, Theme.sp(2)) : parent.height
            radius: height / 2
            color:  pip.kind === "fired" ? root.tone
                  : clean                 ? root.toneEasing
                  :                         "transparent"
            border.width: pip.kind === "notAssessable" ? 1 : 0
            border.color: Theme.colorText3
        }
    }
    component Pips: Row {
        id: pips
        property var ticks: []
        spacing: root.pipGap
        Repeater {
            model: pips.ticks
            Pip {
                required property var modelData
                kind: modelData.state
            }
        }
    }

    // A label and its value, in the opened row.
    component Fact: Item {
        id: fact
        property string label: ""
        property string text:  ""
        default property alias extra: valueRow.data
        readonly property int labelW: Theme.sp(84)
        visible: fact.text !== "" || valueRow.children.length > 0
        height:  visible ? Math.max(factLabel.implicitHeight, factText.visible ? factText.implicitHeight : 0,
                                    valueRow.implicitHeight) : 0
        // On the value's first baseline; beside a row of marks, on its centre.
        Micro {
            id: factLabel
            anchors.baseline: fact.text !== "" ? factText.baseline : undefined
            y: Math.round((valueRow.implicitHeight - implicitHeight) / 2)
            text: fact.label
        }
        Row {
            id: valueRow
            x: fact.labelW
            spacing: Theme.sp(8)
        }
        Text {
            id: factText
            visible: fact.text !== ""
            x: fact.labelW + (valueRow.children.length > 0 ? valueRow.implicitWidth + Theme.sp(12) : 0)
            width: fact.width - x
            text:  fact.text
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText2
            wrapMode:       Text.WordWrap
            lineHeight:     1.35
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // The card
    // ════════════════════════════════════════════════════════════════════════

    Item {
        id: card
        width:  root.width
        height: body.y + body.implicitHeight + root.cardPad

        Rectangle {
            anchors.fill: parent
            radius:       Theme.radiusLg
            color:        Theme.colorSurface
            border.width: 1
            border.color: Theme.colorBorderMid
        }
        // The top rule: a rounded tone shape, its lower part covered by the surface again.
        Rectangle {
            width: parent.width; height: Theme.radiusLg * 2
            radius: Theme.radiusLg
            color:  root.tone
        }
        Rectangle {
            x: 1; y: 3
            width: parent.width - 2; height: Theme.radiusLg * 2
            color: Theme.colorSurface
        }

        Micro {
            id: cardTitle
            x: root.cardPad; y: root.cardPad + Theme.sp(2)
            text:  root.title
            color: root.tone
        }
        Micro {
            objectName: "workOnsUpdating"
            anchors.baseline: cardTitle.baseline
            x: card.width - root.cardPad - implicitWidth
            visible: root.catchingUp
            text: root.controller
                  ? qsTr("reading %1 of %2 sessions").arg(root.controller.sessionCount)
                                                      .arg(root.controller.sessionsFound)
                  : ""
            font.letterSpacing: Theme.trackingData
        }
        // Heads the pips' column, where the catch-up is not speaking.
        Micro {
            visible: !root.catchingUp && root.items.length > 0
            anchors.baseline: cardTitle.baseline
            x: card.width - root.cardPad - root.chevronW - implicitWidth
            text: qsTr("SESSIONS")
            font.letterSpacing: Theme.trackingData
        }

        Column {
            id: body
            x: root.cardPad
            y: cardTitle.y + cardTitle.implicitHeight + Theme.sp(6)
            width: root.innerWidth

            Item {
                visible: root.items.length === 0
                width:   parent.width
                height:  visible ? Theme.sp(44) : 0
                Text {
                    objectName: "workOnsEmpty"
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: parent.width
                    text: root.updating ? qsTr("Reading your sessions…")
                                        : qsTr("Nothing yet. A fault is listed once it recurs across a session's shots.")
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText3
                    wrapMode:       Text.WordWrap
                }
            }

            Repeater {
                model: root.showAll ? root.items : root.items.slice(0, root.collapsedCount)

                Column {
                    id: rowCol
                    required property var modelData
                    required property int index
                    readonly property bool open: root.openId === modelData.id
                    readonly property bool easing: modelData.status === "easing"
                    readonly property bool unconfirmed: modelData.unconfirmed === true
                    objectName: "workOnRow"
                    width: parent.width

                    Rectangle {     // hairline between rows
                        visible: rowCol.index > 0
                        width: parent.width; height: 1
                        color: Theme.colorBorder
                    }

                    Item {
                        id: head
                        width:  parent.width
                        // One line: the name's line, padded. Two: the name, then the meter's line.
                        readonly property real lineH: Theme.fontSzBody * 1.3
                        readonly property real meterLineY: root.oneLine ? 0 : nameText.y + nameText.height + Theme.sp(6)
                        height: root.oneLine ? Math.max(nameText.height, Theme.sp(18)) + 2 * root.rowPad
                                             : meterLineY + Theme.sp(18) + root.rowPad
                        // Where the one-line row's meter column starts.
                        readonly property real rightX: width - root.rightBlockW
                        readonly property real nameMaxW: (root.oneLine ? rightX - Theme.sp(20) : width - root.chevronW)
                                                         - root.textIndent - (chip.visible ? chip.width + Theme.sp(10) : 0)

                        Loader {
                            y: Math.round(nameText.y + head.lineH / 2 - root.badgeSize / 2)
                            sourceComponent: rowCol.unconfirmed ? unconfirmedBadge
                                           : rowCol.easing      ? easingBadge : aimBadge
                        }
                        Text {
                            id: nameText
                            x: root.textIndent
                            y: root.rowPad
                            width: Math.min(implicitWidth, head.nameMaxW)
                            text:  rowCol.modelData.name
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            font.weight:    Theme.fontBodyWeight
                            color:          rowPress.containsMouse ? Theme.colorAccent : Theme.colorText
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.3
                        }
                        Chip {
                            id: chip
                            x: nameText.x + nameText.width + Theme.sp(10)
                            y: Math.round(nameText.y + head.lineH / 2 - height / 2)
                            text: rowCol.easing ? qsTr("↓ easing")
                                : rowCol.unconfirmed ? rowCol.modelData.lastSeenText : ""
                            tone:   rowCol.easing ? root.toneEasing : root.toneQuiet
                            tinted: rowCol.easing
                        }

                        // Meter and count: a column of their own on one line, under the name on two.
                        Item {
                            id: meterLine
                            x: root.oneLine ? head.rightX : root.textIndent
                            y: root.oneLine ? Math.round(nameText.y + head.lineH / 2 - height / 2) : head.meterLineY
                            width: (root.oneLine ? root.rightBlockW : head.width - root.textIndent)
                            height: Theme.sp(18)
                            Meter {
                                id: meter
                                anchors.verticalCenter: parent.verticalCenter
                                share: root.share(rowCol.modelData)
                            }
                            Text {
                                anchors { left: meter.right; leftMargin: Theme.sp(10)
                                          verticalCenter: parent.verticalCenter }
                                text: rowCol.modelData.countText
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                font.weight:    Theme.fontBodyWeight
                                color:          Theme.colorText3
                            }
                            Pips {
                                anchors { right: parent.right; rightMargin: root.chevronW
                                          verticalCenter: parent.verticalCenter }
                                ticks: root.latestTicks(rowCol.modelData.ticks)
                            }
                        }

                        // Open / closed.
                        Shape {
                            id: chevron
                            readonly property int s: Theme.sp(10)
                            width: s; height: s
                            x: head.width - s
                            y: Math.round(nameText.y + head.lineH / 2 - s / 2)
                            rotation: rowCol.open ? 90 : 0
                            preferredRendererType: Shape.CurveRenderer
                            ShapePath {
                                strokeColor: rowCol.open ? Theme.colorText2 : Theme.colorText3
                                strokeWidth: Math.max(1.2, Theme.sp(1.3)); fillColor: "transparent"
                                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                                startX: chevron.s * 0.32; startY: chevron.s * 0.12
                                PathLine { x: chevron.s * 0.70; y: chevron.s * 0.5 }
                                PathLine { x: chevron.s * 0.32; y: chevron.s * 0.88 }
                            }
                        }

                        MouseArea {
                            id: rowPress
                            objectName: "workOnPress"
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    root.openId = rowCol.open ? "" : rowCol.modelData.id
                        }
                    }

                    // ── The row, opened ─────────────────────────────────────────
                    Column {
                        objectName: "workOnDetail"
                        visible: rowCol.open
                        x:       root.textIndent
                        width:   parent.width - root.textIndent - root.chevronW
                        spacing: Theme.sp(10)

                        Text {
                            width:          Math.min(parent.width, Theme.sp(720))
                            visible:        text !== ""
                            text:           rowCol.modelData.consequence
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Theme.fontBodyWeight
                            color:          Theme.colorText2
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.4
                        }

                        Item { width: 1; height: Theme.sp(2) }

                        Fact { width: parent.width; label: qsTr("NOW");    text: rowCol.modelData.statusText }
                        Fact { width: parent.width; label: qsTr("LATEST"); text: rowCol.modelData.latestText }
                        Fact {
                            width: parent.width
                            label: qsTr("SWINGS")
                            text:  rowCol.modelData.coverageText !== ""
                                   ? qsTr("%1 · %2").arg(rowCol.modelData.countText).arg(rowCol.modelData.coverageText)
                                   : rowCol.modelData.countText
                        }
                        // The pips' key, counted over every session.
                        Fact {
                            width: parent.width
                            label: qsTr("SESSIONS")
                            Repeater {
                                model: [
                                    { state: "fired",         words: qsTr("%1 showed it") },
                                    { state: "clean",         words: qsTr("%1 clean") },
                                    { state: "notAssessable", words: qsTr("%1 not measured") }
                                ]
                                Row {
                                    required property var modelData
                                    readonly property int n: root.tickCount(rowCol.modelData.ticks, modelData.state)
                                    visible: n > 0
                                    spacing: Theme.sp(6)
                                    height: Math.ceil(body2Metrics.height * 1.35)
                                    Pip { anchors.verticalCenter: parent.verticalCenter; kind: parent.modelData.state }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: parent.modelData.words.arg(parent.n)
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          Theme.colorText2
                                    }
                                }
                            }
                        }
                        Fact { width: parent.width; label: qsTr("LINKED"); text: rowCol.modelData.causedByText }

                        // The drill, set in: what to do about it.
                        Rectangle {
                            visible: rowCol.modelData.drillLabel !== ""
                            width:   Math.min(parent.width, Theme.sp(720))
                            height:  visible ? drillCol.implicitHeight + 2 * Theme.sp(12) : 0
                            radius:  Theme.radius
                            color:   Qt.alpha(Theme.colorText, Theme.dark ? 0.035 : 0.04)
                            border.width: 1
                            border.color: Theme.colorBorder
                            Column {
                                id: drillCol
                                x: Theme.sp(14); y: Theme.sp(12)
                                width: parent.width - 2 * Theme.sp(14)
                                spacing: Theme.sp(4)
                                Row {
                                    spacing: Theme.sp(10)
                                    Micro {
                                        anchors.baseline: drillName.baseline
                                        text: qsTr("TRY")
                                    }
                                    Text {
                                        id: drillName
                                        text: rowCol.modelData.drillLabel
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody
                                        font.weight:    Theme.fontBodyWeight
                                        color:          Theme.colorAccent
                                    }
                                }
                                Text {
                                    width: parent.width
                                    text:  rowCol.modelData.drillInstruction
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText2
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.4
                                }
                            }
                        }

                        Text {
                            objectName:     "workOnReview"
                            visible:        rowCol.modelData.sessionDir !== ""
                            text:           qsTr("Review the %1 session →").arg(rowCol.modelData.sessionLabel)
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color:          Theme.colorAccent

                            MouseArea {
                                anchors.fill: parent
                                cursorShape:  Qt.PointingHandCursor
                                onClicked:    root.reviewSessionRequested(rowCol.modelData.sessionDir)
                            }
                        }

                        Item { width: 1; height: Theme.sp(10) }
                    }
                }
            }

            // What the collapsed list is not showing, and what has left it.
            Item {
                readonly property int hidden: Math.max(0, root.items.length - root.collapsedCount)
                visible: hidden > 0 || root.cleared > 0
                width:   parent.width
                height:  visible ? Theme.sp(44) : 0

                Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

                Text {
                    objectName: "workOnsMore"
                    anchors { left: parent.left; leftMargin: root.textIndent
                              verticalCenter: parent.verticalCenter; verticalCenterOffset: Theme.sp(4) }
                    visible: parent.hidden > 0
                    text:    root.showAll ? qsTr("Show fewer") : qsTr("%1 more").arg(parent.hidden)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorAccent

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.showAll = !root.showAll
                    }
                }

                Micro {
                    anchors { right: parent.right; rightMargin: root.chevronW
                              verticalCenter: parent.verticalCenter; verticalCenterOffset: Theme.sp(4) }
                    visible: root.cleared > 0
                    text:    qsTr("%1 cleared").arg(root.cleared)
                    font.letterSpacing: Theme.trackingData
                    color:   root.toneEasing
                }
            }
        }
    }

    Component { id: aimBadge;         AimBadge {} }
    Component { id: easingBadge;      EasingBadge {} }
    Component { id: unconfirmedBadge; UnconfirmedBadge {} }
}
