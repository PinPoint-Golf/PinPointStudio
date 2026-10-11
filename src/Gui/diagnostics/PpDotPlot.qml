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

// DOTPLOT — C's This-shot figure: every condition on ONE common, corridor-normalised scale. In those
// steps a reading sits at 0 in the middle of its corridor, the pass edge is at 2 (where a condition
// fires) and the fault line at 3; x = strengthExcess for this shot. Fired conditions are warn marks on a
// warn stem from the pass edge; clean ones are clean marks inside the wash; "‹" = out on the other side,
// so clean here. Rows sort by distance. Laid out in as many blocks as fit: rich (session tier · name ·
// plot · value) from roles.flows.dotRich em a block, tight (names wrap, values never) from dotTight.
// filter "fired" draws only the fired rows (Replay); the scale stays the same.
// Round 4:
//   • showTier: the tier column FOLDS before the plot goes tight — between dotRich − the tier column and
//     dotRich the rows stay rich and the tier word joins the name ("pattern · Over the top").
//   • rowsFitting(h[, list]): how many rows (of allRows, or `list`) fit a height as ONE block — the
//     plot's own arithmetic, so a register never re-implements it.
//   • PRINT: the column heads are ONE atom across every block (and the list's head: printGroup); every
//     row is an atom, so print cuts between rows even with several blocks side by side.
Item {
    id: dk
    objectName: "sdDotPlot"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var readout: null
    property string filter: "all"               // "all" | "fired"
    property var rowsOverride: null             // print: a slice of `rows` (keeps the scale of the whole)
    property int maxBlocks: 3
    // HEIGHT IS A RESOURCE TOO (house rule 12): when > 0, the plot splits into blocks only when its rows
    // do not fit this height. 0 = width alone decides, as before.
    property real availableHeight: 0
    property bool showHead: true
    property bool showTier: true                // false: the tier never takes a column (it folds onto the name)
    property string printGroup: ""              // print: name the list so the PpPager repeats the heads after a break
    readonly property var keyKinds: ["fired", "clean", "corridor", "fault", "other"]
    signal conditionActivated(string id)
    signal conditionHovered(string id)

    // ── data (C) ──
    readonly property var allRows: {
        const cs = (readout && readout.conditions) || []
        const rows = []
        for (let i = 0; i < cs.length; ++i) {
            const c = cs[i]
            if (c.stateKind === "notAssessable") continue
            const fired = c.stateKind === "fired", known = c.strengthKnown !== false
            const ex = Number(c.strengthExcess), other = !fired && known && ex >= 2
            rows.push({ id: c.id, name: c.name, fired: fired, known: known, other: other,
                        x: (other || !known) ? 0 : ex, value: c.valueText || "-", stateKind: c.stateKind, tierTag: c.tierTag || "",
                        tier: c.tierTag === "clean all session" ? "" : c.tierTag })
        }
        rows.sort((a, b) => (b.x - a.x) || ((b.fired ? 1 : 0) - (a.fired ? 1 : 0)) || a.name.localeCompare(b.name))
        rows.forEach((r, i) => r.rank = i)
        return rows
    }
    readonly property var rows: rowsOverride ? rowsOverride : (filter === "fired" ? allRows.filter(r => r.fired) : allRows)
    readonly property real dMax: { let m = 4; for (let i = 0; i < allRows.length; ++i) m = Math.max(m, allRows[i].x); return Math.ceil(m + 0.4) }
    FontMetrics { id: fmFig; font: dk.roles.fFigS }
    FontMetrics { id: fmSmall; font: dk.roles.fSmall }
    // (each measure below reads its FontMetrics' font, so it follows a theme change: advanceWidth() is a
    // call, which a binding cannot track)
    readonly property real valueW: { if (fmFig.font.pixelSize <= 0) return 0; let m = 0; for (let i = 0; i < allRows.length; ++i) m = Math.max(m, fmFig.advanceWidth(String(allRows[i].value))); return Math.ceil(m) + roles.u(4) }
    readonly property real pitch: roles.u(12.5)
    readonly property real tierColW: roles.u(50)
    function wrapLines(t, w) {
        if (fmSmall.font.pixelSize <= 0) return 1
        const words = String(t || "").split(" "), sp = fmSmall.advanceWidth(" ")
        let lines = 1, cur = 0
        for (let i = 0; i < words.length; ++i) {
            const ww = fmSmall.advanceWidth(words[i])
            if (cur === 0) cur = ww; else if (cur + sp + ww <= w - 1) cur += sp + ww; else { lines++; cur = ww }
        }
        return lines
    }
    readonly property real richMin: roles.flows.dotRich * roles.em
    readonly property real richMinFolded: richMin - tierColW
    readonly property var lay: {
        const w = width, g = roles.gutter(w)
        const tightMin = roles.flows.dotTight * roles.em
        let nb = Math.floor((w + g) / (richMin + g)), rich = true
        if (nb < 2) {
            const nt = Math.floor((w + g) / (tightMin + g))
            if (nt >= 2) { nb = nt; rich = false } else { nb = 1; rich = w >= richMinFolded }
        }
        if (availableHeight > 0)
            nb = Math.min(nb, Math.max(1, Math.ceil(rows.length * pitch / Math.max(pitch, availableHeight - headH))))
        if (nb === 1) rich = w >= richMinFolded
        nb = Math.max(1, Math.min(maxBlocks, nb, Math.max(1, Math.ceil(rows.length / 4))))
        const bw = (w - (nb - 1) * g) / nb
        return { nb: nb, rich: rich, g: g, bw: bw, tierCol: rich && showTier && bw >= richMin }
    }
    function geoFor(bw, rich, tierCol) {
        const U = roles.u
        if (rich) {
            const tierW = tierCol ? tierColW : 0, plotW = U(172), nameW = Math.min(U(204), bw - tierW - plotW - valueW - U(24))
            const plotX = tierW + nameW + U(12)
            return { rich: true, tierCol: tierCol, tierW: tierW, nameX: tierW, nameW: nameW, plotX: plotX, plotW: plotW, valueX: plotX + plotW + U(12) }
        }
        const plotW = U(104), gap = U(10)
        const nameW = Math.max(U(110), bw - plotW - valueW - 2 * gap)
        return { rich: false, tierCol: false, tierW: 0, nameX: 0, nameW: nameW, plotX: nameW + gap, plotW: plotW, valueX: nameW + gap + plotW + gap }
    }
    readonly property var geo: geoFor(lay.bw, lay.rich, lay.tierCol)
    readonly property bool tierFolded: !geo.tierCol
    function label(r, g) { return (!(g || geo).tierCol && r.tier ? r.tier + " · " : "") + r.name }
    function rowHFor(r, g) { return g.rich && g.tierCol ? pitch : pitch + (wrapLines(label(r, g), g.nameW) - 1) * fmSmall.lineSpacing }
    function rowH(r) { return rowHFor(r, geo) }
    // rows (of `list`, default allRows, in order) that fit height h as ONE block at this width
    function rowsFitting(h, list) {
        const rs = list || allRows
        const g = geoFor(width, width >= richMinFolded, showTier && width >= richMin)
        const avail = h - headH - roles.u(4)
        let acc = 0, n = 0
        for (; n < rs.length; ++n) { const rh = rowHFor(rs[n], g); if (acc + rh > avail) break; acc += rh }
        return n
    }
    readonly property var blocks: {
        if (lay.nb <= 1) return [rows]
        let total = 0; for (let i = 0; i < rows.length; ++i) total += rowH(rows[i])
        const target = total / lay.nb, out = [[]]
        let acc = 0
        for (let i = 0; i < rows.length; ++i) {
            const h = rowH(rows[i])
            if (acc + h > target + 1 && out.length < lay.nb && out[out.length - 1].length) { out.push([]); acc = 0 }
            out[out.length - 1].push(rows[i]); acc += h
        }
        return out
    }
    readonly property real headH: showHead ? roles.u(17) : roles.u(4)
    readonly property real maxH: { let m = 0; for (let b = 0; b < blocks.length; ++b) { let h = 0; for (let i = 0; i < blocks[b].length; ++i) h += rowH(blocks[b][i]); m = Math.max(m, h) } return m }
    function xs(v) { return geo.plotX + Math.max(0, Math.min(dMax, v)) / dMax * geo.plotW }

    implicitHeight: headH + maxH + roles.u(4)
    height: implicitHeight

    // the column heads of every block: ONE atom, the list's head
    Item {
        id: headRow
        objectName: "sdDotHead"
        property bool atom: true
        property bool keepNext: true
        visible: dk.showHead
        width: dk.width; height: dk.headH
        Repeater {
            model: dk.roles ? dk.blocks.length : 0
            delegate: Item {
                required property int index
                x: index * (dk.lay.bw + dk.lay.g); width: dk.lay.bw; height: dk.headH
                Text { visible: dk.geo.tierCol; text: "session"; font: dk.roles.fLabel; color: dk.roles.ink3 }
                Text { x: dk.geo.nameX; width: dk.geo.nameW; horizontalAlignment: Text.AlignRight; text: "condition"; font: dk.roles.fLabel; color: dk.roles.ink3 }
                Text { x: dk.geo.plotX; width: dk.xs(2) - dk.geo.plotX; horizontalAlignment: Text.AlignHCenter; text: dk.geo.rich ? "corridor" : ""; font: dk.roles.fLabel; color: dk.roles.ink3 }
                Text { x: dk.xs(3) + dk.roles.u(3); text: (dk.geo.rich ? "fault line" : "fault") + " →"; font: dk.roles.fLabel; color: dk.roles.warn }
                Text { x: dk.geo.valueX; text: "this shot"; font: dk.roles.fLabel; color: dk.roles.ink3 }
            }
        }
    }
    Repeater {
        model: dk.roles ? (dk.blocks) : []
        delegate: Item {
            id: blk
            required property var modelData
            required property int index
            readonly property var rs: modelData
            readonly property var ys: { const o = []; let y = 0; for (let i = 0; i < rs.length; ++i) { o.push(y); y += dk.rowH(rs[i]) } o.push(y); return o }
            x: index * (dk.lay.bw + dk.lay.g)
            width: dk.lay.bw; height: dk.height
            // corridor wash and fault line
            Rectangle { x: dk.geo.plotX; y: dk.headH - 2; width: dk.xs(2) - dk.geo.plotX; height: blk.ys[blk.rs.length] + 4; color: dk.roles.wash }
            Rectangle { x: dk.xs(3); y: dk.headH - 2; width: dk.roles.hw; height: blk.ys[blk.rs.length] + 4; color: dk.roles.fault }
            Repeater {
                model: dk.roles ? (blk.rs) : []
                delegate: Item {
                    id: row
                    objectName: "sdDotRow"
                    required property var modelData
                    required property int index
                    readonly property var r: modelData
                    readonly property string conditionId: r.id
                    readonly property string stateKind: r.stateKind || ""
                    readonly property string tierTag: r.tierTag || ""
                    readonly property bool hot: dk.brush && dk.brush.hoveredCondition === r.id
                    readonly property bool open: dk.brush && dk.brush.pickedCondition === r.id
                    readonly property bool folded: !dk.geo.tierCol && r.tier !== ""
                    property bool atom: true
                    y: dk.headH + blk.ys[index]
                    width: blk.width; height: blk.ys[index + 1] - blk.ys[index]
                    opacity: dk.brush ? dk.brush.conditionOpacity(r.id, dk.roles.dim) : 1
                    Behavior on opacity { NumberAnimation { duration: dk.roles.normal } }
                    PpPulseWash { anchors.fill: parent; roles: dk.roles; brush: dk.brush; conditionId: row.conditionId }
                    Rectangle { anchors.fill: parent; visible: row.hot; color: dk.roles.rowHover }
                    Rectangle { visible: index > 0 && row.r.rank % 5 === 0; width: parent.width; height: dk.roles.hw; color: dk.roles.grid; opacity: dk.roles.gridOpacity }
                    Text { visible: dk.geo.tierCol; y: (dk.pitch - height) / 2; text: row.r.tier; font: dk.roles.fLabel; color: dk.roles.ink3 }
                    Text {
                        x: dk.geo.nameX; width: dk.geo.nameW
                        y: (dk.pitch - fmSmall.height) / 2
                        horizontalAlignment: Text.AlignRight
                        wrapMode: dk.geo.rich && dk.geo.tierCol ? Text.NoWrap : Text.WordWrap
                        textFormat: row.folded ? Text.StyledText : Text.PlainText
                        text: row.folded ? "<font color=\"" + dk.roles.hex(dk.roles.ink3) + "\">" + dk.roles.esc(row.r.tier) + " · </font>" + dk.roles.esc(row.r.name) : row.r.name
                        font: row.open || row.hot ? dk.roles.fSmallM : dk.roles.fSmall
                        color: row.r.fired || row.hot ? dk.roles.ink : dk.roles.ink2
                    }
                    Rectangle {
                        visible: row.r.fired
                        x: dk.xs(2); width: dk.xs(row.r.x) - dk.xs(2); height: dk.roles.hw
                        y: dk.pitch / 2
                        color: Qt.alpha(dk.roles.warn, dk.roles.r.stemAlpha)
                    }
                    PpMark {
                        visible: row.r.known
                        roles: dk.roles
                        fired: row.r.fired
                        cx: dk.xs(row.r.x); cy: dk.pitch / 2
                    }
                    Text {
                        visible: row.r.other
                        text: "‹"; color: dk.roles.ink3; font: dk.roles.fSmall
                        x: dk.geo.plotX - width - dk.roles.u(2); y: (dk.pitch - height) / 2
                    }
                    Text {
                        x: dk.geo.valueX; y: (dk.pitch - height) / 2
                        text: row.r.value; font: dk.roles.fFigS
                        color: row.r.fired ? dk.roles.warn : dk.roles.ink3
                    }
                    MouseArea {
                        enabled: dk.interactive; visible: enabled
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onContainsMouseChanged: { if (dk.brush) dk.brush.hoverCondition(row.r.id, containsMouse); dk.conditionHovered(containsMouse ? row.r.id : "") }
                        onClicked: dk.conditionActivated(row.r.id)
                    }
                }
            }
        }
    }
}
