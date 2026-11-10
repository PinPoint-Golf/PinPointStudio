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

import QtQuick
import QtQuick.Shapes
import PinPointStudio

Item {
    id: root

    property var nodes:   []
    property var links:   []
    property var columns: []
    property var legend:  []
    property real plotHeight:   Theme.sp(360)       // the height the layout was made for: plot units are pixels
    property real nodeWidth:    Theme.sp(12)
    property real labelGutter:  Theme.sp(104)       // room left of the first column and right of the last
    property real compactWidth: Theme.sp(640)

    // A node was activated (pressed, or Return on its focus). Exposed; the tendency flow leaves it unwired for now.
    signal nodeActivated(int index)

    readonly property bool compact: root.width < root.compactWidth
    property int  hoveredNode: -1
    property int  hoveredLink: -1
    readonly property bool examining: root.hoveredNode >= 0 || root.hoveredLink >= 0

    implicitWidth:  Theme.sp(720)
    implicitHeight: body.implicitHeight

    function colX(c) {
        const x0 = root.labelGutter, x2 = Math.max(x0, root.width - root.labelGutter - root.nodeWidth)
        return c === 0 ? x0 : c === 2 ? x2 : Math.round((x0 + x2) / 2)
    }
    // A node's ink: the caller's tone, else green for a good node and ink for the rest.
    function nodeTone(n) { return n && n.tone !== undefined ? n.tone : n && n.good ? Theme.colorGood : Theme.colorText }
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
            const y = Math.max(n.y + n.h / 2 - lineH / 2, floor)
            out.push({ index: i, y: y })
            floor = y + lineH + Theme.gap(2)
        }
        // …and back up from the plot's foot, so a crowded column's last labels never run off the bottom.
        let ceiling = root.plotHeight
        for (let k = out.length - 1; k >= 0; --k) {
            out[k].y = Math.min(out[k].y, ceiling - lineH)
            ceiling = out[k].y - Theme.gap(2)
        }
        return out
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

    onLinksChanged: fade.restart()

    Column {
        id: body
        width: root.width
        spacing: Theme.gap(10)

        // ── Column heads ────────────────────────────────────────────────────
        Item {
            width: parent.width
            height: Theme.sp(14)
            Repeater {
                model: root.columns
                PpMicro {
                    required property var modelData
                    required property int index
                    kind: "caps"
                    text: String(modelData)
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

            NumberAnimation { id: fade; target: plot; property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal }

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
                    readonly property real y0: modelData.y0
                    readonly property real y1: modelData.y1
                    readonly property real h:  modelData.h

                    ShapePath {
                        fillColor:   Qt.alpha(ribbon.modelData.tone, (ribbon.modelData.strength || 0.4) * (root.lit(ribbon.index) ? 1 : 0.25))
                        strokeColor: Theme.colorSurface
                        strokeWidth: Math.max(0.5, 0.6 * Theme.sp(1))
                        startX: ribbon.x0; startY: ribbon.y0
                        PathCubic { control1X: ribbon.xm; control1Y: ribbon.y0; control2X: ribbon.xm; control2Y: ribbon.y1
                                    x: ribbon.x1; y: ribbon.y1 }
                        PathLine  { x: ribbon.x1; y: ribbon.y1 + ribbon.h }
                        PathCubic { control1X: ribbon.xm; control1Y: ribbon.y1 + ribbon.h; control2X: ribbon.xm; control2Y: ribbon.y0 + ribbon.h
                                    x: ribbon.x0; y: ribbon.y0 + ribbon.h }
                        PathLine  { x: ribbon.x0; y: ribbon.y0 }
                    }
                    HoverHandler { onHoveredChanged: root.linkHover(ribbon.index, hovered) }
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
                    x: root.colX(modelData.column) - Theme.sp(3)
                    y: modelData.y
                    width:  root.nodeWidth + Theme.sp(6)
                    height: Math.max(Theme.sp(2), modelData.h)
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: (modelData.label || "") + ", " + (modelData.sub || "")
                    onActiveFocusChanged: root.hoveredNode = activeFocus ? node.index : (root.hoveredNode === node.index ? -1 : root.hoveredNode)
                    Keys.onReturnPressed: root.nodeActivated(node.index)
                    Keys.onEnterPressed:  root.nodeActivated(node.index)

                    Rectangle {
                        x: Theme.sp(3)
                        width: root.nodeWidth
                        height: parent.height
                        radius: Theme.sp(1)
                        color: root.nodeTone(node.modelData)
                        opacity: root.reached(node.index) ? 1 : 0.35
                    }
                    Rectangle {     // the keyboard focus
                        visible: node.activeFocus
                        anchors.fill: parent
                        anchors.margins: -Theme.sp(2)
                        color: "transparent"
                        border.width: Theme.sp(2)
                        border.color: Theme.colorAccent
                        radius: Theme.radius
                    }
                    HoverHandler { onHoveredChanged: root.hoveredNode = hovered ? node.index : (root.hoveredNode === node.index ? -1 : root.hoveredNode) }
                    TapHandler   { onTapped: root.nodeActivated(node.index) }
                }
            }

            // Labels, column by column.
            Repeater {
                model: 3
                Item {
                    id: labelCol
                    required property int index
                    readonly property int column: index
                    readonly property real lineH: Theme.fontSzBody + (root.compact ? 0 : Theme.fontSzLabel + Theme.gap(2))
                                                 + (column === 1 ? Theme.gap(6) : 0)     // the middle column's chip padding
                    anchors.fill: parent
                    Repeater {
                        model: root.labelPlaces(labelCol.column, labelCol.lineH)
                        Item {
                            id: place
                            required property var modelData
                            readonly property var n: root.nodes[modelData.index] || ({})
                            width: words.width + (labelCol.column === 1 ? Theme.gap(8) : 0)
                            height: words.height + (labelCol.column === 1 ? Theme.gap(4) : 0)
                            x: labelCol.column === 0 ? root.colX(0) - Theme.gap(8) - width
                             : labelCol.column === 2 ? root.colX(2) + root.nodeWidth + Theme.gap(8)
                                                     : root.colX(1) + root.nodeWidth + Theme.gap(6)
                            y: modelData.y - (labelCol.column === 1 ? Theme.gap(2) : 0)
                            opacity: root.reached(modelData.index) ? 1 : 0.35
                            Rectangle {     // the middle column's knock-out chip
                                visible: labelCol.column === 1
                                anchors.fill: parent
                                radius: Theme.radius
                                color: Qt.alpha(Theme.colorSurface, 0.86)
                            }
                            Column {
                                id: words
                                anchors.centerIn: parent
                                Text {
                                    objectName: "sankeyNodeLabel"
                                    anchors.right: labelCol.column === 0 ? parent.right : undefined
                                    text: place.n.label || ""
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    font.weight:    Theme.fontBodyWeight
                                    color: root.nodeTone(place.n)
                                }
                                Text {
                                    objectName: "sankeyNodeSub"
                                    visible: (!root.compact || root.examining) && text !== ""
                                    anchors.right: labelCol.column === 0 ? parent.right : undefined
                                    text: root.examining ? root.examinedSub(place.modelData.index) : (place.n.sub || "")
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzLabel
                                    color: root.examining ? Theme.colorText : Theme.colorText2
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
            spacing: Theme.gap(16)
            visible: root.legend.length > 0
            Repeater {
                model: root.legend
                Row {
                    required property var modelData
                    objectName: "sankeyLegend"
                    spacing: Theme.gap(6)
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.sp(14); height: Theme.sp(8)
                        radius: Theme.sp(2)
                        color: Qt.alpha(parent.modelData.tone, parent.modelData.strength || 0.4)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.name || ""
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody2
                        font.weight:    Theme.fontBodyWeight
                        color: Theme.colorText
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.line || ""
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzLabel
                        color: Theme.colorText2
                    }
                }
            }
        }
    }
}
