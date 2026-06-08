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

// Replay-synchronized metric graph: one metric curve over the swing window with a
// zero line, swing-phase tick markers, and a playhead that scrubs in lockstep with
// the ¼× replay. Reads one metric out of the shot's `analysisDetail` role; the
// playhead binds to `shotProcessor.replayPositionUs` (same µs domain as the series
// t_us, so no conversion). Pure Shape/Rectangle — no Canvas, no QtCharts.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // One metric: { key, label, unit, t_us:[…], value:[…], phaseSamples:[…] }.
    property var    series:     ({})
    property var    phases:     []          // [{ phase, t_us, conf }]
    // Window/axis extents + playhead, in EventBuffer µs (== series t_us domain).
    property real   startUs:    0
    property real   endUs:      0
    property real   impactUs:   0
    property real   playheadUs: 0
    property bool   showPlayhead: true

    readonly property var  _t: (series && series.t_us)  ? series.t_us  : []
    readonly property var  _v: (series && series.value) ? series.value : []
    readonly property bool _hasData: _t.length > 1 && _v.length === _t.length
    // Axis extent: an explicit span (live replay) if given, else the series' own range
    // (static review). The playhead maps onto the same axis.
    readonly property real _axisStart: (startUs > 0 && endUs > startUs) ? startUs : (_t.length ? _t[0] : 0)
    readonly property real _axisEnd:   (startUs > 0 && endUs > startUs) ? endUs   : (_t.length ? _t[_t.length - 1] : 1)

    readonly property real _vmin: _hasData ? Math.min.apply(null, _v) : 0
    readonly property real _vmax: _hasData ? Math.max.apply(null, _v) : 1
    readonly property real _pad:  Math.max((_vmax - _vmin) * 0.12, 2)
    readonly property real _lo:   _vmin - _pad
    readonly property real _hi:   _vmax + _pad

    function xForT(t) { return (_axisEnd > _axisStart) ? (t - _axisStart) / (_axisEnd - _axisStart) * width : 0 }
    function yForV(v) { return (_hi > _lo) ? height - (v - _lo) / (_hi - _lo) * height : height / 2 }
    function _phaseTag(p) { return ["ADR", "TKW", "TOP", "TRN", "DWN", "IMP", "REL", "FIN"][p] || "" }

    // Zero reference line (only when the range straddles 0).
    Rectangle {
        visible: root._hasData && root._lo < 0 && root._hi > 0
        x: 0; width: parent.width; height: 1
        y: root.yForV(0)
        color: Theme.colorBorderMid
        opacity: 0.6
    }

    // Phase tick markers (Impact most prominent).
    Repeater {
        model: root.phases
        delegate: Item {
            id: tick
            required property var modelData
            readonly property bool isImpact: tick.modelData.phase === 5   // Phase::Impact
            visible: root._hasData && tick.modelData.t_us >= root._axisStart && tick.modelData.t_us <= root._axisEnd
            x: root.xForT(tick.modelData.t_us)
            width: 1; height: root.height

            Rectangle {
                width: 1; height: parent.height
                color: tick.isImpact ? Theme.colorAccent : Theme.colorBorderMid
                opacity: tick.isImpact ? 0.9 : 0.35
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.leftMargin: Theme.sp(2)
                text: root._phaseTag(tick.modelData.phase)
                font.pixelSize: Theme.fontSzMicro
                color: tick.isImpact ? Theme.colorText2 : Theme.colorText3
            }
        }
    }

    // The metric curve.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.colorAccent
            strokeWidth: Theme.sp(1.5)
            fillColor:   "transparent"
            joinStyle:   ShapePath.RoundJoin
            capStyle:    ShapePath.RoundCap
            PathPolyline {
                path: !root._hasData ? []
                    : root._t.map(function (t, i) { return Qt.point(root.xForT(t), root.yForV(root._v[i])) })
            }
        }
    }

    // Playhead — scrubs with the replay.
    Rectangle {
        visible: root.showPlayhead && root._hasData
        width: Theme.sp(1.5)
        height: parent.height
        x: root.xForT(root.playheadUs) - width / 2
        color: Theme.colorGood
        opacity: 0.9
    }

    // Empty state.
    Text {
        anchors.centerIn: parent
        visible: !root._hasData
        text: qsTr("No analysis")
        font.pixelSize: Theme.fontSzMicro
        color: Theme.colorText3
    }
}
