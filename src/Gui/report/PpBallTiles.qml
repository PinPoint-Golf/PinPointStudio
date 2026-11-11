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

// THE BALL — the cover's launch-monitor block (design §3 page 1, §3.1): each figure a typical value (the median)
// in large type with its range beneath. Only with a launch monitor (the map's `ball` is null without one, and the
// cover then draws nothing here: empty means absent).
//   · one club: up to 8 figures, four to a row (the mock's look);
//   · several clubs: medians across clubs mean nothing, so the block becomes a small table, one row per club
//     (4 at most; the map words "N more clubs") and its first four figures, the full set being in the numbers. The
//     columns are the readings any club has, in the map's order (the model writes each club's tiles in one fixed order
//     and leaves out a reading it has none of, so clubs can differ); a club without that reading — or with no launch
//     monitor reading at all, a club the map lists with no tiles — says "-" in that cell: shown, never left out.
// Widths are shares of the text width; a club column is sized from the widest club name.
Column {
    id: bt
    property QtObject ro: null
    property var ball: null
    readonly property var groups: ((ball || {}).groups || []).slice(0, 4)
    readonly property bool multi: groups.length > 1
    // one club: its own tiles (the first club that has any)
    readonly property var tiles: {
        for (const g of groups) if ((g.tiles || []).length > 0) return g.tiles.slice(0, 8)
        return []
    }
    // the readings across the clubs, each where it first comes in the map's order, the first four of them
    readonly property var heads: {
        const out = []
        for (const g of groups) {
            let at = 0
            for (const t of (g.tiles || [])) {
                const i = out.findIndex(h => h.label === t.label)
                if (i >= 0) { at = i + 1; continue }
                out.splice(at, 0, t)
                at += 1
            }
        }
        return out.slice(0, 4)
    }
    // a club's cells under the heads: its tile for each, or "-"
    function cellsOf(g) {
        const ts = (g && g.tiles) || []
        return heads.map(h => ts.find(t => t.label === h.label) || { label: h.label, value: "-", unit: "", range: "" })
    }
    spacing: 10
    width: 600

    Row {
        spacing: 10
        Text { id: bh; text: "The ball"; font: bt.ro.fH3; color: bt.ro.ink }
        Text {
            anchors.baseline: bh.baseline
            width: Math.min(implicitWidth, bt.width - bh.width - 10)
            text: (bt.ball || {}).byline || ""; font: bt.ro.fSmall; color: bt.ro.ink3; wrapMode: Text.WordWrap
        }
    }
    Rectangle { width: parent.width; height: bt.ro.hw; color: bt.ro.hair }

    // one club: the figures in rows of four
    Grid {
        visible: !bt.multi
        columns: 4; columnSpacing: 0; rowSpacing: 14
        Repeater {
            model: bt.multi ? [] : bt.tiles
            delegate: Column {
                id: tile
                required property var modelData
                width: bt.width / 4; spacing: 1
                Text { width: parent.width - 8; text: tile.modelData.label || ""; font: bt.ro.fSmall; color: bt.ro.ink2; wrapMode: Text.WordWrap }
                Row {
                    spacing: 4
                    Text { id: tv; text: tile.modelData.value || "-"; font: bt.ro.fFigL; color: bt.ro.ink }
                    Text { anchors.baseline: tv.baseline; text: tile.modelData.unit || ""; font: bt.ro.fSmall; color: bt.ro.ink2 }
                }
                Text { width: parent.width - 8; text: tile.modelData.range || ""; font: bt.ro.fMicro; color: bt.ro.ink3; wrapMode: Text.WordWrap }
            }
        }
    }

    // several clubs: a row per club, the first four figures
    Column {
        id: multiCol
        visible: bt.multi
        width: bt.width; spacing: 8
        FontMetrics { id: clubFm; font: bt.ro.fBodyM }
        readonly property real clubW: {
            let w = 0
            for (const g of bt.groups) w = Math.max(w, clubFm.advanceWidth(g.club || ""))
            return Math.min(bt.width * 0.3, w + 2 * bt.ro.em)
        }
        readonly property real cellW: (bt.width - clubW) / Math.max(1, bt.heads.length)
        Row {
            Item { width: multiCol.clubW; height: 1 }
            Repeater {
                model: bt.multi ? bt.heads : []
                delegate: Text { required property var modelData; width: multiCol.cellW - 8; text: modelData.label || ""; font: bt.ro.fSmall; color: bt.ro.ink2; wrapMode: Text.WordWrap }
            }
        }
        Repeater {
            model: bt.multi ? bt.groups : []
            delegate: Row {
                id: grow
                required property var modelData
                readonly property real clubW: multiCol.clubW
                readonly property real cellW: multiCol.cellW
                Text { width: grow.clubW - 8; text: grow.modelData.club || ""; font: bt.ro.fBodyM; color: bt.ro.ink; wrapMode: Text.WordWrap; topPadding: 6 }
                Repeater {
                    model: bt.cellsOf(grow.modelData)
                    delegate: Column {
                        id: cell
                        objectName: "reportBallCell"
                        readonly property string club: grow.modelData.club || ""
                        readonly property string text: cell.modelData.value || "-"
                        required property var modelData
                        width: grow.cellW; spacing: 0
                        Row {
                            spacing: 4
                            Text { id: cvv; text: cell.text; font.family: bt.ro.figs; font.pixelSize: 19; color: cell.text !== "-" ? bt.ro.ink : bt.ro.ink3 }
                            Text { anchors.baseline: cvv.baseline; text: cell.modelData.unit || ""; font: bt.ro.fMicro; color: bt.ro.ink2 }
                        }
                        Text { width: parent.width - 8; text: cell.modelData.range || ""; font: bt.ro.fMicro; color: bt.ro.ink3; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
        Text { visible: !!(bt.ball || {}).more; width: bt.width; text: (bt.ball || {}).more || ""; font: bt.ro.fMicro; color: bt.ro.ink3; wrapMode: Text.WordWrap }
    }
    Text { visible: text !== ""; width: parent.width; text: (bt.ball || {}).note || ""; font: bt.ro.fMicro; color: bt.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
}
