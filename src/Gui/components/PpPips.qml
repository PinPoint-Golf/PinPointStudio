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

// A row of session pips, oldest first. `marks` are the summary's seen/unseen bools; `ticks` are
// PpTickRun's { state } and win when there are any. `maxPips` keeps the latest N of them (0 = all),
// so a caller no longer slices its own; a card that lines its rows up still sizes its pip column to
// the widest row. Size and gap are the theme's pipSize / pipGap.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Row {
    id: pips

    property var   marks:     []
    property var   ticks:     []
    property color tone:      Theme.colorText3
    property color cleanTone: Theme.colorGood
    property int   size:      Theme.sp(Theme.pipSize)
    property int   gap:       Theme.sp(Theme.pipGap)
    property int   maxPips:   0     // 0 = every session; otherwise the latest N
    readonly property bool useTicks: pips.ticks !== null && pips.ticks !== undefined && pips.ticks.length > 0
    // What is drawn: the winning list, cut to the latest maxPips.
    readonly property var shown: {
        const all = (pips.useTicks ? pips.ticks : pips.marks) || []
        return pips.maxPips > 0 && all.length > pips.maxPips ? all.slice(all.length - pips.maxPips) : all
    }

    spacing: pips.gap
    Repeater {
        model: pips.shown
        PpPip {
            required property var modelData
            objectName: "pip"
            kind:      pips.useTicks ? modelData.state : (modelData ? "seen" : "unseen")
            tone:      pips.tone
            cleanTone: pips.cleanTone
            size:      pips.size
        }
    }
}
