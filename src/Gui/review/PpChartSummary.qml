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

// PpChartSummary — the per-window summary cards row: one card per series, each fed by
// ChartMetrics.summary(series.t_us, series.value, startUs, endUs). A card shows @end / peak
// / Δ-segment / peak-rate over the active window, with the @end value tinted by the band of
// the swing state nearest the window edge. Recomputes live as the segment chips / brush move
// the window — selecting TOP→IMP shows the downswing's numbers, not the full swing's. Pure
// binding; all stats come from ChartMetrics.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import PinPointStudio

ColumnLayout {
    id: root

    // series: [{ key, label, unit, t_us, value, phaseSamples, color }]
    property var    series:      []
    property real   startUs:     0
    property real   endUs:       0
    property string segmentName: ""
    property bool   showHeader:  true     // false when a host SectionHeader labels this

    spacing: Theme.sp(9)

    ChartMetrics { id: cm }

    function _fmt(v) { var r = Math.round(v); return (r > 0 ? "+" : "") + r + "°" }
    function _bandColor(b) {
        return b === "warn"      ? Theme.colorWarn
             : b === "attention" ? Theme.colorAttention
             :                     Theme.colorGood
    }

    // Header — "SUMMARY · <segment>" + a hairline rule.
    RowLayout {
        visible: root.showHeader
        Layout.fillWidth: true
        spacing: Theme.sp(9)
        Text {
            text: qsTr("SUMMARY") + (root.segmentName ? " · " + root.segmentName : "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colorBorder }
    }

    // Cards — equal-width columns, wrapping to the available width.
    GridLayout {
        id: grid
        Layout.fillWidth: true
        columnSpacing: Theme.sp(10); rowSpacing: Theme.sp(10)
        columns: Math.max(1, Math.min(root.series.length,
                                      Math.floor(grid.width / Theme.sp(150))))

        Repeater {
            model: root.series
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property var    st:  cm.summary(card.modelData.t_us, card.modelData.value,
                                                         root.startUs, root.endUs)
                readonly property string bnd: cm.bandAtNearest(card.modelData.phaseSamples, root.endUs)
                readonly property string nm:  cm.shortLabel(card.modelData.key)
                                              || card.modelData.label || card.modelData.key

                Layout.fillWidth: true
                Layout.preferredWidth: 1            // equal columns
                implicitHeight: cardCol.implicitHeight + Theme.sp(22)
                radius: Theme.sp(10)
                color: Theme.colorBg
                border.width: 1; border.color: Theme.colorBorder
                clip: true

                Rectangle {                          // series colour edge
                    width: Theme.sp(3); height: parent.height
                    color: card.modelData.color
                }

                ColumnLayout {
                    id: cardCol
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              leftMargin: Theme.sp(13); rightMargin: Theme.sp(11)
                              topMargin: Theme.sp(11) }
                    spacing: Theme.sp(10)

                    RowLayout {                       // name + unit
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: card.nm; elide: Text.ElideRight
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                            color: Theme.colorText
                        }
                        Text {
                            text: (card.modelData.unit || qsTr("deg"))
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            font.letterSpacing: Theme.trackingData
                            color: Theme.colorText3
                        }
                    }

                    GridLayout {                      // 2×2 metric grid
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.sp(12); rowSpacing: Theme.sp(9)

                        // @ end — tinted by band at the window edge.
                        Column {
                            Layout.fillWidth: true
                            Text { text: qsTr("@ END"); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
                                   color: Theme.colorText3 }
                            Text { text: root._fmt(card.st.end); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzData; color: root._bandColor(card.bnd) }
                        }
                        Column {
                            Layout.fillWidth: true
                            Text { text: qsTr("PEAK"); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
                                   color: Theme.colorText3 }
                            Text { text: root._fmt(card.st.peak); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzData; color: Theme.colorText }
                        }
                        Column {
                            Layout.fillWidth: true
                            Text { text: qsTr("Δ SEGMENT"); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
                                   color: Theme.colorText3 }
                            Text { text: root._fmt(card.st.delta); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzData; color: Theme.colorText }
                        }
                        Column {
                            Layout.fillWidth: true
                            Text { text: qsTr("PK RATE"); font.family: Theme.fontData
                                   font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
                                   color: Theme.colorText3 }
                            Row {
                                spacing: Theme.sp(3)
                                Text { id: rateVal
                                       text: Math.round(card.st.rate); font.family: Theme.fontData
                                       font.pixelSize: Theme.fontSzData; color: Theme.colorText }
                                Text { anchors.baseline: rateVal.baseline
                                       text: qsTr("°/100ms"); font.family: Theme.fontData
                                       font.pixelSize: Theme.fontSzMicro; color: Theme.colorText3 }
                            }
                        }
                    }
                }
            }
        }
    }
}
