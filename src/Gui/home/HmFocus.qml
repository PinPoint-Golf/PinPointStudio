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

// YOUR FOCUS — the one thing to practise, first on the home screen and drawn as its hero: the
// summary's card with a heavier amber rule, a faint amber wash on the surface and an amber
// hairline, and the drill's name as a display-size headline.
//
//   AIM FOR          the ideal, ticked green            | RIGHT NOW   the faults it covers, each with
//                                                       |             the summary's meter, frequency
//                                                       |             words and session pips
//   WHY IT MATTERS   a sentence or two
//   HOW TO PRACTISE  the drill, set in
//   why this one     one quiet line: the honest reason it was chosen
//
// The two columns sit on the same split as the two cards below the focus, so the page keeps one
// grid; each AIM FOR line faces the RIGHT NOW line it answers. Narrow, they stack.
//
// Every string is WorkOnsController's (swingSummaryView's focus, docs/design/home_themes_design.md):
//   focusItem { present, title, aimFor [string], rightNow [{ text, share, frequency, trend,
//               sessions: [bool…] }], why, practiseLabel, practise, reason }
// The title is the drill's label (for a fault with no drill, its ideal); practiseLabel repeats it
// and is not drawn twice. Not drawn when present is false — nothing on the list yet. Positions and
// paints only.

import QtQuick
import PinPointStudio

