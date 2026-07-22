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

// Orientation glyph — geometric read of a face/wrist rotation state (square/open/
// closed) as two lines: a fixed neutral reference and a coloured line rotated to
// the state's angle. This encodes the categorical word as geometry + colour rather
// than as a bare label — the same "never just a word" rule as PpStateChip — because
// an angle reads instantly where "OPEN" vs "CLOSED" must be parsed letter by letter.
// Unknown state draws only a dashed neutral reference line, no rotated line.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property string orientation: ""   // "square" | "open" | "closed" | ""
    property string band:        ""
    property string caption:      ""

    // Same tiny local function every dashboard zone uses — keep it in sync.
    function _bandColor(b) {
        return b === "green"  ? Theme.colorRagGood
             : b === "yellow" ? Theme.colorRagWatch
             : b === "red"    ? Theme.colorRagFault
             :                  Theme.colorRagNone
    }

    // Vibrant by RAG band; bright primary text when the state is known but unscored,
    // never the muted grey a bare _bandColor("") would give (matches the stat tiles).
    readonly property color _color: (root.band === "green" || root.band === "yellow"
                                     || root.band === "red")
                                    ? _bandColor(root.band) : Theme.colorText
    readonly property bool  _known: root.orientation === "square"
                                  || root.orientation === "open"
                                  || root.orientation === "closed"
    readonly property real  _angle: root.orientation === "open"   ? -18
                                   : root.orientation === "closed" ?  18
                                   : 0

    readonly property int _glyphSize: Theme.sp(46)
    readonly property int _margin:    Theme.sp(6)

    implicitWidth:  col.implicitWidth
    implicitHeight: col.implicitHeight

    Column {
        id: col
        spacing: Theme.sp(3)

        Text {
            id: capText
            visible: root.caption.length > 0
            text: root.caption.toUpperCase()
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }

        Item {
            id: glyphBox
            width:  root._glyphSize
            height: root._glyphSize

            // Reference line — fixed, neutral. Dashed when the state is unknown
            // (the sole visual cue in that case); solid once a state is known.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.colorBorderMid
                    strokeWidth: Theme.sp(2)
                    fillColor:   "transparent"
                    capStyle:    ShapePath.RoundCap
                    strokeStyle: root._known ? ShapePath.SolidLine : ShapePath.DashLine
                    dashPattern: root._known ? [] : [1, 2]

                    startX: root._margin
                    startY: glyphBox.height / 2
                    PathLine {
                        x: glyphBox.width - root._margin
                        y: glyphBox.height / 2
                    }
                }
            }

            // State line — rotates about the glyph's centre to encode the
            // orientation; hidden entirely when the state is unknown.
            Shape {
                id: stateShape
                anchors.fill: parent
                visible: root._known
                rotation: root._angle
                Behavior on rotation { NumberAnimation { duration: Theme.durationNormal } }
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: root._color
                    strokeWidth: Theme.sp(3)
                    fillColor:   "transparent"
                    capStyle:    ShapePath.RoundCap

                    startX: root._margin
                    startY: glyphBox.height / 2
                    PathLine {
                        x: glyphBox.width - root._margin
                        y: glyphBox.height / 2
                    }
                }
            }
        }

        // The categorical read is this tile's "value" — sized and weighted to match
        // the scalar tiles' big band-coloured number, not a micro caption.
        Text {
            visible: root.orientation.length > 0
            text: root.orientation.toUpperCase()
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzHeading
            font.weight:        Font.DemiBold
            font.letterSpacing: Theme.trackingMicro
            color:              root._color
        }
    }
}
