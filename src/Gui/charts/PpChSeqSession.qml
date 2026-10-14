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

// PPCHSEQSESSION — the sequence across the session (design §2.2): an ORDER BARCODE (each segment's rank on
// every shot, a late rank in warn and underlined — a mark plus a word in the head line) and a PEAK-TIME DOT
// PLOT on one ms axis (each shot a PpMark, late ones warn, the median a rule, this shot ringed; marks that
// would overprint stack upward). Every number is ChartsTracesPresenter.session.sequence's; the stand-first
// sentence (stand) says the order count and the late pelvis neutrally. Hover a shot and it lights in both.
// objectNames: chSeqBarcode, chSeqDots.
Column {
    id: ss
    property QtObject roles: null
    property var brush: null
    property var sequence: null                  // session.sequence
    property bool interactive: true
    property bool atom: false
    readonly property var keyKinds: ["ring", "fired"]
    signal shotActivated(int ordinal)
    spacing: roles ? roles.u(10) : 10
    readonly property var segs: sequence && sequence.segments ? sequence.segments : []
    readonly property int nShots: segs.length ? (segs[0].cells || []).length : 0
    readonly property real em: roles ? roles.em : 14
    readonly property real nameW: Math.round(6.5 * em)
    readonly property bool figsSide: roles ? roles.atLeast(width, "compact") : true
    readonly property real figW: figsSide ? Math.round(15 * em) : 0
    readonly property real plotW: Math.max(40, width - nameW - figW - (figsSide && roles ? roles.gutter(width) : 0))
    readonly property real pitch: roles ? Math.min(roles.u(24), plotW / Math.max(1, nShots)) : 10
    readonly property real cellH: roles ? roles.u(17) : 17
    readonly property real lo: sequence && sequence.lo !== undefined && sequence.lo !== null ? sequence.lo : -100
    readonly property real hi: sequence && sequence.hi !== undefined && sequence.hi !== null ? sequence.hi : 100
    function xOf(ms) { return nameW + (ms - lo) / Math.max(1e-9, hi - lo) * plotW }
    readonly property real dotRowH: roles ? roles.u(28) : 28
    // the presenter's ms ticks, thinned by width
    readonly property var xt0: sequence && sequence.ticks ? sequence.ticks : []
    readonly property int xStep: Math.max(1, Math.ceil(xt0.length / Math.max(3, Math.floor(plotW / (5 * em)))))
    readonly property var xt: xt0.filter(function (t, i) { return i % xStep === 0 })
    visible: !!sequence && sequence.present === true

    PpChText { roles: ss.roles; sentence: ss.sequence ? ss.sequence.stand : null; measure: false; width: Math.min(ss.width, ss.roles ? ss.roles.measureW(ss.width) * 1.2 : ss.width) }

    // ── the order barcode ──
    Item {
        objectName: "chSeqBarcode"
        width: ss.width; height: ss.cellH * (ss.segs.length + 1) + (ss.roles ? ss.roles.u(6) : 6)
        Text { y: 0; text: "Order"; font: ss.roles ? ss.roles.fLabel : Qt.font({}); color: ss.roles ? ss.roles.ink3 : "grey" }
        Repeater {
            model: ss.roles && ss.segs.length ? ss.segs[0].cells : []
            delegate: Item {
                required property var modelData
                required property int index
                readonly property int ord: modelData.ordinal
                x: ss.nameW + index * ss.pitch; width: ss.pitch; height: parent ? parent.height : 0
                Rectangle { anchors.fill: parent; anchors.topMargin: ss.cellH - ss.roles.u(2)
                            visible: !!ss.brush && (ss.brush.isPicked(parent.ord) || ss.brush.isHovered(parent.ord))
                            color: ss.brush && ss.brush.isPicked(parent.ord) ? ss.roles.pickWash : ss.roles.hoverWash }
                Text { anchors.horizontalCenter: parent.horizontalCenter
                       visible: ss.roles.axisLabelShown(index, ss.nShots, ss.roles.shotLabelStep(ss.pitch), [ss.brush ? ss.brush.pickedShot - 1 : -1])
                       text: String(parent.ord); font: ss.roles.fFigLabel; color: ss.brush && ss.brush.isPicked(parent.ord) ? ss.roles.ink : ss.roles.ink3 }
                MouseArea { enabled: ss.interactive; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onContainsMouseChanged: if (ss.brush) ss.brush.hoverShot(parent.ord, containsMouse)
                            onClicked: ss.shotActivated(parent.ord) }
            }
        }
        Repeater {
            model: ss.roles ? ss.segs : []
            delegate: Item {
                id: br
                required property var modelData
                required property int index
                y: ss.cellH * (index + 1); width: ss.width; height: ss.cellH
                Text { anchors.verticalCenter: parent.verticalCenter; text: br.modelData.word; font: ss.roles.fSmall; color: ss.roles.ink2 }
                Repeater {
                    model: br.modelData.cells || []
                    delegate: Text {
                        required property var modelData
                        required property int index
                        x: ss.nameW + index * ss.pitch; width: ss.pitch; horizontalAlignment: Text.AlignHCenter
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: ss.brush ? ss.brush.shotOpacity(modelData.ordinal, ss.roles.dim) : 1
                        text: modelData.text; font: ss.roles.fFigS
                        color: modelData.late ? ss.roles.warn : modelData.rank > 0 ? ss.roles.ink : ss.roles.ink3
                        Rectangle { visible: modelData.late; anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.baseline; anchors.topMargin: 1
                                    width: Math.min(parent.width - 2, ss.roles.u(9)); height: 1; color: ss.roles.warn }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; x: ss.nameW; width: ss.nShots * ss.pitch; height: ss.roles.hw; color: ss.roles.hair }
            }
        }
    }

    // ── the peak-time dot plot, one ms axis ──
    Item {
        objectName: "chSeqDots"
        width: ss.width; height: (ss.roles ? ss.roles.u(16) : 16) + ss.segs.length * ss.dotRowH + (ss.roles ? ss.roles.u(30) : 30)
        readonly property real topY: ss.roles ? ss.roles.u(16) : 16
        Text { text: ss.roles ? ss.roles.labelUnit("Peak time from impact", "ms") : ""; font: ss.roles ? ss.roles.fLabelUnit : Qt.font({}); color: ss.roles ? ss.roles.ink3 : "grey" }
        Rectangle { x: Math.round(ss.xOf(0)); y: parent.topY; width: ss.roles ? ss.roles.hw : 1; height: ss.segs.length * ss.dotRowH; color: ss.roles ? ss.roles.ink2 : "grey" }
        Repeater {
            model: ss.roles && ss.sequence ? (ss.sequence.pTicks || []) : []
            delegate: Item {
                required property var modelData
                Rectangle { x: Math.round(ss.xOf(modelData.ms)); y: ss.roles.u(16); width: ss.roles.hw; height: ss.segs.length * ss.dotRowH; color: ss.roles.hair }
                Text { x: ss.xOf(modelData.ms) - width / 2; y: ss.roles.u(16) + ss.segs.length * ss.dotRowH + ss.roles.u(2); text: modelData.tag; font: ss.roles.fFigLabel; color: ss.roles.ink3 }
            }
        }
        Text { visible: !!ss.roles; x: ss.xOf(0) + (ss.roles ? ss.roles.u(3) : 3); y: (ss.roles ? ss.roles.u(16) : 16) + ss.segs.length * ss.dotRowH + (ss.roles ? ss.roles.u(2) : 2)
               text: "P7 impact"; font: ss.roles ? ss.roles.fFigLabel : Qt.font({}); color: ss.roles ? ss.roles.ink2 : "grey" }
        Repeater {
            model: ss.roles ? ss.xt : []
            delegate: Text {
                required property var modelData
                visible: Math.abs(modelData.v) > 1e-9
                x: ss.xOf(modelData.v) - width / 2; y: ss.roles.u(16) + ss.segs.length * ss.dotRowH + ss.roles.u(14)
                text: modelData.text; font: ss.roles.fFigLabel; color: ss.roles.ink3
            }
        }
        Repeater {
            model: ss.roles ? ss.segs : []
            delegate: Item {
                id: drow
                required property var modelData
                required property int index
                y: ss.roles.u(16) + index * ss.dotRowH; width: ss.width; height: ss.dotRowH
                // marks that would overprint stack upward (layout only: positions from the presenter's ms)
                readonly property var stacked: {
                    const out = [], w = ss.roles.markFired + 1, seen = []
                    const pts = (drow.modelData.points || []).slice().sort(function (a, b) { return a.ms - b.ms })
                    for (const p of pts) {
                        const x = ss.xOf(p.ms); let k = 0
                        while (seen.some(function (q) { return Math.abs(q.x - x) < w && q.k === k })) k++
                        seen.push({ x: x, k: k }); out.push({ x: x, k: k, p: p })
                    }
                    return out
                }
                readonly property int maxK: Math.max(1, Math.max.apply(null, [0].concat(stacked.map(function (s) { return s.k }))))
                readonly property real step: Math.min(ss.roles.markFired * 0.9, (ss.dotRowH - ss.roles.u(10)) / maxK)
                Text { anchors.verticalCenter: parent.verticalCenter; text: drow.modelData.word; font: ss.roles.fSmall; color: ss.roles.ink2 }
                Rectangle { x: ss.nameW; anchors.bottom: parent.bottom; width: ss.plotW; height: ss.roles.hw; color: ss.roles.hair }
                Text { visible: drow.modelData.never === true; x: ss.nameW + ss.roles.u(4); anchors.verticalCenter: parent.verticalCenter
                       text: drow.modelData.figText || ""; font: ss.roles.fSmall; color: ss.roles.ink3 }
                Rectangle { visible: drow.modelData.median !== null && drow.modelData.median !== undefined && drow.modelData.never !== true
                            x: Math.round(ss.xOf(drow.modelData.median || 0)) - 0.75; width: 1.5; height: parent.height * 0.7; y: parent.height * 0.15; color: ss.roles.ink }
                Repeater {
                    model: drow.stacked
                    delegate: PpMark {
                        required property var modelData
                        roles: ss.roles; brush: ss.brush; shotId: modelData.p.ordinal; fired: modelData.p.late === true
                        cx: modelData.x; cy: drow.height - ss.roles.u(5) - modelData.k * drow.step
                        interactive: ss.interactive
                        onActivated: ss.shotActivated(modelData.p.ordinal)
                    }
                }
                Text {
                    visible: ss.figsSide && drow.modelData.never !== true
                    x: ss.width - ss.figW; width: ss.figW; anchors.verticalCenter: parent.verticalCenter
                    font: ss.roles.fFigS; color: ss.roles.ink2; wrapMode: Text.WordWrap
                    text: drow.modelData.figText || ""
                }
            }
        }
    }
    Flow {
        visible: !ss.figsSide
        width: ss.width; spacing: ss.roles ? ss.roles.u(12) : 12
        Repeater {
            model: ss.roles && !ss.figsSide ? ss.segs : []
            delegate: Row {
                required property var modelData
                spacing: ss.roles.u(4)
                Text { text: modelData.word; font: ss.roles.fSmall; color: ss.roles.ink2 }
                Text { text: modelData.figText || ""; font: ss.roles.fFigLabel; color: ss.roles.ink3 }
            }
        }
    }
}
