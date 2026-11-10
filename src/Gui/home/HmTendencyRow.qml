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

// One row of Home's Tendencies tab (HmTendencies): a PpExpandRow on the table's column template. A shot has
// three factors, strike, direction and flight, and the row says each one's usual.
//
//   header   chevron · club (with "· part swing") · usual distance (average ± spread) · strike · direction ·
//            flight · work-on. A factor cell is its usual over its words: a fault in the fault tone with how
//            often beneath ("Pull" / "fairly often"); a value plain and alone ("Solid", "Mid"); still building,
//            "Building — N sessions" beside a dashed ring, and never a verdict's tone.
//   detail   one block per factor with something to say, headed by the factor's name: every shot it was read on
//            (a PpBandBar over its words), the trend, what leads to it (the golfer's words with the words that
//            link them), also seen, how to play to it, Practice. Then one Together block: when two factors' usuals
//            are both faults, how often they came together and where each one's shots went; and the commonest
//            combinations of all three (TendencyTogether). Each factor's usual is chosen on its own, so the row
//            alone can read as one shot shape when it was different shots; this says which.
//
// `tendency` is one of WorkOnsController.tendencyItems, every string final: { key, clubLine, distance, workOnText,
// strike, direction, flight }, each factor { label, fault, building, frequency, sessions, shots, seenWith, trend,
// trendWords, mix: { line, parts: [{ count, kind }] } }. tendencyDetail(key) gives { strike, direction, flight,
// together }, each factor { chain, alsoSeen, earlyNote, playTo, practiseConditionId } or {} for a factor with
// nothing to say, together { pairs, combos, moreCombos }.
// The golfer layer: the only figures in a row are the usual distances, the shot mix's percentages, the sessions
// a building factor still needs words for, and the Together block's shot counts. Colour is never the only channel: a fault has its words, the band bar
// has its words beneath, the trend carries an arrow and a word.
//
// A shot-mix part's tone by its kind: good → clean, value → value (neither praise nor fault), main → habit,
// other → occasional (Theme.bandTone). The trend's tone follows Theme.trendTone of the negated value, as
// HmAreaRow's: a fault that grows is worse.
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

    readonly property var factorKeys: ["strike", "direction", "flight"]
    function factorName(f) { return f === "strike" ? qsTr("Strike") : f === "direction" ? qsTr("Direction") : qsTr("Flight") }

    // ── The words as drawn (what a probe reads) ─────────────────────────────
    readonly property string key:        tendencyRow.str("key")
    readonly property string clubLine:   tendencyRow.str("clubLine")
    readonly property string distance:   tendencyRow.str("distance")
    readonly property string workOnText: tendencyRow.str("workOnText")

    function str(k) { return tendencyRow.tendency && tendencyRow.tendency[k] !== undefined ? String(tendencyRow.tendency[k]) : "" }
    // One factor of the row, {} when the row has none.
    function factor(f) { return tendencyRow.tendency && tendencyRow.tendency[f] ? tendencyRow.tendency[f] : ({}) }
    // Building, and there is something so far (a factor never read has nothing to build).
    function building(f) { const x = tendencyRow.factor(f); return !!x.building && (x.shots === undefined || x.shots > 0) }
    // An earned fault: the only one that wears the fault tone.
    function fault(f) { return !!tendencyRow.factor(f).fault && !tendencyRow.building(f) }
    function factorLabel(f) {
        const l = tendencyRow.factor(f).label
        return l === undefined || l === "" || l === "-" ? "—" : String(l)
    }
    function factorWords(f) {
        const x = tendencyRow.factor(f)
        if (tendencyRow.building(f))
            return Theme.plural(x.sessions || 0, qsTr("Building — %1 session"), qsTr("Building — %1 sessions"))
        return tendencyRow.fault(f) ? String(x.frequency || "") : ""
    }
    function mixTone(kind) {
        return Theme.bandTone(kind === "good" ? "clean" : kind === "value" ? "value" : kind === "main" ? "habit" : "occasional")
    }

    readonly property var tendencyDetail: tendencyRow.controller && tendencyRow.key !== ""
                                          && typeof tendencyRow.controller.tendencyDetail === "function"
                                          ? (tendencyRow.controller.tendencyDetail(tendencyRow.key) || ({})) : ({})
    function detailOf(f) { return tendencyRow.tendencyDetail[f] || ({}) }
    function hasFactorDetail(f) {
        const d = tendencyRow.detailOf(f)
        return (d.chain || []).length > 0 || (d.alsoSeen || "") !== "" || (d.playTo || "") !== ""
               || (d.practiseConditionId || "") !== "" || (d.earlyNote || "") !== ""
    }
    readonly property var  detailFactors: tendencyRow.factorKeys.filter(f => tendencyRow.hasFactorDetail(f))
    // How the three factors came together on the row's shots: lines, golfer-ready, shown last in the open row.
    readonly property var  together:      tendencyRow.tendencyDetail.together || ({})
    readonly property var  togetherPairs:  tendencyRow.together.pairs || []
    readonly property var  togetherCombos: tendencyRow.together.combos || []
    readonly property bool hasTogether:   tendencyRow.togetherPairs.length > 0 || tendencyRow.togetherCombos.length > 0
    readonly property bool hasDetail:     tendencyRow.detailFactors.length > 0 || tendencyRow.hasTogether

    secondLine: true
    // As tall as its tallest cell: a long club line or a building factor wraps rather than losing its words.
    rowHeight: Math.max(Theme.sp(Theme.rowH2),
                        Math.max(clubCell.implicitHeight, strikeCell.implicitHeight, directionCell.implicitHeight,
                                 flightCell.implicitHeight) + Theme.gap(16))

    accessibleName: tendencyRow.clubLine + ", "
                    + tendencyRow.factorKeys.map(f => tendencyRow.factorName(f) + " " + tendencyRow.factorLabel(f)).join(", ")
    pressObjectName: "homeTendencyPress"
    detail: tendencyRow.hasDetail ? tendencyRow._detail : null
    detailIndent:     tendencyRow.columnX(1) + tendencyRow.padH
    detailRightInset: tendencyRow.padH

    // A factor's header cell: its usual over its words.
    component FactorCell: Column {
        id: cell
        property string label:    ""
        property string words:    ""
        property bool   fault:    false
        property bool   building: false
        spacing: Theme.gap(2)
        Text {
            objectName: "tendencyFactorLabel"
            width: cell.width
            text:  cell.label
            wrapMode: Text.WordWrap
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Theme.fontBodyWeight
            color: cell.fault ? Theme.colorWarn : cell.label === "—" ? Theme.colorText3 : Theme.colorText
        }
        Row {
            visible: cell.words !== ""
            width:   cell.width
            spacing: Theme.gap(6)
            PpBadge {
                id: buildingMark
                visible: cell.building
                anchors.verticalCenter: parent.verticalCenter
                kind:  "unconfirmed"
                plain: true
                size:  Theme.sp(14)
            }
            PpCaption {
                objectName: "tendencyFactorWords"
                width:    Math.max(0, parent.width - (buildingMark.visible ? buildingMark.width + parent.spacing : 0))
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     cell.words
            }
        }
    }

    // Held as a property, not a child: a child would land in the header's cells.
    readonly property Component _detail: Component {
        Column {
            id: blocks
            spacing:       Theme.gap(18)
            topPadding:    Theme.gap(2)
            bottomPadding: Theme.gap(16)

            Repeater {
                model: tendencyRow.detailFactors
                // One factor's block, under its name.
                Column {
                    id: block
                    required property var modelData
                    readonly property string f: String(block.modelData)
                    readonly property var    fx: tendencyRow.factor(block.f)
                    readonly property var    d:  tendencyRow.detailOf(block.f)
                    readonly property var    mix:      block.fx.mix || ({})
                    readonly property var    chain:    block.d.chain || []
                    readonly property string alsoSeen: block.d.alsoSeen || ""
                    readonly property string earlyNote: block.d.earlyNote || ""
                    readonly property string playTo:   block.d.playTo || ""
                    readonly property string practiseId: block.d.practiseConditionId || ""
                    readonly property string trendText: block.fx.trendWords !== undefined ? String(block.fx.trendWords) : ""
                    objectName: "tendencyBlock:" + block.f
                    width:   blocks.width
                    spacing: Theme.gap(8)

                    PpMicro {
                        objectName: "tendencyBlockHead"
                        text: Theme.caps(tendencyRow.factorName(block.f))
                    }

                    Grid {
                        id: opened
                        readonly property bool stacked: width < Theme.sp(640)
                        readonly property real wordsW:  opened.stacked ? opened.width : Math.round(opened.width * 0.52)
                        readonly property real playW:   opened.stacked ? opened.width
                                                                       : Math.max(0, opened.width - opened.wordsW - opened.columnSpacing)
                        width: block.width
                        columns: opened.stacked ? 1 : 2
                        columnSpacing: Theme.gap(24)
                        rowSpacing:    Theme.gap(14)

                        // Every shot this factor was read on, the trend, and WHAT LEADS TO IT: each step in the
                        // golfer's words, the link to the next beneath it.
                        Column {
                            width:   opened.wordsW
                            spacing: Theme.gap(6)
                            PpBandBar {
                                objectName: "tendencyMixBar"
                                visible: (block.mix.parts || []).length > 0
                                width:   parent.width
                                segments: (block.mix.parts || []).map(p => ({ count: p.count, tone: tendencyRow.mixTone(p.kind) }))
                            }
                            PpCaption {
                                objectName: "tendencyMixWords"
                                visible:  text !== ""
                                width:    parent.width
                                elide:    Text.ElideNone
                                wrapMode: Text.WordWrap
                                text:     block.mix.line || ""
                            }
                            Text {
                                objectName: "tendencyTrend"
                                visible: block.trendText !== ""
                                width:   parent.width
                                text:    block.trendText
                                elide:   Text.ElideRight
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzBody2
                                color: Theme.trendTone(-(block.fx.trend || 0))
                            }
                            PpCaption {
                                visible: block.chain.length > 0
                                text:    tendencyRow.building(block.f) ? qsTr("Seen so far") : qsTr("What leads to it")
                            }
                            Repeater {
                                model: block.chain
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
                                visible:  block.alsoSeen !== ""
                                width:    parent.width
                                elide:    Text.ElideNone
                                wrapMode: Text.WordWrap
                                text:     block.alsoSeen
                            }
                            PpCaption {
                                objectName: "tendencyEarlyNote"
                                visible:  block.earlyNote !== ""
                                width:    parent.width
                                elide:    Text.ElideNone
                                wrapMode: Text.WordWrap
                                text:     block.earlyNote
                            }
                        }

                        // Play to it, and the way to practise it.
                        Column {
                            width:   opened.playW
                            spacing: Theme.gap(10)
                            PpInset {
                                objectName: "tendencyPlayTo"
                                visible: block.playTo !== ""
                                width:   parent.width
                                PpMicro { text: Theme.caps(qsTr("Play to it")) }
                                Text {
                                    width: parent.width
                                    text:  block.playTo
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
                                visible: block.practiseId !== ""
                                primary: true
                                glyph:   "▶"
                                label:   qsTr("Practice")
                                onClicked: tendencyRow.practiseRequested(block.practiseId, String(block.fx.seenWith || ""))
                            }
                        }
                    }
                }
            }

            // How the factors came together: the pairs of usual faults, then the commonest combinations.
            Column {
                id: togetherBlock
                objectName: "tendencyBlock:together"
                visible: tendencyRow.hasTogether
                width:   blocks.width < Theme.sp(640) ? blocks.width : Math.round(blocks.width * 0.52)
                spacing: Theme.gap(8)

                PpMicro {
                    objectName: "tendencyBlockHead"
                    text: Theme.caps(qsTr("Together"))
                }
                Repeater {
                    model: tendencyRow.togetherPairs
                    Text {
                        required property var modelData
                        objectName: "tendencyTogetherPair"
                        width: togetherBlock.width
                        text:  String(modelData)
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight
                        color:          Theme.colorText2
                        wrapMode:       Text.WordWrap
                        lineHeight:     1.4
                    }
                }
                PpCaption {
                    visible: tendencyRow.togetherCombos.length > 0
                    text:    qsTr("Your most common shots")
                }
                Repeater {
                    model: tendencyRow.togetherCombos
                    Text {
                        required property var modelData
                        objectName: "tendencyTogetherCombo"
                        width: togetherBlock.width
                        text:  String(modelData)
                        elide: Text.ElideRight
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight
                        color:          Theme.colorText
                    }
                }
                PpCaption {
                    objectName: "tendencyTogetherMore"
                    visible: text !== ""
                    width:   togetherBlock.width
                    text:    tendencyRow.together.moreCombos || ""
                }
            }
        }
    }

    // ── Header cells, in column order ───────────────────────────────────────
    Item { width: 1; height: 1 }                // the chevron's column

    Text {                                      // club
        id: clubCell
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

    FactorCell {
        id: strikeCell
        objectName: "tendencyStrike"
        width:    tendencyRow.columnWidth(3)
        label:    tendencyRow.factorLabel("strike")
        words:    tendencyRow.factorWords("strike")
        fault:    tendencyRow.fault("strike")
        building: tendencyRow.building("strike")
    }
    FactorCell {
        id: directionCell
        objectName: "tendencyDirection"
        width:    tendencyRow.columnWidth(4)
        label:    tendencyRow.factorLabel("direction")
        words:    tendencyRow.factorWords("direction")
        fault:    tendencyRow.fault("direction")
        building: tendencyRow.building("direction")
    }
    FactorCell {
        id: flightCell
        objectName: "tendencyFlight"
        width:    tendencyRow.columnWidth(5)
        label:    tendencyRow.factorLabel("flight")
        words:    tendencyRow.factorWords("flight")
        fault:    tendencyRow.fault("flight")
        building: tendencyRow.building("flight")
    }

    Text {                                      // work-on
        objectName: "tendencyWorkOn"
        width: tendencyRow.columnWidth(6)
        text:  tendencyRow.workOnText !== "" ? tendencyRow.workOnText : "—"
        elide: Text.ElideRight
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          tendencyRow.workOnText !== "" ? Theme.colorText : Theme.colorText3
    }
}
