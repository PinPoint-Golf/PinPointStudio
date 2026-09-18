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

// PpSequenceStrip — the kinematic sequence, laid out as the one thing the golfer is told
// (docs/design/kinematic_sequence_design.md §8): the four segment chips in the order they PEAKED,
// the gap between neighbours, and the verdict — or the honest refusal of one.
//
// It is shown under the chart only while the METRICS preset is "Kinematic sequence", because the
// chips are the peaks of exactly the four curves that preset draws; a strip with no curves above
// it would be a number with nothing to check it against.
//
// EVERY STRING ON THIS STRIP IS C++'s. ChartMetrics.sequenceRows / sequenceVerdictText /
// sequenceRouteText carry the ordering walk and the string rules, so chart_metrics_test can assert
// them; nothing here derives — it binds. A chip's greyed state is the row's `placed` flag, which the
// producer decided from its own σ (a node less certain than the placement threshold is emitted
// unplaced, never dropped), so the reader sees the segment was attempted and from which view.
//
// The method glyph (I / T / P: inertial, triangulated, projected) is the directory's vocabulary
// for HOW a rung got its number. It is what tells a reader with one IMU which chip to trust.
ColumnLayout {
    id: root
    objectName: "sequenceStrip"

    property var  kinematicSequence: null      // analysisDetail.kinematicSequence (may be null)
    property real impactUs: -1                 // carried for parity with the summary; the rows are already impact-relative

    spacing: Theme.sp(6)

    ChartMetrics { id: cm }

    readonly property var    _ks:      root.kinematicSequence ? root.kinematicSequence : ({})
    readonly property var    _rows:    cm.sequenceRows(root._ks)
    readonly property string _verdict: cm.sequenceVerdictText(root._ks)
    readonly property string _route:   cm.sequenceRouteText(root._ks)

    // ── header — the same shape as the summary section's ──────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp(9)
        Text {
            objectName: "sequenceStripHeader"
            text: qsTr("SEQUENCE") + (root._route ? " · " + root._route : "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colorBorder }
    }

    // ── the chips, in peak order, with the gap to the next between them ───────────────────────
    Flow {
        Layout.fillWidth: true
        spacing: Theme.sp(6)

        Repeater {
            model: root._rows
            delegate: Row {
                id: entry
                required property var modelData
                spacing: Theme.sp(6)

                Rectangle {
                    id: chip
                    objectName: "sequenceChip:" + entry.modelData.segment
                    readonly property bool placed: entry.modelData.placed
                    width:  chipCol.width + Theme.sp(18)
                    height: chipCol.height + Theme.sp(12)
                    radius: Theme.sp(8)
                    color: Theme.colorBg
                    border.width: 1; border.color: Theme.colorBorder
                    opacity: chip.placed ? 1.0 : 0.45

                    Column {
                        id: chipCol
                        x: Theme.sp(9); y: Theme.sp(6)
                        spacing: Theme.sp(2)

                        Row {
                            spacing: Theme.sp(6)
                            Text {
                                text: entry.modelData.label
                                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                                color: chip.placed ? Theme.colorText : Theme.colorText3
                            }
                            // Method glyph — the directory's inertial / triangulated / projected.
                            Rectangle {
                                width: Theme.sp(14); height: Theme.sp(14); radius: Theme.sp(3)
                                anchors.verticalCenter: parent.verticalCenter
                                color: "transparent"
                                border.width: 1; border.color: Theme.colorBorderStrong
                                Text {
                                    anchors.centerIn: parent
                                    text: entry.modelData.glyph
                                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                    color: Theme.colorText2
                                }
                            }
                        }
                        // "−87 ms  ±12 ms" — the offset from impact and its σ, or the reason
                        // there is none.
                        Row {
                            spacing: Theme.sp(5)
                            visible: chip.placed
                            Text {
                                id: offsetText
                                text: entry.modelData.beforeText
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
                                color: Theme.colorText
                            }
                            Text {
                                text: entry.modelData.sigmaText
                                anchors.baseline: offsetText.baseline
                                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                color: Theme.colorText3
                            }
                        }
                        Text {
                            visible: chip.placed
                            text: entry.modelData.peakText
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            color: Theme.colorText2
                        }
                        Text {
                            visible: !chip.placed
                            text: entry.modelData.unplacedText
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzMicro
                            color: Theme.colorText3
                        }
                    }
                }

                // The gap to the next placed chip — a thin connector with the lead on it.
                Item {
                    visible: entry.modelData.gapMs >= 0
                    width: gapText.width + Theme.sp(6)
                    height: chip.height
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 1
                        color: Theme.colorBorderStrong
                    }
                    Text {
                        id: gapText
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.verticalCenter
                        anchors.bottomMargin: Theme.sp(3)
                        text: entry.modelData.gapText
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                        color: Theme.colorText3
                    }
                }
            }
        }
    }

    // ── the verdict — one sentence, or the honest refusal of one ──────────────────────────────
    Text {
        objectName: "sequenceVerdict"
        Layout.fillWidth: true
        visible: root._verdict.length > 0
        text: root._verdict
        wrapMode: Text.WordWrap
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        color: Theme.colorText2
    }
}
