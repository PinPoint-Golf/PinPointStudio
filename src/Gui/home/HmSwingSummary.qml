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

// COACHING — the golfer's swing organised by swing stage (coaching_plan.h), built from the same
// habit pool as Home's "Your swing" tab so every recurring habit (and every area's Biggest fault
// when it is a habit) is accounted for here in sequence:
//
//   Before you swing      static Setup checks (set before motion begins — never a swing thought,
//                         shown only when the golfer has Setup habits)
//   Your focus            HmFocus: ONE dynamic swing thought to practise, with aim, current rate,
//                         why it matters, how to practise, and Practice button
//   Your swing by stage   the in-motion swing in order (Backswing & top, Downswing & impact,
//                         Finish), ranked by Wilson lower bound inside each stage, with a Focus
//                         tag on the active focus items and gentle upstream notes when an earlier
//                         active habit feeds a later one
//
// Every fault row opens its full detail in place (HmFaultRow -> HmFaultDetail), one at a time.
// Golfer words only (DC-9, design_system/README.md): no P-codes, no raw metrics, no causal claims.

import QtQuick
import PinPointStudio
import "hm_text.js" as HomeText

Column {
    id: root
    objectName: "swingSummary"

    // The controller. Passed in rather than reached for, so a harness can drive the component.
    property var controller: ((typeof app !== "undefined" && !!app.workOns)) ? app.workOns : null

    // The focus card's Practice.
    signal practiseRequested(string conditionId, string label)
    // An open fault's "Review the … session →", here or on the focus card.
    signal reviewSessionRequested(string sessionDir)

    // The fault open in place across Before you swing and Your swing by stage ("" when none).
    property string openId: ""
    // Stage keys expanded past stageLimit ("backswingTop": true, …), for this sitting only.
    property var expandedStages: ({})

    readonly property int stageLimit: 3

    readonly property var    plan:        controller && controller.coachingPlan !== undefined
                                          ? controller.coachingPlan : null
    readonly property bool   hasPlan:     plan !== null
    readonly property bool   ready:       hasPlan ? !!plan.ready : (controller ? controller.summaryReady : false)
    readonly property bool   updating:    controller ? (controller.updating || controller.summaryUpdating) : false
    readonly property bool   catchingUp:  controller ? controller.updating : false
    readonly property var    setupChecks: hasPlan ? (plan.setupChecks || []) : []
    readonly property var    stages:      hasPlan ? (plan.stages || []) : []
    readonly property bool   hasFocus:    hasPlan
                                          ? !!(plan.focus && plan.focus.present === true)
                                          : !!(controller && controller.focusItem && controller.focusItem.present === true)
    readonly property bool   hasStageHabits: {
        for (const s of root.stages) if (s && s.habitCount > 0) return true
        return false
    }
    readonly property bool   hasAnyHabit: root.hasFocus || root.setupChecks.length > 0 || root.hasStageHabits
    readonly property string tooFewNote:  controller && controller.outcomeTooFewNote !== undefined
                                          ? (controller.outcomeTooFewNote || "") : ""
    // Under a shot filter with too few measured shots and no habits, show the too-few notice rather
    // than "No repeating habits" so unmeasured shots never read as clean.
    readonly property bool   showTooFewOnly: root.tooFewNote !== "" && !root.hasAnyHabit

    // ── Tones ────────────────────────────────────────────────────────────────
    readonly property color toneWork: Theme.colorAttention

    // ── Shared geometry ──────────────────────────────────────────────────────
    readonly property int cardGap: Theme.gap(16)
    readonly property int markW:   Theme.sp(20)
    // Full-width table template: mark · words · how often (meter + words + trend) · sessions · chevron.
    readonly property var columns: [
        { width: root.markW },
        { fill: true },
        { width: Theme.sp(260) },
        { width: Theme.sp(80), align: "right" },
        { width: Theme.sp(10) }]

    function stageExpanded(key) { return !!root.expandedStages[key] }
    function toggleStage(key) {
        const next = Object.assign({}, root.expandedStages)
        next[key] = !next[key]
        root.expandedStages = next
    }
    function visibleItemsFor(stage) {
        const all = stage && stage.items ? stage.items : []
        if (all.length <= root.stageLimit || root.stageExpanded(stage.key)) return all
        return all.slice(0, root.stageLimit)
    }

    width:   parent ? parent.width : 0
    spacing: 0

    // ════════════════════════════════════════════════════════════════════════
    // Pieces
    // ════════════════════════════════════════════════════════════════════════

    component CardHeader: PpTableRow {
        id: head
        property string title:    ""
        property string subtitle: ""
        property color  tone:     Theme.colorText
        property bool   captions: true
        height: Theme.sp(Theme.tableHeaderH)
        hoverEnabled: false
        Item { width: 1; height: 1 }        // the mark's column
        Row {
            width: head.columnWidth(1)
            spacing: Theme.gap(10)
            Text {
                id: headTitle
                anchors.verticalCenter: parent.verticalCenter
                text:  head.title
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.DemiBold
                color:          head.tone
            }
            PpCaption {
                visible: head.subtitle !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - headTitle.implicitWidth - parent.spacing)
                text:  head.subtitle
            }
        }
        PpCaption { visible: head.captions; text: qsTr("How often"); width: Math.min(implicitWidth, head.columnWidth(2)) }
        PpCaption { visible: head.captions; text: qsTr("Sessions");  width: Math.min(implicitWidth, head.columnWidth(3)) }
        Item { width: 1; height: 1 }        // the chevron's column
    }

    // One fault row used by both "Before you swing" and "Your swing by stage". Opens HmFaultDetail in place.
    component CoachingRow: HmFaultRow {
        id: row
        required property var modelData
        property bool showFocusChip: false
        // The words as drawn — what a probe reads.
        readonly property string itemText:      HomeText.capitalised(row.modelData.text)
        readonly property string frequencyText: row.modelData.frequency || ""
        readonly property string plainText:     itemText + " — " + frequencyText
        readonly property int    pipCount:      (row.modelData.sessions || []).length
        readonly property bool   isFocus:       row.showFocusChip && !!row.modelData.isFocus

        controller:  root.controller
        conditionId: row.modelData.conditionId || ""
        openId:      root.openId
        onOpenRequested: (next) => root.openId = next
        onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
        secondLine: true
        topRule: true
        rowHeight: Math.max(Theme.sp(Theme.rowH2),
                            Math.max(rowWords.implicitHeight, rowFreq.implicitHeight) + Theme.gap(18))
        accessibleName: row.itemText

        PpBadge {
            kind: "ring"
            plain: true
            tone: row.isFocus ? Theme.colorAccent : root.toneWork
            size: root.markW
        }
        Column {
            id: rowWords
            width: row.columnWidth(1)
            spacing: Theme.gap(2)
            Item {
                width:  parent.width
                height: Math.max(rowHead.height, focusChip.visible ? focusChip.height : 0)
                HmHeadline {
                    id: rowHead
                    width: parent.width - (focusChip.visible ? focusChip.width + Theme.gap(10) : 0)
                    text:  row.itemText
                }
                PpChip {
                    id: focusChip
                    objectName: "stageFocusChip"
                    visible:  row.isFocus
                    anchors.right: parent.right
                    y: Math.round((rowHead.lineCount > 1 ? rowHead.height / rowHead.lineCount
                                                         : rowHead.height) / 2 - height / 2)
                    text:     Theme.caps(qsTr("Focus"))
                    tone:     Theme.colorAccent
                    tracking: Theme.trackingMicro
                }
            }
            // Gentle upstream note when an earlier active habit is linked in the model.
            PpCaption {
                objectName: "stageUpstreamNote"
                visible:  text !== ""
                width:    parent.width
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     row.modelData.upstreamNote || ""
            }
            // What it does on the ball, only when a shot has shown it.
            PpCaption {
                objectName: "homeOnTheBall"
                visible:  text !== ""
                width:    parent.width
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     row.modelData.onTheBall || ""
            }
            // Under one outcome: how often on those shots against the others, only then.
            PpCaption {
                objectName: "homeOutcomeContrast"
                visible:  text !== ""
                width:    parent.width
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     row.modelData.outcomeContrast || ""
            }
        }
        Row {
            id: rowFreq
            width: row.columnWidth(2)
            spacing: Theme.gap(10)
            PpMeter {
                id: rowMeter
                anchors.verticalCenter: parent.verticalCenter
                share: row.modelData.share !== undefined ? row.modelData.share : 0
                tone:  row.isFocus ? Theme.colorAccent : root.toneWork
            }
            PpCaption {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(0, parent.width - rowMeter.width - parent.spacing
                                                        - (rowTrend.visible ? rowTrend.width + parent.spacing : 0)))
                elide:    Text.ElideNone
                wrapMode: Text.WordWrap
                text:     row.frequencyText
                color:    Theme.colorText3
            }
            PpChip {
                id: rowTrend
                anchors.verticalCenter: parent.verticalCenter
                trend: row.modelData.trend !== undefined ? row.modelData.trend : 0
            }
        }
        PpPips {
            marks:   row.modelData.sessions
            maxPips: HomeText.maxPips
            tone:    Theme.colorText2
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Layout
    // ════════════════════════════════════════════════════════════════════════

    // The first reading of a golfer's sessions, said once and quietly.
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

    // Nothing to read yet: no session with a swing in it.
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

        // ── 1. Before you swing (static Setup checks — not a swing thought) ──
        PpCard {
            id: setupCard
            objectName: "setupChecksCard"
            visible: !root.showTooFewOnly
            width:   root.width
            height:  visible ? implicitHeight : 0
            table:   true
            columns: root.columns

            CardHeader {
                title:    qsTr("Before you swing")
                subtitle: root.setupChecks.length > 0
                          ? qsTr("Static setup checks — set these before you move")
                          : qsTr("No repeating setup habits")
                tone:     Theme.colorText
                captions: root.setupChecks.length > 0
            }

            Repeater {
                model: root.setupChecks
                CoachingRow {
                    objectName: "setupCheckItem"
                    showFocusChip: false
                }
            }
        }

        // ── 2. Your focus (ONE dynamic swing thought) ────────────────────────
        HmFocus {
            id: focusCard
            width:      root.width
            height:     implicitHeight
            controller: root.controller
            gridGap:    root.cardGap
            onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
            onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
        }

        // ── 3. Your swing by stage (in-motion sequence, strongest first) ─────
        PpCard {
            id: stagesCard
            objectName: "coachingStagesCard"
            visible: !root.showTooFewOnly && root.stages.length > 0
            width:   root.width
            height:  visible ? implicitHeight : 0
            table:   true
            columns: root.columns

            CardHeader {
                title:    qsTr("Your swing by stage")
                subtitle: qsTr("In sequence from backswing to finish — ranked strongest first")
                tone:     root.toneWork
                captions: root.hasStageHabits
            }

            Repeater {
                model: root.stages
                Column {
                    id: stageBlock
                    required property var modelData
                    readonly property var shownItems: root.visibleItemsFor(stageBlock.modelData)
                    readonly property int hiddenCount: Math.max(0, (stageBlock.modelData.items ? stageBlock.modelData.items.length : 0) - root.stageLimit)
                    readonly property bool expanded: root.stageExpanded(stageBlock.modelData.key)
                    width: stagesCard.width

                    PpTableRow {
                        objectName: "coachingStageHeader"
                        width: stagesCard.width
                        layout: stagesCard.columnLayout
                        height: Theme.sp(Theme.tableHeaderH)
                        hoverEnabled: false
                        topRule: true
                        Item { width: 1; height: 1 }
                        Row {
                            width: parent ? stagesCard.columnLayout[1].width : 0
                            spacing: Theme.gap(10)
                            PpMicro {
                                anchors.verticalCenter: parent.verticalCenter
                                text: Theme.caps(stageBlock.modelData.label || "")
                            }
                            PpCaption {
                                anchors.verticalCenter: parent.verticalCenter
                                text: stageBlock.modelData.habitCount > 0
                                      ? Theme.plural(stageBlock.modelData.habitCount, qsTr("%1 habit"), qsTr("%1 habits"))
                                      : qsTr("No repeating habits")
                            }
                        }
                        Item { width: 1; height: 1 }
                        Item { width: 1; height: 1 }
                        Item { width: 1; height: 1 }
                    }

                    Repeater {
                        model: stageBlock.shownItems
                        CoachingRow {
                            objectName: "stageFaultItem"
                            width: stagesCard.width
                            layout: stagesCard.columnLayout
                            showFocusChip: true
                        }
                    }

                    PpTableRow {
                        visible: stageBlock.hiddenCount > 0
                        width: stagesCard.width
                        layout: stagesCard.columnLayout
                        height: Theme.sp(Theme.itemRowH)
                        hoverEnabled: false
                        topRule: true
                        Item { width: 1; height: 1 }
                        PpLink {
                            objectName: "stageShowMore"
                            text: stageBlock.expanded
                                  ? qsTr("Show fewer")
                                  : qsTr("Show %1 more").arg(stageBlock.hiddenCount)
                            hoverTint: false
                            onClicked: root.toggleStage(stageBlock.modelData.key)
                        }
                        Item { width: 1; height: 1 }
                        Item { width: 1; height: 1 }
                        Item { width: 1; height: 1 }
                    }
                }
            }
        }

        // ── Too-few-measured notice (under a shot filter with no habits) ─────
        PpCard {
            id: emptyCard
            visible: root.showTooFewOnly
            width:   root.width
            height:  visible ? implicitHeight : 0
            table:   true
            columns: root.columns

            PpTableRow {
                hoverEnabled: false
                height: Math.max(Theme.sp(Theme.rowH), emptyWords.implicitHeight + Theme.gap(18))
                Item { width: 1; height: 1 }
                PpCaption {
                    id: emptyWords
                    objectName: "needsWorkEmpty"
                    width:    emptyCard.contentWidth - root.markW - emptyCard.columnGap
                    elide:    Text.ElideNone
                    wrapMode: Text.WordWrap
                    text:     root.tooFewNote
                }
                Item { width: 1; height: 1 }
                Item { width: 1; height: 1 }
                Item { width: 1; height: 1 }
            }
        }
    }
}
