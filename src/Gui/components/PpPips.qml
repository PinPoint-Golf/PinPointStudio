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
// PpTickRun's { state } and win when there are any. Callers keep their own latest-N slicing, so
// every row on a card can share one width.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Row {
    id: pips

    property var   marks:     []
    property var   ticks:     []
    property color tone:      Theme.colorText3
    property color cleanTone: Theme.colorGood
    property int   size:      Theme.sp(6)
    property int   gap:       Theme.sp(4)
    readonly property bool useTicks: pips.ticks !== null && pips.ticks !== undefined && pips.ticks.length > 0

    spacing: pips.gap
    Repeater {
        model: pips.useTicks ? pips.ticks : pips.marks
        PpPip {
            required property var modelData
            kind:      pips.useTicks ? modelData.state : (modelData ? "seen" : "unseen")
            tone:      pips.tone
            cleanTone: pips.cleanTone
            size:      pips.size
        }
    }
}
