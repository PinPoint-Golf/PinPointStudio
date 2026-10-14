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

import QtQuick
import PinPointStudio

// PPCHREADINGTRACE — one reading's curve through this swing, set against the session AT ITS INSTANTS
// (ChartsRelationsPresenter TRACE; B's Trace): the 40 ms windowed mean in the metric's hue (the figures come
// from it), the raw samples faint behind it, a hairline at every P-position, and at each P-position the
// reading is taken at a small upright box of all the shots there with this shot ringed. The playhead follows
// the video; a click or drag seeks (seekUsAtShotMs). The value at the playhead is the presenter's text.
// objectName: chReadingTrace (+ readingId).
Item {
    id: tr
    objectName: "chReadingTrace"
    property QtObject roles: null
    property var brush: null
    property QtObject presenter: null            // ChartsRelationsPresenter: seekUsAtShotMs, ticks
    property var trace: null
    property bool interactive: false
    property real playMs: NaN                    // (shotReplay.positionUs − status.pickedImpactUs) / 1000
    property string playText: ""                 // playhead.openValueText / leadValueText
    property real plotH: roles ? roles.u(roles.isPrint ? 110 : 120) : 120
    property bool atom: true
    readonly property var keyKinds: ["box", "ring", "dots"]
    signal shotActivated(int ordinal)
    readonly property string readingId: trace ? trace.readingId : ""
    readonly property string key: trace ? trace.key : ""
    readonly property real t0: trace && trace.window ? trace.window.t0 : 0
    readonly property real t1: trace && trace.window ? trace.window.t1 : 1
    readonly property real lo: trace && trace.yRange ? trace.yRange.lo : 0
    readonly property real hi: trace && trace.yRange ? trace.yRange.hi : 1
    readonly property var yt: presenter && trace ? presenter.ticks(lo, hi, plotH > (roles ? roles.u(100) : 100) ? 4 : 3, trace.unit) : []
    readonly property var xt: presenter && trace && roles ? presenter.ticks(t0, t1, Math.max(2, Math.floor(pw / (6.5 * roles.em))), "ms") : []
    readonly property real gutL: roles ? roles.u(40) : 40
    readonly property real pTop: roles ? roles.u(18) : 18
    readonly property real inset: roles ? roles.u(8) : 8
    readonly property real pw: Math.max(10, width - gutL - (roles ? roles.u(10) : 10))
    function tx(t) { return gutL + inset + (t - t0) / Math.max(1e-9, t1 - t0) * (pw - 2 * inset) }
    function vy(v) { return pTop + (1 - (v - lo) / Math.max(1e-9, hi - lo)) * plotH }
    function tAt(px) { return t0 + Math.max(0, Math.min(1, (px - gutL - inset) / Math.max(1, pw - 2 * inset))) * (t1 - t0) }
    readonly property bool playIn: isFinite(playMs) && playMs >= t0 && playMs <= t1
    implicitHeight: pTop + plotH + (roles ? roles.u(34) : 34)
    height: implicitHeight

    Repeater {
        model: tr.roles ? tr.yt : []
        delegate: Item {
            required property var modelData
            Rectangle { x: tr.gutL; width: tr.pw; y: Math.round(tr.vy(modelData.v)); height: tr.roles.hw; color: tr.roles.grid; opacity: tr.roles.gridOpacity }
            Text { x: tr.gutL - width - tr.roles.u(5); y: tr.vy(modelData.v) - height / 2; text: modelData.text; font: tr.roles.fFigLabel; color: tr.roles.ink3 }
        }
    }
    Text { text: tr.trace ? (tr.trace.unitText || "") : ""; font: tr.roles ? tr.roles.fLabelUnit : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey" }
    Repeater {
        model: tr.roles && tr.trace ? (tr.trace.phases || []) : []
        delegate: Item {
            required property var modelData
            required property int index
            readonly property real px: Math.round(tr.tx(modelData.shotMs))
            Rectangle { x: parent.px; y: tr.pTop; width: tr.roles.hw; height: tr.plotH; color: tr.roles.grid; opacity: modelData.impact ? 1 : 0.55 }
            Text {
                readonly property real prev: index > 0 ? tr.tx(tr.trace.phases[index - 1].shotMs) : -1e9
                visible: parent.px - prev > tr.roles.u(19) || modelData.impact === true
                x: parent.px - width / 2; y: tr.pTop - height - tr.roles.u(2)
                text: modelData.tag; font: tr.roles.fFigLabel; color: modelData.impact ? tr.roles.ink2 : tr.roles.ink3
            }
        }
    }
    Repeater {
        model: tr.roles ? tr.xt : []
        delegate: Text {
            required property var modelData
            readonly property real px: tr.tx(modelData.v)
            visible: px - width / 2 > tr.gutL - 2 && px + width / 2 < tr.gutL + tr.pw - msLab.width - tr.roles.u(10)
            x: px - width / 2; y: tr.pTop + tr.plotH + tr.roles.u(5)
            text: modelData.text; font: tr.roles.fFigLabel; color: tr.roles.ink3
        }
    }
    Text { id: msLab; x: tr.gutL + tr.pw - width; y: tr.pTop + tr.plotH + (tr.roles ? tr.roles.u(5) : 5); text: "ms from impact"; font: tr.roles ? tr.roles.fLabel : Qt.font({}); color: tr.roles ? tr.roles.ink3 : "grey" }
    PpChPlot {
        x: tr.gutL + tr.inset; y: tr.pTop; width: tr.pw - 2 * tr.inset; height: tr.plotH
        roles: tr.roles; zeroLine: false
        x0: tr.t0; x1: tr.t1; lo: tr.lo; hi: tr.hi
        layers: !tr.roles || !tr.trace || !tr.trace.curve ? [] : [
            { k: "dots", t: tr.trace.curve.t, v: tr.trace.curve.raw, color: tr.roles.hue(tr.key), alpha: tr.roles.ch.rawDotAlpha, d: tr.roles.u(tr.roles.ch.rawDot) },
            { k: "line", t: tr.trace.curve.t, v: tr.trace.curve.v, color: tr.roles.hue(tr.key), w: tr.roles.ch.thisShotW, z: 3 }
        ]
    }
    MouseArea {
        enabled: tr.interactive && !!tr.presenter && typeof shotReplay !== "undefined"
        x: tr.gutL; y: tr.pTop; width: tr.pw; height: tr.plotH
        cursorShape: Qt.SizeHorCursor
        function seekTo(px) { const us = tr.presenter.seekUsAtShotMs(tr.tAt(px + tr.gutL)); if (us >= 0) shotReplay.seekToUs(us) }
        onPressed: (e) => { shotReplay.beginScrub(); seekTo(e.x) }
        onPositionChanged: (e) => { if (pressed) seekTo(e.x) }
        onReleased: shotReplay.endScrub()
        onCanceled: shotReplay.endScrub()
    }
    // the session at each instant the reading is taken
    Repeater {
        model: tr.roles && tr.trace ? (tr.trace.instants || []) : []
        delegate: PpChBoxCell {
            required property var modelData
            vertical: true; markScale: 0.85
            x: Math.round(tr.tx(modelData.shotMs) - width / 2); y: tr.pTop; width: tr.roles.u(12); height: tr.plotH
            lo: tr.lo; hi: tr.hi
            roles: tr.roles; brush: tr.brush; interactive: tr.interactive
            box: modelData.box; values: modelData.values; fired: modelData.firedShots || []
            color: tr.roles.hue(tr.key)
            onShotActivated: (o) => tr.shotActivated(o)
        }
    }
    Rectangle { visible: tr.playIn; x: Math.round(tr.tx(tr.playMs)); y: tr.pTop - tr.roles.u(2); width: 1; height: tr.plotH + tr.roles.u(2); color: tr.roles ? tr.roles.ink : "black"; opacity: 0.8 }
    Text {
        visible: tr.playIn && tr.playText !== ""
        readonly property real px: tr.tx(tr.playMs)
        x: px + width + tr.roles.u(6) > tr.gutL + tr.pw ? px - width - tr.roles.u(4) : px + tr.roles.u(4)
        y: tr.pTop + tr.roles.u(1)
        text: tr.playText; font: tr.roles ? tr.roles.fFigLabel : Qt.font({}); color: tr.roles ? tr.roles.ink : "black"
        Rectangle { z: -1; anchors.fill: parent; anchors.margins: -1; color: tr.roles ? tr.roles.knock : "transparent" }
    }
}
