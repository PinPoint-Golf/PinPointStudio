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
    property string title: Theme.caps(qsTr("Work ons"))

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
    readonly property int cardPad:    Theme.gap(20)
    readonly property int badgeSize:  Theme.sp(20)
    readonly property int textIndent: badgeSize + Theme.sp(12)
    readonly property int rowPad:     Theme.gap(14)
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
    // The card
    // ════════════════════════════════════════════════════════════════════════

    Item {
        id: card
        width:  root.width
        height: body.y + body.implicitHeight + root.cardPad

        // The summary's card: surface, hairline border, and the 3 px rule in the tone.
        PpCardShell {
            anchors.fill: parent
            tone: root.tone
        }

        PpMicro {
            id: cardTitle
            x: root.cardPad; y: root.cardPad + Theme.sp(2)
            text:  root.title
            color: root.tone
        }
        PpMicro {
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
        PpMicro {
            visible: !root.catchingUp && root.items.length > 0
            anchors.baseline: cardTitle.baseline
            x: card.width - root.cardPad - root.chevronW - implicitWidth
            text: Theme.caps(qsTr("Sessions"))
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

                        PpBadge {
                            y: Math.round(nameText.y + head.lineH / 2 - root.badgeSize / 2)
                            size: root.badgeSize
                            kind: rowCol.unconfirmed ? "unconfirmed" : rowCol.easing ? "easing" : "target"
                            tone: rowCol.unconfirmed ? root.toneQuiet : rowCol.easing ? root.toneEasing : root.tone
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
                        PpChip {
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
                            PpMeter {
                                id: meter
                                anchors.verticalCenter: parent.verticalCenter
                                share: root.share(rowCol.modelData)
                                tone:  root.tone
                            }
                            Text {
                                anchors { left: meter.right; leftMargin: Theme.gap(10)
                                          verticalCenter: parent.verticalCenter }
                                text: rowCol.modelData.countText
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody2
                                font.weight:    Theme.fontBodyWeight
                                color:          Theme.colorText3
                            }
                            PpPips {
                                anchors { right: parent.right; rightMargin: root.chevronW
                                          verticalCenter: parent.verticalCenter }
                                ticks:     root.latestTicks(rowCol.modelData.ticks)
                                tone:      root.tone
                                cleanTone: root.toneEasing
                                size:      root.pipSize
                                gap:       root.pipGap
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
                        spacing: Theme.gap(10)

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

                        PpFact { width: parent.width; label: Theme.caps(qsTr("Now"));    text: rowCol.modelData.statusText }
                        PpFact { width: parent.width; label: Theme.caps(qsTr("Latest")); text: rowCol.modelData.latestText }
                        PpFact {
                            width: parent.width
                            label: Theme.caps(qsTr("Swings"))
                            text:  rowCol.modelData.coverageText !== ""
                                   ? qsTr("%1 · %2").arg(rowCol.modelData.countText).arg(rowCol.modelData.coverageText)
                                   : rowCol.modelData.countText
                        }
                        // The pips' key, counted over every session.
                        PpFact {
                            width: parent.width
                            label: Theme.caps(qsTr("Sessions"))
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
                                    spacing: Theme.gap(6)
                                    height: Math.ceil(body2Metrics.height * 1.35)
                                    PpPip {
                                        anchors.verticalCenter: parent.verticalCenter
                                        kind:      parent.modelData.state
                                        tone:      root.tone
                                        cleanTone: root.toneEasing
                                        size:      root.pipSize
                                    }
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
                        PpFact { width: parent.width; label: Theme.caps(qsTr("Linked")); text: rowCol.modelData.causedByText }

                        // The drill, set in: what to do about it.
                        PpInset {
                            visible: rowCol.modelData.drillLabel !== ""
                            width:   Math.min(parent.width, Theme.sp(720))
                            Row {
                                spacing: Theme.gap(10)
                                PpMicro {
                                    anchors.baseline: drillName.baseline
                                    text: Theme.caps(qsTr("Try"))
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

                        PpLink {
                            objectName: "workOnReview"
                            visible:    rowCol.modelData.sessionDir !== ""
                            text:       qsTr("Review the %1 session →").arg(rowCol.modelData.sessionLabel)
                            hoverTint:  false
                            onClicked:  root.reviewSessionRequested(rowCol.modelData.sessionDir)
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

                PpLink {
                    objectName: "workOnsMore"
                    anchors { left: parent.left; leftMargin: root.textIndent
                              verticalCenter: parent.verticalCenter; verticalCenterOffset: Theme.sp(4) }
                    visible:   parent.hidden > 0
                    text:      root.showAll ? qsTr("Show fewer") : qsTr("%1 more").arg(parent.hidden)
                    hoverTint: false
                    onClicked: root.showAll = !root.showAll
                }

                PpMicro {
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
}
