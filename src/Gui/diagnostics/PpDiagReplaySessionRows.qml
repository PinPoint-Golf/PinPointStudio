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

// REPLAYSESSIONROWS — Replay's Session figure: one PpSparkRow per pattern (name · shot strip · this shot's
// value · sparkline), under ONE quiet column head that names what each column is. The rows flow into as
// many columns as fit at 40 em each, at most two, filled down then across so the rank order still reads
// top to bottom. The head is the lib's PpSparkHead, which shares PpSparkRow's geometry (roles.sparkGeo),
// so it follows any session length (D7).
Item {
    id: sr
    property QtObject roles: null
    property var brush: null
    property var cards: []
    property var readout: null
    property bool interactive: true
    property bool conditionsInteractive: true    // false on the cast: names take no click (D4); shots still pick
    property int maxCols: 2
    signal conditionActivated(string id)
    signal shotActivated(string swingDir, int shotId)

    readonly property var list: cards || []
    readonly property real g: roles.gutter(width) * 2
    readonly property int cols: Math.max(1, Math.min(maxCols, roles.columns(width, 40, g), list.length))
    readonly property real cw: (width - (cols - 1) * g) / cols
    readonly property int per: Math.ceil(list.length / cols)
    readonly property var slices: { const o = []; for (let c = 0; c < cols; ++c) o.push(list.slice(c * per, (c + 1) * per)); return o }
    readonly property int nShots: readout ? readout.shotCount : (list.length && list[0].ticks ? list[0].ticks.length : 0)

    implicitHeight: row.implicitHeight
    height: implicitHeight

    Row {
        id: row
        spacing: sr.g
        Repeater {
            model: sr.roles ? sr.slices : []
            delegate: Column {
                id: colm
                required property var modelData
                width: sr.cw
                // ── the column head: the lib's PpSparkHead, from the same roles.sparkGeo() the rows use ──
                PpSparkHead { width: colm.width; roles: sr.roles; readout: sr.readout; shots: sr.nShots }
                Repeater {
                    model: colm.modelData
                    delegate: PpSparkRow {
                        required property var modelData
                        width: colm.width
                        roles: sr.roles; brush: sr.brush; card: modelData; interactive: sr.interactive
                        conditionsInteractive: sr.conditionsInteractive
                        onConditionActivated: id => sr.conditionActivated(id)
                        onShotActivated: (d, s) => sr.shotActivated(d, s)
                    }
                }
            }
        }
    }
}
