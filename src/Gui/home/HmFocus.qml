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

// YOUR FOCUS — the one thing to practise, first on Home's Coaching tab, as a table card in the
// §2.5 visual language (docs/design/ux_refactor_away_from_4_modes/03_home_coaching.png):
//
//   header      focus mark · YOUR FOCUS · the drill's title · the short reason · Practise
//   captions    Aim for            | Right now      How often                     Sessions
//   rows        ✓ the ideal        | the fault      meter · frequency words + trend   pips
//   expand row  ▸ Why it matters · how to practise   (closed: the two paragraphs inside)
//
// The aims and the faults share one row grid, split by a hairline: each AIM FOR line faces the
// RIGHT NOW line it answers, and a row is as tall as the taller of the two. Where one list is
// longer, the other side of its extra rows is left empty.
//
// Every string is WorkOnsController's (swingSummaryView's focus, docs/design/home_themes_design.md):
//   focusItem { present, title, aimFor [string], rightNow [{ text, share, frequency, trend,
//               sessions: [bool…] }], why, practiseLabel, practise, reason, reasonShort,
//               focusConditionId }
// The title is the drill's label (for a fault with no drill, its ideal); practiseLabel repeats it
// and is not drawn twice. Not drawn when present is false — nothing on the list yet. Positions and
// paints only; Practise is passed up (practiseRequested) for the screen to open the wizard with.

import QtQuick
import PinPointStudio

