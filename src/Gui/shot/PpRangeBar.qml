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

// A reusable corridor bar — a muted track carrying a nested amber/green band pair
// and a coloured marker for where the current value falls. Replaces bare numbers
// with a "where does it sit" read. Shared by the Verdict zone's tempo-ratio call-out
// (full size, axis labels) and the Setup zone's per-metric mini band-bar (compact,
// caller draws its own label/value row): `compact` toggles size and text, everything
// else — domain math, band tinting, marker placement — is identical either way.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property real   value: 0
    property real   greenLo: 0
    property real   greenHi: 0
    property real   amberLo: 0
    property real   amberHi: 0
    property string band: ""            // "green" | "yellow" | "red" | ""  → marker/value tint
    property string unit: ""
    property bool   compact: false       // true = Setup mini-bar (short, no axis labels)
    property bool   hasValue: true       // false = corridor drawn neutral, marker hidden

    implicitHeight: compact ? Theme.sp(22) : Theme.sp(40)

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

    readonly property color _markerColor: _bandColor(band)

    // Domain = amber band padded 12% each side so the amber band reads as a band,
    // not the whole track (mirrors NormativeBar's corridor read). Falls back to the
    // green band when amber is degenerate (zero/negative span — the unconfigured
    // default), then to value±1 when both are degenerate. Never zero-span, never NaN.
    readonly property real _amberSpan:  amberHi - amberLo
    readonly property bool _amberValid: isFinite(_amberSpan) && _amberSpan > 1e-9
    readonly property real _greenSpan:  greenHi - greenLo
    readonly property bool _greenValid: isFinite(_greenSpan) && _greenSpan > 1e-9

    readonly property real _baseLo:   _amberValid ? amberLo : (_greenValid ? greenLo : value - 1)
    readonly property real _baseHi:   _amberValid ? amberHi : (_greenValid ? greenHi : value + 1)
    readonly property real _baseSpan: Math.max(1e-6, _baseHi - _baseLo)
    readonly property real _pad:      _baseSpan * 0.12
    readonly property real _domLo:    _baseLo - _pad
    readonly property real _domHi:    _baseHi + _pad
    readonly property real _domSpan:  Math.max(1e-6, _domHi - _domLo)

    // Domain value → 0..1 fraction, clamped — this is what keeps the marker pinned
    // inside the corridor even when `value` sits outside the configured bounds.
    function _fx(v) {
        return isFinite(v) ? Math.max(0, Math.min(1, (v - _domLo) / _domSpan)) : 0
    }

    // ── Value + unit read, above the marker (non-compact only) ────────────────
    Text {
        id: valueText
        visible: !root.compact && root.hasValue
        text: root._fmtVal(root.value) + (root.unit.length ? (" " + root.unit) : "")
        font.family:    Theme.fontData
        font.pixelSize: Theme.fontSzBody2
        color: root._markerColor
        anchors.top: parent.top
        x: Math.max(0, Math.min(root.width - width, root.width * root._fx(root.value) - width / 2))
    }

    // ── Track — amber margin + green core over a muted rail ───────────────────
    Item {
        id: track
        anchors.left:  parent.left
        anchors.right: parent.right
        // Compact: centred in the whole (short) item. Full: sits below the value
        // read, leaving room for the tick row below.
        anchors.top:            root.compact ? undefined : valueText.bottom
        anchors.topMargin:      root.compact ? 0 : Theme.sp(2)
        anchors.verticalCenter: root.compact ? parent.verticalCenter : undefined
        height:  root.compact ? Theme.sp(6) : Theme.sp(9)
        opacity: root.hasValue ? 1.0 : 0.5

        Rectangle {
            id: rail
            anchors.fill: parent
            radius: height / 2
            color:  Theme.colorBg2
        }

        Rectangle {
            id: amberBand
            visible: root.hasValue
            x:      parent.width * root._fx(root.amberLo)
            width:  Math.max(0, parent.width * (root._fx(root.amberHi) - root._fx(root.amberLo)))
            height: parent.height
            radius: height / 2
            color:  Theme.colorBandAmber
        }

        Rectangle {
            id: greenBand
            visible: root.hasValue
            x:      parent.width * root._fx(root.greenLo)
            width:  Math.max(0, parent.width * (root._fx(root.greenHi) - root._fx(root.greenLo)))
            height: parent.height
            radius: height / 2
            color:  Theme.colorBandGreen
        }

        // ── Marker — vertical stroke at the current value, nested in the track's
        // local coordinate space so its x math needs no extra offset.
        Shape {
            id: marker
            visible: root.hasValue
            preferredRendererType: Shape.CurveRenderer
            width:  Theme.sp(2)
            height: track.height + Theme.sp(6)
            y: -Theme.sp(3)
            x: track.width * root._fx(root.value) - width / 2

            Behavior on x { NumberAnimation { duration: Theme.durationNormal } }

            ShapePath {
                strokeColor: root._markerColor
                strokeWidth: Theme.sp(2)
                capStyle:    ShapePath.RoundCap
                fillColor:   "transparent"
                startX: marker.width / 2
                startY: 0
                PathLine { x: marker.width / 2; y: marker.height }
            }
        }
    }

    // ── Corridor bounds — greenLo left, greenHi right (non-compact only) ──────
    Item {
        id: tickRow
        visible: !root.compact
        anchors.left:      parent.left
        anchors.right:     parent.right
        anchors.top:       track.bottom
        anchors.topMargin: Theme.sp(4)
        height: visible ? loTick.implicitHeight : 0

        Text {
            id: loTick
            anchors.left: parent.left
            text: root._fmtVal(root.greenLo)
            font.family:      Theme.fontData
            font.pixelSize:   Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color: Theme.colorText3
        }
        Text {
            anchors.right: parent.right
            text: root._fmtVal(root.greenHi)
            font.family:      Theme.fontData
            font.pixelSize:   Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color: Theme.colorText3
        }
    }
}
