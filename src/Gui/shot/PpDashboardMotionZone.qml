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

// Motion zone — headline cards for the time-series metrics. NO charts. The metric set
// is SWING-AGNOSTIC (the dashboard is configured to display any swing): it shows the
// zone's configured metrics — the pinned list, or by default every LIVE scored
// time-series metric — and for the CURRENT swing reduces each one's curve to a big
// band-tinted number (@impact where present, else peak) plus a micro sub-line. A
// configured metric the swing has no value for shows "NA" (it is NOT dropped), so the
// layout stays stable across swings. Reduction is ChartMetrics.summary (C++).

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

    readonly property int _phaseImpact: 5   // Phase::Impact

    ChartMetrics   { id: cm }
    TimelineLabels { id: labels }

    readonly property var _cards: _build()
    readonly property bool _hasContent: _cards.length > 0
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
             :                  Theme.colorText
    }
    function _fmtVal(v) {
        var a = Math.abs(v)
        return a >= 100 ? String(Math.round(v)) : String(Math.round(v * 10) / 10)
    }

    // Configured metric cards for this zone (swing-agnostic), each joined to the current
    // swing → reduced value, or has=false ("NA").
    function _build() {
        var out = []
        if (!catalog) return out
        var cat = catalog.query({ type: "timeSeries", scored: true }, {})
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
            var label = (desc.shortLabel && desc.shortLabel.length) ? desc.shortLabel : desc.label
            var s = _seriesFor(keys[k])
            if (!s || !s.t_us || s.t_us.length === 0) {
                out.push({ label: label, unit: desc.unit, has: false,
                           planned: desc.planned === true })   // NA / not-computed card
                continue
            }
            var summ = cm.summary(s.t_us, s.value, s.t_us[0], s.t_us[s.t_us.length - 1])
            var imp  = _sampleAt(s, _phaseImpact)
            out.push({ label: label, unit: s.unit, has: true,
                       value: imp ? imp.value : summ.peak,
                       band:  imp ? imp.band  : "",
                       peak:  summ.peak,
                       peakPhase: labels.phaseShortTag(cm.nearestPhase(detail.phases, summ.tPeakUs)),
                       impact: imp ? imp.value : NaN })
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
            spacing: Theme.sp(8)

            Text {
                text: qsTr("MOTION")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.sp(10)
                Repeater {
                    model: zone._cards
                    delegate: MetricCard { required property var modelData; data_: modelData }
                }
            }
        }
    }

    component MetricCard: Rectangle {
        property var data_: ({})
        readonly property bool _has: data_.has === true
        width: Theme.sp(150)
        implicitHeight: mcCol.implicitHeight + Theme.sp(16)
        radius: Theme.radius
        color: Theme.colorBg
        border.width: Theme.borderWidth
        border.color: Theme.colorBorder
        opacity: _has ? 1.0 : 0.6

        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: Theme.sp(3); radius: Theme.radius
            color: _has ? zone._bandColor(data_.band) : Theme.colorRagNone
        }

        ColumnLayout {
            id: mcCol
            anchors { left: parent.left; right: parent.right; top: parent.top
                      leftMargin: Theme.sp(12); rightMargin: Theme.sp(8); topMargin: Theme.sp(8) }
            spacing: Theme.sp(1)
            Text {
                text: (data_.label || "").toUpperCase()
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            RowLayout {
                spacing: Theme.sp(3)
                Text {
                    // Value, else "NA" (live, no value this swing) or "soon" (planned).
                    text: _has ? zone._fmtVal(data_.value)
                               : (data_.planned ? qsTr("soon") : qsTr("NA"))
                    font.family: Theme.fontData
                    font.pixelSize: _has ? Theme.sp(28) : Theme.sp(22); font.weight: Font.DemiBold
                    font.italic: !_has && data_.planned === true
                    color: _has ? zone._bandColor(data_.band) : Theme.colorText3
                }
                Text {
                    Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: Theme.sp(5)
                    visible: _has
                    text: data_.unit || ""
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                    color: Theme.colorText3
                }
            }
            Text {
                Layout.fillWidth: true
                text: _has
                      ? (qsTr("PEAK %1 · %2").arg(zone._fmtVal(data_.peak)).arg(data_.peakPhase)
                         + (isNaN(data_.impact) ? "" : ("  ·  @IMP " + zone._fmtVal(data_.impact))))
                      : (data_.planned ? qsTr("not yet computed") : qsTr("not measured in this swing"))
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                elide: Text.ElideRight
            }
        }
    }
}
