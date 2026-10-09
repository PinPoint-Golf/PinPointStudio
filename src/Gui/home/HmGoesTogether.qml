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

// WHAT GOES TOGETHER — the swing themes (docs/design/home_themes_design.md), in the Swing
// diagnostics screen (ScreenSwingDiagnostics.qml) under the faults: one card in YOUR SWING's family,
// a row per pair: a signal-strength mark for how firmly it holds, the two halves joined by a
// connector, and a six-stop swing timeline showing where the pair first shows. It lived on the home
// screen until the home became focus-led; what moves together is for the golfer who wants the
// detail, and for their coach.
//
// Every string is WorkOnsController's (swingSummaryView):
//   togetherItems  [{ tier "firm"|"probably"|"possibly", first ("When …", or the one part),
//                     second ("" for one part), startStop 0..5 (−1 unplaced: no timeline),
//                     startWords, trend -1|0|1 }]
//   togetherNote   "" or why nothing can be said yet

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Column {
    id: root
    objectName: "goesTogether"

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null

    readonly property var    together:  controller ? controller.togetherItems  : []
    readonly property string note:      controller ? controller.togetherNote   : ""

    // ── Palette ──────────────────────────────────────────────────────────────
    readonly property color toneTogether: Theme.gradientCool     // a relation, neither praise nor fault

    // ── Shared geometry ──────────────────────────────────────────────────────
    readonly property int cardPad:   Theme.sp(20)
    readonly property int cardGap:   Theme.sp(16)
    readonly property int badgeSize: Theme.sp(20)
    readonly property int textIndent: badgeSize + Theme.sp(12)
    readonly property int headlineSize: Theme.fontSzBody
    readonly property int itemGap:   Theme.sp(18)

    width:   parent ? parent.width : 0
    spacing: 0

    function capitalised(s) { s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

    // ════════════════════════════════════════════════════════════════════════
    // Pieces
    // ════════════════════════════════════════════════════════════════════════

    // A micro heading — the app's mono caps.
    component Micro: Text {
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }

    // A card: surface, hairline border, a 3 px rule in the card's tone along the top edge that
    // follows the rounded corners, and a heading row (title left, an aside right).
    component Card: Item {
        id: card
        property color  tone: Theme.colorAccent
        property string title: ""
        property string aside: ""
        readonly property int pad: Theme.sp(20)
        property int    itemGap: Theme.sp(18)
        property int    asideWidth: 0               // right-align the aside over a column this wide
        default property alias content: body.data
        readonly property int innerWidth: width - 2 * card.pad
        implicitHeight: body.y + body.implicitHeight + card.pad

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
            color:  card.tone
        }
        Rectangle {
            x: 1; y: 3
            width: parent.width - 2; height: Theme.radiusLg * 2
            color: Theme.colorSurface
        }

        Micro {
            id: cardTitle
            x: card.pad; y: card.pad + Theme.sp(2)
            text:  card.title
            color: card.tone
        }
        Micro {
            visible: card.aside !== ""
            anchors.baseline: cardTitle.baseline
            x: card.width - card.pad - Math.max(implicitWidth, card.asideWidth)
               + (card.asideWidth > implicitWidth ? card.asideWidth - implicitWidth : 0)
            text: card.aside
            font.letterSpacing: Theme.trackingData
        }

        Column {
            id: body
            x: card.pad
            y: cardTitle.y + cardTitle.implicitHeight + Theme.sp(18)
            width: card.innerWidth
            spacing: card.itemGap
        }
    }

    // "↑ growing" / "↓ easing": a pill, tiny mono. Nothing when flat.
    component TrendChip: Rectangle {
        id: chip
        property int trend: 0
        readonly property color tone: trend > 0 ? Theme.colorWarn : Theme.colorGood
        visible: trend !== 0
        width:  visible ? chipText.implicitWidth + Theme.sp(12) : 0
        height: Theme.sp(18)
        radius: height / 2
        color:  Qt.alpha(chip.tone, Theme.dark ? 0.12 : 0.09)
        border.width: 1
        border.color: Qt.alpha(chip.tone, 0.45)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.trend > 0 ? qsTr("↑ growing") : qsTr("↓ easing")
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              chip.tone
        }
    }

    // How firmly a pair holds, as a signal-strength mark: 3 / 2 / 1 bars, a word under it.
    component Strength: Column {
        id: strength
        property string tier: "possibly"
        property color  tone: Theme.gradientCool
        readonly property int bars: tier === "firm" ? 3 : tier === "probably" ? 2 : 1
        spacing: Theme.sp(6)
        Row {
            spacing: Theme.sp(3)
            Repeater {
                model: 3
                Item {
                    required property int index
                    width: Theme.sp(5); height: Theme.sp(18)
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; height: Theme.sp(8) + index * Theme.sp(5)
                        radius: Theme.sp(1.5)
                        color: index < strength.bars ? strength.tone : Theme.colorBorderStrong
                    }
                }
            }
        }
        Micro {
            text: strength.tier === "firm" ? qsTr("CLEAR")
                : strength.tier === "probably" ? qsTr("LIKELY") : qsTr("POSSIBLE")
            color: Theme.colorText2
            font.letterSpacing: Theme.trackingData
        }
    }

    // Six stops through the swing; where the pair first shows is marked and the track runs on
    // from there in the tone.
    component SwingTimeline: Item {
        id: tl
        property int    start: 0            // 0 Address … 5 Finish
        property color  tone: Theme.gradientCool
        readonly property var stops: [qsTr("ADDRESS"), qsTr("BACK"), qsTr("TOP"),
                                      qsTr("DOWN"), qsTr("IMPACT"), qsTr("FINISH")]
        TextMetrics { id: firstLabel; text: tl.stops[0]; font.family: Theme.fontData
                      font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData }
        TextMetrics { id: lastLabel; text: tl.stops[5]; font.family: Theme.fontData
                      font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData }
        // First label's left edge on the item's left edge; last label's right edge on its right.
        readonly property real inset: Math.ceil(firstLabel.advanceWidth / 2)
        readonly property real step: (width - inset - Math.ceil(lastLabel.advanceWidth / 2)) / 5
        function stopX(i) { return inset + i * step }
        readonly property int trackY: Theme.sp(7)
        implicitHeight: trackY + Theme.sp(10) + Theme.fontSzMicro + Theme.sp(4)

        // Base track, then the tone from the start stop on.
        Rectangle {
            x: tl.stopX(0); y: tl.trackY - height / 2
            width: tl.stopX(5) - tl.stopX(0); height: 1
            color: Theme.colorBorderStrong
        }
        Rectangle {
            x: tl.stopX(tl.start); y: tl.trackY - height / 2
            width: tl.stopX(5) - tl.stopX(tl.start); height: Theme.sp(2); radius: height / 2
            color: Qt.alpha(tl.tone, 0.7)
        }
        Repeater {
            model: 6
            Item {
                id: stop
                required property int index
                readonly property bool isStart: index === tl.start
                readonly property bool after:   index > tl.start
                x: tl.stopX(index); y: tl.trackY

                Rectangle {        // halo on the start stop
                    visible: stop.isStart
                    width: Theme.sp(16); height: width; radius: width / 2
                    x: -width / 2; y: -height / 2
                    color: Qt.alpha(tl.tone, 0.22)
                }
                Rectangle {
                    readonly property int d: stop.isStart ? Theme.sp(9) : Theme.sp(5)
                    width: d; height: d; radius: d / 2
                    x: -d / 2; y: -d / 2
                    color: stop.isStart || stop.after ? tl.tone : Theme.colorSurface
                    border.width: stop.isStart || stop.after ? 0 : 1
                    border.color: Theme.colorBorderStrong
                }
                Micro {
                    x: -implicitWidth / 2
                    y: Theme.sp(10)
                    text: tl.stops[stop.index]
                    font.letterSpacing: Theme.trackingData
                    color: stop.isStart ? Theme.colorText : Theme.colorText3
                }
            }
        }
    }

    // A card with nothing in it says so in one quiet line, so the pair keeps its shape.
    component CardNote: Text {
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText3
        wrapMode:       Text.WordWrap
        lineHeight:     1.3
    }

    // ════════════════════════════════════════════════════════════════════════
    // Layout
    // ════════════════════════════════════════════════════════════════════════

    Column {
        width:   root.width
        spacing: root.cardGap

        Card {
            id: togetherCard
            width: root.width
            height: implicitHeight
            tone:  root.toneTogether
            title: qsTr("WHAT GOES TOGETHER")

            Text {
                width: togetherCard.innerWidth
                visible: root.together.length > 0
                text:  qsTr("Things that rise and fall together from one swing to the next.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                font.italic:    true
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }

            Repeater {
                model: root.together
                Item {
                    id: pair
                    objectName: "togetherItem"
                    required property var modelData
                    required property int index
                    // The words as drawn — what a probe reads.
                    readonly property string tier:           pair.modelData.tier
                    readonly property string firstText:      pair.modelData.first
                    readonly property string secondText:     pair.modelData.second
                    readonly property string startWordsText: pair.modelData.startWords
                    readonly property string plainText:
                        [firstText, secondText, startWordsText].filter(s => s !== "").join(" — ")
                    readonly property bool hasSecond: secondText !== ""
                    readonly property bool placed:    pair.modelData.startStop >= 0
                    readonly property int strengthW: Theme.sp(68)
                    readonly property int linkIndent: Theme.sp(18)
                    readonly property int chipRoom: chip.visible ? chip.width + Theme.sp(16) : 0
                    width:  togetherCard.innerWidth
                    height: topRule.height + Theme.sp(16) + pairBody.implicitHeight

                    Rectangle {     // hairline between pairs (and under the intro)
                        id: topRule
                        width: parent.width; height: 1
                        color: Theme.colorBorder
                    }

                    Strength {
                        x: 0
                        y: topRule.height + Theme.sp(16) + Math.round(firstLine.lineCentre) - Theme.sp(9)
                        tier: pair.modelData.tier
                        tone: root.toneTogether
                    }

                    Item {
                        id: pairBody
                        x: pair.strengthW
                        y: topRule.height + Theme.sp(16)
                        width: parent.width - pair.strengthW
                        // The words, then the timeline (with its words beside it or beneath).
                        readonly property real wordsBottom: pair.hasSecond ? secondLine.y + secondLine.height
                                                                           : firstLine.height
                        implicitHeight: !pair.placed       ? wordsBottom
                                      : pairBody.wordsBeside ? timeline.y + timeline.implicitHeight
                                      :                        startWords.y + startWords.height

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

                        // "When …" is the controller's, as is the one part said alone.
                        Text {
                            id: firstLine
                            readonly property real lineCentre: (font.pixelSize * 1.3) / 2
                            x: pair.linkIndent
                            width: parent.width - pair.linkIndent - pair.chipRoom
                            text:  pair.modelData.first
                            font.family:    Theme.fontBody
                            font.pixelSize: root.headlineSize
                            font.weight:    Theme.fontBodyWeight
                            color:          Theme.colorText
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.3
                        }
                        Text {
                            id: secondLine
                            readonly property real lineCentre: (font.pixelSize * 1.3) / 2
                            visible: pair.hasSecond
                            x: pair.linkIndent
                            y: firstLine.height + Theme.sp(6)
                            width: parent.width - pair.linkIndent - pair.chipRoom
                            text:  pair.modelData.second
                            font.family:    Theme.fontBody
                            font.pixelSize: root.headlineSize
                            font.weight:    Theme.fontBodyWeight
                            color:          Theme.colorText
                            wrapMode:       Text.WordWrap
                            lineHeight:     1.3
                        }
                        TrendChip {
                            id: chip
                            anchors.right: parent.right
                            y: Math.round(firstLine.lineCentre - height / 2)
                            trend: pair.modelData.trend
                        }

                        // Timeline, with its words beside it where there is room, else beneath.
                        // A theme with no swing position has neither.
                        readonly property int timelineW: Math.min(Theme.sp(400), width - pair.linkIndent)
                        readonly property bool wordsBeside:
                            width - pair.linkIndent - timelineW - Theme.sp(24) >= startWords.implicitWidth
                        SwingTimeline {
                            id: timeline
                            visible: pair.placed
                            x: pair.linkIndent
                            y: pairBody.wordsBottom + Theme.sp(14)
                            width: pairBody.timelineW
                            start: Math.max(0, pair.modelData.startStop)
                            tone:  root.toneTogether
                        }
                        Text {
                            id: startWords
                            visible: pair.placed
                            x: pairBody.wordsBeside ? timeline.x + timeline.width + Theme.sp(8)
                                                    : pair.linkIndent
                            y: pairBody.wordsBeside ? timeline.y + timeline.trackY - height / 2
                                                    : timeline.y + timeline.implicitHeight + Theme.sp(6)
                            text: pair.modelData.startWords
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Theme.fontBodyWeight
                            font.italic:    true
                            color:          Theme.colorText3
                        }
                    }
                }
            }

            // Nothing that holds yet: said calmly, in the card.
            Text {
                objectName: "togetherNote"
                visible: root.together.length === 0
                width:   togetherCard.innerWidth
                text:    root.note !== "" ? root.note
                                          : qsTr("Nothing yet. This fills in as your sessions build up.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText2
                wrapMode:       Text.WordWrap
                lineHeight:     1.3
            }
        }
    }
}
