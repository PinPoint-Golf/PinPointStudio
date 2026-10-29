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

// PPCHSHOTSHAPEGRID — the Club delivery section's shot shape (docs/design/club_delivery_chart_design.md §4.6): a 3×3
// grid, where the ball STARTED (left · straight · right of the target, from the monitor's face) across, how it CURVED
// (left · straight · right, from the face-to-path) down — the target's own left and right, so it reads the same for
// either hand; the shot's words ("Pull, slight fade") are the presenter's and mirror for a left-hander. This shot is
// the filled dot with a ring and its word; the session's shots are small dots in their cells, spread so a cell of
// many shows many. The monitor's values only: a shot the monitor gave no face is in no cell (its word "-").
// Hover a dot: its shot and words, under the grid; a click opens a session shot (shotActivated).
//   shot: SHAPE (thisShot.shape) | null — the ringed one; marks: session.shapes.marks; line: the words when nothing is
//   hovered (the session's commonest shape, or this shot's word).
// objectNames: chShotShapeGrid, chShapeCell (+ start, curve, count), chShapeThis, chShapeMark (+ ordinal), chShapeWords.
Column {
    id: gr
    objectName: "chShotShapeGrid"
    property QtObject roles: null
    property var shot: null
    property var marks: []
    property string line: ""
    property bool interactive: true
    // a cell takes its column head ("straight") whole
    property real cell: roles ? Math.max(roles.u(30), Math.ceil(fm.advanceWidth("straight")) + roles.u(6)) : 30
    signal shotActivated(int ordinal)
    readonly property bool ok: !!roles
    readonly property var cols: [-1, 0, 1]
    readonly property real labW: ok ? Math.ceil(Math.max(fm.advanceWidth("curves left"), fm.advanceWidth("curves right"))) + roles.u(8) : 60
    property string hoverText: ""
    spacing: ok ? roles.u(4) : 4
    FontMetrics { id: fm; font: gr.ok ? gr.roles.fLabelUnit : Qt.font({}) }

    // the session's dots in a cell: spread on a small lattice round its centre, in shot order
    function dotsIn(start, curve) {
        return (marks || []).filter(function (m) { return m.available === true && m.start === start && m.curve === curve && m.picked !== true })
    }
    function spot(i, n) {
        if (n <= 1) return { dx: 0, dy: 0 }
        const k = Math.ceil(Math.sqrt(n)), step = Math.min(cell / (k + 1), ok ? roles.u(6) : 6)
        const r = Math.floor(i / k), c = i % k
        return { dx: (c - (k - 1) / 2) * step, dy: (r - (Math.ceil(n / k) - 1) / 2) * step }
    }

    // the head across: where it started
    Row {
        Text { width: gr.labW - (gr.ok ? gr.roles.u(6) : 6); horizontalAlignment: Text.AlignRight; rightPadding: 0
               text: "starts"; font: gr.ok ? gr.roles.fLabelUnit : Qt.font({}); color: gr.ok ? gr.roles.ink3 : "grey" }
        Item { width: gr.ok ? gr.roles.u(6) : 6; height: 1 }
        Repeater {
            model: gr.ok ? ["left", "straight", "right"] : []
            delegate: Text { required property var modelData; width: gr.cell; horizontalAlignment: Text.AlignHCenter
                             text: modelData; font: gr.roles.fLabelUnit; color: gr.roles.ink3 }
        }
    }
    Row {
        Column {
            width: gr.labW
            Repeater {
                model: gr.ok ? ["curves left", "straight", "curves right"] : []
                delegate: Text { required property var modelData; width: gr.labW - gr.roles.u(6); height: gr.cell
                                 horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter
                                 text: modelData; font: gr.roles.fLabelUnit; color: gr.roles.ink3 }
            }
        }
        Grid {
            columns: 3
            Repeater {
                model: gr.ok ? 9 : 0
                delegate: Item {
                    id: cl
                    required property int index
                    objectName: "chShapeCell"
                    readonly property int start: gr.cols[index % 3]
                    readonly property int curve: gr.cols[Math.floor(index / 3)]
                    readonly property var dots: gr.dotsIn(start, curve)
                    readonly property bool isThis: !!gr.shot && gr.shot.available === true && gr.shot.start === start && gr.shot.curve === curve
                    readonly property int count: dots.length + (isThis ? 1 : 0)
                    width: gr.cell; height: gr.cell
                    Rectangle { anchors.fill: parent; color: "transparent"; border.width: gr.roles.hw; border.color: gr.roles.hair }
                    // straight-straight: the target's cell, a quiet cross at its centre
                    Rectangle { visible: cl.start === 0 && cl.curve === 0; anchors.centerIn: parent; width: gr.roles.u(7); height: gr.roles.hw; color: gr.roles.ink3 }
                    Rectangle { visible: cl.start === 0 && cl.curve === 0; anchors.centerIn: parent; width: gr.roles.hw; height: gr.roles.u(7); color: gr.roles.ink3 }
                    Repeater {
                        model: cl.dots
                        delegate: Rectangle {
                            id: dt
                            required property var modelData
                            required property int index
                            objectName: "chShapeMark"
                            readonly property int ordinal: modelData.ordinal
                            readonly property var at: gr.spot(index, cl.dots.length + (cl.isThis ? 1 : 0))
                            readonly property real d: 2 * Math.round(gr.roles.u(3.5) / 2)
                            x: gr.cell / 2 + at.dx - d / 2; y: gr.cell / 2 + at.dy - d / 2; width: d; height: d
                            radius: gr.roles.square ? 0 : d / 2
                            color: ma.containsMouse ? gr.roles.ink : gr.roles.ink3
                            MouseArea {
                                id: ma
                                anchors.fill: parent; anchors.margins: -3
                                hoverEnabled: true; enabled: gr.interactive
                                cursorShape: Qt.PointingHandCursor
                                onContainsMouseChanged: gr.hoverText = containsMouse ? "Shot " + dt.modelData.ordinal + "  " + (dt.modelData.word || "")
                                                                                     : (gr.hoverText.indexOf("Shot " + dt.modelData.ordinal + " ") === 0 ? "" : gr.hoverText)
                                onClicked: gr.shotActivated(dt.modelData.ordinal)
                            }
                        }
                    }
                    // this shot: filled ink, ringed
                    Item {
                        objectName: "chShapeThis"
                        visible: cl.isThis
                        readonly property var at: gr.spot(cl.dots.length, cl.dots.length + 1)
                        x: gr.cell / 2 + at.dx; y: gr.cell / 2 + at.dy
                        readonly property real d: 2 * Math.round(gr.roles.u(7) / 2)
                        readonly property real rd: 2 * Math.round(gr.roles.ringD / 2) + 2
                        Rectangle { x: -parent.d / 2; y: -parent.d / 2; width: parent.d; height: parent.d; radius: gr.roles.square ? 0 : parent.d / 2; color: gr.roles.ink }
                        Rectangle { x: -parent.rd / 2; y: -parent.rd / 2; width: parent.rd; height: parent.rd; radius: gr.roles.square ? 0 : parent.rd / 2
                                    color: "transparent"; border.width: 1.25; border.color: gr.roles.ink }
                    }
                }
            }
        }
    }
    Text {
        objectName: "chShapeWords"
        width: Math.max(gr.labW + 3 * gr.cell, implicitWidth)
        text: gr.hoverText !== "" ? gr.hoverText : gr.line
        visible: text !== ""
        font: gr.ok ? gr.roles.fSmall : Qt.font({}); color: gr.ok ? gr.roles.ink2 : "grey"
    }
}