Item {
    id: root
    objectName: "focusCard"

    property var controller: (typeof workOns !== "undefined") ? workOns : null
    // Kept for callers that still set it; the card no longer lines up with the cards below it.
    property int gridGap: Theme.sp(16)

    // Practise: open the wizard with this focus.
    signal practiseRequested(string conditionId, string label)

    // null when there is no focus: before the first summary, and when nothing needs work.
    readonly property var focusItem: controller && controller.focusItem && controller.focusItem.present === true
                                     ? controller.focusItem : null
    readonly property var aimFor:   focusItem ? (focusItem.aimFor   || []) : []
    readonly property var rightNow: focusItem ? (focusItem.rightNow || []) : []
    readonly property string conditionId: focusItem ? (focusItem.focusConditionId || "") : ""
    readonly property string reasonShort: focusItem ? (focusItem.reasonShort || focusItem.reason || "") : ""
    readonly property bool hasWhyHow: focusItem !== null
                                      && ((focusItem.why || "") !== "" || (focusItem.practise || "") !== "")

    // The focus mark's tone, the aims' and the faults'.
    readonly property color tone:      Theme.colorAccent
    readonly property color toneGood:  Theme.colorGood
    readonly property color toneFault: Theme.colorAttention

    readonly property int maxPips: 8
    readonly property int markW:   Theme.sp(20)

    function capitalised(s) { s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

    visible: focusItem !== null
    implicitHeight: visible ? focusTable.implicitHeight : 0

    // ════════════════════════════════════════════════════════════════════════
    // Pieces
    // ════════════════════════════════════════════════════════════════════════

    // A sentence-case caption, as PpCard draws its column captions.
    component Caption: Text {
        elide:          Text.ElideRight
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText3
    }
    component Line: Text {
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText
        wrapMode:       Text.WordWrap
        lineHeight:     1.2
    }
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

    PpCard {
        id: focusTable
        width: root.width
        table: true

        // The aims-and-faults grid: on the card's content box, like any table row.
        readonly property var gridLayout: focusTable.resolveColumns([
            { title: qsTr("Aim for"),   fill: 1.0 },
            { title: qsTr("Right now"), fill: 0.5 },
            { title: qsTr("How often"), fill: 0.8 },
            { title: qsTr("Sessions"),  width: Theme.sp(80), align: "right" }])
        // Where the two halves part: half a column gap before "Right now".
        readonly property real splitX: focusTable.rowPad + (focusTable.gridLayout.length > 1
                                       ? focusTable.gridLayout[1].x - focusTable.columnGap / 2 : 0)

        // ── The header: mark · YOUR FOCUS · title · reason · Practise ─────────
        Item {
            id: header
            width:  focusTable.width
            height: Theme.sp(Theme.rowH) + Theme.sp(16)

            readonly property int pad: focusTable.rowPad
            readonly property int gapS: Theme.gap(12)
            // What the title and the reason share: the row less the mark, the label and Practise.
            readonly property real room: header.width - 2 * header.pad - mark.width - label.implicitWidth
                                         - 3 * header.gapS
                                         - (practise.visible ? practise.width + header.gapS : 0)

            PpBadge {
                id: mark
                x: header.pad
                anchors.verticalCenter: parent.verticalCenter
                kind:  "target"
                plain: true
                tone:  root.tone
                size:  root.markW
            }
            PpMicro {
                id: label
                x: mark.x + mark.width + header.gapS
                anchors.baseline: headline.baseline
                text: Theme.caps(qsTr("Your focus"))
            }
            Text {
                id: headline
                objectName: "focusTitle"
                x: label.x + label.implicitWidth + header.gapS
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, Math.min(implicitWidth, header.room))
                elide: Text.ElideRight
                text:  root.focusItem ? root.focusItem.title : ""
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzHeading
                font.weight:    Font.DemiBold
                color:          Theme.colorText
            }
            // Why this one, in a line: "covers 2 things on your list · comes earliest in your swing".
            Text {
                id: reason
                objectName: "focusReason"
                x: headline.x + headline.width + header.gapS
                anchors.baseline: headline.baseline
                readonly property real room: header.room - headline.width - header.gapS
                width:   Math.max(0, Math.min(implicitWidth, room))
                visible: text !== "" && room >= Theme.sp(60)
                elide:   Text.ElideRight
                text:    root.reasonShort
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText3
            }
            PpButton {
                id: practise
                objectName: "focusPracticeButton"
                anchors { right: parent.right; rightMargin: header.pad; verticalCenter: parent.verticalCenter }
                visible: root.conditionId !== ""
                glyph:   "▶"
                label:   qsTr("Practise")
                onClicked: root.practiseRequested(root.conditionId, root.focusItem ? root.focusItem.title : "")
            }
        }

        // ── Aim for | Right now ──────────────────────────────────────────────
        Item {
            id: grid
            width:  focusTable.width
            height: gridCol.implicitHeight

            Column {
                id: gridCol
                width: parent.width

                // The captions. The card draws the hairline above them; the first row draws the one
                // under them.
                PpTableRow {
                    id: capRow
                    layout: focusTable.gridLayout
                    height: Theme.sp(Theme.tableHeaderH)
                    hoverEnabled: false
                    Caption { text: qsTr("Aim for");   width: Math.min(implicitWidth, capRow.columnWidth(0)) }
                    Caption { text: qsTr("Right now"); width: Math.min(implicitWidth, capRow.columnWidth(1)) }
                    Caption { text: qsTr("How often"); width: Math.min(implicitWidth, capRow.columnWidth(2)) }
                    Caption { text: qsTr("Sessions");  width: Math.min(implicitWidth, capRow.columnWidth(3)) }
                }

                Repeater {
                    model: Math.max(root.aimFor.length, root.rightNow.length)
                    PpTableRow {
                        id: pairRow
                        required property int index
                        readonly property bool hasAim:   index < root.aimFor.length
                        readonly property bool hasFault: index < root.rightNow.length
                        readonly property var  aim:      hasAim   ? root.aimFor[index]   : ""
                        readonly property var  fault:    hasFault ? root.rightNow[index] : null
                        layout: focusTable.gridLayout
                        hoverEnabled: false
                        topRule: true
                        // Wrapped words make the row grow; never below the table row.
                        height: Math.max(Theme.sp(Theme.rowH),
                                         Math.max(aimCell.height, faultText.height) + Theme.sp(18))

                        // Aim for: a tick and the ideal.
                        Item {
                            id: aimCell
                            objectName: pairRow.hasAim ? "focusAim" : ""
                            // The words as drawn — what a probe reads.
                            readonly property string plainText: root.capitalised(pairRow.aim)
                            visible: pairRow.hasAim
                            width:  pairRow.columnWidth(0)
                            height: aimText.height
                            PpBadge {
                                kind:  "check"
                                plain: true
                                tone:  root.toneGood
                                size:  root.markW
                                y: Math.round((aimText.lineCount > 1 ? aimText.height / aimText.lineCount
                                                                     : aimText.height) / 2 - height / 2)
                            }
                            Line {
                                id: aimText
                                x: root.markW + focusTable.columnGap
                                width: Math.max(0, parent.width - x)
                                text:  aimCell.plainText
                            }
                        }

                        // Right now: the fault…
                        Line {
                            id: faultText
                            objectName: pairRow.hasFault ? "focusNow" : ""
                            // The words as drawn — what a probe reads.
                            readonly property string itemText:      pairRow.fault ? root.capitalised(pairRow.fault.text) : ""
                            readonly property string frequencyText: pairRow.fault ? pairRow.fault.frequency : ""
                            readonly property string plainText:     itemText + " — " + frequencyText
                            readonly property int    pipCount:      pairRow.fault ? (pairRow.fault.sessions || []).length : 0
                            visible: pairRow.hasFault
                            width: pairRow.columnWidth(1)
                            text:  itemText
                        }
                        // …how often, in segments and in words, and which way it is going…
                        Row {
                            visible: pairRow.hasFault
                            width: pairRow.columnWidth(2)
                            spacing: Theme.gap(12)
                            PpMeter {
                                id: meter
                                anchors.verticalCenter: parent.verticalCenter
                                share: pairRow.fault ? pairRow.fault.share : 0
                                tone:  root.toneFault
                            }
                            Caption {
                                id: freq
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, Math.max(0, parent.width - meter.width - parent.spacing
                                                                        - (trendChip.visible ? trendChip.width + parent.spacing : 0)))
                                text:  pairRow.fault ? pairRow.fault.frequency : ""
                                color: Theme.colorText3
                            }
                            PpChip {
                                id: trendChip
                                anchors.verticalCenter: parent.verticalCenter
                                trend: pairRow.fault ? pairRow.fault.trend : 0
                            }
                        }
                        // …and the sessions that saw it.
                        PpPips {
                            visible: pairRow.hasFault
                            marks:   pairRow.fault ? pairRow.fault.sessions : []
                            maxPips: root.maxPips
                            tone:    Theme.colorText2
                        }
                    }
                }
            }

            // The split, a hairline down the grid where the aims and the faults part.
            Rectangle {
                x: Math.round(focusTable.splitX)
                width: 1; height: grid.height
                color: Theme.colorHairline
            }
        }

        // ── Why it matters · how to practise ─────────────────────────────────
        PpExpandRow {
            id: whyRow
            objectName: "focusWhyRow"
            visible: root.hasWhyHow
            accessibleName: qsTr("Why it matters, how to practise")
            layout: focusTable.resolveColumns([{ width: root.markW }, { fill: true }])
            detailIndent: focusTable.rowPad + root.markW + focusTable.columnGap
            detailRightInset: focusTable.rowPad
            Item { width: 1; height: 1 }        // the chevron's column
            Text {
                width: whyRow.headerRow.columnWidth(1)
                elide: Text.ElideRight
                text:  qsTr("Why it matters · how to practise")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText2
            }
            detail: Component {
                Column {
                    topPadding:    Theme.sp(4)
                    bottomPadding: Theme.sp(20)
                    spacing: Theme.gap(16)

                    // WHY IT MATTERS
                    Column {
                        width: parent.width
                        spacing: Theme.gap(6)
                        visible: root.focusItem !== null && (root.focusItem.why || "") !== ""
                        Caption { text: qsTr("Why it matters") }
                        Prose {
                            objectName: "focusWhy"
                            width: Math.min(parent.width, Theme.sp(760))
                            text:  root.focusItem ? (root.focusItem.why || "") : ""
                        }
                    }
                    // HOW TO PRACTISE
                    Column {
                        width: parent.width
                        spacing: Theme.gap(6)
                        visible: root.focusItem !== null && (root.focusItem.practise || "") !== ""
                        Caption { text: qsTr("How to practise") }
                        Prose {
                            objectName: "focusPractise"
                            width: Math.min(parent.width, Theme.sp(760))
                            text:  root.focusItem ? (root.focusItem.practise || "") : ""
                            color: Theme.colorText
                        }
                    }
                }
            }
        }
    }
}
