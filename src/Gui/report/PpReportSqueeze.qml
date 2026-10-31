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

// THE TIGHT SQUEEZE of a report section (the work-on pages, Why this order, the swing grid): a section that would run on
// by less than a fifth of a page is set once more, tighter, rather than strand its last lines on a page of their own.
// `level` steps up (to `max`) when `target`'s laid-out height is over `pageH` by less than a fifth — a ONE-SHOT check
// 40 ms after the height last changed (so no binding loop), which only ever tightens: content that needs a second page
// still flows onto it ("Work-on 3, continued"). The section sets `level = 0` when its map changes.
//   PpReportSqueeze { id: fit; target: op; pageH: op.pageH }   …   function gap(n) { return fit.level > 0 ? … : n }
Timer {
    id: sq
    property Item target: null
    property real pageH: 950
    property int max: 1                                 // the work-on page takes a second, last step (2)
    property int level: 0
    readonly property real targetH: target ? target.height : 0
    onTargetHChanged: restart()
    interval: 40
    onTriggered: if (target && level < max && target.height > pageH + 1 && target.height <= pageH * 1.2) level++
}
