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

// PPCHPOSTUREFRAME — ONE FRAME OF THE POSTURE PICTURE STRIP (docs/design/posture_chart_design.md §5.4, stage 5), on
// screen (PpChPosturePicture) and in print (PpPosturePage): the golfer's still at one position, the frozen address coach
// lines, the band offsets at impact and the golfer's own butt line at impact, then its caption and note. EVERY coordinate,
// line and word is the presenter's (charts/posture_picture.h: FRAME, MARK — each in the frame's own image pixels); this
// only scales them by the drawn image (s = drawn height / frame.height) and picks colours. `sets` says which coach sets
// are drawn (the reader's Coach lines toggles, else the picture's default sets); a band or ink mark is always drawn.
// Also the Club delivery strip's frame (PpChDeliveryPicture; charts/delivery_picture.h), whose slot is a "poly" mark: a
// filled polygon in the coach colour at polyAlpha, under the lines.
// objectName: chPosturePictureFrame (+ tag, camera, seekUs, coachDrawn, bandDrawn, inkDrawn, hasImage).
Item {
    id: fr
    objectName: "chPosturePictureFrame"
    property var frame: ({})
    property var sets: ({})
    property real frameH: 200
    property color coachColor: "#2E8B57"
    property real coachAlpha: 0.8
    property color inkColor: "white"          // the golfer's own line: light over a dark halo, so it reads on any frame
    property color paper: "#202020"
    property color captionColor: "black"
    property color noteColor: "grey"
    property font captionFont: Qt.font({ pixelSize: 12 })
    property font noteFont: Qt.font({ pixelSize: 11 })
    property var hueOf: null                  // function(metric key) → its colour (the band's wash)
    property real washAlpha: 0.34
    property real polyAlpha: 0.16             // a "poly" mark's fill (the Club delivery slot)
    property real pen: 1.6
    property bool interactive: false
    property bool asyncImage: true            // print: false (the page is grabbed once it is laid out)
    signal activated(var frame)

    readonly property string tag: frame && frame.tag ? frame.tag : ""
    readonly property string camera: frame && frame.camera ? frame.camera : ""
    readonly property real seekUs: frame && frame.seekUs !== undefined ? frame.seekUs : -1
    readonly property var marks: frame && frame.marks ? frame.marks : []
    readonly property var shown: marks.filter(function (m) { return m.role !== "coach" || (!!fr.sets && fr.sets[m.set] === true) })
    readonly property int coachDrawn: shown.filter(function (m) { return m.role === "coach" }).length
    readonly property int polyDrawn: shown.filter(function (m) { return m.kind === "poly" }).length
    readonly property int bandDrawn: shown.filter(function (m) { return m.role === "band" }).length
    readonly property int inkDrawn: shown.filter(function (m) { return m.role === "ink" }).length
    readonly property bool hasImage: img.status === Image.Ready
    readonly property real s: frame && frame.height > 0 ? frameH / frame.height : 0
    width: frame && frame.width > 0 && frame.height > 0 ? frame.width * s : frameH * 0.6
    height: frameH + capCol.height

    Rectangle {
        id: pic
        width: fr.width; height: fr.frameH
        color: fr.paper
        clip: true
        Image {
            id: img
            anchors.fill: parent
            source: fr.frame && fr.frame.src ? fr.frame.src : ""
            fillMode: Image.Stretch
            asynchronous: fr.asyncImage
            cache: fr.asyncImage
            smooth: true
        }
        Text {
            visible: img.status !== Image.Ready
            anchors.centerIn: parent
            width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: fr.frame && fr.frame.imageWhy ? fr.frame.imageWhy : ""
            font: fr.noteFont
            color: Qt.alpha(fr.noteColor, 0.9)
        }
        Canvas {
            id: cv
            anchors.fill: parent
            readonly property var deps: [fr.shown, fr.s, width, height, fr.coachColor, fr.inkColor]
            onDepsChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const s = fr.s
                if (!(s > 0)) return
                const list = fr.shown
                const dashOf = function (pattern, pen) { return pattern.map(function (v) { return v / pen }) }
                // filled polygons (the slot), then the bands, under the lines
                for (let q = 0; q < list.length; ++q) {
                    const m = list[q]
                    if (m.kind !== "poly" || !m.pts || m.pts.length < 3) continue
                    ctx.fillStyle = Qt.alpha(m.hueKey && fr.hueOf ? fr.hueOf(m.hueKey) : fr.coachColor, fr.polyAlpha)
                    ctx.setLineDash([])
                    ctx.beginPath()
                    ctx.moveTo(m.pts[0][0] * s, m.pts[0][1] * s)
                    for (let v = 1; v < m.pts.length; ++v) ctx.lineTo(m.pts[v][0] * s, m.pts[v][1] * s)
                    ctx.closePath()
                    ctx.fill()
                }
                for (let i = 0; i < list.length; ++i) {
                    const m = list[i]
                    if (m.kind !== "rect") continue
                    const c = fr.hueOf ? fr.hueOf(m.hueKey || "") : fr.coachColor
                    ctx.fillStyle = Qt.alpha(c, fr.washAlpha)
                    ctx.fillRect(m.x * s, m.y * s, m.w * s, m.h * s)
                    ctx.strokeStyle = Qt.alpha(c, Math.min(1, fr.washAlpha * 2.6))
                    ctx.lineWidth = 1
                    ctx.setLineDash([])
                    ctx.beginPath(); ctx.moveTo(m.x * s, m.y * s); ctx.lineTo(m.x * s, (m.y + m.h) * s)
                    ctx.moveTo((m.x + m.w) * s, m.y * s); ctx.lineTo((m.x + m.w) * s, (m.y + m.h) * s); ctx.stroke()
                }
                for (let j = 0; j < list.length; ++j) {
                    const m = list[j]
                    if (m.kind === "rect" || m.kind === "poly") continue
                    const ink = m.role === "ink"
                    ctx.save()
                    ctx.globalAlpha = ink ? 1.0 : fr.coachAlpha
                    ctx.strokeStyle = ink ? fr.inkColor : fr.coachColor
                    ctx.lineWidth = ink ? fr.pen * 1.5 : fr.pen
                    ctx.lineCap = "butt"
                    ctx.lineJoin = "round"
                    if (m.dash === "dash") ctx.setLineDash(dashOf([6, 4.5], ctx.lineWidth))
                    else if (m.dash === "long") ctx.setLineDash(dashOf([18, 6], ctx.lineWidth))
                    else if (m.dash === "dot") { ctx.lineWidth = fr.pen * 1.3; ctx.lineCap = "round"; ctx.setLineDash([0.01, 2]) }
                    else ctx.setLineDash([])
                    if (ink) {
                        // the halo under the golfer's own line
                        ctx.save()
                        ctx.strokeStyle = Qt.rgba(0, 0, 0, 0.65)
                        ctx.lineWidth = fr.pen * 1.5 + 2.5
                        ctx.setLineDash([])
                        ctx.beginPath()
                        if (m.kind === "vline") { ctx.moveTo(m.x * s, 0); ctx.lineTo(m.x * s, height) }
                        else if (m.kind === "polyline" && m.pts && m.pts.length >= 2) {   // the head at impact's tick
                            ctx.moveTo(m.pts[0][0] * s, m.pts[0][1] * s)
                            for (let h = 1; h < m.pts.length; ++h) ctx.lineTo(m.pts[h][0] * s, m.pts[h][1] * s)
                        }
                        ctx.stroke()
                        ctx.restore()
                    }
                    ctx.beginPath()
                    if (m.kind === "circle") ctx.arc(m.cx * s, m.cy * s, m.r * s, 0, 2 * Math.PI)
                    else if (m.kind === "vline") { ctx.moveTo(m.x * s, 0); ctx.lineTo(m.x * s, height) }
                    else if (m.kind === "polyline" && m.pts && m.pts.length >= 2) {
                        ctx.moveTo(m.pts[0][0] * s, m.pts[0][1] * s)
                        for (let k = 1; k < m.pts.length; ++k) ctx.lineTo(m.pts[k][0] * s, m.pts[k][1] * s)
                    }
                    ctx.stroke()
                    ctx.restore()
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: fr.interactive && fr.seekUs >= 0
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: fr.activated(fr.frame)
        }
    }
    Column {
        id: capCol
        y: fr.frameH + 4
        width: fr.width
        spacing: 1
        Text { width: parent.width; text: fr.frame && fr.frame.caption ? fr.frame.caption : ""; font: fr.captionFont; color: fr.captionColor; wrapMode: Text.WordWrap }
        Text { visible: text !== ""; width: parent.width; text: fr.frame && fr.frame.note ? fr.frame.note : ""; font: fr.noteFont; color: fr.noteColor; wrapMode: Text.WordWrap }
    }
}
