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

// THE VALUE RUN: the tick run with its values put back. One mark per shot in the order they were
// struck, at the height of its reading, over the grade bands as horizontal stripes, with the
// session's rolling median drawn through them.
//
// IT KEEPS EVERY PROMISE THE TICK RUN MAKES (PpTickRun's header), because it replaces it on the
// card and is the same claim drawn with more in it:
//   · a shot that could not be measured is a SHORT GREY STUB on the baseline (no longer outlined:
//     at card size an outlined box read as a "0") — present,
//     obviously different, never a gap. A gap would say nothing happened on that swing;
//   · fired and clean keep the colours they have everywhere else on the panel;
//   · the reviewed shot is the WIDE OUTLINED mark, not a colour change: the colour already means
//     fired or clean and a selection that recoloured it would overwrite what it points at.
//
// THE MEDIAN IS TRAILING (session_spread.h trailingMedians) over the ledger's resolving window,
// so the line at shot 30 is what the golfer had been doing as of shot 30. Not-assessable shots
// add nothing to it and the line carries on across them.
//
// A TAP PICKS THAT SHOT, the same request a carousel click makes: `shotPicked` goes up to the
// panel, which hands it to the carousel's own path (PpSessionDiagnosticsPanel). This file selects
// nothing itself — the carousel owns the selection and every surface reads it back.
//
// Every mark's position is the model's (SessionDiagnosticsModel::spreadFor: `run`, `median`,
// `bands`, `faultLines`); this file scales fractions to pixels.

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property var spread: null
    property real fit: 1.0
    property bool large: false
    property bool interactive: true

    // index into `run`, the model's shot id, and its swing folder — the identity the carousel shares.
    signal shotPicked(int index, int shotId, string swingDir)

    // THE LINKED HOVER (PpCorridorStrip's header): the owner holds the hovered shot, this run
    // reports what is under the pointer and draws what it is handed. It never reads the strip.
    property int hoverIndex: -1
    signal hovered(int index)
    // A tooltip with the shot's readout, for where the owner draws no readout line of its own
    // (a card too short for one). Off where the line is on screen: the same words twice is noise.
    property bool tooltip: false

    objectName: "sdValueRun"

    function px(n) { return Math.round(n * Theme.fontScale * root.fit) }
    readonly property int tzCaption: Math.max(1, Math.round(Theme.sp(8) * fit))

    readonly property var _run:    spread ? (spread.run || []) : []
    readonly property var _median: spread ? (spread.median || []) : []
    readonly property var _bands:  spread ? (spread.bands || []) : []
    readonly property var _faults: spread ? (spread.faultLines || []) : []
    readonly property int count: _run.length

    // PpTickRun's own metrics, through the same two factors, so the not-assessable tick and the
    // selected mark are the same shapes the reader learned on the tick run.
    readonly property real _unit:   Theme.fontScale * root.fit
    readonly property real _tickW:  2.5 * _unit
    readonly property real _naW:    Math.max(1, Math.round(1.2 * _unit))
    readonly property int  _shortH: Math.max(1, Math.round(5 * _unit))
    readonly property real _selW:   6.0 * _unit
    readonly property real _dotD:   large ? px(6) : px(4)
    // ── the Y labels (large only) ────────────────────────────────────────────
    //
    // Round values in the measure's unit (spreadFor: yTicks, the axis's own decimals), in a gutter
    // on the left that the marks never enter. The unit is not repeated on every tick; the detail's
    // legend under the run names it once.
    readonly property var  _ticks: (large && spread) ? (spread.yTicks || []) : []
    readonly property string _longestTick: _ticks.reduce((a, t) => (t.text || "").length > a.length ? (t.text || "") : a, "")
    TextMetrics { id: tickMetrics; font.family: Theme.fontData; font.pixelSize: root.tzCaption; text: root._longestTick }
    readonly property real _gutter: _ticks.length > 0 ? Math.ceil(tickMetrics.advanceWidth) + px(8) : 0
    readonly property real _pw: Math.max(1, width - _gutter)
    readonly property real _pitch:  count > 0 ? _pw / count : _pw

    // Large is the condition detail's: TWICE the first round's 120, because the run's Y axis was
    // too condensed to read values off, and the detail scrolls — height is not what it is short of.
    implicitHeight: large ? px(240) : px(30)
    visible: count > 0

    // The bands are the corridor strip's (Theme.corridorFill); the strip above the run names them
    // in words.

    // A value of 1 is the top of the axis; the plot reserves the not-assessable tick's height at
    // the bottom so a reading at the axis floor never sits on top of one.
    readonly property real _plotH: Math.max(1, height - _shortH - px(1))
    function _y(fy) { return _plotH - fy * _plotH }

    // ── bands, as stripes ────────────────────────────────────────────────────
    Repeater {
        model: root._bands
        Rectangle {
            required property var modelData
            objectName: "sdRunBand"
            readonly property string grade: modelData.grade || ""
            y: root._y(modelData.f1 || 0)
            x: root._gutter; width: root._pw
            height: Math.max(0, root._y(modelData.f0 || 0) - root._y(modelData.f1 || 0))
            color: Theme.corridorFill(grade)
        }
    }
    Repeater {
        model: root._faults
        Rectangle {
            required property var modelData
            objectName: "sdRunFaultLine"
            // Over the marks, so its label chip is too; the line itself is a hairline.
            z: 5
            y: root._y(modelData.f || 0)
            x: root._gutter; width: root._pw
            height: 1
            color: Theme.colorWarn
            opacity: 0.7
            // ⚠ UNLABELLED, ON PURPOSE. A label here sat on the run's own dots wherever it went —
            // a session's readings lie all along this line — and the strip directly above names
            // the same line in the same colour, at the same value.
        }
    }
    Repeater {
        model: root._ticks
        Item {
            required property var modelData
            objectName: "sdRunYTick"
            readonly property string text: modelData.text || ""
            readonly property real ty: root._y(modelData.f || 0)
            // A faint gridline across the plot, and the value in the gutter beside it.
            Rectangle {
                x: root._gutter
                y: parent.ty
                width: root._pw
                height: 1
                color: Theme.gridColor(Theme.colorBorderMid)
                opacity: Theme.gridOpacity(0.5)
            }
            Text {
                text: parent.text
                width: root._gutter - root.px(5)
                horizontalAlignment: Text.AlignRight
                y: Math.max(0, Math.min(root.height - implicitHeight, parent.ty - implicitHeight / 2))
                font.family: Theme.fontData
                font.pixelSize: root.tzCaption
                color: Theme.colorText2
            }
        }
    }
    // The baseline the short ticks stand on.
    Rectangle {
        y: root.height - 1
        x: root._gutter; width: root._pw
        height: 1
        color: Theme.baselineColor(Theme.colorBorderMid)
        opacity: Theme.baselineOpacity(1.0)
    }

    // ── the hovered column, so a tap target is visible before it is pressed ──
    Rectangle {
        id: hoverCol
        visible: root.hoverIndex >= 0 && root.hoverIndex < root.count
        x: root._gutter + root.hoverIndex * root._pitch
        width: Math.max(1, root._pitch)
        height: root.height
        color: Theme.colorAccent
        opacity: 0.12
    }

    // ── the selected shot: the tick run's wide outlined mark, full height ────
    Repeater {
        model: root._run
        Rectangle {
            required property var modelData
            required property int index
            objectName: "sdRunSelected"
            visible: modelData.selected === true
            x: root._gutter + (modelData.fx || 0) * root._pw - width / 2
            width: Math.max(root._selW, root._dotD + root.px(3))
            height: root.height
            color: "transparent"
            radius: Math.max(1, Math.round(root._unit))
            border.width: 1
            border.color: Theme.colorText
            z: 2
        }
    }

    // ── the rolling median ───────────────────────────────────────────────────
    Shape {
        objectName: "sdRunMedian"
        anchors.fill: parent
        visible: root._median.length > 1
        preferredRendererType: Shape.CurveRenderer
        z: 1
        ShapePath {
            strokeColor: Theme.colorText2
            strokeWidth: Math.max(1, root.px(Theme.curveWidth(root.large ? 2 : 1.5)))
            fillColor: "transparent"
            joinStyle: ShapePath.RoundJoin
            capStyle: ShapePath.RoundCap
            PathPolyline {
                path: root._median.map(p => Qt.point(root._gutter + p.fx * root._pw, root._y(p.fy)))
            }
        }
    }

    // ── one mark per shot ────────────────────────────────────────────────────
    Repeater {
        id: markRep
        model: root._run
        Item {
            id: mark
            required property var modelData
            required property int index
            objectName: "sdRunMark"
            readonly property bool assessable: modelData.assessable === true
            readonly property bool fired: modelData.state === "fired"
            readonly property bool ringed: modelData.current === true || modelData.selected === true
            readonly property bool hot: index === root.hoverIndex
            readonly property real d: (ringed || hot) ? root._dotD * 1.7 : root._dotD
            z: hot ? 5 : ringed ? 4 : 3
            x: root._gutter + (modelData.fx || 0) * root._pw - width / 2
            width:  assessable ? d : root._naW
            height: assessable ? d : root._shortH
            y: assessable ? root._y(modelData.fy || 0) - d / 2 : root.height - height - 1

            // NOT MEASURED IS A BARE GREY STUB, not an outlined box. At card size a 2.5 × 5 px
            // outlined rectangle drew as a "0" — a zero, on the one mark whose whole meaning is
            // "there is no number" (missing metrics show "-", never 0). A solid stub in the quiet
            // text colour has no inside to read as a digit, and is still present: never a gap.
            Rectangle {
                anchors.fill: parent
                radius: mark.assessable ? width / 2 : 0
                color: !mark.assessable ? Theme.colorText3
                     : (mark.fired ? Theme.colorWarn : Theme.colorGood)
                opacity: mark.assessable ? 1.0 : 0.6
                border.width: mark.assessable && (mark.ringed || mark.hot) ? Math.max(1, root.px(mark.hot ? 2 : 1)) : 0
                border.color: mark.hot ? Theme.colorAccent : Theme.colorText
            }
            // The hover on an unmeasured stub: an accent outline round it, wider than the stub.
            Rectangle {
                visible: !mark.assessable && mark.hot
                anchors.centerIn: parent
                width: parent.width + root.px(4)
                height: parent.height + root.px(4)
                color: "transparent"
                border.width: Math.max(1, root.px(1.5))
                border.color: Theme.colorAccent
            }
        }
    }

    // ── picking a shot ───────────────────────────────────────────────────────
    //
    // THE NEAREST MARK'S CENTRE within a radius — the same rule the strip uses, and for the same
    // reason (PpCorridorStrip, "picking"): a linear scan over the marks, which is nothing at a
    // session's few hundred shots. An unmeasured shot's stub is a mark like any other, so it can
    // be hovered and read ("-") and picked. Nothing within reach refuses the press, which then
    // falls through to the card and opens the condition, as a press on empty space always has.
    MouseArea {
        id: tap
        objectName: "sdRunTap"
        anchors.fill: parent
        enabled: root.interactive && root.count > 0
        hoverEnabled: true
        readonly property real radius: Math.max(root.px(12), root._dotD * 1.5)
        property int near: -1
        cursorShape: near >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        function nearestAt(mx, my) {
            let best = -1, bestD = radius * radius
            for (let i = 0; i < markRep.count; ++i) {
                const it = markRep.itemAt(i)
                if (!it) continue
                const dx = it.x + it.width / 2 - mx, dy = it.y + it.height / 2 - my
                const d2 = dx * dx + dy * dy
                if (d2 <= bestD) { bestD = d2; best = i }
            }
            return best
        }
        // Same rule as the strip's: clear the owner's hover on leaving only if it is still ours.
        property int sent: -1
        function track(mx, my) {
            near = nearestAt(mx, my)
            if (near >= 0 || root.hoverIndex === sent) { sent = near; root.hovered(near) }
        }
        onEntered: track(mouseX, mouseY)
        onPositionChanged: (mouse) => track(mouse.x, mouse.y)
        onExited: { near = -1; if (sent >= 0 && root.hoverIndex === sent) root.hovered(-1); sent = -1 }
        onPressed: (mouse) => { if (nearestAt(mouse.x, mouse.y) < 0) mouse.accepted = false }
        onClicked: (mouse) => root.pick(nearestAt(mouse.x, mouse.y))
        ToolTip.visible: root.tooltip && containsMouse && near >= 0
        ToolTip.delay: 300
        ToolTip.text: near >= 0 && near < root.count ? (root._run[near].readout || "") : ""
    }

    // The tap's one action, callable so a probe can exercise the real path without a pointer.
    function pick(i) {
        if (i < 0 || i >= root.count) return
        const r = root._run[i]
        root.shotPicked(i, r.shotId !== undefined ? r.shotId : -1, r.swingDir || "")
    }
    // The nearest mark to a point in this run's coordinates, for a probe or a test.
    function nearestMark(x, y) { return tap.nearestAt(x, y) }
}
