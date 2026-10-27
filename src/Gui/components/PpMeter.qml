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

// How often, as ten segments: a share of 0..1 lights round(share × 10) of them in the tone; the
// rest are Theme.colorMeterUnlit. The segment count carries the reading, so colour is never the
// only channel. Geometry is the theme's (meterSegW × meterSegH, meterSegGap apart, meterSegRadius
// corners) — a segment is part of the mark, so its gap scales with sp(), not with gap().

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Row {
    id: meter

    property real  share: 0
    property color tone: Theme.colorAccent
    readonly property int filled: Math.max(0, Math.min(10, Math.round(share * 10)))

    spacing: Theme.sp(Theme.meterSegGap)
    Repeater {
        model: 10
        Rectangle {
            required property int index
            objectName: "meterSegment"
            width:  Theme.sp(Theme.meterSegW)
            height: Theme.sp(Theme.meterSegH)
            radius: Math.min(height / 2, Theme.sp(Theme.meterSegRadius))
            color:  index < meter.filled ? meter.tone : Theme.colorMeterUnlit
        }
    }
}
