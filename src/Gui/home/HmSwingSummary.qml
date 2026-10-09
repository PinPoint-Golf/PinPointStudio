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

// YOUR SWING — the current athlete's swing as a short coaching page, on the home screen
// (docs/design/home_themes_design.md). Drawn for a typical golfer, to be LOOKED at before it is
// read, with one thing to the fore:
//
//   Your focus         HmFocus: the one drill to practise, the ideal it aims at, the faults it
//                      covers, why it matters, how to practise it, and why it was chosen
//   What you do well   green card: a tick, the phrase, what backs it, one pip per session
//   Next on your list  amber card, quieter than the focus: what comes after it, in order, the
//                      first marked NEXT; each with the ten-step "how often" meter, a trend chip
//                      when there is one, one pip per session that saw it
//   Swing diagnostics  one link row into the technical layer (ScreenSwingDiagnostics): every
//                      fault, how it was measured, and what goes together
//
// No metrics, no figures, no P-positions, no claims about cause — a golfer reads this. Colour is
// never the only channel: the meter carries a count of segments, and each has words beside it.
//
// Every string is WorkOnsController's (swingSummaryView over the pack's golfer phrases):
//   summaryReady, summaryUpdating, updating, athleteDir, summarySubtitle
//   focusItem      see HmFocus.qml
//   doWellItems    [{ text, caption, sessions: [bool…] }]
//   nextItems      [{ text, share 0..1, frequency, trend -1|0|1, sessions: [bool…] }]
// This file positions and paints. ScreenHome hides it when there is no athlete and takes the link
// to the Swing diagnostics screen; before the first summary lands it says it is reading, and with
// no session to read it says "Nothing yet".
//
// The pips are the LATEST maxPips judged sessions, oldest first, so a long history keeps the row
// its width; the "SESSIONS" aside heads them.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Column {
    id: root
    objectName: "swingSummary"

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: (typeof workOns !== "undefined") ? workOns : null

    // The link row: open the Swing diagnostics screen.
    signal diagnosticsRequested()

    readonly property var    doWell:    controller ? controller.doWellItems    : []
    readonly property var    needsWork: controller ? controller.nextItems      : []
    readonly property string subtitle:  controller ? controller.summarySubtitle : ""
    readonly property bool   ready:     controller ? controller.summaryReady    : false
    readonly property bool   updating:  controller ? controller.summaryUpdating : false
    // The work-ons catch-up comes first; the summary is read off what it leaves current.
    readonly property bool   catchingUp: controller ? controller.updating       : false

    // At most this many pips in a row: the latest sessions.
    readonly property int maxPips: 8
    function latestPips(marks) {
        const m = marks || []
        return m.length > root.maxPips ? m.slice(m.length - root.maxPips) : m
    }
    // The widest row of pips over both cards, so their pips share a right edge and a heading.
    readonly property int pipCount: {
        let n = 1
        for (const it of root.doWell)    n = Math.max(n, Math.min(root.maxPips, (it.sessions || []).length))
        for (const it of root.needsWork) n = Math.max(n, Math.min(root.maxPips, (it.sessions || []).length))
        return n
    }

    // Two cards side by side down to this width; below it they stack.
    readonly property bool stacked: width < Theme.sp(640)

    // ── Palette for the three cards ──────────────────────────────────────────
    readonly property color toneWell:     Theme.colorGood
    readonly property color toneWork:     Theme.colorAccent      // the app's own "your focus" amber

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

    // One pip per session: filled = seen (or clean, for "do well") that session.
    component Pips: Row {
        id: pips
        property var   marks: []
        property color tone: Theme.colorText3
        spacing: Theme.sp(4)
        Repeater {
            model: pips.marks
            Rectangle {
                required property var modelData
                width: Theme.sp(6); height: width; radius: width / 2
                color:        modelData ? pips.tone : "transparent"
                border.width: modelData ? 0 : 1
                border.color: Theme.colorBorderStrong
            }
        }
    }
    readonly property int pipsWidth: root.pipCount * Theme.sp(6) + (root.pipCount - 1) * Theme.sp(4)

    // Leading badges, one per card, so the two columns share a left edge and a rhythm.
    component CheckBadge: Rectangle {
        id: badge
        readonly property int size: Theme.sp(20)
        property color tone: Theme.colorGood
        width: badge.size; height: width; radius: width / 2
        color: Qt.alpha(badge.tone, Theme.dark ? 0.16 : 0.12)
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: badge.tone; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.29; startY: badge.size * 0.52
                PathLine { x: badge.size * 0.44; y: badge.size * 0.66 }
                PathLine { x: badge.size * 0.72; y: badge.size * 0.36 }
            }
        }
    }
    component AimBadge: Rectangle {
        id: badge
        readonly property int size: Theme.sp(20)
        property color tone: Theme.colorAccent
        width: badge.size; height: width; radius: width / 2
        color: Qt.alpha(badge.tone, Theme.dark ? 0.16 : 0.12)
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(badge.size * 0.56); height: width; radius: width / 2
            color: "transparent"; border.width: Math.max(1.5, Theme.sp(1.4)); border.color: badge.tone
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.round(badge.size * 0.2); height: width; radius: width / 2
            color: badge.tone
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

    // How often, as ten rounded segments.
    component FrequencyMeter: Row {
        id: meter
        property real  share: 0
        property color tone: Theme.colorAccent
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

    // "NEXT": the first thing on the list after the focus. A pill in the tone, tiny mono.
    component NextChip: Rectangle {
        id: chip
        width:  chipText.implicitWidth + Theme.sp(12)
        height: Theme.sp(18)
        radius: height / 2
        color:  Qt.alpha(root.toneWork, Theme.dark ? 0.12 : 0.09)
        border.width: 1
        border.color: Qt.alpha(root.toneWork, 0.45)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: qsTr("NEXT")
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              root.toneWork
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

    Item {
        width:  root.width
        height: Theme.sp(34)

        Micro {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("YOUR SWING")
        }
        // The first reading of a golfer's sessions, said once and quietly. A recompute after
        // that leaves the summary standing and says nothing: it changes when it lands.
        Micro {
            objectName: "swingSummaryUpdating"
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            visible: root.updating && !root.ready
            text: qsTr("reading your sessions…")
            font.letterSpacing: Theme.trackingData
        }
    }
    Text {
        objectName: "swingSummarySubtitle"
        visible: root.ready && root.subtitle !== ""
        width:   root.width
        text:    root.subtitle
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        font.italic:    true
        color:          Theme.colorText3
    }
    Item { width: 1; height: Theme.sp(16) }

    // Nothing to read yet: no session with a swing in it. Not said before the controller has
    // been pointed at the athlete (the first pass waits out the launch), when it is not yet known.
    Item {
        visible: !root.ready && !root.updating && !root.catchingUp
                 && root.controller !== null && root.controller.athleteDir !== ""
        height:  visible ? 40 : 0
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

            Card {
                id: wellCard
                width:  row1.cardW
                height: root.stacked ? implicitHeight : row1.tallest
                tone:   root.toneWell
                title:  qsTr("WHAT YOU DO WELL")
                aside:  root.doWell.length > 0 ? qsTr("SESSIONS") : ""
                asideWidth: root.pipsWidth

                Repeater {
                    model: root.doWell
                    Item {
                        id: wellItem
                        objectName: "doWellItem"
                        required property var modelData
                        // The words as drawn — what a probe reads.
                        readonly property string itemText:    wellItem.modelData.text
                        readonly property string captionText: wellItem.modelData.caption
                        readonly property string plainText:   itemText + " — " + captionText
                        readonly property int    pipCount:    (wellItem.modelData.sessions || []).length
                        width:  wellCard.innerWidth
                        height: wellCol.implicitHeight

                        CheckBadge {
                            tone: root.toneWell
                            y: Math.round((wellHead.lineCentre) - height / 2)
                        }
                        Column {
                            id: wellCol
                            x: root.textIndent
                            width: parent.width - root.textIndent
                            spacing: Theme.sp(4)
                            Text {
                                id: wellHead
                                readonly property real lineCentre: (font.pixelSize * 1.3) / 2
                                width: parent.width
                                text:  root.capitalised(wellItem.modelData.text)
                                font.family:    Theme.fontBody
                                font.pixelSize: root.headlineSize
                                font.weight:    Theme.fontBodyWeight
                                color:          Theme.colorText
                                wrapMode:       Text.WordWrap
                                lineHeight:     1.3
                            }
                            Item {
                                width:  parent.width
                                height: Math.max(wellCaption.implicitHeight, Theme.sp(18))
                                Text {
                                    id: wellCaption
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - root.pipsWidth - Theme.sp(12)
                                    text:  wellItem.modelData.caption
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText3
                                    wrapMode:       Text.WordWrap
                                }
                                Pips {
                                    anchors { right: parent.right; verticalCenter: wellCaption.verticalCenter }
                                    marks: root.latestPips(wellItem.modelData.sessions)
                                    tone:  root.toneWell
                                }
                            }
                        }
                    }
                }

                CardNote {
                    objectName: "doWellEmpty"
                    visible: root.doWell.length === 0
                    width:   wellCard.innerWidth
                    text:    qsTr("Nothing stands out here yet.")
                }
            }

            Card {
                id: workCard
                x: root.stacked ? 0 : row1.cardW + root.cardGap
                y: root.stacked ? wellCard.implicitHeight + root.cardGap : 0
                width:  row1.cardW
                height: root.stacked ? implicitHeight : row1.tallest
                tone:   root.toneWork
                title:  qsTr("NEXT ON YOUR LIST")
                aside:  root.needsWork.length > 0 ? qsTr("SESSIONS") : ""
                asideWidth: root.pipsWidth

                Repeater {
                    model: root.needsWork
                    Item {
                        id: workItem
                        objectName: "nextItem"
                        required property var modelData
                        required property int index
                        // The words as drawn — what a probe reads.
                        readonly property string itemText:      workItem.modelData.text
                        readonly property string frequencyText: workItem.modelData.frequency
                        readonly property string plainText:     itemText + " — " + frequencyText
                        readonly property int    pipCount:      (workItem.modelData.sessions || []).length
                        width:  workCard.innerWidth
                        height: workCol.implicitHeight

                        AimBadge {
                            tone: root.toneWork
                            y: Math.round(workHead.lineCentre - height / 2)
                        }
                        Column {
                            id: workCol
                            x: root.textIndent
                            width: parent.width - root.textIndent
                            spacing: Theme.sp(4)
                            Text {
                                id: workHead
                                readonly property real lineCentre: (font.pixelSize * 1.3) / 2
                                // The first item wears NEXT just after its words; the line gives
                                // it room.
                                readonly property real nextRoom: workItem.index === 0 ? nextChip.width + Theme.sp(10) : 0
                                width: Math.min(implicitWidth,
                                                parent.width - nextRoom
                                                - (workChip.visible && !meterRow.chipFits ? workChip.width + Theme.sp(12) : 0))
                                text:  root.capitalised(workItem.modelData.text)
                                font.family:    Theme.fontBody
                                font.pixelSize: root.headlineSize
                                font.weight:    Theme.fontBodyWeight
                                color:          Theme.colorText
                                wrapMode:       Text.WordWrap
                                lineHeight:     1.3
                                NextChip {
                                    id: nextChip
                                    objectName: "nextChip"
                                    visible: workItem.index === 0
                                    x: workHead.contentWidth + Theme.sp(10)
                                    y: Math.round(workHead.lineCentre - height / 2)
                                }
                            }
                            Item {
                                id: meterRow
                                width:  parent.width
                                height: Math.max(freqWords.implicitHeight, Theme.sp(18))
                                // The trend follows the frequency words; where the row has no room
                                // for it, it moves up to the headline's line, at the right edge.
                                readonly property bool chipFits:
                                    meter.width + freqWords.implicitWidth + workChip.width
                                    + Theme.sp(10) + Theme.sp(8) + Theme.sp(8) + root.pipsWidth <= width
                                TrendChip {
                                    id: workChip
                                    x: meterRow.chipFits ? freqWords.x + freqWords.width + Theme.sp(8)
                                                         : meterRow.width - width
                                    y: meterRow.chipFits ? Math.round((meterRow.height - height) / 2)
                                                         : -meterRow.y + Math.round(workHead.lineCentre - height / 2)
                                    trend: workItem.modelData.trend
                                }
                                FrequencyMeter {
                                    id: meter
                                    anchors.verticalCenter: parent.verticalCenter
                                    share: workItem.modelData.share
                                    tone:  root.toneWork
                                }
                                Text {
                                    id: freqWords
                                    anchors { left: meter.right; leftMargin: Theme.sp(10)
                                              verticalCenter: parent.verticalCenter }
                                    // Never under the pips, on a narrow card.
                                    width: Math.min(implicitWidth, meterRow.width - meter.width - Theme.sp(10)
                                                                   - root.pipsWidth - Theme.sp(8))
                                    elide: Text.ElideRight
                                    text: workItem.modelData.frequency
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    font.weight:    Theme.fontBodyWeight
                                    color:          Theme.colorText3
                                }
                                Pips {
                                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                    marks: root.latestPips(workItem.modelData.sessions)
                                    tone:  root.toneWork
                                }
                            }
                        }
                    }
                }

                CardNote {
                    objectName: "needsWorkEmpty"
                    visible: root.needsWork.length === 0
                    width:   workCard.innerWidth
                    text:    qsTr("Nothing else shows up on most of your swings.")
                }
            }
        }
    }

    // ── Into the technical layer ─────────────────────────────────────────
    Item { width: 1; height: Theme.sp(20); visible: diagLink.visible }
    Item {
        id: diagLink
        objectName: "swingDiagnosticsLink"
        // Shown whenever there is an athlete, not only once the summary lands: the faults list
        // must be reachable during the first catch-up too.
        visible: root.controller !== null && root.controller.athleteDir !== ""
        width:  root.width
        // One line where the words fit beside the link, two where they do not.
        readonly property bool beside: linkText.implicitWidth + Theme.sp(16) + linkWords.implicitWidth <= width
        height: Theme.sp(8) + (beside ? Math.max(linkText.implicitHeight, linkWords.implicitHeight)
                                      : linkWords.y + linkWords.implicitHeight - Theme.sp(8))
                + Theme.sp(8)
        Text {
            id: linkText
            y: Theme.sp(8)
            text: qsTr("Swing diagnostics →")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            color:          linkPress.containsMouse ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
        }
        Text {
            id: linkWords
            x: diagLink.beside ? linkText.implicitWidth + Theme.sp(16) : 0
            y: diagLink.beside ? Theme.sp(8) + linkText.baselineOffset - baselineOffset
                               : linkText.y + linkText.implicitHeight + Theme.sp(4)
            width: diagLink.beside ? implicitWidth : root.width
            text: qsTr("Every fault, how it was measured, and what moves together — for you and your coach.")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText3
            wrapMode:       Text.WordWrap
        }
        MouseArea {
            id: linkPress
            objectName: "diagnosticsLink"
            width: linkText.implicitWidth; height: parent.height
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked:    root.diagnosticsRequested()
        }
    }
}
