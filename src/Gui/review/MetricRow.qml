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

// One directory row in the Metric Catalogue. Chromeless until hovered (the project's
// subtle-chrome preference); shows the metric label, a type pill + "key · unit"
// secondary line, and a source glyph per producer (IMU / camera / club / ball). It is
// purely presentational: it takes a row map (as emitted by MetricCatalog.query) and
// emits clicked() — the parent owns navigation.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // A single row map from MetricCatalog.query():
    //   { key, label, shortLabel, unit, type, group, scored, sources:[…], availability }
    property var metric: ({})

    signal clicked()

    readonly property string _label: (metric.label && metric.label.length > 0) ? metric.label
                                    : (metric.shortLabel && metric.shortLabel.length > 0) ? metric.shortLabel
                                    : (metric.key || "")
    readonly property var    _sources: metric.sources || []
    readonly property bool   _hovered: rowMa.containsMouse
    readonly property bool   _planned: metric.planned === true    // roadmap placeholder

    // Human-readable name for a MetricType token.
    function _typeName(t) {
        switch (t) {
        case "summary":     return qsTr("Summary")
        case "pointInTime": return qsTr("Point in time")
        case "timeSeries":  return qsTr("Time series")
        case "sequence":    return qsTr("Sequence")
        }
        return t || ""
    }

    // Compact producer tag text.
    function _srcName(s) {
        switch (s) {
        case "imu":    return qsTr("IMU")
        case "camera": return qsTr("CAM")
        case "club":   return qsTr("CLUB")
        case "ball":   return qsTr("BALL")
        }
        return (s || "").toUpperCase()
    }

    implicitHeight: Theme.sp(50)

    // Hover fill — the only chrome until pointed at.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root._hovered ? Theme.colorBg2 : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin:  Theme.sp(10)
        anchors.rightMargin: Theme.sp(12)
        spacing: Theme.sp(10)

        // ── Label + secondary line ──────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(3)

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(7)

                // Scored marker — metrics that feed a session score.
                Rectangle {
                    visible: metric.scored === true
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth:  Theme.sp(6)
                    implicitHeight: Theme.sp(6)
                    radius: width / 2
                    color: Theme.colorAccent
                }

                Text {
                    Layout.fillWidth: true
                    text: root._label
                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody
                    color: root._planned ? Theme.colorText2 : Theme.colorText
                    elide: Text.ElideRight
                }

                // Roadmap placeholder marker.
                Rectangle {
                    visible: root._planned
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth:  plannedLbl.implicitWidth + Theme.sp(12)
                    implicitHeight: Theme.sp(16)
                    radius: Theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.colorBorderMid
                    Text {
                        id: plannedLbl
                        anchors.centerIn: parent
                        text: qsTr("PLANNED")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText3
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(7)

                // Type pill — filled, muted.
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth:  typeLbl.implicitWidth + Theme.sp(12)
                    implicitHeight: Theme.sp(16)
                    radius: Theme.radius
                    color: Theme.colorBg3
                    Text {
                        id: typeLbl
                        anchors.centerIn: parent
                        text: root._typeName(metric.type)
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText3
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: (metric.key || "")
                          + ((metric.unit && metric.unit.length > 0) ? "  ·  " + metric.unit : "")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                    elide: Text.ElideRight
                }
            }
        }

        // ── Source glyphs ───────────────────────────────────────────────────
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.sp(4)

            Repeater {
                model: root._sources
                delegate: Rectangle {
                    required property var modelData
                    width:  srcLbl.implicitWidth + Theme.sp(10)
                    height: Theme.sp(16)
                    radius: Theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.colorBorderMid
                    Text {
                        id: srcLbl
                        anchors.centerIn: parent
                        text: root._srcName(modelData)
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText3
                    }
                }
            }
        }

        // Disclosure affordance — brightens on hover.
        Text {
            Layout.alignment: Qt.AlignVCenter
            text: "›"
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzHeading
            color: root._hovered ? Theme.colorText2 : Theme.colorText3
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
    }

    // Bottom hairline separator.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.colorBorder
        opacity: Theme.borderOpacityNormal
    }

    PpPressable { id: rowMa; hoverScale: 1.0; onClicked: root.clicked() }
}
