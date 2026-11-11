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

// A Sankey: things flowing left to right through columns of nodes, each flow a ribbon as thick as its count. Three
// columns (docs/design/system/components/PpSankey.md; first used by the tendency flow, tendencies_flow_design.md).
//
// IT WORKS OUT NO NUMBER. Nodes and ribbons arrive placed, in plot units (y down from the plot's top, heights), from the
// C++ layout that owns them (tendency_flow.h for the tendency flow), with every word final; this draws them, labels
// them and lets them be examined. The caller picks each ribbon's tone and strength, so the same component draws any
// flow in its own colours.
//
//   nodes  [{ column (0, 1, 2), label, sub ("22 · 28%"), good, tone (optional; else good → colorGood, colorText), y, h,
//             withSub: [per node: what it says while this node is examined] }]
//   links  [{ from, to (node indices), y0, y1, h, tone, strength (fill alpha), through: [node], bySub: [per node] }]
//          in draw order
//   columns [title]   legend [{ tone, strength, name, line }]
//
// Strike labels (column 0) sit left of their nodes, flight labels (column 2) right, the middle column's on a knock-out
// chip right of its node. A label that would overlap the one above it in its column steps down. Below `compactWidth`
// the node subs are left out (their counts still show on hover).
//
// EXAMINED, as every chart in the app (values on the plot, at their own places): a node under the pointer or keyboard
// focus lights every ribbon its shots pass through and dims the rest to 25 %; a ribbon under the pointer lights alone.
// NOTHING IS LAID OVER THE PLOT (Mark, 10 Nov 2026: readouts on the ribbons "covers up the plot making it almost
// impossible to follow"): the node labels already there say the examined shots instead, each node how many of them
// pass through it and what share ("2 · 67%"), from the node's `withSub` or the ribbon's `bySub`; a node none of them
// reaches dims with its label. The examined node keeps its own count. Colour is never the only channel: every node is
// named and counted, and the legend says each tone's words and count.
//
// Motion: the flow fades in when its data changes (Theme.durationNormal, 0 under reduced motion). Geometry never moves.
//
// PRINT (the report's Your tendencies page): pass `roles`, the page's PpTypeset, and every ink, font and unit comes from it
// instead of the live theme, so the page never depends on a screen setting. `layoutHeight` says the units the layout was
// made in; the plot scales them to `plotHeight`. `patterns` lays a greyscale-safe pattern over each ribbon whose link
// names one ("dots", "hatch", "cross"; tendencies_flow_design.md §6.1), and over its legend swatch, drawn as vector
// strokes inside the ribbon (its thickness is the same all along its curve) so a mono print still tells the groups apart.
// Nothing is examined in print.

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property var nodes:   []
    property var links:   []
    property var columns: []
    property var legend:  []
    property QtObject roles: null                    // a PpTypeset: print inks, fonts and units; null on screen
    readonly property bool onPaper: !!root.roles
    property real plotHeight:   root.u(360)          // the drawn plot's height
    property real layoutHeight: root.plotHeight      // the height the layout was made in; its units scale to plotHeight
    readonly property real k:   root.layoutHeight > 0 ? root.plotHeight / root.layoutHeight : 1
    property real nodeWidth:    root.u(12)
    property real labelGutter:  root.u(104)          // room left of the first column and right of the last
    property real compactWidth: root.u(640)
    property bool patterns:     false                // lay each link's `pattern` over its ribbon and swatch

    // Size and space, ink and type: the roles' in print, the theme's on screen.
    function u(n) { return root.roles ? root.roles.u(n) : Theme.sp(n) }
    function g(n) { return root.roles ? root.roles.g(n) : Theme.gap(n) }
    readonly property color ink:     root.roles ? root.roles.ink  : Theme.colorText
    readonly property color ink2:    root.roles ? root.roles.ink2 : Theme.colorText2
    readonly property color ink3:    root.roles ? root.roles.ink3 : Theme.colorText3
    readonly property color good:    root.roles ? root.roles.good : Theme.colorGood
    readonly property color surface: root.roles ? root.roles.paper : Theme.colorSurface
    readonly property font fName:  root.roles ? root.roles.fBody
                                 : Qt.font({ family: Theme.fontBody, pixelSize: Theme.fontSzBody, weight: Theme.fontBodyWeight })
    readonly property font fSub:   root.roles ? root.roles.fFigLabel : Qt.font({ family: Theme.fontData, pixelSize: Theme.fontSzLabel })
    readonly property font fKey:   root.roles ? root.roles.fSmall
                                 : Qt.font({ family: Theme.fontBody, pixelSize: Theme.fontSzBody2, weight: Theme.fontBodyWeight })
    readonly property font fHead:  root.roles ? root.roles.fLabel
                                 : Qt.font({ family: Theme.fontData, pixelSize: Theme.fontSzMicro, letterSpacing: Theme.trackingMicro })

    // A node was activated (pressed, or Return on its focus). Exposed; the tendency flow leaves it unwired for now.
    signal nodeActivated(int index)

    readonly property bool compact: !root.onPaper && root.width < root.compactWidth
    property int  hoveredNode: -1
    property int  hoveredLink: -1
    readonly property bool examining: root.hoveredNode >= 0 || root.hoveredLink >= 0

    implicitWidth:  root.u(720)
    implicitHeight: body.implicitHeight

    function colX(c) {
        const x0 = root.labelGutter, x2 = Math.max(x0, root.width - root.labelGutter - root.nodeWidth)
        return c === 0 ? x0 : c === 2 ? x2 : Math.round((x0 + x2) / 2)
    }
    // A node's ink: the caller's tone, else green for a good node and ink for the rest.
    function nodeTone(n) { return n && n.tone !== undefined ? n.tone : n && n.good ? root.good : root.ink }
    // A node's column, safe while nodes and links are being replaced one after the other.
    function colOf(i) { const n = root.nodes[i]; return n ? n.column : 0 }
    function lit(i) {
        if (root.hoveredLink >= 0) return i === root.hoveredLink
        if (root.hoveredNode >= 0) return ((root.links[i] || {}).through || []).indexOf(root.hoveredNode) >= 0
        return true
    }
    // Ribbon hover: every ribbon under the pointer reports; the one drawn last (on top) is examined.
    property var _linkHovers: ({})
    function linkHover(i, on) {
        const h = Object.assign({}, root._linkHovers)
        if (on) h[i] = true; else delete h[i]
        root._linkHovers = h
        const keys = Object.keys(h).map(Number)
        root.hoveredLink = keys.length ? Math.max.apply(null, keys) : -1
    }
    // A column's labels, each centred on its node and stepped clear of its neighbours inside the plot: [{ index, y }].
    function labelPlaces(column, lineH) {
        const out = []
        let floor = -1e9
        for (let i = 0; i < root.nodes.length; ++i) {
            const n = root.nodes[i]
            if (n.column !== column) continue
            const y = Math.max((n.y + n.h / 2) * root.k - lineH / 2, floor)
            out.push({ index: i, y: y })
            floor = y + lineH + root.g(2)
        }
        // …and back up from the plot's foot, so a crowded column's last labels never run off the bottom.
        let ceiling = root.plotHeight
        for (let j = out.length - 1; j >= 0; --j) {
            out[j].y = Math.min(out[j].y, ceiling - lineH)
            ceiling = out[j].y - root.g(2)
        }
        return out
    }
    // Whether a column's labels, `lineH` tall, would have to move off their own nodes to stay clear of each other: then the
    // column sets each label on one line ("Thin 4 · 5%"), so every label stays level with its bar. A label stepped half
    // its height or more from its node's middle reads as the next node's (Mark, 11 Nov 2026: "Thin" sat beside Solid).
    function crowded(column, lineH) {
        for (const p of root.labelPlaces(column, lineH)) {
            const n = root.nodes[p.index]
            if (n && Math.abs(p.y + lineH / 2 - (n.y + n.h / 2) * root.k) >= lineH / 2) return true
        }
        return false
    }
    // What node i's label says while something is examined: the examined shots through it ("" for none).
    function examinedSub(i) {
        if (root.hoveredNode >= 0) {
            if (i === root.hoveredNode) return (root.nodes[i] || {}).sub || ""
            return ((root.nodes[root.hoveredNode] || {}).withSub || [])[i] || ""
        }
        if (root.hoveredLink >= 0) return ((root.links[root.hoveredLink] || {}).bySub || [])[i] || ""
        return (root.nodes[i] || {}).sub || ""
    }
    function reached(i) { return !root.examining || root.examinedSub(i) !== "" }

    // A ribbon's pattern as one SVG path, inside the ribbon: its top edge is the cubic from (x0, y0) to (x1, y1) with both
    // controls at the middle x, and it is `h` thick straight down all along it. "hatch" runs diagonals from the foot to
    // the top a thickness further on; "cross" adds the other diagonals; "dots" is a grid of small dots. `step` apart.
    function patternPath(kind, x0, y0, x1, y1, h, step) {
        if (!kind || h < root.u(2) || x1 <= x0) return ""
        const xm = (x0 + x1) / 2, xs = [], ys = []
        for (let i = 0; i <= 48; ++i) {
            const t = i / 48, a = (1 - t) * (1 - t) * (1 - t), b = 3 * (1 - t) * (1 - t) * t, c = 3 * (1 - t) * t * t, d = t * t * t
            xs.push(a * x0 + (b + c) * xm + d * x1)
            ys.push(a * y0 + b * y0 + c * y1 + d * y1)
        }
        const top = x => {
            let i = 0
            while (i < 47 && xs[i + 1] < x) ++i
            const f = xs[i + 1] > xs[i] ? (x - xs[i]) / (xs[i + 1] - xs[i]) : 0
            return ys[i] + f * (ys[i + 1] - ys[i])
        }
        const fx = v => v.toFixed(2)
        let p = ""
        if (kind === "dots") {
            const r = Math.max(0.4, step * 0.12)
            for (let x = x0 + step / 2; x < x1; x += step)
                for (let y = top(x) + step / 2; y < top(x) + h; y += step)
                    p += "M" + fx(x - r) + " " + fx(y) + " a" + fx(r) + " " + fx(r) + " 0 1 0 " + fx(2 * r) + " 0 a" + fx(r) + " "
                         + fx(r) + " 0 1 0 " + fx(-2 * r) + " 0 "
            return p
        }
        const d = Math.min(h, x1 - x0)
        for (let x = x0; x + d <= x1; x += step) {
            p += "M" + fx(x) + " " + fx(top(x) + h) + " L" + fx(x + d) + " " + fx(top(x + d)) + " "
            if (kind === "cross") p += "M" + fx(x) + " " + fx(top(x)) + " L" + fx(x + d) + " " + fx(top(x + d) + h) + " "
        }
        return p
    }

    onLinksChanged: fade.restart()

    Column {
        id: body
        width: root.width
        spacing: root.g(10)

        // ── Column heads ────────────────────────────────────────────────────
        Item {
            width: parent.width
            height: root.u(14)
            Repeater {
                model: root.columns
                Text {
                    required property var modelData
                    required property int index
                    text: root.roles ? String(modelData).toUpperCase() : Theme.caps(String(modelData))
                    font: root.fHead
                    color: root.ink3
                    x: index === 0 ? root.colX(0) + root.nodeWidth - width
                     : index === 2 ? root.colX(2)
                                   : root.colX(1) + root.nodeWidth / 2 - width / 2
                }
            }
        }

        // ── The plot ────────────────────────────────────────────────────────
        Item {
            id: plot
            objectName: "sankeyPlot"
            width:  parent.width
            height: root.plotHeight

            NumberAnimation { id: fade; target: plot; property: "opacity"; from: 0; to: 1; duration: root.onPaper ? 0 : Theme.durationNormal }

            Repeater {
                model: root.links
                Shape {
                    id: ribbon
                    required property var modelData
                    required property int index
                    objectName: "sankeyLink"
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    containsMode: Shape.FillContains
                    readonly property real x0: root.colX(root.colOf(modelData.from)) + root.nodeWidth
                    readonly property real x1: root.colX(root.colOf(modelData.to))
                    readonly property real xm: (x0 + x1) / 2
                    readonly property real y0: modelData.y0 * root.k
                    readonly property real y1: modelData.y1 * root.k
                    readonly property real h:  modelData.h * root.k

                    ShapePath {
                        fillColor:   Qt.alpha(ribbon.modelData.tone, (ribbon.modelData.strength || 0.4) * (root.lit(ribbon.index) ? 1 : 0.25))
                        strokeColor: root.surface
                        strokeWidth: Math.max(0.5, 0.6 * root.u(1))
                        startX: ribbon.x0; startY: ribbon.y0
                        PathCubic { control1X: ribbon.xm; control1Y: ribbon.y0; control2X: ribbon.xm; control2Y: ribbon.y1
                                    x: ribbon.x1; y: ribbon.y1 }
                        PathLine  { x: ribbon.x1; y: ribbon.y1 + ribbon.h }
                        PathCubic { control1X: ribbon.xm; control1Y: ribbon.y1 + ribbon.h; control2X: ribbon.xm; control2Y: ribbon.y0 + ribbon.h
                                    x: ribbon.x0; y: ribbon.y0 + ribbon.h }
                        PathLine  { x: ribbon.x0; y: ribbon.y0 }
                    }
                    ShapePath {     // the greyscale-safe pattern, inside the ribbon
                        fillColor:   ribbon.modelData.pattern === "dots" ? Qt.alpha(root.ink, 0.55) : "transparent"
                        strokeColor: ribbon.modelData.pattern === "dots" ? "transparent" : Qt.alpha(root.ink, 0.45)
                        strokeWidth: Math.max(0.4, 0.5 * root.u(1))
                        PathSvg { path: root && root.patterns ? root.patternPath(ribbon.modelData.pattern, ribbon.x0, ribbon.y0, ribbon.x1, ribbon.y1,
                                                                         ribbon.h, root.u(4)) : "" }
                    }
                    HoverHandler { enabled: !root.onPaper; onHoveredChanged: root.linkHover(ribbon.index, hovered) }
                }
            }

            // Nodes: a bar, focusable, pressable; its hover and focus examine it.
            Repeater {
                model: root.nodes
                Item {
                    id: node
                    required property var modelData
                    required property int index
                    objectName: "sankeyNode"
                    x: root.colX(modelData.column) - root.u(3)
                    y: modelData.y * root.k
                    width:  root.nodeWidth + root.u(6)
                    height: Math.max(root.u(2), modelData.h * root.k)
                    activeFocusOnTab: !root.onPaper
                    Accessible.role: Accessible.Button
                    Accessible.name: (modelData.label || "") + ", " + (modelData.sub || "")
                    onActiveFocusChanged: root.hoveredNode = activeFocus ? node.index : (root.hoveredNode === node.index ? -1 : root.hoveredNode)
                    Keys.onReturnPressed: root.nodeActivated(node.index)
                    Keys.onEnterPressed:  root.nodeActivated(node.index)

                    Rectangle {
                        x: root.u(3)
                        width: root.nodeWidth
                        height: parent.height
                        radius: root.u(1)
                        color: root.nodeTone(node.modelData)
                        opacity: root.reached(node.index) ? 1 : 0.35
                    }
                    Rectangle {     // the keyboard focus
                        visible: node.activeFocus
                        anchors.fill: parent
                        anchors.margins: -root.u(2)
                        color: "transparent"
                        border.width: root.u(2)
                        border.color: Theme.colorAccent
                        radius: Theme.radius
                    }
                    HoverHandler { enabled: !root.onPaper
                                   onHoveredChanged: root.hoveredNode = hovered ? node.index : (root.hoveredNode === node.index ? -1 : root.hoveredNode) }
                    TapHandler   { enabled: !root.onPaper; onTapped: root.nodeActivated(node.index) }
                }
            }

            // Labels, column by column.
            Repeater {
                model: 3
                Item {
                    id: labelCol
                    required property int index
                    readonly property int column: index
                    readonly property real chipPad: column === 1 ? root.g(6) : 0     // the middle column's chip padding
                    readonly property real twoLineH: root.fName.pixelSize * 1.2 + (root.compact ? 0 : root.fSub.pixelSize * 1.2 + root.g(2)) + chipPad
                    readonly property bool oneLine: !root.compact && root.crowded(column, twoLineH)
                    readonly property real lineH: oneLine ? Math.max(root.fName.pixelSize, root.fSub.pixelSize) * 1.2 + chipPad : twoLineH
                    anchors.fill: parent
                    Repeater {
                        model: root.labelPlaces(labelCol.column, labelCol.lineH)
                        Item {
                            id: place
                            required property var modelData
                            readonly property var n: root.nodes[modelData.index] || ({})
                            width: words.width + (labelCol.column === 1 ? root.g(8) : 0)
                            height: words.height + (labelCol.column === 1 ? root.g(4) : 0)
                            x: labelCol.column === 0 ? root.colX(0) - root.g(8) - width
                             : labelCol.column === 2 ? root.colX(2) + root.nodeWidth + root.g(8)
                                                     : root.colX(1) + root.nodeWidth + root.g(6)
                            y: modelData.y - (labelCol.column === 1 ? root.g(2) : 0)
                            opacity: root.reached(modelData.index) ? 1 : 0.35
                            Rectangle {     // the middle column's knock-out chip
                                visible: labelCol.column === 1
                                anchors.fill: parent
                                radius: root.u(3)
                                color: Qt.alpha(root.surface, 0.86)
                            }
                            Grid {      // the name over its count, or beside it in a crowded column
                                id: words
                                anchors.centerIn: parent
                                columns: labelCol.oneLine ? 2 : 1
                                columnSpacing: root.g(6)
                                horizontalItemAlignment: labelCol.column === 0 ? Grid.AlignRight : Grid.AlignLeft
                                verticalItemAlignment: Grid.AlignVCenter
                                Text {
                                    objectName: "sankeyNodeLabel"
                                    text: place.n.label || ""
                                    font: root.fName
                                    color: root.nodeTone(place.n)
                                }
                                Text {
                                    objectName: "sankeyNodeSub"
                                    visible: (!root.compact || root.examining) && text !== ""
                                    text: root.examining ? root.examinedSub(place.modelData.index) : (place.n.sub || "")
                                    font: root.fSub
                                    color: root.examining ? root.ink : root.ink2
                                }
                            }
                        }
                    }
                }
            }

        }

        // ── The legend: each tone's swatch, its words and its count ────────
        Flow {
            width: parent.width
            spacing: root.g(16)
            visible: root.legend.length > 0
            Repeater {
                model: root.legend
                Row {
                    id: key
                    required property var modelData
                    objectName: "sankeyLegend"
                    spacing: root.g(6)
                    Item {
                        id: swatch
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.u(root.patterns ? 18 : 14); height: root.u(root.patterns ? 10 : 8)
                        clip: true
                        Rectangle {
                            anchors.fill: parent
                            radius: root.u(2)
                            color: Qt.alpha(key.modelData.tone, key.modelData.strength || 0.4)
                        }
                        Shape {     // the same pattern as its ribbons
                            anchors.fill: parent
                            visible: !!root && root.patterns && !!key.modelData.pattern
                            ShapePath {
                                fillColor:   key.modelData.pattern === "dots" ? Qt.alpha(root.ink, 0.55) : "transparent"
                                strokeColor: key.modelData.pattern === "dots" ? "transparent" : Qt.alpha(root.ink, 0.45)
                                strokeWidth: Math.max(0.4, 0.5 * root.u(1))
                                PathSvg { path: root && root.patterns ? root.patternPath(key.modelData.pattern, 0, 0, swatch.width, 0,
                                                                                         swatch.height, root.u(4)) : "" }
                            }
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: key.modelData.name || ""
                        font: root.fKey
                        color: root.ink
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: key.modelData.line || ""
                        font: root.fSub
                        color: root.ink2
                    }
                }
            }
        }
    }
}