Item {
    id: root
    objectName: "focusCard"

    property var controller: (typeof workOns !== "undefined") ? workOns : null
    // The gap between the two cards under the focus, so the columns land on their split.
    property int gridGap: Theme.sp(16)

    // null when there is no focus: before the first summary, and when nothing needs work.
    readonly property var focusItem: controller && controller.focusItem && controller.focusItem.present === true
                                     ? controller.focusItem : null
    readonly property var aimFor:   focusItem ? (focusItem.aimFor   || []) : []
    readonly property var rightNow: focusItem ? (focusItem.rightNow || []) : []

    readonly property color tone:     Theme.colorAccent
    readonly property color toneGood: Theme.colorGood

    readonly property int padX:       Theme.sp(20)
    readonly property int padY:       Theme.sp(24)
    readonly property int badgeSize:  Theme.sp(20)
    readonly property int textIndent: badgeSize + Theme.sp(12)
    readonly property int innerWidth: width - 2 * padX
    readonly property bool stacked:   width < Theme.sp(640)
    // Side by side: each column is the card below it, less its padding.
    readonly property real colW:      stacked ? innerWidth : (width - gridGap) / 2 - 2 * padX
    // Body-relative x of the RIGHT NOW column: the right-hand card's content edge.
    readonly property real col2X:     stacked ? 0 : (width + gridGap) / 2
    readonly property real lineH:     Theme.fontSzBody * 1.3
    readonly property int  itemGap:   Theme.sp(16)

    readonly property int maxPips: 8
    function latestPips(marks) {
        const m = marks || []
        return m.length > root.maxPips ? m.slice(m.length - root.maxPips) : m
    }
    readonly property int pipCount: {
        let n = 1
        for (const it of root.rightNow) n = Math.max(n, Math.min(root.maxPips, (it.sessions || []).length))
        return n
    }
    readonly property int pipsWidth: pipCount * Theme.sp(6) + (pipCount - 1) * Theme.sp(4)

    function capitalised(s) { s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

    visible: focusItem !== null
    implicitHeight: visible ? body.y + body.implicitHeight + padY : 0

    // ════════════════════════════════════════════════════════════════════════
    // Pieces — the summary's are shared (components/Pp*); the prose is this card's own
    // ════════════════════════════════════════════════════════════════════════

    component Prose: Text {
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText2
        wrapMode:       Text.WordWrap
        lineHeight:     1.45
    }

    // ════════════════════════════════════════════════════════════════════════
    // The card
    // ════════════════════════════════════════════════════════════════════════

    // The hero shell: the surface, an amber wash and hairline, and the heavier 4 px top rule.
    PpCardShell {
        anchors.fill: parent
        hero: true
        tone: root.tone
    }

    PpMicro {
        id: label
        x: root.padX; y: root.padY
        text:  qsTr("YOUR FOCUS")
        color: root.tone
    }
    Text {
        id: headline
        objectName: "focusTitle"
        x: root.padX
        y: label.y + label.implicitHeight + Theme.sp(10)
        width: root.innerWidth
        text:  root.focusItem ? root.focusItem.title : ""
        font.family:    Theme.fontDisplay
        font.pixelSize: Theme.fontSzDisplay
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText
        wrapMode:       Text.WordWrap
        lineHeight:     1.15
    }

    Column {
        id: body
        x: root.padX
        y: headline.y + headline.height + Theme.sp(24)
        width: root.innerWidth
        spacing: Theme.sp(24)

        // ── AIM FOR | RIGHT NOW ──────────────────────────────────────────────
        Item {
            id: pair
            width:  parent.width
            readonly property real aimH: aimCol.y + aimCol.implicitHeight
            readonly property real nowH: nowCol.y + nowCol.implicitHeight
            height: root.stacked ? nowBlock.y + pair.nowH : Math.max(aimH, nowH)

            // AIM FOR
            Item {
                id: aimBlock
                width: root.colW
                PpMicro { id: aimLabel; text: qsTr("AIM FOR"); color: root.toneGood }
                Column {
                    id: aimCol
                    y: aimLabel.implicitHeight + Theme.sp(14)
                    width: root.colW
                    spacing: root.itemGap
                    Repeater {
                        model: root.aimFor
                        Item {
                            objectName: "focusAim"
                            required property var modelData
                            // The words as drawn — what a probe reads.
                            readonly property string plainText: root.capitalised(modelData)
                            width:  aimCol.width
                            required property int index
                            // Side by side, each line keeps the pitch of the fault it faces.
                            readonly property Item facing: root.stacked ? null : nowRepeater.itemAt(index)
                            height: facing ? Math.max(aimText.height, facing.height) : aimText.height
                            PpBadge { kind: "check"; tone: root.toneGood; y: Math.round(root.lineH / 2 - height / 2) }
                            Text {
                                id: aimText
                                x: root.textIndent
                                width: parent.width - root.textIndent
                                text:  root.capitalised(parent.modelData)
                                font.family:    Theme.fontBody
                                font.pixelSize: Theme.fontSzBody
                                font.weight:    Theme.fontBodyWeight
                                color:          Theme.colorText
                                wrapMode:       Text.WordWrap
                                lineHeight:     1.3
                            }
                        }
                    }
                }
            }

            // The split, a hairline down the middle where the two cards below part.
            Rectangle {
                visible: !root.stacked
                x: Math.round(root.width / 2 - root.padX)
                y: 0
                width: 1; height: pair.height
                color: Theme.colorBorder
            }

            // RIGHT NOW
            Item {
                id: nowBlock
                x: root.col2X
                y: root.stacked ? pair.aimH + Theme.sp(22) : 0
                width: root.colW
                PpMicro { id: nowLabel; text: qsTr("RIGHT NOW"); color: root.tone }
                PpMicro {
                    anchors.baseline: nowLabel.baseline
                    x: root.colW - implicitWidth
                    text: qsTr("SESSIONS")
                    font.letterSpacing: Theme.trackingData
                }
                Column {
                    id: nowCol
                    y: nowLabel.implicitHeight + Theme.sp(14)
                    width: root.colW
                    spacing: root.itemGap
                    Repeater {
                        id: nowRepeater
                        model: root.rightNow
                        Item {
                            id: fault
                            objectName: "focusNow"
                            required property var modelData
                            // The words as drawn — what a probe reads.
                            readonly property string itemText:      root.capitalised(modelData.text)
                            readonly property string frequencyText: modelData.frequency
                            readonly property string plainText:     itemText + " — " + frequencyText
                            readonly property int    pipCount:      (modelData.sessions || []).length
                            width:  nowCol.width
                            height: faultCol.implicitHeight
                            PpBadge { kind: "target"; tone: root.tone; y: Math.round(root.lineH / 2 - height / 2) }
                            Column {
                                id: faultCol
                                x: root.textIndent
                                width: parent.width - root.textIndent
                                spacing: Theme.sp(4)
                                Text {
                                    id: nowHead
                                    // The chip's room, when it has had to move up to this line.
                                    width: parent.width - (nowChip.visible && !nowRow.chipFits
                                                           ? nowChip.width + Theme.sp(12) : 0)
                                    readonly property real lineCentre: lineCount > 0 ? implicitHeight / lineCount / 2
                                                                                     : implicitHeight / 2
                                    text:  root.capitalised(fault.modelData.text)
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText
                                    wrapMode:       Text.WordWrap
                                    lineHeight:     1.3
                                }
                                Item {
                                    id: nowRow
                                    width:  parent.width
                                    height: Theme.sp(18)
                                    // As the cards below: the trend follows the frequency words, and
                                    // where this narrower column has no room for it, it moves up to the
                                    // headline's line at the right edge rather than over the pips.
                                    readonly property bool chipFits:
                                        meter.width + freq.implicitWidth + nowChip.width
                                        + Theme.sp(10) + Theme.sp(8) + Theme.sp(8) + root.pipsWidth <= width
                                    PpMeter {
                                        id: meter
                                        anchors.verticalCenter: parent.verticalCenter
                                        share: fault.modelData.share
                                        tone:  root.tone
                                    }
                                    Text {
                                        id: freq
                                        anchors { left: meter.right; leftMargin: Theme.sp(10)
                                                  verticalCenter: parent.verticalCenter }
                                        width: Math.min(implicitWidth, parent.width - meter.width - Theme.sp(10)
                                                                       - root.pipsWidth - Theme.sp(8))
                                        elide: Text.ElideRight
                                        text: fault.modelData.frequency
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        font.weight:    Theme.fontBodyWeight
                                        color:          Theme.colorText3
                                    }
                                    PpChip {
                                        id: nowChip
                                        x: nowRow.chipFits ? freq.x + freq.width + Theme.sp(8) : nowRow.width - width
                                        y: nowRow.chipFits ? Math.round((nowRow.height - height) / 2)
                                                           : -nowRow.y + Math.round(nowHead.lineCentre - height / 2)
                                        trend: fault.modelData.trend
                                    }
                                    PpPips {
                                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                        marks: root.latestPips(fault.modelData.sessions)
                                        tone:  root.tone
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.colorBorder }

        // ── WHY IT MATTERS ───────────────────────────────────────────────────
        Column {
            width: parent.width
            spacing: Theme.sp(10)
            visible: root.focusItem !== null && root.focusItem.why !== ""
            PpMicro { text: qsTr("WHY IT MATTERS") }
            Prose {
                objectName: "focusWhy"
                width: Math.min(parent.width, Theme.sp(720))
                text:  root.focusItem ? root.focusItem.why : ""
            }
        }

        // ── HOW TO PRACTISE ──────────────────────────────────────────────────
        // Set in, with the drill's own mark: an amber bar down the inset's left edge.
        PpInset {
            visible: root.focusItem !== null && root.focusItem.practise !== ""
            width:   parent.width
            tone:    root.tone
            bar:     true
            padX:    Theme.sp(18)
            padY:    Theme.sp(16)
            spacing: Theme.sp(8)
            PpMicro { text: qsTr("HOW TO PRACTISE") }
            Text {
                objectName: "focusPractise"
                width: Math.min(parent.width, Theme.sp(760))
                text:  root.focusItem ? root.focusItem.practise : ""
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText
                wrapMode:       Text.WordWrap
                lineHeight:     1.45
            }
        }

        // ── Why this one ─────────────────────────────────────────────────────
        Text {
            objectName: "focusReason"
            visible: text !== ""
            width:   parent.width
            text:    root.focusItem ? root.focusItem.reason : ""
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            font.italic:    true
            color:          Theme.colorText3
            wrapMode:       Text.WordWrap
        }
    }
}
