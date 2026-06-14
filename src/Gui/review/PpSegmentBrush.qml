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

// PpSegmentBrush — whole-swing overview strip with a draggable selection window. Shows a
// faint per-series sparkline (each normalised to its own range) and the swing-phase ticks
// across the full extent, then a movable/resizable window the user drags to scope the
// chart. Emits windowChanged(startUs,endUs) as the body is dragged, an edge handle is
// pulled, or a fresh window is swept out — the parent writes that into its chart-local
// viewStartUs/viewEndUs (never shotReplay / SwingDataSource). Pure scale/path binding;
// per-series ranges come from ChartMetrics.summary, phase tags from TimelineLabels.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // series: [{ t_us, value, color, … }] (the decorated, currently-visible series)
    property var  series: []
    property var  phases: []
    property real axisStartUs: 0
    property real axisEndUs:   1
    property real winStartUs:  0
    property real winEndUs:    1
    property real impactUs:    0
    property real minWidthUs:  40000      // clamp so the window can't collapse

    // NB: not "windowChanged" — that collides with QQuickItem's inherited window NOTIFY.
    signal viewWindowChanged(real startUs, real endUs)

    implicitHeight: Theme.sp(64)

    ChartMetrics   { id: cm }
    TimelineLabels { id: labels }

    // ── Geometry + scales ─────────────────────────────────────────────────────────
    readonly property real _plotL: Theme.sp(8)
    readonly property real _plotR: width - Theme.sp(8)
    readonly property real _plotT: Theme.sp(6)
    readonly property real _plotB: height - Theme.sp(15)     // room for phase tags
    readonly property real _plotW: Math.max(1, _plotR - _plotL)
    readonly property real _plotH: Math.max(1, _plotB - _plotT)
    readonly property real _hit:   Theme.sp(7)               // edge-grab tolerance (px)

    function xs(t) {
        return _plotL + (axisEndUs > axisStartUs
                         ? (t - axisStartUs) / (axisEndUs - axisStartUs) * _plotW : 0)
    }
    function toT(px) {
        var f = (px - _plotL) / _plotW
        return Math.max(axisStartUs, Math.min(axisEndUs, axisStartUs + f * (axisEndUs - axisStartUs)))
    }

    // ── Frame ─────────────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        radius: Theme.sp(9)
        color: Theme.colorBg
        border.width: 1; border.color: Theme.colorBorder
    }

    // ── Faint per-series sparklines (each normalised to its own range) ─────────────
    Repeater {
        model: root.series
        delegate: Shape {
            id: spark
            required property var modelData
            anchors.fill: parent
            opacity: 0.5
            preferredRendererType: Shape.CurveRenderer
            readonly property var _st: cm.summary(spark.modelData.t_us, spark.modelData.value,
                                                  root.axisStartUs, root.axisEndUs)
            function ys(v) {
                var lo = spark._st.min, hi = spark._st.max
                return (hi > lo) ? root._plotT + root._plotH - (v - lo) / (hi - lo) * root._plotH
                                 : root._plotT + root._plotH / 2
            }
            ShapePath {
                strokeColor: spark.modelData.color
                strokeWidth: 1
                fillColor:   "transparent"
                joinStyle:   ShapePath.RoundJoin
                PathPolyline {
                    path: (spark.modelData.t_us || []).map(function (t, i) {
                        return Qt.point(root.xs(t), spark.ys(spark.modelData.value[i]))
                    })
                }
            }
        }
    }

    // ── Phase ticks + tags ────────────────────────────────────────────────────────
    Repeater {
        model: root.phases
        delegate: Item {
            id: pt
            required property var modelData
            readonly property bool isImpact: pt.modelData.phase === 5
            readonly property real tx: root.xs(pt.modelData.t_us)
            Rectangle {
                x: pt.tx; y: root._plotT
                width: 1; height: root._plotH
                color: pt.isImpact ? Theme.colorAccent : Theme.colorBorderMid
                opacity: pt.isImpact ? 0.7 : 0.3
            }
            Text {
                x: pt.tx + Theme.sp(2); y: root._plotB + Theme.sp(2)
                text: labels.phaseShortTag(pt.modelData.phase)
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                color: pt.isImpact ? Theme.colorText2 : Theme.colorText3
            }
        }
    }

    // ── Selection window: dim sides + body + outline + handles ─────────────────────
    readonly property real _xL: root.xs(root.winStartUs)
    readonly property real _xR: root.xs(root.winEndUs)

    Rectangle {                                   // dim left of window
        x: root._plotL; y: root._plotT
        width: Math.max(0, root._xL - root._plotL); height: root._plotH
        color: Theme.colorBg2; opacity: 0.7
    }
    Rectangle {                                   // dim right of window
        x: root._xR; y: root._plotT
        width: Math.max(0, root._plotR - root._xR); height: root._plotH
        color: Theme.colorBg2; opacity: 0.7
    }
    Rectangle {                                   // window body fill
        x: root._xL; y: root._plotT
        width: Math.max(1, root._xR - root._xL); height: root._plotH
        color: Theme.colorAccent; opacity: 0.06
    }
    Rectangle {                                   // window outline
        x: root._xL; y: root._plotT
        width: Math.max(1, root._xR - root._xL); height: root._plotH
        color: "transparent"
        border.width: Theme.sp(1.5); border.color: Theme.colorAccent
    }
    Repeater {                                     // L / R edge handles
        model: [{ x: root._xL }, { x: root._xR }]
        delegate: Item {
            id: handle
            required property var modelData
            Rectangle {
                x: handle.modelData.x - Theme.sp(3); y: root._plotT
                width: Theme.sp(6); height: root._plotH
                color: Theme.colorAccent; opacity: 0.85
            }
            Rectangle {                            // grip pip
                x: handle.modelData.x - 1; y: root._plotT + root._plotH / 2 - Theme.sp(6)
                width: 2; height: Theme.sp(12)
                color: Theme.colorSurface; opacity: 0.9
            }
        }
    }

    // ── Drag interaction ──────────────────────────────────────────────────────────
    property string _mode: ""        // "L" | "R" | "move" | "new"
    property real   _origStart: 0
    property real   _origEnd:   0
    property real   _anchorT:   0

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: {
            if (root._mode === "L" || root._mode === "R") return Qt.SizeHorCursor
            var px = mouseX
            if (Math.abs(px - root._xL) < root._hit || Math.abs(px - root._xR) < root._hit)
                return Qt.SizeHorCursor
            if (px > root._xL && px < root._xR) return Qt.OpenHandCursor
            return Qt.CrossCursor
        }

        onPressed: (m) => {
            var px = m.x
            root._origStart = root.winStartUs
            root._origEnd   = root.winEndUs
            root._anchorT   = root.toT(px)
            if (Math.abs(px - root._xL) < root._hit)        root._mode = "L"
            else if (Math.abs(px - root._xR) < root._hit)   root._mode = "R"
            else if (px > root._xL && px < root._xR)         root._mode = "move"
            else {
                root._mode = "new"
                root.viewWindowChanged(root._anchorT, root._anchorT)
            }
        }
        onPositionChanged: (m) => {
            if (root._mode === "")  return
            var t = root.toT(m.x)
            var s = root.winStartUs, e = root.winEndUs
            if (root._mode === "L")        { s = Math.min(t, root.winEndUs - root.minWidthUs) }
            else if (root._mode === "R")   { e = Math.max(t, root.winStartUs + root.minWidthUs) }
            else if (root._mode === "new") {
                s = Math.min(root._anchorT, t); e = Math.max(root._anchorT, t)
            } else if (root._mode === "move") {
                var off = t - root._anchorT
                s = root._origStart + off; e = root._origEnd + off
                if (s < root.axisStartUs) { e += root.axisStartUs - s; s = root.axisStartUs }
                if (e > root.axisEndUs)   { s -= e - root.axisEndUs;   e = root.axisEndUs }
            }
            if (e - s < root.minWidthUs) e = s + root.minWidthUs
            root.viewWindowChanged(s, e)
        }
        onReleased: root._mode = ""
        onCanceled: root._mode = ""
    }

    // Hint text (top-right).
    Text {
        anchors { top: parent.top; right: parent.right; topMargin: Theme.sp(5); rightMargin: Theme.sp(8) }
        text: qsTr("drag to select · drag edges to resize")
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color: Theme.colorText3
    }
}
