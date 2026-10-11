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

// SMALLMULTIPLES — the grid of PpSmallMultiple tiles: columns = min(4, ⌊w / roles.flows.tile em⌋), rows
// are atoms (print cuts between rows), a row shares one header height so the charts align.
Column {
    id: sm
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property bool conditionsInteractive: true    // false: the condition names take no click and show no pointer (a
                                                 // persistent cast picks shots, never opens a condition); hover still brushes
    property var cards: []
    property var readout: null
    property var source: null
    property int maxCols: 4
    property bool commonTop: false              // true: one window top for the whole grid; false: per row (round 4)
    readonly property var keyKinds: ["fired", "clean", "ring", "median", "fault", "corridor"]
    signal conditionActivated(string id)
    signal shotActivated(string swingDir, int shotId)
    signal shotHovered(int shotId)

    readonly property real gx: roles.gutter(width) * 1.4
    readonly property int cols: Math.max(1, Math.min(maxCols, roles.columns(width, roles.flows.tile, gx)))
    readonly property real tileW: (width - (cols - 1) * gx) / cols
    readonly property var rowsData: { const out = [], cs = cards || []; const k = Math.max(1, cols || 1); for (let i = 0; i < cs.length; i += k) out.push(cs.slice(i, i + k)); return out }
    FontMetrics { id: fmBody; font: sm.roles.fBody }
    property real gridMax: 6
    function lines(t, w) {
        if (fmBody.font.pixelSize <= 0) return 1     // reads the font, so the measure follows a theme change: advanceWidth() is a call, which a binding cannot track
        const words = String(t || "").split(" "), sp = fmBody.advanceWidth(" ")
        let n = 1, cur = 0
        for (let i = 0; i < words.length; ++i) { const ww = fmBody.advanceWidth(words[i]); if (cur === 0) cur = ww; else if (cur + sp + ww <= w - 3) cur += sp + ww; else { n++; cur = ww } }
        return n
    }
    spacing: roles.u(18)
    Repeater {
        model: sm.roles ? (sm.rowsData) : []
        delegate: Row {
            id: tr
            required property var modelData
            required property int index
            property bool atom: true
            readonly property real hdrH: {
                let n = 1
                for (let i = 0; i < modelData.length; ++i) n = Math.max(n, sm.lines(modelData[i].name, sm.tileW - sm.roles.u(144)))
                return Math.max(sm.roles.u(36), n * fmBody.lineSpacing + sm.roles.u(4))
            }
            spacing: sm.gx
            // the row's window top: what its highest shot needs (identically scaled across the row)
            readonly property real zTop: {
                let m = -2
                const n = tileRep.count
                for (let i = 0; i < n; ++i) { const t = tileRep.itemAt(i); if (t) m = Math.max(m, t.zMax) }
                if (sm.commonTop) m = Math.max(m, sm.gridMax)
                return Math.max(3.8, Math.min(6, m + 0.7))
            }
            Repeater {
                id: tileRep
                model: sm.roles ? (tr.modelData) : []
                delegate: PpSmallMultiple {
                    zTop: tr.zTop
                    required property var modelData
                    required property int index
                    labelBand: tr.index === 0 && index === 0
                    width: sm.tileW
                    roles: sm.roles; brush: sm.brush; medium: sm.medium; interactive: sm.interactive
                    conditionsInteractive: sm.conditionsInteractive
                    card: modelData; readout: sm.readout; source: sm.source
                    panelWidth: sm.width; hdrH: tr.hdrH
                    onConditionActivated: id => sm.conditionActivated(id)
                    onShotActivated: (d, s) => sm.shotActivated(d, s)
                    onShotHovered: s => sm.shotHovered(s)
                }
            }
        }
    }
}
