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
import PinPointStudio

// PULSEWASH — the after-shot pulse (round 4, D1): ONE soft ink wash over a row (or a tile, a mark's
// cell) whose condition changed state on the shot just surfaced. It rises in Theme.durationFast and
// fades over Theme.durationSlow, staggered by the row's place in brush.pulseIds (the focused condition
// first). Inert under reduceMotion, in print, and when the condition is not in the pulse.
// brush.pulseHold (renders only) holds the wash at full so a static grab can show it.
// Put it as the FIRST child of a row so it sits under the text: PpPulseWash { anchors.fill: parent; … }.
Rectangle {
    id: pw
    objectName: "sdPulseWash"
    property QtObject roles: null
    property var brush: null
    property string conditionId: ""
    property real level: 0                       // animated 0 → 1 → 0
    readonly property int order: brush && conditionId !== "" ? brush.pulseOrder(conditionId) : -1
    readonly property bool held: !!brush && brush.pulseHold && order >= 0
    readonly property bool pulsing: anim.running || held
    color: roles ? Qt.alpha(roles.ink, Theme.dark ? 0.11 : 0.08) : "transparent"
    opacity: !roles || roles.isPrint ? 0 : held ? 1 : level
    visible: opacity > 0
    function fire() {
        anim.stop(); level = 0
        if (!roles || roles.isPrint || Theme.reduceMotion || order < 0 || held) return
        anim.delay = order * (brush ? brush.pulseStagger : 70)
        anim.start()
    }
    Connections {
        target: pw.brush
        ignoreUnknownSignals: true
        function onPulseTokenChanged() { pw.fire() }
    }
    SequentialAnimation {
        id: anim
        property int delay: 0
        PauseAnimation { duration: anim.delay }
        NumberAnimation { target: pw; property: "level"; to: 1; duration: Math.max(1, Theme.durationFast) }
        NumberAnimation { target: pw; property: "level"; to: 0; duration: Math.max(1, Theme.durationSlow); easing.type: Easing.OutCubic }
    }
}
