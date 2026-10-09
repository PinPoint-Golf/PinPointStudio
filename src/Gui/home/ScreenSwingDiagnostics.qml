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

// SWING DIAGNOSTICS — the technical layer under YOUR SWING, reached from the home screen's
// "Swing diagnostics →" row: every fault by its technical name with how it was measured (the
// work-ons card, headed FAULTS), then what moves together. For the golfer who wants the detail,
// and for their coach. The home screen's column, on its own screen in the app's stack (Main.qml,
// screenSwingDiagnostics); "← Home" goes back to it.
//
// A fault row's "Review the … session →" link is passed up as reviewSessionRequested, which Main
// handles exactly as it did when the work-ons sat on the home screen.
//
// Every string is WorkOnsController's (the context property `workOns`): summarySubtitle for the
// line under the heading, items for the faults, togetherItems / togetherNote for what goes
// together. This file positions only.

import QtQuick
import PinPointStudio

Item {
    id: root
    objectName: "swingDiagnosticsScreen"

    // The controller: the summary's and the work-ons' are one object in the app. Passed in
    // rather than reached for, so a harness can drive the screen.
    property var controller: (typeof workOns !== "undefined") ? workOns : null
    // The faults card, for a harness to open a row.
    readonly property alias faults: faultsCard

    signal backRequested()
    signal reviewSessionRequested(string sessionDir)

    readonly property string subtitle: controller ? controller.summarySubtitle : ""

    // Back to the top: Main calls it when the home screen's link opens the screen afresh.
    function toTop() { flick.contentY = 0 }

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    Flickable {
        id: flick
        anchors.fill:  parent
        contentWidth:  width
        contentHeight: mainCol.implicitHeight + 80
        clip:          true

        Column {
            id: mainCol
            anchors.horizontalCenter: parent.horizontalCenter
            width:   Theme.contentWidth(parent.width)
            spacing: 0

            Item { width: 1; height: Theme.sp(48) }

            // ── ← Home ───────────────────────────────────────────────────────
            PpLink {
                id: back
                objectName:  "diagnosticsBack"
                text:        qsTr("← Home")
                pressMargin: Theme.sp(6)
                onClicked:   root.backRequested()
            }
            Item { width: 1; height: Theme.sp(20) }

            // ── Heading ──────────────────────────────────────────────────────
            Item {
                width:  mainCol.width
                height: Theme.sp(34)
                PpMicro {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: qsTr("SWING DIAGNOSTICS")
                }
            }
            Text {
                objectName: "swingDiagnosticsSubtitle"
                width:   mainCol.width
                visible: root.subtitle !== ""
                text:    qsTr("Every fault, how it was measured, and what moves together. %1.").arg(root.subtitle)
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                font.italic:    true
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }
            Item { width: 1; height: Theme.sp(16) }

            // ── Faults ───────────────────────────────────────────────────────
            HmWorkOns {
                id: faultsCard
                width:      mainCol.width
                title:      qsTr("FAULTS")
                controller: root.controller
                onReviewSessionRequested: (sessionDir) => root.reviewSessionRequested(sessionDir)
            }
            Item { width: 1; height: Theme.sp(16) }

            // ── What goes together ───────────────────────────────────────────
            HmGoesTogether {
                width:      mainCol.width
                controller: root.controller
            }
        }
    }
}
