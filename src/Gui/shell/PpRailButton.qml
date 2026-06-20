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

Item {
    id: root

    property string iconText:  ""
    property string labelText: ""
    property bool   isActive:  false
    property bool   isMuted:   false
    // When the rail is locked, enabled (non-muted) buttons tint their glyph and
    // label with the attention colour to draw the eye to what's still clickable.
    property bool   attention: false

    readonly property bool _attn: attention && !isMuted

    signal clicked()

    implicitWidth:  Theme.sp(60)
    implicitHeight: buttonCol.implicitHeight

    Column {
        id: buttonCol
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.sp(3)

        Rectangle {
            id: bg
            width:  Theme.sp(60)
            height: Theme.sp(60)
            radius: Theme.radius

            // Hover fill only (fast). The active treatment is a separate layer
            // below so selection can cross-fade independently of hover.
            color: (!root.isMuted && !root.isActive && mouseArea.containsMouse)
                       ? Theme.colorBg3 : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            // Active treatment — fill (+ Instrument border), cross-fading in/out
            // at durationNormal so the selection glides between rail buttons
            // rather than snapping. Synced with the page cross-fade in Main.qml.
            // (Editorial / Vector use the accent bar below instead of a border.)
            Rectangle {
                anchors.fill: parent
                radius:       parent.radius
                color: (Theme.aesthetic === "editorial" || Theme.aesthetic === "vector")
                           ? Theme.colorAccentLight : Theme.colorSurface
                border.width: (Theme.aesthetic === "editorial" || Theme.aesthetic === "vector") ? 0 : 1
                border.color: Theme.colorBorderMid
                opacity:      root.isActive ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
                }
            }

            // Editorial / Vector active: 2px left-edge accent bar (cross-fades too)
            Rectangle {
                x: 0
                y: 0
                width:   2
                height:  parent.height
                color:   Theme.colorAccent
                opacity: (root.isActive && (Theme.aesthetic === "editorial" || Theme.aesthetic === "vector")) ? 1 : 0
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
                }
            }

            Text {
                anchors.centerIn: parent
                text:            root.iconText
                // Per-glyph compensation so all rail icons share a common
                // visual size despite differing ink heights (platform-aware,
                // see Theme.symbolScale).
                font.pixelSize:  Math.round(Theme.sp(21) * Theme.symbolScale(root.iconText))
                font.family:     Theme.fontSymbol
                color: {
                    if (root.isActive) return root._attn ? Theme.colorAttention : Theme.colorAccent
                    if (!root.isMuted && mouseArea.containsMouse) return Theme.colorText2
                    return root._attn ? Theme.colorAttention : Theme.colorText3
                }
                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast }
                }
                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast }
                }
            }
        }

        Text {
            id: labelItem
            width:                  Theme.sp(60)
            horizontalAlignment:    Text.AlignHCenter
            text:                   root.labelText.toUpperCase()
            font.family:            Theme.fontBody
            font.pixelSize:         Theme.sp(8)
            font.letterSpacing:     Theme.trackingMicro
            color: {
                if (root.isActive) return root._attn ? Theme.colorAttention : Theme.colorAccent
                if (!root.isMuted && mouseArea.containsMouse) return Theme.colorText2
                return root._attn ? Theme.colorAttention : Theme.colorText3
            }
            Behavior on color {
                ColorAnimation { duration: Theme.durationFast }
            }
        }
    }

    // Layer 1: shared hover-grow / press-dip motion (matches every other
    // clickable surface app-wide — the rail had missed this rollout). Drives
    // root.scale; cursor + muted gating come from `enabled`. Keeps the
    // `mouseArea` id so the glyph/label hover-colour bindings still resolve.
    PpPressable {
        id: mouseArea
        enabled:   !root.isMuted
        onClicked: root.clicked()
    }
}
