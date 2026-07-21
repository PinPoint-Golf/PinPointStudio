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

// Detail view for a single metric. Reads the full descriptor from MetricCatalog and
// renders it in plain language: what it means, how to read it, its normative corridors
// (one NormativeBar per phase), what it needs to compute, and where it is used. Purely
// read-only; it emits back() for the parent to return to the directory.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Item {
    id: root

    // The MetricCatalog façade and TimelineLabels solver, injected by the host.
    property var    mc
    property var    labels
    property string metricKey: ""

    signal back()

    // Full descriptor map for the current key (empty when unset / unknown).
    readonly property var d: (mc && metricKey && metricKey.length > 0) ? mc.descriptor(metricKey) : ({})

    readonly property var _norm:      (d && d.normative) ? d.normative : ({})
    readonly property var _corridors: (_norm && _norm.corridors) ? _norm.corridors : []
    readonly property var _usedBy:    (d && d.usedBy) ? d.usedBy : []

    // ── plain-language helpers ────────────────────────────────────────────────
    function _typeName(t) {
        switch (t) {
        case "summary":     return qsTr("Summary")
        case "pointInTime": return qsTr("Point in time")
        case "timeSeries":  return qsTr("Time series")
        case "sequence":    return qsTr("Sequence")
        }
        return t || ""
    }

    // "LeadForearm" → "Lead forearm"
    function _humanRole(r) {
        var s = String(r).replace(/([A-Z])/g, " $1").trim().toLowerCase()
        return s.charAt(0).toUpperCase() + s.slice(1)
    }

    // "chart:review" → "Review chart"
    function _humanUsage(u) {
        var parts = String(u).split(":")
        if (parts.length === 2 && parts[1].length > 0)
            return parts[1].charAt(0).toUpperCase() + parts[1].slice(1) + " " + parts[0]
        return u
    }

    // The requirement, rendered as plain-language capability lines.
    function _measures() {
        var out = []
        var r = (d && d.requires) ? d.requires : ({})
        var roles = r.imuRoles || []
        if (roles.length > 0) {
            var names = roles.map(_humanRole)
            out.push(names.join(" + ") + (roles.length > 1 ? qsTr(" IMUs") : qsTr(" IMU")))
        }
        if (r.faceOnCamera) out.push(qsTr("Face-on camera"))
        if (r.clubTrack)    out.push(qsTr("Club tracking"))
        if (r.ballTrack)    out.push(qsTr("Ball tracking"))
        if (r.minTier && r.minTier !== "angles2D")
            out.push(qsTr("Requires %1 reconstruction").arg(r.minTier))
        return out
    }

    // ── reusable section eyebrow ──────────────────────────────────────────────
    component Eyebrow: Text {
        font.family:         Theme.fontBody
        font.pixelSize:      Theme.fontSzMicro
        font.letterSpacing:  Theme.trackingMicro
        font.capitalization: Font.AllUppercase
        color:               Theme.colorText3
    }

    // ── reusable body paragraph ───────────────────────────────────────────────
    component Body: Text {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.sp(26)
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText2
        wrapMode:       Text.WordWrap
        lineHeight:     1.35
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            id: contentCol
            x:       Theme.sp(32)
            y:       Theme.sp(24)
            width:   scrollView.availableWidth - Theme.sp(64)
            spacing: Theme.sp(20)

            // ── Back affordance ───────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                implicitHeight: backRow.implicitHeight + Theme.sp(4)

                Row {
                    id: backRow
                    spacing: Theme.sp(6)
                    Text {
                        text: "‹"
                        anchors.verticalCenter: parent.verticalCenter
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzHeading
                        color: backMa.containsMouse ? Theme.colorText : Theme.colorText3
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                    Text {
                        text: qsTr("Metric catalogue")
                        anchors.verticalCenter: parent.verticalCenter
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        color: backMa.containsMouse ? Theme.colorText2 : Theme.colorText3
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                }
                PpPressable { id: backMa; hoverScale: 1.0; onClicked: root.back() }
            }

            // ── Header ─────────────────────────────────────────────────────────
            Eyebrow { text: (d && d.group) ? d.group : "" }

            PpDisplayText {
                Layout.fillWidth: true
                text: (d && d.label) ? d.label : ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.sp(8)

                // Type pill
                Rectangle {
                    implicitWidth:  hdrType.implicitWidth + Theme.sp(12)
                    implicitHeight: Theme.sp(18)
                    radius: Theme.radius
                    color: Theme.colorBg3
                    Text {
                        id: hdrType
                        anchors.centerIn: parent
                        text: root._typeName(d ? d.type : "")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorText2
                    }
                }

                // Unit
                Text {
                    visible: (d.unit || "").length > 0
                    text: qsTr("Unit: %1").arg(d.unit || "")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }

                Item { Layout.fillWidth: true }

                // Scored badge
                Rectangle {
                    visible: d && d.scored === true
                    implicitWidth:  hdrScored.implicitWidth + Theme.sp(12)
                    implicitHeight: Theme.sp(18)
                    radius: Theme.radius
                    color: Theme.colorAccentLight
                    border.width: 1
                    border.color: Theme.colorAccentMid
                    Text {
                        id: hdrScored
                        anchors.centerIn: parent
                        text: qsTr("SCORED")
                        font.family:        Theme.fontData
                        font.pixelSize:     Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingMicro
                        color: Theme.colorAccent
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── What it means ──────────────────────────────────────────────────
            Eyebrow { text: qsTr("What it means") }
            Body { text: (d && d.description) ? d.description : "" }

            // ── How to read ────────────────────────────────────────────────────
            Eyebrow { text: qsTr("How to read") }
            Body { text: (d && d.howToRead) ? d.howToRead : "" }

            // ── Normative ──────────────────────────────────────────────────────
            Eyebrow { text: qsTr("Normative") }

            ColumnLayout {
                Layout.fillWidth:  true
                Layout.leftMargin: Theme.sp(26)
                spacing: Theme.sp(14)

                // Context note (shared across this metric's corridors).
                Text {
                    visible: (root._norm.contextNote || "").length > 0
                    Layout.fillWidth: true
                    text: (root._norm && root._norm.contextNote) ? root._norm.contextNote : ""
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                    wrapMode: Text.WordWrap
                }

                // One corridor per phase.
                Repeater {
                    model: root._corridors
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.sp(6)

                        Text {
                            text: (root.labels && modelData.phase !== undefined)
                                  ? root.labels.phaseFullName(modelData.phase) : ""
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText
                        }

                        NormativeBar {
                            Layout.fillWidth: true
                            corridor: modelData
                        }
                    }
                }

                // Heuristic caption — this norm is a rule of thumb, not a hard band.
                Text {
                    visible: root._corridors.length > 0 && root._norm && root._norm.heuristic === true
                    Layout.fillWidth: true
                    text: qsTr("Heuristic — expected to move through the swing, not a fixed target.")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                    wrapMode: Text.WordWrap
                }

                // Emit-nothing-never-garbage: an honest note instead of an empty box.
                Text {
                    visible: root._corridors.length === 0
                    Layout.fillWidth: true
                    text: qsTr("No normative reference yet.")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                }
            }

            // ── How it's measured ──────────────────────────────────────────────
            Eyebrow { text: qsTr("How it's measured") }

            ColumnLayout {
                Layout.fillWidth:  true
                Layout.leftMargin: Theme.sp(26)
                spacing: Theme.sp(6)

                Repeater {
                    model: root._measures()
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.sp(8)
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth:  Theme.sp(5)
                            implicitHeight: Theme.sp(5)
                            radius: width / 2
                            color: Theme.colorText3
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText2
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ── Where it's used ────────────────────────────────────────────────
            Eyebrow {
                text: qsTr("Where it's used")
                visible: root._usedBy.length > 0
            }

            Flow {
                Layout.fillWidth:  true
                Layout.leftMargin: Theme.sp(26)
                visible: root._usedBy.length > 0
                spacing: Theme.sp(7)

                Repeater {
                    model: root._usedBy
                    delegate: Rectangle {
                        required property var modelData
                        width:  useLbl.implicitWidth + Theme.sp(16)
                        height: Theme.sp(22)
                        radius: Theme.radius
                        color: Theme.colorBg2
                        border.width: 1
                        border.color: Theme.colorBorderMid
                        Text {
                            id: useLbl
                            anchors.centerIn: parent
                            text: root._humanUsage(modelData)
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            color: Theme.colorText2
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true; implicitHeight: Theme.sp(8) }
        }
    }
}
