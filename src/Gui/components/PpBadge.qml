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

// A leading badge: a round mark at the head of a coaching line, a shape for each meaning so
// colour is never the only channel.
//
//   check        a tick, green: something done well, an ideal to aim for
//   target       a ring and dot, amber: something to work on
//   easing       a down arrow, green: a fault that is fading
//   unconfirmed  a dashed ring and dot, grey and untinted: placed on evidence no recent session
//                could renew
//
// Every kind but unconfirmed sits on a faint fill of its tone. Only the kind's own glyph is
// loaded, so a badge draws exactly what its inline original drew.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Rectangle {
    id: badge

    property string kind: "check"
    property color  tone: kind === "check" || kind === "easing" ? Theme.colorGood
                        : kind === "target"                     ? Theme.colorAccent
                        :                                         Theme.colorText3
    property int    size: Theme.sp(20)

    width: badge.size; height: width; radius: width / 2
    color: badge.kind === "unconfirmed" ? "transparent" : Qt.alpha(badge.tone, Theme.dark ? 0.16 : 0.12)

    Loader {
        anchors.fill: parent
        sourceComponent: badge.kind === "target"      ? targetGlyph
                       : badge.kind === "easing"      ? easingGlyph
                       : badge.kind === "unconfirmed" ? unconfirmedGlyph
                       :                                checkGlyph
    }

    Component {
        id: checkGlyph
        Shape {
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: badge.tone; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.29; startY: badge.size * 0.52
                PathLine { x: badge.size * 0.44; y: badge.size * 0.66 }
                PathLine { x: badge.size * 0.72; y: badge.size * 0.36 }
            }
        }
    }
    Component {
        id: targetGlyph
        Item {
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(badge.size * 0.56); height: width; radius: width / 2
                color: "transparent"; border.width: Math.max(1.5, Theme.sp(1.4)); border.color: badge.tone
            }
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(badge.size * 0.2); height: width; radius: width / 2
                color: badge.tone
            }
        }
    }
    // The trend chip's own glyph.
    Component {
        id: easingGlyph
        Shape {
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: badge.tone; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.5; startY: badge.size * 0.27
                PathLine { x: badge.size * 0.5; y: badge.size * 0.72 }
            }
            ShapePath {
                strokeColor: badge.tone; strokeWidth: Math.max(1.5, Theme.sp(1.6)); fillColor: "transparent"
                capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
                startX: badge.size * 0.31; startY: badge.size * 0.53
                PathLine { x: badge.size * 0.5;  y: badge.size * 0.72 }
                PathLine { x: badge.size * 0.69; y: badge.size * 0.53 }
            }
        }
    }
    // Something was here, and nothing recent says so.
    Component {
        id: unconfirmedGlyph
        Item {
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: badge.tone; strokeWidth: Math.max(1.2, Theme.sp(1.2)); fillColor: "transparent"
                    strokeStyle: ShapePath.DashLine; dashPattern: [2, 2]
                    PathAngleArc {
                        centerX: badge.size / 2; centerY: badge.size / 2
                        radiusX: badge.size / 2 - 1; radiusY: radiusX
                        startAngle: 0; sweepAngle: 360
                    }
                }
            }
            Rectangle {
                anchors.centerIn: parent
                width: Math.round(badge.size * 0.2); height: width; radius: width / 2
                color: badge.tone
            }
        }
    }
}
