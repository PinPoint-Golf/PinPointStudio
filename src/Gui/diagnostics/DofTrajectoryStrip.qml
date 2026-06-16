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

// One DOF trajectory strip: a header (name + sub + Δ read-out at the selected position), the plot
// (shaded band corridor + player line + position cursor + shape-coded RAG markers + y-axis
// consequence poles + P1–P8 labels), and a source/confidence footer. Mirrors the PpChartPlot Shapes
// idiom (review/PpChartPlot.qml) with a discrete P1–P8 x-scale. Pure binding; all values via Theme.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    // strip = { name, sub, source, confidence, poleTop, poleBottom,
    //           points:[{ value, rag, available, banded, bandLo, bandHi }] }
    property var strip: ({})
    property int selected: 0
    // positions = [{ phase, name, tag, note, available }] — supplies the x-axis phase labels.
    property var positions: []

    readonly property var _points: strip.points || []
    readonly property int _n: 8
    function _tag(i) { return (root.positions[i] && root.positions[i].tag) ? root.positions[i].tag : "" }

    // Natural (minimum) plot height; the plot fills any extra height the layout grants so the
    // strip can stretch to fill its space. Constant here (not plot.height) to avoid a binding loop.
    readonly property real _minPlotH: Theme.sp(112)
    implicitHeight: head.implicitHeight + _minPlotH + foot.implicitHeight + Theme.sp(6)

    // ── RAG → colour / accessibility glyph (mirrors PpChartPlot._bandColor) ─────────
    function _ragColor(r) {
        return r === "green" ? Theme.colorRagGood
             : r === "amber" ? Theme.colorRagWatch
             : r === "red"   ? Theme.colorRagFault
             : r === "ref"   ? Theme.colorText3
             :                 Theme.colorRagNone
    }
    function _ragGlyph(r) {
        return r === "green" ? "●"   // ●
             : r === "amber" ? "▲"   // ▲
             : r === "red"   ? "■"   // ■
             :                 "◆"   // ◆ (ref / no data)
    }

    // ── Header: name · sub + Δ read-out at the selected position ────────────────────
    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        implicitHeight: Theme.sp(18)
        height: implicitHeight
        Text {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            text: (root.strip.name || "")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2; font.weight: Font.Medium
            color: Theme.colorText
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: nameMetrics.width + Theme.sp(6)
            anchors.verticalCenter: parent.verticalCenter
            text: (root.strip.sub || "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }
        TextMetrics { id: nameMetrics; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                      font.weight: Font.Medium; text: (root.strip.name || "") }
        Text {
            id: readOut
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            readonly property var sp: root._points[root.selected]
            text: (sp && sp.available)
                  ? ((sp.value > 0 ? "+" : "") + Math.round(sp.value) + "° at " + root._tag(root.selected))
                  : qsTr("— no data")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
            color: Theme.colorText2
        }
    }

    // ── Plot ────────────────────────────────────────────────────────────────────────
    Item {
        id: plot
        anchors { left: parent.left; right: parent.right
                  top: head.bottom; topMargin: Theme.sp(2)
                  bottom: foot.top; bottomMargin: Theme.sp(2) }

        readonly property real padL: Theme.sp(8)
        readonly property real padR: Theme.sp(8)
        readonly property real padT: Theme.sp(10)
        readonly property real padB: Theme.sp(18)          // P-labels row
        readonly property real plotL: padL
        readonly property real plotR: width - padR
        readonly property real plotT: padT
        readonly property real plotB: height - padB
        readonly property real plotW: Math.max(1, plotR - plotL)
        readonly property real plotH: Math.max(1, plotB - plotT)

        function xAt(i) { return plotL + i * (plotW / (root._n - 1)) }

        // value range over available points + band corridor (always includes 0)
        readonly property var range: {
            var lo = 0, hi = 0
            var pts = root._points
            for (var i = 0; i < pts.length; ++i) {
                var p = pts[i]
                if (p.available) { lo = Math.min(lo, p.value); hi = Math.max(hi, p.value) }
                if (p.banded)    { lo = Math.min(lo, p.bandLo); hi = Math.max(hi, p.bandHi) }
            }
            var pad = ((hi - lo) * 0.12) || 1
            return { lo: lo - pad, hi: hi + pad }
        }
        function yAt(v) {
            var r = range
            return (r.hi > r.lo) ? plotB - (v - r.lo) / (r.hi - r.lo) * plotH : plotB - plotH / 2
        }

        // Shaded band corridor — closed polygon: upper edge (bandHi) forward, lower (bandLo) back.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: Theme.colorBandGreen
                strokeColor: "transparent"
                PathPolyline {
                    path: {
                        var up = [], dn = [], pts = root._points
                        for (var i = 0; i < pts.length; ++i)
                            if (pts[i].banded) up.push(Qt.point(plot.xAt(i), plot.yAt(pts[i].bandHi)))
                        for (var j = pts.length - 1; j >= 0; --j)
                            if (pts[j].banded) dn.push(Qt.point(plot.xAt(j), plot.yAt(pts[j].bandLo)))
                        return up.concat(dn)
                    }
                }
            }
        }

        // Zero (address) reference line.
        Rectangle {
            x: plot.plotL; width: plot.plotW
            y: plot.yAt(0); height: 1
            color: Theme.colorBorderMid; opacity: 0.6
        }

        // Position cursor.
        Rectangle {
            width: 1; y: 0; height: plot.plotB + Theme.sp(2)
            x: plot.xAt(root.selected)
            color: Theme.colorBorderStrong; opacity: 0.7
        }

        // Compare-to ghost (previous swing) — dashed, drawn under the player line.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.colorText3
                strokeWidth: Theme.sp(1.5)
                fillColor: "transparent"
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 4]
                PathPolyline {
                    path: {
                        var out = [], pts = root._points
                        for (var i = 0; i < pts.length; ++i)
                            if (pts[i].ghost !== undefined)
                                out.push(Qt.point(plot.xAt(i), plot.yAt(pts[i].ghost)))
                        return out
                    }
                }
            }
        }

        // Player line (connects available points).
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.colorText
                strokeWidth: Theme.sp(2)
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline {
                    path: {
                        var out = [], pts = root._points
                        for (var i = 0; i < pts.length; ++i)
                            if (pts[i].available) out.push(Qt.point(plot.xAt(i), plot.yAt(pts[i].value)))
                        return out
                    }
                }
            }
        }

        // Shape-coded RAG markers (accessibility — colour is never the only channel).
        Repeater {
            model: root._points
            delegate: Text {
                required property var modelData
                required property int index
                readonly property real vy: modelData.available ? plot.yAt(modelData.value) : plot.yAt(0)
                x: plot.xAt(index) - width / 2
                y: vy - height / 2
                text: root._ragGlyph(modelData.rag)
                font.family: Theme.fontSymbol
                font.pixelSize: Theme.fontSzDataSm
                color: root._ragColor(modelData.rag)
            }
        }

        // Y-axis consequence poles (design §8.4 add).
        Text {
            x: plot.plotL + Theme.sp(2); y: plot.plotT
            text: "↑ " + (root.strip.poleTop || "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }
        Text {
            x: plot.plotL + Theme.sp(2); y: plot.plotB - height - Theme.sp(2)
            text: "↓ " + (root.strip.poleBottom || "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }

        // Phase labels (ADR … FLW) from the supplied positions.
        Repeater {
            model: root._n
            delegate: Text {
                required property int index
                x: plot.xAt(index) - width / 2
                y: plot.plotB + Theme.sp(4)
                text: root._tag(index)
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                color: index === root.selected ? Theme.colorText : Theme.colorText3
            }
        }
    }

    // Accessible description carries the pole terms (the QML analog of the mockup's aria-label).
    Accessible.role: Accessible.Chart
    Accessible.name: (root.strip.name || "") + " " + (root.strip.sub || "")
    Accessible.description: qsTr("up toward %1, down toward %2")
                            .arg(root.strip.poleTop || "").arg(root.strip.poleBottom || "")

    // ── Footer: source + confidence pips ────────────────────────────────────────────
    Row {
        id: foot
        anchors { left: parent.left; bottom: parent.bottom }
        spacing: Theme.sp(2)
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: (root.strip.source || "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color: Theme.colorText3
        }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.sp(0.5)
            Repeater {
                model: 4
                delegate: Rectangle {
                    required property int index
                    width: Theme.sp(7); height: Theme.sp(2); radius: Theme.sp(0.5)
                    color: index < Math.round((root.strip.confidence || 0) * 4)
                           ? Theme.colorText2 : Theme.colorBorderStrong
                }
            }
        }
    }
}
