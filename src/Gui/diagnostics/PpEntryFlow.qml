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

// ENTRYFLOW — B's run-in head entries in newspaper columns with column rules.
//   kind "fired":   items = fired conditions; each entry "Name  22.1% hand rise. The corridor … ; seven more after
//                   this one." Columns ≥ roles.flows.firedEntry em, balanced by estimated height.
//   kind "session": items = cards; numbered entries (margin numeral), the sentence, a meta line, and a
//                   PpSparkline in the margin when a column is ≥ flows.entryMargin em, else on the head line.
// Clicking an entry's name → conditionActivated(id); hovering brushes the condition.
Item {
    id: ef
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    readonly property bool isPrint: medium === "print"
    property bool interactive: !isPrint
    property string kind: "fired"
    property var items: []
    property string label: ""
    property var cards: []                      // fired: the card per condition (for the trend words)
    property var readout: null
    property string xrefId: ""                  // session, print: the entry that cross-references the feature
    property int xrefPage: 0
    property bool showFocus: !isPrint           // session: the quiet "Focus" link on the meta line
    property int cols: 0                         // 0 = count from the width
    property bool atom: true
    readonly property var keyKinds: kind === "session" ? ["fired", "clean", "ring", "fault", "corridor"] : []
    signal conditionActivated(string id)
    signal conditionHovered(string id)
    signal focusActivated(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property int shotCount: readout ? readout.shotCount : 0
    readonly property real g: roles.gutter(width)
    readonly property int n: cols > 0 ? cols : Math.min(roles.columns(width, kind === "session" ? roles.flows.entry : roles.flows.firedEntry, g),
                                                          Math.max(1, (items || []).length))
    readonly property real colW: (width - (n - 1) * g) / n
    readonly property bool margin: kind === "session" && colW >= roles.flows.entryMargin * roles.em
    readonly property real numW: 2.3 * roles.em
    readonly property real sparkW: roles.flows.sparkline * roles.em
    function plain(h) { return String(h).replace(/<[^>]+>/g, "").replace(/&[a-z]+;/g, "x") }
    function linesFor(len, px, w) { return Math.max(1, Math.ceil(len * px * 0.53 / Math.max(40, w))) }
    function balance(list, k, est) {
        const hs = list.map(est), total = hs.reduce((a, b) => a + b, 0), target = total / k
        const out = []; for (let i = 0; i < k; ++i) out.push([])
        let c = 0, acc = 0
        for (let i = 0; i < list.length; ++i) {
            if (c < k - 1 && out[c].length > 0 && acc + hs[i] / 2 > target) { c++; acc = 0 }
            out[c].push(list[i]); acc += hs[i]
        }
        return out.filter(x => x.length)
    }
    function entryHtml(c) {
        if (kind === "fired") return roles.firedEntry(c, roles.byId(cards, c.id), shotCount)
        return roles.sessionEntry(c, margin, isPrint && c.id === xrefId ? xrefPage : 0, shotCount)
    }
    function metaHtml(c) {
        const bits = [c.phase, roles.lower1(c.measure)]
        if (c.reachesBall) bits.push("reaches the ball")
        if (c.rootHere) bits.push("root")
        return roles.esc(bits.filter(b => b).join(" · "))
    }
    readonly property var colsData: kind === "session"
        ? (() => { const per = Math.max(1, Math.ceil((items || []).length / Math.max(1, n || 1))), out = []; for (let i = 0; i < (items || []).length; i += per) out.push(items.slice(i, i + per)); return out })()
        : balance(items || [], n, c => linesFor(plain(entryHtml(c)).length, roles.fBody.pixelSize, colW) * roles.fBody.pixelSize * roles.r.leading + roles.u(12))

    implicitHeight: frow.y + frow.height
    height: implicitHeight

    Text {
        id: lbl
        visible: ef.label.length > 0
        text: ef.label; font: ef.roles.fKicker; color: ef.roles.ink3
    }
    Row {
        id: frow
        y: lbl.visible ? lbl.implicitHeight + ef.roles.u(6) : 0
        spacing: ef.g
        Repeater {
            model: ef.roles ? (ef.colsData) : []
            delegate: Column {
                required property var modelData
                width: ef.colW; spacing: ef.kind === "session" ? ef.roles.u(22) : ef.roles.u(12)
                Repeater {
                    model: ef.roles ? (modelData) : []
                    delegate: Item {
                        id: entry
                        required property var modelData
                        readonly property var c: modelData
                        readonly property bool hot: ef.brush && ef.brush.hoveredCondition === c.id
                        width: ef.colW
                        height: ef.kind === "session" ? meta.y + meta.height : para.height
                        opacity: ef.brush ? ef.brush.conditionOpacity(c.id, ef.roles.dim) : 1
                        Behavior on opacity { NumberAnimation { duration: ef.roles.normal } }
                        Rectangle { anchors.fill: parent; anchors.margins: -ef.roles.u(3); visible: entry.hot; color: ef.roles.rowHover }
                        MouseArea {
                            enabled: ef.interactive; visible: enabled
                            anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onContainsMouseChanged: { if (ef.brush) ef.brush.hoverCondition(entry.c.id, containsMouse); ef.conditionHovered(containsMouse ? entry.c.id : "") }
                            onClicked: ef.conditionActivated(entry.c.id)
                        }
                        // session: margin numeral
                        Text {
                            visible: ef.kind === "session"
                            text: String(entry.c.rankText || "").replace("#", "")
                            font: ef.roles.fTitle; color: ef.roles.ink3
                            y: (ef.margin ? para.y + para.baselineOffset : nameT.y + nameT.baselineOffset) - baselineOffset
                        }
                        Text {
                            id: nameT
                            visible: ef.kind === "session" && !ef.margin
                            x: ef.numW; width: ef.colW - ef.numW - ef.sparkW - ef.roles.u(12)
                            wrapMode: Text.Wrap
                            text: entry.c.name || ""
                            font: ef.roles.fBodyM; color: ef.roles.ink
                        }
                        Text {
                            id: para
                            x: ef.kind === "session" ? ef.numW : 0
                            y: ef.kind !== "session" || ef.margin ? 0 : Math.max(nameT.visible ? nameT.height : 0, spark.y + spark.height) + ef.roles.u(3)
                            width: ef.kind !== "session" ? ef.colW : ef.margin ? ef.colW - ef.numW - ef.sparkW - ef.roles.u(18) : ef.colW - ef.numW
                            wrapMode: Text.Wrap; textFormat: Text.RichText
                            text: ef.entryHtml(entry.c)
                            font: ef.roles.fBody; color: ef.roles.ink2; lineHeight: ef.roles.r.leading
                        }
                        Row {
                            id: meta
                            visible: ef.kind === "session"
                            x: ef.numW; y: Math.max(para.y + para.height, ef.margin ? spark.y + spark.height : 0) + ef.roles.u(4)
                            width: para.width
                            spacing: ef.roles.u(10)
                            Text {
                                width: Math.min(implicitWidth, para.width - (focusT.visible ? focusT.width + ef.roles.u(10) : 0))
                                wrapMode: Text.Wrap; textFormat: Text.RichText
                                text: ef.metaHtml(entry.c); font: ef.roles.fSmall; color: ef.roles.ink3
                            }
                            Text {
                                id: focusT
                                visible: ef.showFocus
                                text: entry.c.focused ? "Focused" : "Focus"; font: ef.roles.fSmallL; color: ef.roles.link
                                MouseArea { enabled: ef.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ef.focusActivated(entry.c.id) }
                            }
                        }
                        PpSparkline {
                            id: spark
                            visible: ef.kind === "session"
                            roles: ef.roles; brush: ef.brush; medium: ef.medium
                            card: visible ? entry.c : null
                            x: ef.colW - ef.sparkW; y: ef.margin ? ef.roles.u(3) : 0
                            width: ef.sparkW; height: ef.margin ? 2.4 * ef.roles.em : 1.5 * ef.roles.em
                            onShotActivated: (d, s) => ef.shotActivated(d, s)
                            onShotHovered: s => ef.shotHovered(s)
                        }
                    }
                }
            }
        }
    }
    Repeater {
        model: ef.roles ? (Math.max(0, ef.colsData.length - 1)) : []
        delegate: Rectangle {
            required property int index
            x: (index + 1) * ef.colW + index * ef.g + ef.g / 2; y: frow.y
            width: ef.roles.hw; height: frow.height; color: ef.roles.hair
        }
    }
}
