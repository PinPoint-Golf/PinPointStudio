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

// MATCHUPS — the swing themes (docs/design/home_themes_design.md), the body of Home's Matchups tab
// (HmHomeTabs): today the faults that travel together; later also pairs that work together (Mark,
// 28 Oct). One table card in the grammar of the Coaching tab's "What you do well" (the §2.5 visual
// language), a row per pair:
//
//   strength mark · the two halves joined by a connector · a six-stop swing timeline of where the
//   pair first shows, with its words beside it where there is room, else beneath
//
// A row opens in place, one at a time, to the rest of what moves with the pair and the faults on the
// same movement; each fault opens as it does elsewhere (HmFaultRow). A shared movement, never a cause.
//
// Every string is WorkOnsController's (swingSummaryView):
//   togetherItems  [{ tier "firm"|"probably"|"possibly", first ("When …", or the one part),
//                     second ("" for one part), startStop 0..5 (−1 unplaced: no timeline),
//                     startWords, trend -1|0|1, also [string], faults [{ text, frequency, trend,
//                     conditionId, … }] }]
//   togetherNote   "" or why nothing can be said yet

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "goesTogether"

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null

    // An open fault's "Review the … session →".
    signal reviewSessionRequested(string sessionDir)

    readonly property var    together:  controller ? controller.togetherItems  : []
    readonly property string note:      controller ? controller.togetherNote   : ""

    // The open pair and the fault open inside it. Held here and not on the row: each publish of the
    // summary rebuilds the rows.
    property string openPair:  ""
    property string openFault: ""

    // ── Palette ──────────────────────────────────────────────────────────────
    readonly property color toneTogether: Theme.gradientCool     // a relation, neither praise nor fault

    width:   parent ? parent.width : 0
    spacing: 0

    PpCard {
        id: togetherCard
        width: root.width
        height: implicitHeight
        table: true

        // Half the card at most, so a narrow window keeps room for the words.
        readonly property int whereW: Math.min(Theme.sp(540), Math.floor(togetherCard.contentWidth / 2))

        columns: [{ width: Theme.sp(68) },
                  { fill: true },
                  { width: togetherCard.whereW },
                  { width: Theme.sp(10) }]              // a row's chevron

        // The card's header row, one line as on the Coaching tab's cards: the title in its tone, in
        // the words column, and the caption of the column it heads.
        PpTableRow {
            id: header
            hoverEnabled: false
            height: Theme.sp(Theme.tableHeaderH)

            Item { width: 1; height: 1 }        // the strength mark's column
            Text {
                width: Math.min(implicitWidth, header.columnWidth(1))
                elide: Text.ElideRight
                text:  qsTr("Faults that travel together")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.DemiBold
                color:          root.toneTogether
            }
            PpCaption {
                visible: root.together.length > 0
                width:   Math.min(implicitWidth, header.columnWidth(2))
                text:    qsTr("Where it starts")
            }
        }

        Repeater {
            model: root.together
            PpExpandRow {
                id: pair
                objectName: "togetherItem"
                required property var modelData
                // The words as drawn — what a probe reads.
                readonly property string tier:           pair.modelData.tier
                readonly property string firstText:      pair.modelData.first
                readonly property string secondText:     pair.modelData.second
                readonly property string startWordsText: pair.modelData.startWords
                readonly property string plainText:
                    [firstText, secondText, startWordsText].filter(s => s !== "").join(" — ")
                readonly property bool hasSecond: secondText !== ""
                readonly property bool placed:    pair.modelData.startStop >= 0
                readonly property int linkIndent: Theme.sp(18)
                readonly property int chipRoom:   chip.visible ? chip.width + Theme.gap(16) : 0

                readonly property string key:    firstText + "|" + secondText
                readonly property var    also:   pair.modelData.also   || []
                readonly property var    faults: pair.modelData.faults || []

                secondLine: true
                rowHeight: Math.max(Theme.sp(Theme.rowH2),
                                    Math.max(strength.height, words.height, where.height) + Theme.gap(18))
                accessibleName: secondText !== "" ? firstText + ", " + secondText : firstText
                pressObjectName: "togetherPress"
                chevronSide: "right"
                expanded:    root.openPair === pair.key
                autoToggle:  false
                onToggled: {
                    root.openFault = ""
                    root.openPair = pair.expanded ? "" : pair.key
                }
                detailIndent:     pair.columnX(1) + pair.padH + pair.linkIndent
                detailRightInset: pair.padH + Theme.sp(10) + togetherCard.columnGap
                detail: pair.also.length > 0 || pair.faults.length > 0 ? pair._detail : null

                // Held as a property, not a child: a child would land in the header's cells.
                readonly property Component _detail: Component {
                    Column {
                        id: opened
                        topPadding:    Theme.gap(2)
                        bottomPadding: Theme.gap(16)
                        spacing: Theme.gap(14)

                        Column {
                            visible: pair.also.length > 0
                            width:   opened.width
                            spacing: Theme.gap(6)
                            PpCaption { text: qsTr("Also moves with it") }
                            Repeater {
                                model: pair.also
                                HmHeadline {
                                    required property string modelData
                                    objectName: "togetherAlso"
                                    width: opened.width
                                    text:  modelData
                                }
                            }
                        }

                        Column {
                            visible: pair.faults.length > 0
                            width:   opened.width
                            spacing: Theme.gap(6)
                            PpCaption { text: qsTr("Faults on the same movement") }
                            Column {
                                width: opened.width
                                Repeater {
                                    model: pair.faults
                                    HmFaultRow {
                                        id: faultRow
                                        objectName: "togetherFault"
                                        required property var modelData
                                        readonly property int gap:    togetherCard.columnGap
                                        readonly property int chevW:  Theme.sp(10)
                                        readonly property int freqW:  Math.min(Theme.sp(240), Math.floor(faultRow.width * 0.4))
                                        readonly property int wordsW: Math.max(0, faultRow.width - faultRow.freqW
                                                                                  - faultRow.chevW - 2 * faultRow.gap)
                                        controller:  root.controller
                                        conditionId: faultRow.modelData.conditionId || ""
                                        openId:      root.openFault
                                        onOpenRequested: (next) => root.openFault = next
                                        onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
                                        width: parent ? parent.width : 0
                                        rowHeight: Math.max(Theme.sp(Theme.itemRowH), faultWords.height + Theme.gap(10))
                                        padH: 0
                                        topRule: true
                                        layout: [{ x: 0, width: faultRow.wordsW, align: "left" },
                                                 { x: faultRow.wordsW + faultRow.gap, width: faultRow.freqW, align: "left" },
                                                 { x: faultRow.wordsW + faultRow.freqW + 2 * faultRow.gap,
                                                   width: faultRow.chevW, align: "left" }]
                                        accessibleName: faultRow.modelData.text
                                        detailIndent:     0
                                        detailRightInset: faultRow.chevW + faultRow.gap

                                        HmHeadline {
                                            id: faultWords
                                            width: faultRow.columnWidth(0)
                                            text:  faultRow.modelData.text
                                        }
                                        Row {
                                            width: faultRow.columnWidth(1)
                                            spacing: Theme.gap(8)
                                            PpCaption {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: Math.min(implicitWidth, parent.width
                                                                - (faultChip.visible ? faultChip.width + parent.spacing : 0))
                                                text:  faultRow.modelData.frequency
                                            }
                                            PpChip {
                                                id: faultChip
                                                anchors.verticalCenter: parent.verticalCenter
                                                trend: faultRow.modelData.trend
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                PpStrengthMark {
                    id: strength
                    tier: pair.modelData.tier
                    tone: root.toneTogether
                }

                // The two halves. "When …" is the controller's, as is the one part said alone.
                Item {
                    id: words
                    width:  pair.columnWidth(1)
                    height: pair.hasSecond ? secondLine.y + secondLine.height : firstLine.height

                    // The connector: a dot at each half, a line between. One half: one dot.
                    Rectangle {
                        visible: pair.hasSecond
                        x: Theme.sp(3) - width / 2 + Theme.sp(0.5)
                        y: firstLine.y + firstLine.lineCentre
                        width: 1
                        height: (secondLine.y + secondLine.lineCentre) - y
                        color: Qt.alpha(root.toneTogether, 0.6)
                    }
                    Repeater {
                        model: pair.hasSecond ? [firstLine, secondLine] : [firstLine]
                        Rectangle {
                            required property var modelData
                            width: Theme.sp(7); height: width; radius: width / 2
                            x: Theme.sp(3) - width / 2 + Theme.sp(0.5)
                            y: modelData.y + modelData.lineCentre - height / 2
                            color: root.toneTogether
                        }
                    }

                    HmHeadline {
                        id: firstLine
                        readonly property real lineCentre: (lineCount > 1 ? height / lineCount : height) / 2
                        x: pair.linkIndent
                        width: Math.max(0, parent.width - pair.linkIndent - pair.chipRoom)
                        text:  pair.modelData.first
                    }
                    HmHeadline {
                        id: secondLine
                        readonly property real lineCentre: (lineCount > 1 ? height / lineCount : height) / 2
                        visible: pair.hasSecond
                        x: pair.linkIndent
                        y: firstLine.height + Theme.gap(6)
                        width: Math.max(0, parent.width - pair.linkIndent - pair.chipRoom)
                        text:  pair.modelData.second
                    }
                    PpChip {
                        id: chip
                        anchors.right: parent.right
                        y: Math.round(firstLine.lineCentre - height / 2)
                        trend: pair.modelData.trend
                    }
                }

                // Where it first shows. A theme with no swing position has neither timeline nor words.
                Item {
                    id: where
                    visible: pair.placed
                    width:   pair.columnWidth(2)
                    height:  !pair.placed      ? 0
                           : where.wordsBeside ? timeline.implicitHeight
                           :                     startWords.y + startWords.height

                    readonly property int timelineW: Math.min(Theme.sp(400), width)
                    readonly property bool wordsBeside:
                        width - timelineW - Theme.gap(8) >= startWords.implicitWidth

                    PpSwingTimeline {
                        id: timeline
                        width: where.timelineW
                        start: Math.max(0, pair.modelData.startStop)
                        tone:  root.toneTogether
                    }
                    PpCaption {
                        id: startWords
                        x: where.wordsBeside ? timeline.width + Theme.gap(8) : 0
                        y: where.wordsBeside ? timeline.trackY - height / 2
                                             : timeline.implicitHeight + Theme.gap(6)
                        text: pair.modelData.startWords
                    }
                }
            }
        }

        // Nothing that holds yet: said calmly, in the card.
        PpTableRow {
            id: emptyRow
            visible: root.together.length === 0
            hoverEnabled: false
            height: Math.max(Theme.sp(Theme.rowH), emptyNote.implicitHeight + Theme.gap(18))
            Item { width: 1; height: 1 }
            PpCaption {
                id: emptyNote
                objectName: "togetherNote"
                width:    emptyRow.columnWidth(1)
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     root.note !== "" ? root.note
                                           : qsTr("Nothing yet. This fills in as your sessions build up.")
            }
        }
    }
}
