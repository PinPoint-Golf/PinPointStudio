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

// Tiny vector glyphs for the telestrator palette, drawn on a Canvas so they read
// identically on every platform (the symbol FONT is a Linux fallback and can't be
// trusted for shape icons). kind: select | line | circle | rect | pencil | chevronLeft.

import QtQuick
import PinPointStudio

Canvas {
    id: ic

    property string kind: "select"
    property color  iconColor: Theme.colorText2

    implicitWidth:  Theme.sp(16)
    implicitHeight: Theme.sp(16)

    onIconColorChanged: requestPaint()
    onKindChanged:      requestPaint()
    onWidthChanged:     requestPaint()
    onHeightChanged:    requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.clearRect(0, 0, width, height)
        var w = width, h = height
        function X(f) { return f * w }
        function Y(f) { return f * h }

        ctx.strokeStyle = iconColor
        ctx.fillStyle   = iconColor
        ctx.lineWidth   = Math.max(1.25, w * 0.1)
        ctx.lineCap     = "round"
        ctx.lineJoin    = "round"

        if (kind === "line") {
            ctx.beginPath()
            ctx.moveTo(X(0.18), Y(0.82))
            ctx.lineTo(X(0.82), Y(0.18))
            ctx.stroke()
        } else if (kind === "rect") {                       // hollow square
            var m = 0.2
            ctx.strokeRect(X(m), Y(m), X(1 - 2 * m), Y(1 - 2 * m))
        } else if (kind === "circle") {                     // ellipse (scale+arc)
            ctx.save()
            ctx.translate(w / 2, h / 2)
            ctx.scale(w * 0.32, h * 0.32)
            ctx.beginPath()
            ctx.arc(0, 0, 1, 0, 2 * Math.PI)
            ctx.restore()
            ctx.stroke()
        } else if (kind === "select") {                     // arrow cursor (filled)
            ctx.beginPath()
            ctx.moveTo(X(0.12), Y(0.04))
            ctx.lineTo(X(0.12), Y(0.80))
            ctx.lineTo(X(0.30), Y(0.63))
            ctx.lineTo(X(0.42), Y(0.92))
            ctx.lineTo(X(0.53), Y(0.87))
            ctx.lineTo(X(0.41), Y(0.59))
            ctx.lineTo(X(0.64), Y(0.59))
            ctx.closePath()
            ctx.fill()
        } else if (kind === "chevronRight") {               // "open palette" (gutter)
            ctx.beginPath()
            ctx.moveTo(X(0.40), Y(0.22))
            ctx.lineTo(X(0.64), Y(0.50))
            ctx.lineTo(X(0.40), Y(0.78))
            ctx.stroke()
        } else if (kind === "chevronLeft") {                // "collapse palette" (toolbar)
            ctx.beginPath()
            ctx.moveTo(X(0.60), Y(0.22))
            ctx.lineTo(X(0.36), Y(0.50))
            ctx.lineTo(X(0.60), Y(0.78))
            ctx.stroke()
        }
    }
}
