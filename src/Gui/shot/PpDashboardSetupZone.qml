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

// Setup zone — a compact address-position checklist. The metric set is SWING-AGNOSTIC
// (the dashboard is configured once to display any swing): it shows the zone's
// configured metrics — the pinned list, or by default every LIVE point-in-time metric
// — and for the CURRENT swing reads each one's value at Address. A configured metric
// the swing has no value for shows "NA" (it is NOT dropped), so the layout is stable
// across swings. Point-in-time metrics carry a single Address PhaseSample.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: zone

    property var catalog: null
    property var shotCtx: ({})
    property var detail: ({})
    property int sessionType: -1
    property var pinnedMetrics: []

    readonly property int _phaseAddress: 0   // Phase::Address

    readonly property var _rows: _build()
    readonly property bool _hasContent: _rows.length > 0
    implicitHeight: _hasContent ? card.implicitHeight : 0
    visible: _hasContent

    function _seriesFor(key) {
        var s = detail && detail.series ? detail.series : []
        for (var i = 0; i < s.length; ++i) if (s[i].key === key) return s[i]
        return null
    }
    function _sampleAt(series, phase) {
        var ps = series && series.phaseSamples ? series.phaseSamples : []
        for (var i = 0; i < ps.length; ++i) if (ps[i].phase === phase) return ps[i]
        return null
    }
    function _bandColor(b) {
        return b === "green"  ? Theme.colorRagGood
             : b === "yellow" ? Theme.colorRagWatch
             : b === "red"    ? Theme.colorRagFault
             :                  Theme.colorRagNone
    }
    function _fmtVal(v) {
        var a = Math.abs(v)
        return a >= 100 ? String(Math.round(v)) : String(Math.round(v * 10) / 10)
    }

    // Configured metric rows for this zone (swing-agnostic), each joined to the current
    // swing → value + band at Address, or has=false ("NA").
    function _build() {
        var out = []
        if (!catalog) return out
        var cat = catalog.query({ type: "pointInTime" }, {})   // all point-in-time descriptors
        var byKey = {}
        for (var i = 0; i < cat.length; ++i) byKey[cat[i].key] = cat[i]

        var pins = pinnedMetrics || []
        var keys = []
        if (pins.length > 0) {
            keys = pins
        } else {
            for (var j = 0; j < cat.length; ++j) if (!cat[j].planned) keys.push(cat[j].key)
        }

        for (var k = 0; k < keys.length; ++k) {
            var desc = byKey[keys[k]]
            if (!desc) continue
            var s = _seriesFor(keys[k])
            var samp = s ? _sampleAt(s, _phaseAddress) : null
            out.push({ label: (desc.shortLabel && desc.shortLabel.length) ? desc.shortLabel : desc.label,
                       unit: desc.unit,
                       has: samp !== null,
                       planned: desc.planned === true,   // no producer yet ⇒ "not computed"
                       value: samp ? samp.value : 0,
                       band: samp ? samp.band : "" })
        }
        return out
    }

    Rectangle {
        id: card
        width: zone.width
        implicitHeight: col.implicitHeight + Theme.sp(22)
        color: Theme.colorSurface
        radius: Theme.radiusLg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder

        ColumnLayout {
            id: col
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(16); rightMargin: Theme.sp(14); topMargin: Theme.sp(11) }
            spacing: Theme.sp(7)

            Text {
                text: qsTr("SETUP · ADDRESS")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            Repeater {
                model: zone._rows
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Theme.sp(8)
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: Theme.sp(8); implicitHeight: Theme.sp(8); radius: width / 2
                        color: modelData.has ? zone._bandColor(modelData.band) : Theme.colorRagNone
                        opacity: modelData.has ? 1.0 : 0.5
                    }
                    Text {
                        text: modelData.label
                        Layout.alignment: Qt.AlignVCenter
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: modelData.has ? Theme.colorText2 : Theme.colorText3
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // Value, else "NA" (live metric, no value this swing) or "soon"
                        // (planned — no producer yet).
                        text: modelData.has
                              ? (zone._fmtVal(modelData.value) + (modelData.unit.length ? (" " + modelData.unit) : ""))
                              : (modelData.planned ? qsTr("soon") : qsTr("NA"))
                        Layout.alignment: Qt.AlignVCenter
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                        font.italic: !modelData.has && modelData.planned
                        color: modelData.has ? Theme.colorText : Theme.colorText3
                    }
                }
            }
        }
    }
}
