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

// A FAULT, OPENED — the detail under a fault row on Home, in place (Mark, 28 Oct: a fault opens where
// it is, never on another page). Used by Coaching's focus ("Right now") and "Next on your list", and
// by an open area's items on Your swing:
//   the contrast    first, only under one outcome (HmHomeTabs' outcome filter; item.outcomeContrast): how often
//                   on those shots against the golfer's other shots. Only where the host asks for it
//                   (showOutcomeContrast, through HmFaultRow): Your swing's area items. Coaching's rows (focus,
//                   Next on your list) already carry the line, so a fault opened there does not repeat it
//   what it does to your shots   only where a shot has shown it (item.onBall): what it produced, what
//                   it costs in carry, the tendency it feeds, "See these shots →"
//   what it costs   the pack's consequence
//   Now · Latest    where it stands — a listed work-on's status, its consistency badge beside it
//                   (R10; the badge alone never makes the line) — and the newest session that judged it
//   Swings          fired of every swing, and where it could not be measured
//   Sessions        the pips' key, counted over every session
//   Linked          what it may follow from
//   Try             the drill, where the pack authors one
//   Review the … session →
//
// `item` is WorkOnsController.faultDetail(conditionId): any condition a session measured, a listed
// work-on or not (Mark, 28 Oct: every fault on Home opens). Every string is the controller's. This
// file positions and paints.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "faultDetail"

    property var item: ({})
    // Draw item.outcomeContrast at the top: true only where the row above does not already carry it (Your swing).
    property bool showOutcomeContrast: false

    signal reviewSessionRequested(string sessionDir)

    readonly property color tone:      Theme.colorWarn
    readonly property color toneClean: Theme.colorGood
    readonly property int   pipSize:   Theme.sp(6)

    function tickCount(ticks, state) {
        let n = 0
        for (const t of ticks || []) if (t.state === state) ++n
        return n
    }
    function str(key) { return root.item && root.item[key] !== undefined ? String(root.item[key]) : "" }

    // What this fault did on the ball ({} when no shot has shown it).
    readonly property var  onBall:    root.item && root.item.onBall ? root.item.onBall : ({})
    readonly property bool hasOnBall: (root.onBall.text || "") !== ""
    readonly property string onBallFoot: [root.onBall.costWords || "", root.onBall.tendencyText || ""]
                                         .filter(s => s !== "").join(" · ")

    // Open the session on this reason's shots: the filter first, so the session reads it as it loads.
    function seeTheseShots() {
        if (typeof sessionReviewController !== "undefined" && sessionReviewController)
            sessionReviewController.reasonFilter = root.onBall.conditionId || ""
        root.reviewSessionRequested(root.str("sessionDir"))
    }

    spacing: Theme.gap(10)

    FontMetrics { id: body2Metrics; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2 }

    Item { width: 1; height: Theme.gap(2) }

    PpCaption {
        objectName: "faultOutcomeContrast"
        visible:  root.showOutcomeContrast && text !== ""
        width:    Math.min(parent.width, Theme.sp(720))
        elide:    Text.ElideNone
        wrapMode: Text.WordWrap
        text:     root.str("outcomeContrast")
    }

    Column {
        objectName: "homeFaultOnBall"
        visible: root.hasOnBall
        width:   Math.min(parent.width, Theme.sp(720))
        spacing: Theme.gap(6)

        PpMicro { text: Theme.caps(qsTr("What it does to your shots")) }
        HmHeadline {
            objectName: "faultOnBallText"
            width: parent.width
            text:  root.onBall.text || ""
        }
        Flow {
            objectName: "faultOnBallProduced"
            visible: (root.onBall.produced || []).length > 0
            width:   parent.width
            spacing: Theme.gap(6)
            Repeater {
                model: root.onBall.produced || []
                PpChip {
                    required property var modelData
                    tinted: false
                    text:   modelData.count > 1 ? qsTr("%1 ×%2").arg(modelData.phrase).arg(modelData.count)
                                                : String(modelData.phrase)
                }
            }
        }
        PpCaption {
            objectName: "faultOnBallFoot"
            visible:  text !== ""
            width:    parent.width
            elide:    Text.ElideNone
            wrapMode: Text.WordWrap
            text:     root.onBallFoot
        }
        PpLink {
            objectName: "faultSeeShots"
            visible:    root.str("sessionDir") !== ""
            text:       qsTr("See these shots →")
            hoverTint:  false
            onClicked:  root.seeTheseShots()
        }
    }

    Item { width: 1; height: Theme.gap(2); visible: root.hasOnBall }

    Text {
        width:          Math.min(parent.width, Theme.sp(720))
        visible:        text !== ""
        text:           root.str("consequence")
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText2
        wrapMode:       Text.WordWrap
        lineHeight:     1.4
    }

    Item { width: 1; height: Theme.gap(2) }

    // NOW: the status, and its consistency badge before it when the mark is graded. Two facts rather
    // than one with a hidden badge: PpFact sets its words after any mark it holds, shown or not.
    readonly property var consistency: root.item && root.item.consistency ? root.item.consistency : null
    readonly property bool consistencyGraded: !!(root.consistency && root.consistency.graded)
    PpFact {
        objectName: "faultNow"
        width: parent.width
        label: Theme.caps(qsTr("Now"))
        text:  root.str("statusText")
        visible: text !== "" && root.consistencyGraded
        PpVariabilityBadge {                            // golfer layer: the word, its line the tooltip
            objectName: "faultConsistency"
            dense: true
            grade: root.consistency ? root.consistency.grade : 0
            measurementLimited: !!(root.consistency && root.consistency.measurementLimited)
        }
    }
    PpFact {
        width: parent.width
        label: Theme.caps(qsTr("Now"))
        text:  root.str("statusText")
        visible: text !== "" && !root.consistencyGraded  // ungraded: the status alone, no badge
    }
    PpFact { width: parent.width; label: Theme.caps(qsTr("Latest")); text: root.str("latestText") }
    PpFact {
        width: parent.width
        label: Theme.caps(qsTr("Swings"))
        text:  root.str("coverageText") !== ""
               ? qsTr("%1 · %2").arg(root.str("countText")).arg(root.str("coverageText"))
               : root.str("countText")
    }
    // The pips' key, counted over every session.
    PpFact {
        width: parent.width
        label: Theme.caps(qsTr("Sessions"))
        Repeater {
            model: [
                { state: "fired",         words: qsTr("%1 showed it") },
                { state: "clean",         words: qsTr("%1 clean") },
                { state: "notAssessable", words: qsTr("%1 not measured") }
            ]
            Row {
                required property var modelData
                readonly property int n: root.tickCount(root.item ? root.item.ticks : [], modelData.state)
                visible: n > 0
                spacing: Theme.gap(6)
                height: Math.ceil(body2Metrics.height * 1.35)
                PpPip {
                    anchors.verticalCenter: parent.verticalCenter
                    kind:      parent.modelData.state
                    tone:      root.tone
                    cleanTone: root.toneClean
                    size:      root.pipSize
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.words.arg(parent.n)
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Theme.fontBodyWeight
                    color:          Theme.colorText2
                }
            }
        }
    }
    PpFact { width: parent.width; label: Theme.caps(qsTr("Linked")); text: root.str("causedByText") }

    // The drill, set in: what to do about it.
    PpInset {
        visible: root.str("drillLabel") !== ""
        width:   Math.min(parent.width, Theme.sp(720))
        Row {
            spacing: Theme.gap(10)
            PpMicro {
                anchors.baseline: drillName.baseline
                text: Theme.caps(qsTr("Try"))
            }
            Text {
                id: drillName
                text: root.str("drillLabel")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorAccent
            }
        }
        Text {
            width: parent.width
            text:  root.str("drillInstruction")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText2
            wrapMode:       Text.WordWrap
            lineHeight:     1.4
        }
    }

    PpLink {
        objectName: "faultReview"
        visible:    root.str("sessionDir") !== ""
        text:       qsTr("Review the %1 session →").arg(root.str("sessionLabel"))
        hoverTint:  false
        onClicked:  root.reviewSessionRequested(root.str("sessionDir"))
    }

    Item { width: 1; height: Theme.gap(10) }
}
