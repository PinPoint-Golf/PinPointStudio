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
    readonly property int cardPad:   Theme.gap(20)
    readonly property int cardGap:   Theme.gap(16)
    readonly property int badgeSize: Theme.sp(20)
    readonly property int textIndent: badgeSize + Theme.sp(12)
    readonly property int headlineSize: Theme.fontSzBody
    readonly property int itemGap:   Theme.gap(18)

    width:   parent ? parent.width : 0
    spacing: 0

    function capitalised(s) { s = String(s); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s }

    // ════════════════════════════════════════════════════════════════════════
    // Pieces — shared with the other coaching cards (components/Pp*)
    // ════════════════════════════════════════════════════════════════════════

    readonly property int pipsWidth: root.pipCount * Theme.sp(6) + (root.pipCount - 1) * Theme.sp(4)

    // ════════════════════════════════════════════════════════════════════════
    // Layout
    // ════════════════════════════════════════════════════════════════════════

    Item {
        width:  root.width
        height: Theme.sp(34)

        PpMicro {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: Theme.caps(qsTr("Your swing"))
        }
        // The first reading of a golfer's sessions, said once and quietly. A recompute after
        // that leaves the summary standing and says nothing: it changes when it lands.
        PpMicro {
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

            PpCard {
                id: wellCard
                width:  row1.cardW
                height: root.stacked ? implicitHeight : row1.tallest
                tone:   root.toneWell
                title:  Theme.caps(qsTr("What you do well"))
                aside:  root.doWell.length > 0 ? Theme.caps(qsTr("Sessions")) : ""
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

                        PpBadge {
                            kind: "check"
                            tone: root.toneWell
                            y: Math.round((wellHead.lineCentre) - height / 2)
                        }
                        Column {
                            id: wellCol
                            x: root.textIndent
                            width: parent.width - root.textIndent
                            spacing: Theme.gap(4)
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
                                PpPips {
                                    anchors { right: parent.right; verticalCenter: wellCaption.verticalCenter }
                                    marks: root.latestPips(wellItem.modelData.sessions)
                                    tone:  root.toneWell
                                }
                            }
                        }
                    }
                }

                PpCardNote {
                    objectName: "doWellEmpty"
                    visible: root.doWell.length === 0
                    width:   wellCard.innerWidth
                    text:    qsTr("Nothing stands out here yet.")
                }
            }

            PpCard {
                id: workCard
                x: root.stacked ? 0 : row1.cardW + root.cardGap
                y: root.stacked ? wellCard.implicitHeight + root.cardGap : 0
                width:  row1.cardW
                height: root.stacked ? implicitHeight : row1.tallest
                tone:   root.toneWork
                title:  Theme.caps(qsTr("Next on your list"))
                aside:  root.needsWork.length > 0 ? Theme.caps(qsTr("Sessions")) : ""
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

                        PpBadge {
                            kind: "target"
                            tone: root.toneWork
                            y: Math.round(workHead.lineCentre - height / 2)
                        }
                        Column {
                            id: workCol
                            x: root.textIndent
                            width: parent.width - root.textIndent
                            spacing: Theme.gap(4)
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
                                PpChip {
                                    id: nextChip
                                    objectName: "nextChip"
                                    text:     Theme.caps(qsTr("Next"))
                                    tone:     root.toneWork
                                    tracking: Theme.trackingMicro
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
                                PpChip {
                                    id: workChip
                                    x: meterRow.chipFits ? freqWords.x + freqWords.width + Theme.sp(8)
                                                         : meterRow.width - width
                                    y: meterRow.chipFits ? Math.round((meterRow.height - height) / 2)
                                                         : -meterRow.y + Math.round(workHead.lineCentre - height / 2)
                                    trend: workItem.modelData.trend
                                }
                                PpMeter {
                                    id: meter
                                    anchors.verticalCenter: parent.verticalCenter
                                    share: workItem.modelData.share
                                    tone:  root.toneWork
                                }
                                Text {
                                    id: freqWords
                                    anchors { left: meter.right; leftMargin: Theme.gap(10)
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
                                PpPips {
                                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                    marks: root.latestPips(workItem.modelData.sessions)
                                    tone:  root.toneWork
                                }
                            }
                        }
                    }
                }

                PpCardNote {
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
        // The words are the link; the press below spans the row's height, and is what a probe clicks.
        PpLink {
            id: linkText
            y: Theme.sp(8)
            text: qsTr("Swing diagnostics →")
            font.pixelSize: Theme.fontSzBody
            interactive: false
            hovered:     linkPress.containsMouse
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
