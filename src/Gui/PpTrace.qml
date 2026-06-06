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

// Declarative polyline trace — used for the shot card's IMU-only fallback and
// the review panel's chart. `points` is a list of normalised (0..1) points
// from the model's tracePoints role; the binding below is a pure scale
// transform to item coordinates (no Canvas, no imperative drawing).

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property var   points:      []
    property color strokeColor: Theme.colorAccent

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.strokeColor
            strokeWidth: Theme.sp(1)
            fillColor:   "transparent"
            joinStyle:   ShapePath.RoundJoin
            capStyle:    ShapePath.RoundCap

            PathPolyline {
                path: root.points.map(p => Qt.point(p.x * root.width, p.y * root.height))
            }
        }
    }
}
