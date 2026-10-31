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

// BREAKSLASH — the conventional pair of slashes that marks a BROKEN value axis (fault-forward: an empty end
// of the scale collapsed into a thin band), drawn once for every figure that breaks its scale: PpZonePlot
// (both ends of a panel), PpSparkline (the left end only) and PpSmallMultiple. At each gap's centre line
// in `ys`, a double slash `k` px wide at the left end of the item and, unless `leftOnly`, at the right end;
// ink3 hairlines. The caller sizes it (normally anchors.fill) and says when it shows. FIGURE-INTERNAL.
Canvas {
    id: bs
    property var roles: null
    property var ys: []                  // the breaks' centre lines, px in this item
    property real k: 3                   // one slash: k wide, 2k tall
    property bool leftOnly: false
    readonly property var deps: [ys, k, leftOnly, width, height, roles ? roles.ink3 : "transparent", roles ? roles.hw : 1]
    onDepsChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d"); ctx.reset()
        if (!roles) return
        ctx.strokeStyle = roles.ink3; ctx.lineWidth = roles.hw
        const ends = leftOnly ? [0] : [0, width - 2 * k]
        for (const yc of (ys || []))
            for (const x0 of ends)
                for (const dx of [0, k]) { ctx.beginPath(); ctx.moveTo(x0 + dx, yc + k); ctx.lineTo(x0 + dx + k, yc - k); ctx.stroke() }
    }
}
