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

// Per-tile telestrator overlay — draws/edits the focused swing's annotations on
// ONE camera tile. Hosted by PpCameraFrame (gated on annotationsEnabled), it sits
// above the pose/club replay overlay. Marks are held in the AnnotationTool store
// (keyed per swing+tile), so they survive Replay↔Analyse churn and clear on swing
// deselect; this layer is a thin imperative view over that array.
//
// Coordinates are stored NORMALIZED [0..1] in the video's source space and mapped
// to screen via the parent's contentRect each paint — so marks stay glued to the
// golfer through resize, letterboxing and aspect changes (same model as the pose
// skeleton and the ball ROI). Render + hit-testing follow PpCameraFrame's existing
// Canvas-paint + MouseArea idiom rather than a per-shape Item tree.

import QtQuick
import PinPointStudio

Item {
    id: layer

    // Active only on the analyse stage (host sets this from SessionMode.analyse).
    // Named `active` (not `enabled`) so it doesn't shadow QQuickItem.enabled.
    property bool active: false
    // The video rectangle inside the tile (x,y,w,h in tile pixels); bound from
    // PpCameraFrame.contentRect — already accounts for inset + letterboxing.
    property rect contentRect: Qt.rect(0, 0, 0, 0)
    // Store key — "<swingDir>#<streamIndex>"; "" disables persistence.
    property string tileKey: ""

    visible: active

    // ── Mark model (live reference into the AnnotationTool store) ──────────────
    property var shapes: []
    property int selectedIndex: -1
    property int _nextId: 1

    // Drag state.
    property string _mode: "none"     // none | new | move | tl | tr | bl | br | p1 | p2
    property real   _startX: 0
    property real   _startY: 0
    property var    _orig: null       // {x1,y1,x2,y2} captured at press for move

    function _reload() {
        shapes = AnnotationTool.marksFor(tileKey)
        selectedIndex = -1
        annoCanvas.requestPaint()
    }
    onTileKeyChanged:      _reload()
    onContentRectChanged:  annoCanvas.requestPaint()
    Component.onCompleted: _reload()
    Component.onDestruction: AnnotationTool.releaseSelection(layer)

    Connections {
        target: AnnotationTool
        function onCleared() { layer._reload() }
        function onSelectionClaimed(owner) {
            if (owner !== layer && layer.selectedIndex !== -1) {
                layer.selectedIndex = -1
                annoCanvas.requestPaint()
            }
        }
    }

    // ── Called by AnnotationTool on behalf of the shared toolbar ───────────────
    function deleteSelectedShape() {
        if (selectedIndex < 0 || selectedIndex >= shapes.length) return
        shapes.splice(selectedIndex, 1)
        selectedIndex = -1
        AnnotationTool.releaseSelection(layer)
        annoCanvas.requestPaint()
    }
    function recolorSelectedShape(c) {
        if (selectedIndex < 0 || selectedIndex >= shapes.length) return
        shapes[selectedIndex].color = c
        annoCanvas.requestPaint()
    }

    // ── Coordinate mapping (normalized ⇄ tile pixels) ──────────────────────────
    readonly property bool _crOk: contentRect.width > 0 && contentRect.height > 0
    function _sx(nx) { return contentRect.x + nx * contentRect.width }
    function _sy(ny) { return contentRect.y + ny * contentRect.height }
    function _nx(px) { return _crOk ? Math.max(0, Math.min(1, (px - contentRect.x) / contentRect.width))  : 0 }
    function _ny(py) { return _crOk ? Math.max(0, Math.min(1, (py - contentRect.y) / contentRect.height)) : 0 }

    // Screen-space corner / endpoint handles for a shape.
    function _handles(s) {
        var p1x = _sx(s.x1), p1y = _sy(s.y1), p2x = _sx(s.x2), p2y = _sy(s.y2)
        if (s.type === "line")
            return [ {id: "p1", x: p1x, y: p1y}, {id: "p2", x: p2x, y: p2y} ]
        var lx = Math.min(p1x, p2x), rx = Math.max(p1x, p2x)
        var ty = Math.min(p1y, p2y), by = Math.max(p1y, p2y)
        return [ {id: "tl", x: lx, y: ty}, {id: "tr", x: rx, y: ty},
                 {id: "bl", x: lx, y: by}, {id: "br", x: rx, y: by} ]
    }

    // Distance from point P to segment AB (screen space) — line body hit-test.
    function _distToSeg(px, py, ax, ay, bx, by) {
        var dx = bx - ax, dy = by - ay
        var len2 = dx * dx + dy * dy
        var t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0
        t = Math.max(0, Math.min(1, t))
        var cx = ax + t * dx, cy = ay + t * dy
        return Math.hypot(px - cx, py - cy)
    }

    function _bodyHit(s, mx, my) {
        var p1x = _sx(s.x1), p1y = _sy(s.y1), p2x = _sx(s.x2), p2y = _sy(s.y2)
        if (s.type === "line")
            return _distToSeg(mx, my, p1x, p1y, p2x, p2y) < Theme.sp(8)
        var lx = Math.min(p1x, p2x), rx = Math.max(p1x, p2x)
        var ty = Math.min(p1y, p2y), by = Math.max(p1y, p2y)
        return mx >= lx && mx <= rx && my >= ty && my <= by
    }

    // ── Render ─────────────────────────────────────────────────────────────────
    Canvas {
        id: annoCanvas
        anchors.fill: parent
        onVisibleChanged: if (visible) requestPaint()

        function _drawHandle(ctx, x, y) {
            var hs = Theme.sp(8)
            ctx.globalAlpha = 1
            ctx.fillStyle   = "#FFFFFF"
            ctx.strokeStyle = Qt.rgba(0, 0, 0, 0.8)
            ctx.lineWidth   = 1
            ctx.fillRect(x - hs / 2, y - hs / 2, hs, hs)
            ctx.strokeRect(x - hs / 2, y - hs / 2, hs, hs)
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!layer._crOk)
                return

            for (var i = 0; i < layer.shapes.length; ++i) {
                var s = layer.shapes[i]
                var p1x = layer._sx(s.x1), p1y = layer._sy(s.y1)
                var p2x = layer._sx(s.x2), p2y = layer._sy(s.y2)
                ctx.globalAlpha = 1
                ctx.strokeStyle = s.color
                ctx.lineWidth   = 2
                ctx.lineCap     = "round"

                if (s.type === "line") {
                    ctx.beginPath()
                    ctx.moveTo(p1x, p1y)
                    ctx.lineTo(p2x, p2y)
                    ctx.stroke()
                } else if (s.type === "rect") {
                    ctx.strokeRect(Math.min(p1x, p2x), Math.min(p1y, p2y),
                                   Math.abs(p2x - p1x), Math.abs(p2y - p1y))
                } else { // circle → ellipse inscribed in the bbox (scale+arc; QML
                         // Canvas has no ctx.ellipse, so transform a unit circle)
                    var cx = (p1x + p2x) / 2, cy = (p1y + p2y) / 2
                    var rx = Math.abs(p2x - p1x) / 2, ry = Math.abs(p2y - p1y) / 2
                    if (rx > 0.5 && ry > 0.5) {
                        ctx.save()
                        ctx.translate(cx, cy)
                        ctx.scale(rx, ry)
                        ctx.beginPath()
                        ctx.arc(0, 0, 1, 0, 2 * Math.PI)
                        ctx.restore()
                        ctx.stroke()
                    }
                }
            }

            // Handles for the selected mark — drawn last, on top.
            if (layer.selectedIndex >= 0 && layer.selectedIndex < layer.shapes.length) {
                var hs = layer._handles(layer.shapes[layer.selectedIndex])
                for (var h = 0; h < hs.length; ++h)
                    _drawHandle(ctx, hs[h].x, hs[h].y)
            }
            ctx.globalAlpha = 1
        }
    }

    // ── Interaction ────────────────────────────────────────────────────────────
    MouseArea {
        id: editArea
        anchors.fill: parent
        enabled: layer.active && layer._crOk
        hoverEnabled: true
        preventStealing: true

        readonly property real hr: Theme.sp(14)   // handle grab radius

        function _handleHit(mx, my) {
            if (layer.selectedIndex < 0 || layer.selectedIndex >= layer.shapes.length)
                return ""
            var hs = layer._handles(layer.shapes[layer.selectedIndex])
            for (var i = 0; i < hs.length; ++i)
                if (Math.abs(mx - hs[i].x) < hr && Math.abs(my - hs[i].y) < hr)
                    return hs[i].id
            return ""
        }

        cursorShape: {
            if (AnnotationTool.tool !== "select") return Qt.CrossCursor
            var z = _handleHit(mouseX, mouseY)
            if (z === "tl" || z === "br") return Qt.SizeFDiagCursor
            if (z === "tr" || z === "bl") return Qt.SizeBDiagCursor
            if (z === "p1" || z === "p2") return Qt.SizeAllCursor
            if (z !== "") return Qt.SizeAllCursor
            for (var i = layer.shapes.length - 1; i >= 0; --i)
                if (layer._bodyHit(layer.shapes[i], mouseX, mouseY)) return Qt.SizeAllCursor
            return Qt.ArrowCursor
        }

        onPressed: (mouse) => {
            layer._startX = mouse.x
            layer._startY = mouse.y

            if (AnnotationTool.tool !== "select") {
                // Begin a new shape — first corner / endpoint at the press point.
                var nx = layer._nx(mouse.x), ny = layer._ny(mouse.y)
                layer.shapes.push({ id: layer._nextId++, type: AnnotationTool.tool,
                                    x1: nx, y1: ny, x2: nx, y2: ny,
                                    color: AnnotationTool.strokeColor })
                layer.selectedIndex = layer.shapes.length - 1
                AnnotationTool.claimSelection(layer)
                layer._mode = "new"
                annoCanvas.requestPaint()
                return
            }

            // Select tool: grab a handle of the current selection first…
            var z = _handleHit(mouse.x, mouse.y)
            if (z !== "") {
                layer._mode = z
                var sc = layer.shapes[layer.selectedIndex]
                layer._orig = { x1: sc.x1, y1: sc.y1, x2: sc.x2, y2: sc.y2 }
                return
            }
            // …else pick the topmost shape under the cursor (move it)…
            for (var i = layer.shapes.length - 1; i >= 0; --i) {
                if (layer._bodyHit(layer.shapes[i], mouse.x, mouse.y)) {
                    layer.selectedIndex = i
                    AnnotationTool.claimSelection(layer)
                    var s = layer.shapes[i]
                    layer._orig = { x1: s.x1, y1: s.y1, x2: s.x2, y2: s.y2 }
                    layer._mode = "move"
                    annoCanvas.requestPaint()
                    return
                }
            }
            // …else click on empty video deselects.
            layer._mode = "none"
            if (layer.selectedIndex !== -1) {
                layer.selectedIndex = -1
                AnnotationTool.releaseSelection(layer)
                annoCanvas.requestPaint()
            }
        }

        onPositionChanged: (mouse) => {
            if (layer._mode === "none") return
            if (layer.selectedIndex < 0 || layer.selectedIndex >= layer.shapes.length) return
            var s = layer.shapes[layer.selectedIndex]
            var nx = layer._nx(mouse.x), ny = layer._ny(mouse.y)
            var minSz = 0.02

            switch (layer._mode) {
            case "new":
                s.x2 = nx; s.y2 = ny
                break
            case "p1":
                s.x1 = nx; s.y1 = ny
                break
            case "p2":
                s.x2 = nx; s.y2 = ny
                break
            case "move": {
                var o = layer._orig
                var dnx = (mouse.x - layer._startX) / layer.contentRect.width
                var dny = (mouse.y - layer._startY) / layer.contentRect.height
                var minx = Math.min(o.x1, o.x2), maxx = Math.max(o.x1, o.x2)
                var miny = Math.min(o.y1, o.y2), maxy = Math.max(o.y1, o.y2)
                dnx = Math.max(-minx, Math.min(1 - maxx, dnx))
                dny = Math.max(-miny, Math.min(1 - maxy, dny))
                s.x1 = o.x1 + dnx; s.x2 = o.x2 + dnx
                s.y1 = o.y1 + dny; s.y2 = o.y2 + dny
                break
            }
            // Corner resize — set the dragged corner, clamp against the opposite
            // edge so the box can't invert (mirrors the ball-ROI editor).
            case "tl":
                s.x1 = Math.min(nx, s.x2 - minSz); s.y1 = Math.min(ny, s.y2 - minSz)
                break
            case "tr":
                s.x2 = Math.max(nx, s.x1 + minSz); s.y1 = Math.min(ny, s.y2 - minSz)
                break
            case "bl":
                s.x1 = Math.min(nx, s.x2 - minSz); s.y2 = Math.max(ny, s.y1 + minSz)
                break
            case "br":
                s.x2 = Math.max(nx, s.x1 + minSz); s.y2 = Math.max(ny, s.y1 + minSz)
                break
            }
            annoCanvas.requestPaint()
        }

        onReleased: (mouse) => {
            if (layer._mode === "new" && layer.selectedIndex >= 0) {
                var s = layer.shapes[layer.selectedIndex]
                // Discard an accidental click (no real drag).
                var tooSmall = (s.type === "line")
                    ? Math.hypot(s.x2 - s.x1, s.y2 - s.y1) < 0.01
                    : (Math.abs(s.x2 - s.x1) < 0.01 || Math.abs(s.y2 - s.y1) < 0.01)
                if (tooSmall) {
                    layer.shapes.splice(layer.selectedIndex, 1)
                    layer.selectedIndex = -1
                    AnnotationTool.releaseSelection(layer)
                } else if (s.type !== "line") {
                    // Normalize the bbox so x1<=x2, y1<=y2 for stable handles.
                    if (s.x1 > s.x2) { var tx = s.x1; s.x1 = s.x2; s.x2 = tx }
                    if (s.y1 > s.y2) { var ty = s.y1; s.y1 = s.y2; s.y2 = ty }
                }
                annoCanvas.requestPaint()
            }
            layer._mode = "none"
            layer._orig = null
        }
    }
}
