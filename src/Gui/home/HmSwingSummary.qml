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

// COACHING — the current athlete's swing as a short coaching page: the body of Home's Coaching tab
// (HmHomeTabs), in the §2.5 visual language (docs/design/ux_refactor_away_from_4_modes/
// 03_home_coaching.png; the golfer's rules are docs/design/home_themes_design.md's). Drawn for a
// typical golfer, to be LOOKED at before it is read, with one thing to the fore:
//
//   Your focus         HmFocus: the one drill to practise, the ideal it aims at, the faults it
//                      covers, and — one click down — why it matters and how to practise it
//   What you do well   a table card: a strength dot, the phrase with what backs it beneath, the
//                      ten-step meter of how often it holds, one pip per session
//   Next on your list  a table card in the same grammar: a fault ring, the phrase with how often
//                      in words beneath (and a trend tag when there is one), the meter, the pips;
//                      the first wears a "Next" tag
//
// No metrics, no figures, no P-positions, no claims about cause — a golfer reads this. Colour is
// never the only channel: the meter carries a count of segments, and each has words beside it.
// The evidence line ("From N swings over M sessions") and the way into every work-on ("All
// work-ons ▸") are the tab bar's, not this page's.
//
// Every string is WorkOnsController's (swingSummaryView over the pack's golfer phrases):
//   summaryReady, summaryUpdating, updating, athleteDir
//   focusItem      see HmFocus.qml
//   doWellItems    [{ text, caption, share 0..1, sessions: [bool…] }]
//   nextItems      [{ text, share 0..1, frequency, trend -1|0|1, sessions: [bool…] }]
// This file positions and paints. Before the first summary lands it says it is reading, and with
// no session to read it says "Nothing yet".
//
// The pips are the LATEST maxPips judged sessions, oldest first, so a long history keeps the row
// its width; the "Sessions" caption heads them.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "swingSummary"

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null

    // Kept for compatibility: the "Swing diagnostics →" row it came from is gone (the tab bar's
    // "All work-ons ▸" replaces it). Never emitted.
    signal diagnosticsRequested()
    // The focus card's Practise.
    signal practiseRequested(string conditionId, string label)

    readonly property var    doWell:    controller ? controller.doWellItems    : []
    readonly property var    needsWork: controller ? controller.nextItems      : []
    readonly property bool   ready:     controller ? controller.summaryReady    : false
    readonly property bool   updating:  controller ? controller.summaryUpdating : false
    // The work-ons catch-up comes first; the summary is read off what it leaves current.
    readonly property bool   catchingUp: controller ? controller.updating       : false

    // At most this many pips in a row: the latest sessions.
    readonly property int maxPips: 8

    // Two cards side by side down to this width; below it they stack.
    readonly property bool stacked: width < Theme.sp(640)

    // ── Tones ────────────────────────────────────────────────────────────────
    // A strength in the in-range colour (Theme.bandTone's "clean"), a fault in amber.
    readonly property color toneWell: Theme.bandTone("clean")
    readonly property color toneWork: Theme.colorAttention

    // ── Shared geometry ──────────────────────────────────────────────────────
    readonly property int cardGap: Theme.gap(16)
    readonly property int markW:   Theme.sp(20)
    // Both cards' column template: mark · words · how often · sessions.
    readonly property var columns: [
        { width: root.markW },
        { fill: true },
        { width: Theme.sp(110) },
        { width: Theme.sp(80), align: "right" }]

    width:   parent ? parent.width : 0
    spacing: 0

    function capitalised(s) { s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

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
    // A card's own header row: its title in its tone, in the words column, and the captions of the
    // other columns on the same line (the mockup's single header band).
    component CardHeader: PpTableRow {
        id: head
        property string title: ""
        property color  tone:  Theme.colorText
        property bool   captions: true
        height: Theme.sp(Theme.tableHeaderH)
        hoverEnabled: false
        Item { width: 1; height: 1 }        // the mark's column
        Text {
            width: Math.min(implicitWidth, head.columnWidth(1))
            elide: Text.ElideRight
            text:  head.title
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Font.DemiBold
            color:          head.tone
        }
        Caption { visible: head.captions; text: qsTr("How often"); width: Math.min(implicitWidth, head.columnWidth(2)) }
        Caption { visible: head.captions; text: qsTr("Sessions");  width: Math.min(implicitWidth, head.columnWidth(3)) }
    }
    component Headline: Text {
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText
        wrapMode:       Text.WordWrap
        lineHeight:     1.2
    }

    // ════════════════════════════════════════════════════════════════════════
    // Layout
    // ════════════════════════════════════════════════════════════════════════

    // The first reading of a golfer's sessions, said once and quietly. A recompute after that
    // leaves the summary standing and says nothing: it changes when it lands.
    Item {
        visible: root.updating && !root.ready
        width:  root.width
        height: visible ? Theme.sp(34) : 0
        PpMicro {
            objectName: "swingSummaryUpdating"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("reading your sessions…")
            font.letterSpacing: Theme.trackingData
        }
    }

    // Nothing to read yet: no session with a swing in it. Not said before the controller has
    // been pointed at the athlete (the first pass waits out the launch), when it is not yet known.
    Item {
        visible: !root.ready && !root.updating && !root.catchingUp
                 && root.controller !== null && root.controller.athleteDir !== ""
        height:  visible ? Theme.sp(40) : 0
        width:   root.width
        Text {
            objectName: "swingSummaryEmpty"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("Nothing yet. Your swing is summed up here once you have hit some shots.")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText3
        }
    }

    Column {
        visible: root.ready
        width:   root.width
        spacing: root.cardGap

        // ── Your focus ───────────────────────────────────────────────────────
        HmFocus {
            id: focusCard
            width:      root.width
            height:     implicitHeight
            controller: root.controller
            gridGap:    root.cardGap
            onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
        }

        // ── What you do well | next on your list ─────────────────────────────
        Item {
            id: row1
            width:  root.width
            readonly property real cardW: root.stacked ? width : (width - root.cardGap) / 2
            // Side by side, both cards end at the taller one's height.
            readonly property real tallest: Math.max(wellCard.implicitHeight, workCard.implicitHeight)
            height: root.stacked ? wellCard.implicitHeight + root.cardGap + workCard.implicitHeight
                                 : tallest

            PpCard {
                id: wellCard
                width:   row1.cardW
                height:  root.stacked ? implicitHeight : row1.tallest
                table:   true
                columns: root.columns

                CardHeader {
                    title:    qsTr("What you do well")
                    tone:     root.toneWell
                    captions: root.doWell.length > 0
                }

                Repeater {
                    model: root.doWell
                    PpTableRow {
                        id: wellItem
                        objectName: "doWellItem"
                        required property var modelData
                        // The words as drawn — what a probe reads.
                        readonly property string itemText:    root.capitalised(wellItem.modelData.text)
                        readonly property string captionText: wellItem.modelData.caption
                        readonly property string plainText:   itemText + " — " + captionText
                        readonly property int    pipCount:    (wellItem.modelData.sessions || []).length
                        secondLine: true
                        hoverEnabled: false
                        height: Math.max(Theme.sp(Theme.rowH2), wellWords.implicitHeight + Theme.sp(18))

                        PpBadge { kind: "dot"; plain: true; tone: root.toneWell; size: root.markW }
                        Column {
                            id: wellWords
                            width: wellItem.columnWidth(1)
                            spacing: Theme.gap(2)
                            Headline { width: parent.width; text: wellItem.itemText }
                            Caption {
                                width:    parent.width
                                text:     wellItem.captionText
                                elide:    Text.ElideNone
                                wrapMode: Text.WordWrap
                            }
                        }
                        PpMeter {
                            share: wellItem.modelData.share !== undefined ? wellItem.modelData.share : 0
                            tone:  root.toneWell
                        }
                        PpPips {
                            marks:   wellItem.modelData.sessions
                            maxPips: root.maxPips
                            tone:    root.toneWell
                        }
                    }
                }

                PpTableRow {
                    id: wellEmptyRow
                    visible: root.doWell.length === 0
                    hoverEnabled: false
                    Item { width: 1; height: 1 }
                    Caption {
                        objectName: "doWellEmpty"
                        width: wellEmptyRow.columnWidth(1)
                        text:  qsTr("Nothing stands out here yet.")
                    }
                }
            }

            PpCard {
                id: workCard
                x: root.stacked ? 0 : row1.cardW + root.cardGap
                y: root.stacked ? wellCard.implicitHeight + root.cardGap : 0
                width:   row1.cardW
                height:  root.stacked ? implicitHeight : row1.tallest
                table:   true
                columns: root.columns

                CardHeader {
                    title:    qsTr("Next on your list")
                    tone:     root.toneWork
                    captions: root.needsWork.length > 0
                }

                Repeater {
                    model: root.needsWork
                    PpTableRow {
                        id: workItem
                        objectName: "nextItem"
                        required property var modelData
                        required property int index
                        // The words as drawn — what a probe reads.
                        readonly property string itemText:      root.capitalised(workItem.modelData.text)
                        readonly property string frequencyText: workItem.modelData.frequency
                        readonly property string plainText:     itemText + " — " + frequencyText
                        readonly property int    pipCount:      (workItem.modelData.sessions || []).length
                        secondLine: true
                        hoverEnabled: false
                        height: Math.max(Theme.sp(Theme.rowH2), workWords.implicitHeight + Theme.sp(18))

                        PpBadge { kind: "ring"; plain: true; tone: root.toneWork; size: root.markW }
                        Column {
                            id: workWords
                            width: workItem.columnWidth(1)
                            spacing: Theme.gap(2)
                            // The phrase; the first wears "Next" at the column's right.
                            Item {
                                width:  parent.width
                                height: Math.max(workHead.height, nextChip.visible ? nextChip.height : 0)
                                Headline {
                                    id: workHead
                                    width: parent.width - (nextChip.visible ? nextChip.width + Theme.sp(12) : 0)
                                    text:  workItem.itemText
                                }
                                PpChip {
                                    id: nextChip
                                    objectName: "nextChip"
                                    visible:  workItem.index === 0
                                    anchors.right: parent.right
                                    y: Math.round((workHead.lineCount > 1 ? workHead.height / workHead.lineCount
                                                                          : workHead.height) / 2 - height / 2)
                                    text:     Theme.caps(qsTr("Next"))
                                    tone:     Theme.colorAccent
                                    tracking: Theme.trackingMicro
                                }
                            }
                            // How often in words, and which way it is going.
                            Row {
                                width: parent.width
                                spacing: Theme.gap(8)
                                Caption {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, parent.width
                                                    - (workChip.visible ? workChip.width + parent.spacing : 0))
                                    text:  workItem.frequencyText
                                }
                                PpChip {
                                    id: workChip
                                    anchors.verticalCenter: parent.verticalCenter
                                    trend: workItem.modelData.trend
                                }
                            }
                        }
                        PpMeter {
                            share: workItem.modelData.share
                            tone:  root.toneWork
                        }
                        PpPips {
                            marks:   workItem.modelData.sessions
                            maxPips: root.maxPips
                            tone:    Theme.colorText2
                        }
                    }
                }

                PpTableRow {
                    id: workEmptyRow
                    visible: root.needsWork.length === 0
                    hoverEnabled: false
                    Item { width: 1; height: 1 }
                    Caption {
                        objectName: "needsWorkEmpty"
                        width: workEmptyRow.columnWidth(1)
                        text:  qsTr("Nothing else shows up on most of your swings.")
                    }
                }
            }
        }
    }
}
