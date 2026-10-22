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
import PinPointStudio

// PPWHATCHANGED — "What changed" (posture_metrics_design.md §8): a composite finding read part by part,
// because the composite's own number is never shown (session diagnostics principle 5). One line per part:
// its name in the golfer's words, the reading at address and at impact (or the top) in the part's own unit
// where the measure names both ends, the change, the part's own band colour (with the band's word, so the
// colour is never the only channel) and an ordinal share — most of it, some, a little, against it. A part
// not read shows "-"; WHY is the trust fold's line, said once there, never here.
// Poor setup posture (mode "distance") draws the same table without a change column: the address reading
// against its Ideal band, and the share.
// Every string and the share ladder are the model's (SessionDiagnosticsModel::whatChangedOf); this file
// positions and paints.
//   info      detail.whatChanged / a this-shot cell's whatChanged
//   sub       a name after the head (Replay names the condition; the detail page needs none)
//   printed   the report: set open under its kicker, nothing to click, one atom for PpPager
// objectNames: sdWhatChanged, sdWhatChangedToggle, sdWhatChangedRow:<part>.
Column {
    id: wc
    objectName: "sdWhatChanged"
    property QtObject roles: null
    property var info: null
    property string sub: ""
    property bool open: true
    property bool printed: false
    property bool interactive: true
    readonly property bool atom: printed
    readonly property bool keepNext: false
    readonly property var keyKinds: []
    readonly property bool _shown: open || printed

    readonly property bool distance: !!info && info.mode === "distance"
    readonly property var rows: (info && info.rows) || []
    // the two ends only when some part names them (a change between two instants)
    readonly property bool hasEnds: !distance && rows.some(r => r.read && (String(r.startText || "") !== "" || String(r.endText || "") !== ""))
    readonly property string endWord: (info && info.endWord) || "impact"
    readonly property var cols: {
        const R = roles
        if (!R) return []
        const out = [{ k: "name", head: "Part", w: 0 }]
        if (distance) {
            out.push({ k: "value", head: "At address", w: 6.2, r: true })
            out.push({ k: "ideal", head: "Ideal", w: 8.4, r: true })
        } else {
            if (hasEnds) {
                out.push({ k: "start", head: "Address", w: 5.6, r: true })
                out.push({ k: "end", head: R.cap1(endWord), w: 5.6, r: true })
            }
            out.push({ k: "change", head: "Change", w: 5.8, r: true })
        }
        out.push({ k: "band", head: "Band", w: 6.6 })
        out.push({ k: "share", head: "Share", w: 6.4 })
        return out
    }
    readonly property real fixedW: roles ? cols.reduce((s, c) => s + c.w * roles.em, 0) + (cols.length - 1) * gap : 0
    readonly property real gap: roles ? roles.u(12) : 12
    // the table is a reading block, not a banner: the name takes what is left up to 20 em, never the whole width
    readonly property real tableW: Math.min(width, fixedW + (roles ? 20 * roles.em : 200))
    readonly property real nameW: Math.max(roles ? roles.u(90) : 90, tableW - fixedW)
    function colW(c) { return c.k === "name" ? nameW : c.w * roles.em }
    function colX(i) { let x = 0; for (let j = 0; j < i; ++j) x += colW(cols[j]) + gap; return x }

    // the part's band, in the status family the corridor bands are drawn in (Theme.corridorTone), from the
    // register's own colours so print keeps its fixed aesthetic
    function tone(g) {
        const fm = roles && roles._fm ? roles._fm : null
        const good = fm && fm.colorGood !== undefined ? fm.colorGood : Theme.colorGood
        const att = fm && fm.colorAttention !== undefined ? fm.colorAttention : Theme.colorAttention
        return g === "ideal" ? good : g === "good" ? Qt.alpha(good, 0.55) : g === "watch" ? att
             : g === "action" ? roles.warn : roles.ink3
    }
    function cell(r, k) {
        if (k === "name") return roles.cap1(r.name || r.part || "") + (r.unitNote ? " (" + r.unitNote + ")" : "")
        if (k === "start") return r.read ? (r.startText || "") : "-"
        if (k === "end") return r.read ? (r.endText || "") : "-"
        if (k === "change") return r.changeText || "-"
        if (k === "value") return r.valueText || "-"
        if (k === "ideal") return r.read ? (r.idealText || "") : ""
        if (k === "share") return r.shareWord || "-"
        return ""
    }

    visible: rows.length > 0
    spacing: roles ? roles.u(4) : 4

    // the head: ▾ What changed · <sub>  (print: the kicker, no glyph)
    Item {
        objectName: "sdWhatChangedToggle"
        width: wc.width
        height: headRow.height
        Flow {
            id: headRow
            width: parent.width
            spacing: wc.roles ? wc.roles.u(7) : 7
            Text { text: (wc.printed ? "" : (wc.open ? "▾ " : "▸ ")) + "What changed"
                   font: wc.roles ? (wc.printed ? wc.roles.fSmallM : wc.roles.fSmallL) : Qt.font({})
                   color: wc.roles ? (wc.printed || !wc.interactive ? wc.roles.ink : wc.roles.link) : "grey" }
            Text { visible: text !== ""
                   text: wc.sub !== "" ? wc.sub
                         : wc.distance ? "your address posture, part by part" : "from address to " + wc.endWord + ", part by part"
                   font: wc.roles ? wc.roles.fSmall : Qt.font({}); color: wc.roles ? wc.roles.ink3 : "grey" }
        }
        MouseArea { anchors.fill: parent; enabled: !wc.printed && wc.interactive; cursorShape: Qt.PointingHandCursor; onClicked: wc.open = !wc.open }
    }
    // the table
    Item {
        visible: wc._shown
        width: wc.width
        height: visible ? tbl.height : 0
        Column {
            id: tbl
            width: wc.tableW
            topPadding: wc.roles ? wc.roles.u(4) : 4
            // column heads
            Item {
                width: tbl.width
                height: wc.roles ? wc.roles.u(20) : 20
                Repeater {
                    model: wc.roles ? wc.cols : []
                    delegate: Text {
                        required property var modelData
                        required property int index
                        x: wc.colX(index); width: wc.colW(modelData); elide: Text.ElideRight
                        horizontalAlignment: modelData.r ? Text.AlignRight : Text.AlignLeft
                        text: modelData.k === "name" ? "" : modelData.head
                        font: wc.roles.fLabel; color: wc.roles.ink3
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: wc.roles ? wc.roles.hw : 1; color: wc.roles ? wc.roles.hair : "grey" }
            }
            Repeater {
                model: wc.roles ? wc.rows : []
                delegate: Item {
                    id: rowItem
                    required property var modelData
                    objectName: "sdWhatChangedRow:" + (modelData.part || "")
                    readonly property bool read: !!modelData.read
                    width: tbl.width
                    height: Math.max(nameMeasure.implicitHeight, wc.roles.u(18)) + wc.roles.u(8)
                    Text { id: nameMeasure; visible: false; width: wc.nameW; wrapMode: Text.WordWrap
                           text: wc.cell(rowItem.modelData, "name"); font: wc.roles.fSmallM }
                    Repeater {
                        model: wc.cols
                        delegate: Item {
                            required property var modelData
                            required property int index
                            readonly property string k: modelData.k
                            x: wc.colX(index); width: wc.colW(modelData); height: rowItem.height
                            Text {
                                id: tx
                                visible: parent.k !== "band"
                                y: wc.roles.u(4); width: parent.width
                                wrapMode: parent.k === "name" ? Text.WordWrap : Text.NoWrap
                                horizontalAlignment: modelData.r ? Text.AlignRight : Text.AlignLeft
                                text: wc.cell(rowItem.modelData, parent.k)
                                font: parent.k === "name" ? (rowItem.modelData.dominant ? wc.roles.fSmallM : wc.roles.fSmall)
                                      : parent.k === "share" || parent.k === "ideal" ? wc.roles.fSmall : wc.roles.fFigS
                                color: !rowItem.read ? wc.roles.ink3
                                       : parent.k === "share" && rowItem.modelData.shareWord === "against it" ? wc.roles.ink3
                                       : parent.k === "name" || parent.k === "change" || parent.k === "value" ? wc.roles.ink : wc.roles.ink2
                            }
                            // the band: a swatch in the band's colour and its word
                            Row {
                                visible: parent.k === "band" && rowItem.read && String(rowItem.modelData.grade || "") !== ""
                                y: wc.roles.u(4); spacing: wc.roles.u(5)
                                Rectangle { width: wc.roles.u(9); height: width; radius: width / 2; anchors.verticalCenter: parent.verticalCenter
                                            color: wc.tone(rowItem.modelData.grade) }
                                Text { text: String(rowItem.modelData.grade || ""); font: wc.roles.fSmall; color: wc.roles.ink2 }
                            }
                            Text {
                                visible: parent.k === "band" && (!rowItem.read || String(rowItem.modelData.grade || "") === "")
                                y: wc.roles.u(4); text: "-"; font: wc.roles.fSmall; color: wc.roles.ink3
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: wc.roles.hw; color: wc.roles.hair }
                }
            }
            Text {
                width: wc.roles ? Math.min(tbl.width, wc.roles.measureW(tbl.width)) : tbl.width
                topPadding: wc.roles ? wc.roles.u(5) : 5
                wrapMode: Text.WordWrap
                text: wc.distance
                      ? "Each part at address against its own ideal band. Share: how much of the finding each part carries."
                      : "Change: " + wc.endWord + " against address, in the direction each part's name says. Share: how much of the finding each part carries; against it, a part that moved the other way."
                font: wc.roles ? wc.roles.fMicro : Qt.font({}); color: wc.roles ? wc.roles.ink3 : "grey"
            }
        }
    }
}
