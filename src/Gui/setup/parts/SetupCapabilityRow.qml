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

// One capability family as a row: a dot, the family, and what it will record. `row` is one entry of
// setup_capability_rows.js rows() — { label, stateText, reason, tone, gateLine, text } — and its
// `tone` ("good" / "warn" / "muted", CapRows.toneOf) is the ONE colour rule: green, amber, quiet ink.
// Drawn in two places:
//   the setup Ready page, "What this session will record" (compact: false) — a padded table row:
//     small dot, the family over its reason and its forecast gate line, the state on the right in
//     the tone's colour;
//   Home's "What this rig can measure" (compact: true) — a dense line: a larger dot, the family,
//     and "state · reason" right-aligned in the quiet ink; a family with nothing recorded has its
//     label in the quiet ink too.
// family / stateText / reason / tone / gateLine are read by the setup tests and probes.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: cr
    objectName: "capabilityRow"

    property var  row:     ({})
    property bool compact: false

    // What the row says, for the tests: the row as one line.
    readonly property string family:    row.label || ""
    readonly property string stateText: row.stateText || ""
    readonly property string reason:    row.reason || ""
    readonly property string tone:      row.tone || ""
    readonly property string gateLine:  row.gateLine || ""
    readonly property color  toneColor: Theme.toneColor(cr.tone)

    height: compact ? dense.rowHeight : table.rowHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: row.text || ""

    // ── Ready: the table row ─────────────────────────────────────────────────
    Item {
        id: table
        visible: !cr.compact
        anchors.fill: parent
        readonly property real rowHeight: capCol.implicitHeight + Theme.gap(20)

        RowLayout {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                      margins: Theme.gap(14) }
            spacing: Theme.gap(10)

            Rectangle {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.gap(7)
                width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
                color: cr.toneColor
            }
            Column {
                id: capCol
                Layout.fillWidth: true
                spacing: Theme.gap(2)
                Text {
                    width:          parent.width
                    text:           cr.family
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    color:          Theme.colorText
                }
                Text {
                    visible:            cr.reason !== ""
                    width:              parent.width
                    text:               cr.reason
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color:              Theme.colorText3
                    wrapMode:           Text.WordWrap
                }
                // the forecast: a gate this setup's routes cannot lift (§7.4)
                Text {
                    objectName:         "capabilityGateLine"
                    visible:            cr.gateLine !== ""
                    width:              parent.width
                    text:               cr.gateLine
                    font.family:        Theme.fontData
                    font.pixelSize:     Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingData
                    color:              Theme.colorText3
                    wrapMode:           Text.WordWrap
                }
            }
            Text {
                Layout.alignment:   Qt.AlignTop
                text:               cr.stateText
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              cr.toneColor
            }
        }
    }

    // ── Home: the dense line ─────────────────────────────────────────────────
    Item {
        id: dense
        visible: cr.compact
        anchors.fill: parent
        readonly property real rowHeight:
            Math.max(Theme.sp(30), Math.max(capLabel.implicitHeight, capWords.implicitHeight) + Theme.sp(10))

        Rectangle {
            id: dot
            x: 0
            anchors.verticalCenter: capLabel.verticalCenter
            width:  Theme.sp(Theme.capDot); height: width; radius: width / 2
            color:  cr.toneColor
        }
        Text {
            id: capLabel
            x: dot.width + Theme.gap(12)
            y: Theme.sp(5)
            width: Math.min(implicitWidth, Math.round(cr.width * 0.45))
            text:           cr.family
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            font.weight:    Theme.fontBodyWeight
            color:          cr.tone === "muted" ? Theme.colorText3 : Theme.colorText
            elide:          Text.ElideRight
        }
        // CapRows' own words, minus the family label it leads `text` with.
        Text {
            id: capWords
            objectName: "capabilityWords"
            anchors { left: capLabel.right; leftMargin: Theme.gap(12); right: parent.right }
            anchors.baseline: capLabel.baseline
            text:                cr.reason !== "" ? cr.stateText + " · " + cr.reason : cr.stateText
            horizontalAlignment: Text.AlignRight
            wrapMode:            Text.WordWrap
            maximumLineCount:    2
            elide:               Text.ElideRight
            font.family:         Theme.fontData
            font.pixelSize:      Theme.fontSzMicro
            font.letterSpacing:  Theme.trackingData
            color:               Theme.colorText3
        }
    }
}
