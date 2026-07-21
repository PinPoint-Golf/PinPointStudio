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

// The Metric Catalogue directory, shaped as a Settings panel. A filter chip row over
// the metric types, then the metrics grouped in manifest order, each row a MetricRow.
// Tapping a row swaps in MetricDetail (internal master→detail). Read-only: all data
// comes from the MetricCatalog façade; there is no shot loaded here, so availability is
// never computed (empty shotCtx everywhere) — a metric's needs are shown via its source
// glyphs and its detail "How it's measured" section, not a live verdict.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: root

    // ── façade + labels (declared declaratively; assembled once, read-only) ────
    MetricCatalog  { id: mc }
    TimelineLabels { id: labels }

    // ── view state ────────────────────────────────────────────────────────────
    property string _typeFilter:  ""    // "" = all types
    property string _selectedKey: ""    // "" = directory (master)

    // Settings-search hook (see ScreenSettings.navigateToResult): return to the
    // directory and report success so the retry loop stops.
    function scrollToItem(itemId) {
        root._selectedKey = ""
        return true
    }

    function _typeName(t) {
        switch (t) {
        case "summary":     return qsTr("Summary")
        case "pointInTime": return qsTr("Point in time")
        case "timeSeries":  return qsTr("Time series")
        case "sequence":    return qsTr("Sequence")
        }
        return t || ""
    }

    // Base filter (type only) — reads _typeFilter so bindings re-run on change.
    function _baseFilters() {
        return root._typeFilter.length > 0 ? { type: root._typeFilter } : ({})
    }
    // Rows for one group under the active type filter.
    function _query(group) {
        var f = root._baseFilters()
        f.group = group
        return mc.query(f, {})
    }
    // Total rows under the active type filter (drives the empty state).
    readonly property int _totalCount: mc.query(root._baseFilters(), {}).length

    // ══ Directory (master) ════════════════════════════════════════════════════
    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        visible: root._selectedKey === ""

        ColumnLayout {
            id: contentCol
            x:       Theme.sp(32)
            y:       Theme.sp(28)
            width:   scrollView.availableWidth - Theme.sp(64)
            spacing: Theme.sp(20)

            // ── Page header ────────────────────────────────────────────────────
            Text {
                text:                qsTr("REFERENCE")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color:               Theme.colorText3
            }

            PpDisplayText { text: qsTr("Metric catalogue") }

            Text {
                Layout.fillWidth: true
                text: qsTr("Every metric Pinpoint can measure — what it means, how to read it, and what it needs to compute. The producers behind a metric are shown as source tags.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
            }

            // ── Type filter chips ──────────────────────────────────────────────
            Flow {
                Layout.fillWidth: true
                spacing: Theme.sp(7)

                // "All" chip
                Rectangle {
                    readonly property bool active: root._typeFilter === ""
                    height: Theme.sp(28); radius: height / 2
                    width:  allLbl.implicitWidth + Theme.sp(24)
                    color: active ? Theme.colorAccentLight
                                  : allMa.containsMouse ? Theme.colorAccentMid : "transparent"
                    border.width: 1
                    border.color: active ? Theme.colorAccentMid
                                         : allMa.containsMouse ? Theme.colorAccentMid : Theme.colorBorderMid
                    Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                    Text {
                        id: allLbl
                        anchors.centerIn: parent
                        text: qsTr("All")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color: parent.active ? Theme.colorAccent : Theme.colorText2
                    }
                    MouseArea {
                        id: allMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._typeFilter = ""
                    }
                }

                // One chip per metric type
                Repeater {
                    model: mc.types
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool active: root._typeFilter === modelData
                        height: Theme.sp(28); radius: height / 2
                        width:  typeChipLbl.implicitWidth + Theme.sp(24)
                        color: active ? Theme.colorAccentLight
                                      : typeChipMa.containsMouse ? Theme.colorAccentMid : "transparent"
                        border.width: 1
                        border.color: active ? Theme.colorAccentMid
                                             : typeChipMa.containsMouse ? Theme.colorAccentMid : Theme.colorBorderMid
                        Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                        Text {
                            id: typeChipLbl
                            anchors.centerIn: parent
                            text: root._typeName(modelData)
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: parent.active ? Theme.colorAccent : Theme.colorText2
                        }
                        MouseArea {
                            id: typeChipMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root._typeFilter = modelData
                        }
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Grouped metric list ────────────────────────────────────────────
            Repeater {
                model: mc.groups
                delegate: ColumnLayout {
                    id: groupBlock
                    required property var modelData
                    readonly property var rows: root._query(modelData)

                    Layout.fillWidth: true
                    spacing: Theme.sp(4)
                    visible: rows.length > 0

                    Text {
                        text:                groupBlock.modelData
                        font.family:         Theme.fontBody
                        font.pixelSize:      Theme.fontSzMicro
                        font.letterSpacing:  Theme.trackingMicro
                        font.capitalization: Font.AllUppercase
                        color:               Theme.colorText3
                        Layout.bottomMargin: Theme.sp(2)
                    }

                    Repeater {
                        model: groupBlock.rows
                        delegate: MetricRow {
                            required property var modelData
                            Layout.fillWidth: true
                            metric: modelData
                            onClicked: root._selectedKey = modelData.key
                        }
                    }
                }
            }

            // ── Empty state (e.g. a type with no metrics yet) ──────────────────
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.sp(8)
                visible: root._totalCount === 0
                text: qsTr("No metrics of this type yet.")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color: Theme.colorText3
            }

            Item { Layout.fillWidth: true; implicitHeight: Theme.sp(8) }
        }
    }

    // ══ Detail ════════════════════════════════════════════════════════════════
    Loader {
        anchors.fill: parent
        active:  root._selectedKey !== ""
        visible: active
        sourceComponent: Component {
            MetricDetail {
                mc:        mc
                labels:    labels
                metricKey: root._selectedKey
                onBack:    root._selectedKey = ""
            }
        }
    }
}
