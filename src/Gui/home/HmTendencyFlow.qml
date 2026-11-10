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

// THE TENDENCY FLOW — beside the rows on Home's Tendencies tab (docs/design/tendencies_flow_design.md): every shot of the
// clubs shown, strike → direction → flight, as a PpSankey. The ideal path (solid, on line, mid or high) is green; every
// other ribbon is coloured by where its shots first left it, in the row's scheme (Mark, 10 Nov 2026): mis-struck red,
// struck well but off line red at a lighter strength, on line but flight off amber.
//
//   micro     Tendencies · <clubs> · <n> shots                  (the card's title)
//   headline  "<p>% of your <club noun> were solid, on line and flew mid or high"
//   subtitle  how to read it
//   the flow, its legend, and how many shots it could not show
//
// Under kTendencyFlowMinShots shots with all three read, one line says what would show it. Paint-only: every word and
// every place is WorkOnsController.tendencyFlow(group, plotHeight, gap), read again whenever the tendencies publish.

import QtQuick
import PinPointStudio

PpCard {
    id: root
    objectName: "homeTendencyFlow"

    property var    controller: null
    property string group: ""

    readonly property real plotHeight: Math.max(Theme.sp(300), Math.round(root.innerWidth / 1.4))
    readonly property real nodeGap:    Theme.sp(12)
    readonly property var  flow: {
        const c = root.controller
        if (!c || typeof c.tendencyFlow !== "function") return ({})
        const items = c.tendencyItems      // read again when the tendencies publish
        return items ? c.tendencyFlow(root.group, root.plotHeight, root.nodeGap) : ({})
    }
    readonly property bool enough: !!root.flow.enough

    // A group's ribbon tone and strength, and its legend swatch.
    function tone(g)     { return g === 0 ? Theme.colorGood : g === 1 ? Theme.toneColor("watch") : Theme.colorWarn }
    // A node and its label in the rows' scheme: good green, Low amber, a fault red, a fold of several in ink.
    function nodeTone(kind) { return kind === "good" ? Theme.colorGood : kind === "watch" ? Theme.toneColor("watch")
                                   : kind === "fault" ? Theme.colorWarn : Theme.colorText }
    // Strong enough to read on a dark ground; the two reds told apart by strength, the legend saying which is which.
    function strength(g) { return g === 0 ? 0.62 : g === 1 ? 0.6 : g === 2 ? 0.42 : 0.78 }

    title: root.flow.micro || qsTr("Tendencies")

    HmHeadline {
        objectName: "tendencyFlowHeadline"
        visible: root.enough
        width: root.innerWidth
        font.pixelSize: Theme.fontSzHeading
        text: root.flow.headline || ""
    }
    Text {
        objectName: "tendencyFlowSubtitle"
        visible: root.enough
        width: root.innerWidth
        text: root.flow.subtitle || ""
        wrapMode: Text.WordWrap
        lineHeight: 1.4
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color: Theme.colorText2
    }
    PpSankey {
        objectName: "tendencyFlowSankey"
        visible: root.enough
        width: root.innerWidth
        plotHeight: root.plotHeight
        labelGutter: Theme.sp(92)
        compactWidth: Theme.sp(300)     // a side panel keeps its counts; only a phone-width flow drops them
        columns: root.flow.columns || []
        nodes: (root.flow.nodes || []).map(n => Object.assign({}, n, { tone: root.nodeTone(n.kind) }))
        links: (root.flow.links || []).map(l => Object.assign({}, l, { tone: root.tone(l.group), strength: root.strength(l.group) }))
        legend: (root.flow.groups || []).filter(g => g.count > 0)
                    .map(g => ({ tone: root.tone(g.group), strength: root.strength(g.group), name: g.name, line: g.line }))
    }
    PpCaption {
        objectName: "tendencyFlowExcluded"
        visible: root.enough && text !== ""
        width: root.innerWidth
        elide: Text.ElideNone
        wrapMode: Text.WordWrap
        text: root.flow.excludedLine || ""
    }
    PpCaption {
        objectName: "tendencyFlowEmpty"
        visible: !root.enough && root.controller !== null
        width: root.innerWidth
        elide: Text.ElideNone
        wrapMode: Text.WordWrap
        text: root.flow.emptyLine || ""
    }
}
