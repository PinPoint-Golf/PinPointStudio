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

// Replay-synchronized multi-metric graph: N metric curves (neutral/absolute, in degrees)
// over the swing window on a shared axis, with a zero line, swing-phase ticks, and a
// playhead that scrubs in lockstep with the ¼× replay. Filter chips below double as the
// legend + a live value/Δ-from-address readout at the playhead. Reads
// analysisDetail.series; playhead binds to shotProcessor.replayPositionUs (same µs domain
// as t_us). Pure Shape/Rectangle — no Canvas, no QtCharts. See docs/SHOT_ANALYZER_VIZ.md.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // Array of metric series: [{ key, label, unit, t_us:[…], value:[…], phaseSamples:[…] }].
    property var  seriesList: []
    property var  phases:     []          // [{ phase, t_us, conf }]
    // Window/axis extents + playhead, in EventBuffer µs (== series t_us domain).
    property real startUs:    0
    property real endUs:      0
    property real impactUs:   0
    property real playheadUs: 0
    property bool showPlayhead: true

    // Per-key enabled state ({} → all on); chips toggle by reassigning the object.
    property var  _enabled: ({})
    function _isOn(key) { return root._enabled[key] !== false }
    function _toggle(key) {
        var e = Object.assign({}, root._enabled)
        e[key] = !root._isOn(key)
        root._enabled = e
    }

    // Stable colour per metric by its position in seriesList.
    readonly property var _palette: [Theme.colorAccent, Theme.colorGood, Theme.colorImuA, Theme.colorImuB]
    function _color(i) { return root._palette[i % root._palette.length] }
    function _shortName(key) {
        return ({ leadWristFlexExt: qsTr("Bow/cup"), leadWristRadUln: qsTr("Hinge"),
                  forearmPronation: qsTr("Roll"),    leadArmFlexion:  qsTr("Elbow") })[key] || key
    }

    readonly property var  _list: root.seriesList || []
    readonly property bool _hasAny: {
        for (var i = 0; i < root._list.length; ++i)
            if (root._list[i] && root._list[i].t_us && root._list[i].t_us.length > 1) return true
        return false
    }
    // Enabled series that have data, tagged with their palette index.
    readonly property var _visible: {
        var out = []
        for (var i = 0; i < root._list.length; ++i) {
            var s = root._list[i]
            if (s && s.t_us && s.t_us.length > 1 && s.value && s.value.length === s.t_us.length
                  && root._isOn(s.key))
                out.push({ s: s, idx: i })
        }
        return out
    }

    // Axis extent: explicit span (live replay) if given, else the union of all series.
    readonly property real _dataStart: root._list.length && root._list[0].t_us && root._list[0].t_us.length ? root._list[0].t_us[0] : 0
    readonly property real _dataEnd: {
        var e = root._dataStart + 1
        for (var i = 0; i < root._list.length; ++i) {
            var t = root._list[i].t_us
            if (t && t.length) e = Math.max(e, t[t.length - 1])
        }
        return e
    }
    readonly property real _axisStart: (root.startUs > 0 && root.endUs > root.startUs) ? root.startUs : root._dataStart
    readonly property real _axisEnd:   (root.startUs > 0 && root.endUs > root.startUs) ? root.endUs   : root._dataEnd

    // Shared value range over the VISIBLE series (recomputes as chips toggle).
    readonly property var _range: {
        var lo = Infinity, hi = -Infinity
        for (var k = 0; k < root._visible.length; ++k) {
            var v = root._visible[k].s.value
            for (var j = 0; j < v.length; ++j) { if (v[j] < lo) lo = v[j]; if (v[j] > hi) hi = v[j] }
        }
        if (lo === Infinity) { lo = 0; hi = 1 }
        var pad = Math.max((hi - lo) * 0.12, 2)
        return { lo: lo - pad, hi: hi + pad }
    }

    // Cursor for the chip readout: the playhead during replay, else impact (else end).
    readonly property real _cursorUs: root.showPlayhead ? root.playheadUs
                                     : (root.impactUs > 0 ? root.impactUs : root._axisEnd)

    function xForT(t) { return (root._axisEnd > root._axisStart) ? (t - root._axisStart) / (root._axisEnd - root._axisStart) * plot.width : 0 }
    function yForV(v) { return (root._range.hi > root._range.lo) ? plot.height - (v - root._range.lo) / (root._range.hi - root._range.lo) * plot.height : plot.height / 2 }
    function _phaseTag(p) { return ["ADR", "TKW", "TOP", "TRN", "DWN", "IMP", "REL", "FIN"][p] || "" }

    function _valueAt(s, t) {
        var ts = s.t_us
        if (!ts || !ts.length) return 0
        var lo = 0, hi = ts.length - 1
        if (t <= ts[0]) return s.value[0]
        if (t >= ts[hi]) return s.value[hi]
        while (hi - lo > 1) { var mid = (lo + hi) >> 1; if (ts[mid] <= t) lo = mid; else hi = mid }
        return (t - ts[lo] <= ts[hi] - t) ? s.value[lo] : s.value[hi]
    }
    function _addrValue(s) {
        var ps = s.phaseSamples || []
        for (var i = 0; i < ps.length; ++i) if (ps[i].phase === 0) return ps[i].value   // Phase::Address
        return s.value && s.value.length ? s.value[0] : 0
    }
    function _fmt(v) { var r = Math.round(v); return (r > 0 ? "+" : "") + r + "°" }

    // ── Plot area (above the chip row) ────────────────────────────────────────────
    Item {
        id: plot
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: chips.top; bottomMargin: Theme.sp(4) }

        // Zero reference line (only when the range straddles 0).
        Rectangle {
            visible: root._hasAny && root._range.lo < 0 && root._range.hi > 0
            x: 0; width: parent.width; height: 1
            y: root.yForV(0)
            color: Theme.colorBorderMid; opacity: 0.6
        }

        // Phase tick markers (Impact most prominent).
        Repeater {
            model: root.phases
            delegate: Item {
                id: tick
                required property var modelData
                readonly property bool isImpact: tick.modelData.phase === 5   // Phase::Impact
                visible: root._hasAny && tick.modelData.t_us >= root._axisStart && tick.modelData.t_us <= root._axisEnd
                x: root.xForT(tick.modelData.t_us)
                width: 1; height: plot.height
                Rectangle {
                    width: 1; height: parent.height
                    color: tick.isImpact ? Theme.colorAccent : Theme.colorBorderMid
                    opacity: tick.isImpact ? 0.9 : 0.35
                }
                Text {
                    anchors { bottom: parent.bottom; left: parent.left; leftMargin: Theme.sp(2) }
                    text: root._phaseTag(tick.modelData.phase)
                    font.pixelSize: Theme.fontSzMicro
                    color: tick.isImpact ? Theme.colorText2 : Theme.colorText3
                }
            }
        }

        // One curve per visible series.
        Repeater {
            model: root._visible
            delegate: Shape {
                id: curve
                required property var modelData
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: root._color(curve.modelData.idx)
                    strokeWidth: Theme.sp(1.5)
                    fillColor:   "transparent"
                    joinStyle:   ShapePath.RoundJoin
                    capStyle:    ShapePath.RoundCap
                    PathPolyline {
                        path: curve.modelData.s.t_us.map(function (t, i) {
                            return Qt.point(root.xForT(t), root.yForV(curve.modelData.s.value[i]))
                        })
                    }
                }
            }
        }

        // Playhead — scrubs with the replay.
        Rectangle {
            visible: root.showPlayhead && root._hasAny
            width: Theme.sp(1.5); height: plot.height
            x: root.xForT(root.playheadUs) - width / 2
            color: Theme.colorText; opacity: 0.85
        }

        Text {
            anchors.centerIn: parent
            visible: !root._hasAny
            text: qsTr("No analysis")
            font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }
    }

    // ── Filter chips = legend + live value/Δ readout ──────────────────────────────
    Flow {
        id: chips
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: Theme.sp(6)
        visible: root._hasAny

        Repeater {
            model: root._list
            delegate: Row {
                id: chip
                required property var modelData
                required property int index
                readonly property bool on: root._isOn(chip.modelData.key)
                readonly property real val: root._valueAt(chip.modelData, root._cursorUs)
                spacing: Theme.sp(4)
                opacity: chip.on ? 1.0 : 0.4

                Rectangle {   // colour swatch
                    width: Theme.sp(8); height: Theme.sp(8); radius: Theme.sp(2)
                    anchors.verticalCenter: parent.verticalCenter
                    color: chip.on ? root._color(chip.index) : Theme.colorText3
                }
                Text {
                    text: root._shortName(chip.modelData.key)
                    font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                    color: Theme.colorText2
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: root._fmt(chip.val) + "  Δ" + root._fmt(chip.val - root._addrValue(chip.modelData))
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText3
                    anchors.verticalCenter: parent.verticalCenter
                }
                TapHandler { onTapped: root._toggle(chip.modelData.key) }
            }
        }
    }
}
