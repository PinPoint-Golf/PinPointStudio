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

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PinPointStudio

// PpSequenceStrip — the kinematic sequence TILE under the chart (2026-10-02), deliberately plain:
// four rows in the order the segments SHOULD peak, each with the place it ACTUALLY peaked, when,
// how fast, and that speed as a % of the tour-pro peak, each with its ±σ. It mirrors the plot:
// every peak marked on a curve has a row with numbers; an unplaced one is dimmed with a "?". A wrong order shows as a red number in
// the ACTUAL column; nothing else needs reading. A first cut carried σ, timing windows, two order
// sentences and a cohort paragraph — Mark: "way too wordy and almost unreadable".
//
// Shown only under the "Kinematic sequence" preset. Every string is ChartMetrics.sequenceTable's
// (chart_metrics_test asserts them); this file binds and picks colours.
ColumnLayout {
    id: root
    objectName: "sequenceStrip"

    property var  kinematicSequence: null      // analysisDetail.kinematicSequence (may be null)
    property real impactUs: -1                 // carried for parity with the summary; the rows are already impact-relative
    // Folded, the tile is its title line alone — the caret, SEQUENCE and the verdict — so the
    // plot above takes the height back and the one answer the tile gives is still on screen.
    // The host owns the state (and persists it); the title line asks for the toggle.
    property bool collapsed: false
    signal toggled()

    spacing: Theme.sp(6)

    ChartMetrics { id: cm }

    component HeadText: Text {
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
        font.letterSpacing: Theme.trackingLabel
        color: Theme.colorText3
    }
    component CellText: Text {
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody
        color: Theme.colorText
    }

    readonly property var _ks:    root.kinematicSequence ? root.kinematicSequence : ({})
    readonly property var _table: cm.sequenceTable(root._ks)

    readonly property real _wLabel: Theme.sp(80)
    readonly property real _wRank:  Theme.sp(60)
    readonly property real _wPeak:  Theme.sp(120)
    readonly property real _wSpeed: Theme.sp(100)

    Rectangle {
        objectName: "sequenceTile"
        Layout.fillWidth: true
        implicitHeight: tileCol.implicitHeight + Theme.sp(20)
        color: Theme.colorSurface
        border.color: Theme.colorBorder; border.width: 1
        radius: Theme.sp(8)

        ColumnLayout {
            id: tileCol
            x: Theme.sp(14); y: Theme.sp(10)
            width: parent.width - Theme.sp(28)
            spacing: Theme.sp(6)

            // ── title + the verdict ────────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(7)
                Text {
                    objectName: "sequenceCaret"
                    text: "▸"; rotation: root.collapsed ? 0 : 90
                    color: Theme.colorText3; font.pixelSize: Theme.fontSzBody2
                    Behavior on rotation { enabled: !Theme.reduceMotion
                                           NumberAnimation { duration: Theme.durationFast } }
                }
                HeadText { objectName: "sequenceStripHeader"; text: qsTr("SEQUENCE") }
                Item { Layout.fillWidth: true }
                Text {
                    objectName: "sequenceVerdict"
                    text: (root._table.verdictState === "match"    ? "✓  "
                         : root._table.verdictState === "mismatch" ? "✗  " : "")
                          + root._table.verdictText
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                    font.weight: Font.DemiBold
                    color: root._table.verdictState === "match"    ? Theme.colorGood
                         : root._table.verdictState === "mismatch" ? Theme.colorError
                                                                   : Theme.colorText2
                }
            }

            RowLayout {
                visible: !root.collapsed
                Layout.fillWidth: true
                spacing: Theme.sp(12)
                HeadText { text: "";                 Layout.preferredWidth: root._wLabel }
                HeadText { text: qsTr("SHOULD");     Layout.preferredWidth: root._wRank }
                HeadText { text: qsTr("ACTUAL");     Layout.preferredWidth: root._wRank }
                HeadText { text: qsTr("PEAK");       Layout.preferredWidth: root._wPeak }
                HeadText { text: qsTr("°/S");        Layout.preferredWidth: root._wSpeed }
                HeadText { text: qsTr("% OF PRO");   Layout.fillWidth: true }
            }

            Repeater {
                model: root._table.rows
                delegate: RowLayout {
                    id: seg
                    required property var modelData
                    objectName: "sequenceRow:" + seg.modelData.segment
                    visible: !root.collapsed
                    Layout.fillWidth: true
                    spacing: Theme.sp(12)
                    // A peak the producer could not pin down is shown — it is on the curve — but
                    // dimmed, and its rank carries a "?".
                    opacity: seg.modelData.placed || seg.modelData.actualRank === "—" ? 1.0 : 0.6
                    CellText {
                        text: seg.modelData.label
                        Layout.preferredWidth: root._wLabel
                        font.family: Theme.fontBody; font.weight: Font.Medium
                    }
                    CellText {
                        text: seg.modelData.shouldRank
                        Layout.preferredWidth: root._wRank
                        color: Theme.colorText3
                    }
                    CellText {
                        objectName: "sequenceActual:" + seg.modelData.segment
                        text: seg.modelData.actualRank
                        Layout.preferredWidth: root._wRank
                        font.weight: Font.Bold
                        color: seg.modelData.outOfTurn          ? Theme.colorError
                             : seg.modelData.actualRank !== "—"  ? Theme.colorGood : Theme.colorText3
                    }
                    CellText {
                        objectName: "sequencePeaked:" + seg.modelData.segment
                        text: seg.modelData.peakText
                        Layout.preferredWidth: root._wPeak
                        color: seg.modelData.placed ? Theme.colorText : Theme.colorText3
                    }
                    CellText {
                        text: seg.modelData.speedText
                        Layout.preferredWidth: root._wSpeed
                    }
                    CellText {
                        objectName: "sequencePct:" + seg.modelData.segment
                        text: seg.modelData.pctText
                        Layout.fillWidth: true
                        font.weight: Font.DemiBold
                    }
                }
            }

            HeadText {
                visible: !root.collapsed
                text: qsTr("Pro = tour average, Cheetham 2008")
                font.letterSpacing: 0
            }
        }

        // The whole title line toggles, edge to edge — the same hit area a section header has.
        Item {
            x: 0; y: 0; width: parent.width
            height: Theme.sp(10) + Theme.sp(24)
            PpPressable { objectName: "sequenceToggle"; hoverScale: 1.0; onClicked: root.toggled() }
        }
    }
}
