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

// Drop-in replacement for a plain MouseArea that gives any clickable element the
// standard PinPoint hover-grow / press-dip motion — the same interaction language
// as the home tiles, shot cards and toolbar pills. It drives its PARENT's `scale`
// through an animated internal value, so the host item needs to declare nothing:
// just swap `MouseArea { ... }` for `PpPressable { ... }`.
//
//   Rectangle {
//       color: pressIt.containsMouse ? Theme.colorBg3 : Theme.colorBg2   // optional hover brighten
//       Text  { ... }
//       PpPressable { id: pressIt; onClicked: doThing() }
//   }
//
// Scale is a render transform (an Item's default transformOrigin is Center), so it
// never reflows neighbours or affects layout, and reduceMotion zeroes the duration.
// Use this ONLY for visually separated controls — a scale on a contiguous segmented
// control would break the shared seam, so brighten those instead. A filled/outline
// fill usually also wants a hover brighten; keep that on the host and read this
// element's `containsMouse` via its id.

import QtQuick
import PinPointStudio

MouseArea {
    id: root

    // Tunables — the card/pill defaults. Set hoverScale: 1.0 (or pressScale: 1.0)
    // to opt out of either half of the motion while keeping the click handling.
    property real hoverScale: 1.02
    property real pressScale: 0.97
    // Hold the grown state even without hover — e.g. while a popup this button owns
    // is open (mirrors the toolbar pills holding while their popup is up).
    property bool held:       false

    // Opt-in one-shot "tap" confirmation, layered on top of the hover/press scale.
    // Off by default so existing buttons are unchanged — turn it on for MOMENTARY
    // action buttons (back/forward nav, athlete selector) where there is no lasting
    // state change to confirm the click landed. The hold-press dip is tied to how
    // long the mouse is down; this fires a fixed quick recoil on the completed click.
    property bool confirmOnClick: false

    anchors.fill: parent
    hoverEnabled: true
    cursorShape:  enabled ? Qt.PointingHandCursor : Qt.ArrowCursor

    // Animated scale, mirrored onto the parent. A bound property + Behavior animates
    // every state change (binding re-evaluation included); the Binding pushes it to
    // the parent so the host item needs no scale/Behavior of its own.
    property real _scale: !enabled                ? 1.0
                        : pressed                 ? pressScale
                        : (containsMouse || held) ? hoverScale
                        :                           1.0
    Behavior on _scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic } }

    // One-shot confirmation factor — rests at 1.0, dips and recovers on click.
    // Multiplied into the scale below so it composes with the hover/press state
    // instead of fighting its binding. reduceMotion zeroes the durations.
    property real _pulse: 1.0
    SequentialAnimation {
        id: confirmAnim
        NumberAnimation { target: root; property: "_pulse"; to: 0.90
                          duration: Math.round(Theme.durationFast * 0.5); easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "_pulse"; to: 1.0
                          duration: Theme.durationFast; easing.type: Easing.OutCubic }
    }
    // Separate slot on the same signal, so the caller's inline onClicked still runs.
    Connections {
        target:  root
        enabled: root.confirmOnClick
        function onClicked() { confirmAnim.restart() }
    }

    Binding {
        target:   root.parent
        property: "scale"
        value:    root._scale * root._pulse
        when:     root.parent !== null
    }
}
