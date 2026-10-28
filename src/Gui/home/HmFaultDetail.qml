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
//   what it costs   the pack's consequence
//   Now · Latest    where it stands, and the newest session that judged it
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

    spacing: Theme.gap(10)

    FontMetrics { id: body2Metrics; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2 }

    Item { width: 1; height: Theme.sp(2) }

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

    Item { width: 1; height: Theme.sp(2) }

    PpFact { width: parent.width; label: Theme.caps(qsTr("Now"));    text: root.str("statusText") }
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

    Item { width: 1; height: Theme.sp(10) }
}
