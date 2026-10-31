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

// STUB — the not-measured mark (D5): a short flat ink3 bar standing on the foot of a figure or strip, so a
// shot that could not be measured is present and never a gap. ONE drawing for every place that sets one
// down: PpRunChart, PpSparkline, PpSmallMultiple, PpChRunBox, PpShotStrip and the page key's swatch.
// Its height is the typeset's stubH of the mark height it stands in for (`size`); its width a share of the
// shot pitch, held between minW and maxW (no pitch: maxW). The caller places it (x / y or anchors) and
// names it (objectName: sdRunStub, sdSparkStub, sdTileStub, sdShotStub — tests count them).
Rectangle {
    property var roles: null
    property real size: 12               // the full mark height stubH is a share of, px
    property real pitch: Infinity        // the shot pitch, px
    property real share: 0.5             // of the pitch
    property real minW: 2
    property real maxW: roles ? roles.u(4) : 4
    width: Math.max(minW, Math.min(maxW, pitch * share))
    height: roles ? roles.stubH(size) : 0
    color: roles ? roles.ink3 : "transparent"
}
