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

// One row of Home's Tendencies tab (HmTendencies): a PpExpandRow on the table's column template.
//
//   header   chevron · club (with "· part swing") · usual distance (average ± spread) · usual miss · every shot with the club (a PpBandBar over its words, wrapping when long) ·
//            seen with and how strongly, or "Building — N sessions" with a dashed ring · trend · work-on
//   detail   what leads to it, in the golfer's words with the words that link them; also seen; how
//            to play to it; Practice
//
// `tendency` is one of WorkOnsController.tendencyItems, every string final. The golfer layer: the only
// figures in a row are the usual distances, the shot mix's percentages and the sessions a building row still needs words for.
// Colour is never the only channel: the band bar has its words beneath, the trend carries an arrow and a word.
//
// The trend's tone follows Theme.trendTone of the negated value, as HmAreaRow's: a miss that grows is
// worse.
//
// tendencyDetail() is read when the row is created; a publish rebuilds the row, so it stays current.

import QtQuick
import PinPointStudio

PpExpandRow {
    id: tendencyRow
    objectName: "homeTendencyRow:" + tendencyRow.key

    property var tendency: ({})
    property var controller: null

    signal practiseRequested(string conditionId, string label)

    // ── The words as drawn (what a probe reads) ─────────────────────────────
    readonly property string key:       tendencyRow.str("key")
    readonly property string clubLine:  tendencyRow.str("clubLine")
    readonly property string miss:      tendencyRow.str("miss")
    readonly property bool   noMiss:    !!(tendencyRow.tendency && tendencyRow.tendency.noUsualMiss)
    readonly property bool   building:  !!(tendencyRow.tendency && tendencyRow.tendency.building)
    readonly property string frequency: tendencyRow.str("frequency")
    readonly property string seenWith:  tendencyRow.str("seenWith")
    readonly property string strength:  tendencyRow.str("strength")
    readonly property string trendText: tendencyRow.str("trendWords")
    readonly property int    trend:     tendencyRow.tendency && tendencyRow.tendency.trend ? tendencyRow.tendency.trend : 0
    readonly property string workOnText: tendencyRow.str("workOnText")
    readonly property string buildingText: Theme.plural(tendencyRow.tendency && tendencyRow.tendency.sessions ? tendencyRow.tendency.sessions : 0,
                                                        qsTr("Building — %1 session"), qsTr("Building — %1 sessions"))
    readonly property string seenText: tendencyRow.building ? tendencyRow.buildingText
                                       : tendencyRow.seenWith !== "" ? tendencyRow.seenWith : "—"

    function str(k) { return tendencyRow.tendency && tendencyRow.tendency[k] !== undefined ? String(tendencyRow.tendency[k]) : "" }

    secondLine: true
    // A long shot mix wraps rather than losing its last words, and the row grows to hold it.
    rowHeight: Math.max(Theme.sp(Theme.rowH2), mixCell.implicitHeight + Theme.gap(16))
    readonly property string distance: tendencyRow.str("distance")
    readonly property var    mix:      tendencyRow.tendency && tendencyRow.tendency.mix ? tendencyRow.tendency.mix : ({})
    readonly property var    mixParts: tendencyRow.mix.parts || []
    readonly property string mixLine:  tendencyRow.mix.line || ""
    function mixTone(kind) { return Theme.bandTone(kind === "good" ? "clean" : kind === "main" ? "habit" : "occasional") }

    readonly property var    tendencyDetail: tendencyRow.controller && tendencyRow.key !== ""
                                             ? tendencyRow.controller.tendencyDetail(tendencyRow.key) : ({})
    readonly property var    chain:      tendencyRow.tendencyDetail.chain || []
    readonly property string alsoSeen:   tendencyRow.tendencyDetail.alsoSeen || ""
    readonly property string playTo:     tendencyRow.tendencyDetail.playTo || ""
    readonly property string practiseId: tendencyRow.tendencyDetail.practiseConditionId || ""
    readonly property string earlyNote:  tendencyRow.tendencyDetail.earlyNote || ""
    readonly property bool   hasDetail:  tendencyRow.chain.length > 0 || tendencyRow.alsoSeen !== ""
                                         || tendencyRow.playTo !== "" || tendencyRow.practiseId !== ""

    accessibleName: tendencyRow.clubLine + ", " + tendencyRow.miss
    pressObjectName: "homeTendencyPress"
    detail: tendencyRow.hasDetail ? tendencyRow._detail : null
    detailIndent:     tendencyRow.columnX(1) + tendencyRow.padH
    detailRightInset: tendencyRow.padH

    // Held as a property, not a child: a child would land in the header's cells.
    readonly property Component _detail: Component {
        Grid {
            id: opened
            readonly property bool stacked: width < Theme.sp(640)
            readonly property real wordsW:  opened.stacked ? opened.width : Math.round(opened.width * 0.52)
            readonly property real playW:   opened.stacked ? opened.width
                                                           : Math.max(0, opened.width - opened.wordsW - opened.columnSpacing)
            columns: opened.stacked ? 1 : 2
            columnSpacing: Theme.gap(24)
            rowSpacing:    Theme.gap(14)
            topPadding:    Theme.gap(2)
            bottomPadding: Theme.gap(16)

            // WHAT LEADS TO IT: each step in the golfer's words, the link to the next beneath it.
            Column {
                width:   opened.wordsW
                spacing: Theme.gap(6)
                PpCaption {
                    visible: tendencyRow.chain.length > 0
                    text:    tendencyRow.building ? qsTr("Seen so far") : qsTr("What leads to it")
                }
                Repeater {
                    model: tendencyRow.chain
                    Column {
                        id: step
                        required property var modelData
                        width:   parent ? parent.width : 0
                        spacing: Theme.gap(2)
                        HmHeadline {
                            objectName: "tendencyChainStep"
                            width: step.width
                            text:  step.modelData.text || ""
                        }
                        PpCaption {
                            objectName: "tendencyChainLink"
                            visible: (step.modelData.words || "") !== ""
                            width:   step.width
                            text:    "← " + (step.modelData.words || "")
                        }
                    }
                }
                PpCaption {
                    objectName: "tendencyAlsoSeen"
                    visible:  tendencyRow.alsoSeen !== ""
                    width:    parent.width
                    elide:    Text.ElideNone
                    wrapMode: Text.WordWrap
                    text:     tendencyRow.alsoSeen
                }
                PpCaption {
                    objectName: "tendencyEarlyNote"
                    visible:  tendencyRow.earlyNote !== ""
                    width:    parent.width
                    elide:    Text.ElideNone
                    wrapMode: Text.WordWrap
                    text:     tendencyRow.earlyNote
                }
            }

            // Play to it, and the way to practise it.
            Column {
                width:   opened.playW
                spacing: Theme.gap(10)
                PpInset {
                    objectName: "tendencyPlayTo"
                    visible: tendencyRow.playTo !== ""
                    width:   parent.width
                    PpMicro { text: Theme.caps(qsTr("Play to it")) }
                    Text {
                        width: parent.width
                        text:  tendencyRow.playTo
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight
                        color:          Theme.colorText2
                        wrapMode:       Text.WordWrap
                        lineHeight:     1.4
                    }
                }
                PpButton {
                    objectName: "tendencyPractise"
                    visible: tendencyRow.practiseId !== ""
                    primary: true
                    glyph:   "▶"
                    label:   qsTr("Practice")
                    onClicked: tendencyRow.practiseRequested(tendencyRow.practiseId, tendencyRow.seenWith)
                }
            }
        }
    }

    // ── Header cells, in column order ───────────────────────────────────────
    Item { width: 1; height: 1 }                // the chevron's column

    Text {                                      // club
        objectName: "tendencyClub"
        width: tendencyRow.columnWidth(1)
        text:  tendencyRow.clubLine
        wrapMode: Text.WordWrap                 // "Pitching wedge · part swing" takes two lines
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText
    }

    Text {                                      // usual distance: average ± spread
        objectName: "tendencyDistance"
        width: tendencyRow.columnWidth(2)
        text:  tendencyRow.distance !== "" ? tendencyRow.distance : "—"
        elide: Text.ElideRight
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzBody2
        color:          tendencyRow.distance !== "" ? Theme.colorText : Theme.colorText3
    }

    Row {                                       // usual miss; "No usual miss" wears a tick
        objectName: "tendencyMiss"
        width:   tendencyRow.columnWidth(3)
        spacing: Theme.gap(4)
        PpBadge {
            id: missTick
            visible: tendencyRow.noMiss
            anchors.verticalCenter: parent.verticalCenter
            kind:  "check"
            plain: true
            size:  Theme.sp(16)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - (missTick.visible ? missTick.width + parent.spacing : 0))
            text:  tendencyRow.miss
            elide: Text.ElideRight
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Theme.fontBodyWeight
            color:          tendencyRow.noMiss ? Theme.colorGood : Theme.colorText
        }
    }

    Column {                                    // every shot with this club: the bar, and its words beneath
        id: mixCell
        objectName: "tendencyOften"
        width:   tendencyRow.columnWidth(4)
        spacing: Theme.gap(4)
        PpBandBar {
            objectName: "tendencyMixBar"
            width: parent.width
            segments: tendencyRow.mixParts.map(p => ({ count: p.count, tone: tendencyRow.mixTone(p.kind) }))
        }
        PpCaption {
            objectName: "tendencyMixWords"
            width: parent.width
            elide:    Text.ElideNone
            wrapMode: Text.WordWrap
            text:  tendencyRow.mixLine !== "" ? tendencyRow.mixLine : tendencyRow.frequency
        }
    }

    Row {                                       // seen with, and how strongly; or still building
        objectName: "tendencySeenWith"
        width:   tendencyRow.columnWidth(5)
        spacing: Theme.gap(6)
        PpBadge {
            id: buildingMark
            visible: tendencyRow.building
            anchors.verticalCenter: parent.verticalCenter
            kind:  "unconfirmed"
            plain: true
            size:  Theme.sp(14)
        }
        Text {
            id: seenWords
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, Math.max(0, parent.width
                                                    - (buildingMark.visible ? buildingMark.width + parent.spacing : 0)
                                                    - (strengthWord.visible ? strengthWord.width + parent.spacing : 0)))
            text:  tendencyRow.seenText
            elide: Text.ElideRight
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Theme.fontBodyWeight
            color:          tendencyRow.building ? Theme.colorText2 : Theme.colorText
        }
        PpCaption {
            id: strengthWord
            objectName: "tendencyStrength"
            visible: !tendencyRow.building && tendencyRow.strength !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: "· " + tendencyRow.strength
        }
    }

    Text {                                      // trend
        objectName: "tendencyTrend"
        width: tendencyRow.columnWidth(6)
        text:  tendencyRow.trendText !== "" ? tendencyRow.trendText : "—"
        elide: Text.ElideRight
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzBody2
        color: tendencyRow.trendText !== "" ? Theme.trendTone(-tendencyRow.trend) : Theme.colorText3
    }

    Text {                                      // work-on
        objectName: "tendencyWorkOn"
        width: tendencyRow.columnWidth(7)
        text:  tendencyRow.workOnText !== "" ? tendencyRow.workOnText : "—"
        elide: Text.ElideRight
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          tendencyRow.workOnText !== "" ? Theme.colorText : Theme.colorText3
    }
}
