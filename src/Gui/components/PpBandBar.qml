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

// A band bar: counts by category as one stacked horizontal bar, each category's share of the
// total in its tone, 1 px apart, on a Theme.bandBarH track. An area row passes its habit counts:
//
//   segments: [{ count: habits,     tone: Theme.bandTone("habit") },
//              { count: occasional, tone: Theme.bandTone("occasional") },
//              { count: clean,      tone: Theme.bandTone("clean") },
//              { count: unclear,    tone: Theme.bandTone("unclear") }]
//
// Segments are drawn in the order given, a zero (or missing, or negative) count drawing nothing.
// A count that is there is never rounded away: every drawn segment is at least `minSegment` wide,
// and the rest of the width is shared by count. With nothing to count the bar is a plain track.
// The ends are rounded; the joins are square.
//
// The bar says how many in proportion only: the caller prints the count beside it ("3 habits"),
// so colour is never the only channel.

import QtQuick
import PinPointStudio

Item {
    id: bar

    property var segments: []
    property int gap:        1
    property int minSegment: Theme.sp(2)

    implicitWidth:  Theme.sp(160)
    implicitHeight: Theme.sp(Theme.bandBarH)

    readonly property int total: {
        let n = 0
        for (const s of bar.segments || []) n += Math.max(0, Number(s.count) || 0)
        return n
    }
    // The drawn segments: [{ x, width, tone }].
    readonly property var pieces: {
        const drawn = []
        for (const s of bar.segments || []) {
            const c = Math.max(0, Number(s.count) || 0)
            if (c > 0) drawn.push({ count: c, tone: s.tone !== undefined ? s.tone : Theme.colorMeterUnlit })
        }
        const n = drawn.length
        if (n === 0 || bar.width <= 0) return []
        const room  = Math.max(0, bar.width - bar.gap * (n - 1))
        const floor = Math.min(bar.minSegment, Math.floor(room / n))
        const share = Math.max(0, room - floor * n)
        const out = []
        let before = 0, x = 0
        for (let i = 0; i < n; ++i) {
            const after = before + drawn[i].count
            // Cumulative rounding, so the pieces always add up to the room.
            const w = floor + Math.round(share * after / bar.total) - Math.round(share * before / bar.total)
            out.push({ x: x, width: w, tone: drawn[i].tone })
            x += w + bar.gap
            before = after
        }
        return out
    }

    readonly property real r: height / 2

    Rectangle {     // the track: what an empty bar is
        visible: bar.pieces.length === 0
        anchors.fill: parent
        radius: bar.r
        color:  Theme.colorMeterUnlit
    }
    Repeater {
        model: bar.pieces
        Rectangle {
            required property var modelData
            required property int index
            readonly property bool first: index === 0
            readonly property bool last:  index === bar.pieces.length - 1
            x: modelData.x
            width: modelData.width; height: bar.height
            topLeftRadius:     first ? bar.r : 0
            bottomLeftRadius:  first ? bar.r : 0
            topRightRadius:    last  ? bar.r : 0
            bottomRightRadius: last  ? bar.r : 0
            color: modelData.tone
        }
    }
}
