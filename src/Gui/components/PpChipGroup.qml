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

Row {
    id: root

    property var    options:  []
    property string selected: ""
    // Chip labels that render greyed and non-selectable. A click on one emits
    // blockedClicked() instead of selectionChanged(). Default [] = all selectable
    // (backward-compatible with existing callers).
    property var    disabledOptions: []

    signal selectionChanged(string value)
    signal blockedClicked(string value)

    spacing: Theme.sp(6)

    Repeater {
        model: root.options

        Rectangle {
            id: chip
            required property string modelData
            required property int    index

            readonly property bool _sel:      modelData === root.selected
            readonly property bool _disabled: root.disabledOptions.indexOf(modelData) >= 0

            height:  Theme.sp(28)
            width:   chipLabel.implicitWidth + Theme.sp(24)
            radius:  Theme.radius
            opacity: chip._disabled ? 0.4 : 1.0
            // Disabled: flat rest fill, no hover ramp. Selected: accent wash. Hover
            // (unselected): a faint bg fill that only ramps alpha (RGB-matched rest)
            // so there's no colour flash; the border eases to accent on hover — the
            // home-tile / pill language.
            color:   chip._disabled       ? Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                   : _sel                 ? Theme.colorAccentLight
                   : chipMa.containsMouse ? Theme.colorBg2
                   :                        Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
            border.width: 1
            border.color: chip._disabled       ? Theme.colorBorderStrong
                        : _sel                 ? Theme.colorAccent
                        : chipMa.containsMouse ? Theme.colorAccentMid
                        :                        Theme.colorBorderStrong
            Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            Text {
                id: chipLabel
                anchors.centerIn: parent
                text:           modelData
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                font.weight:    _sel ? Font.Normal : Theme.fontBodyWeight
                color:          chip._disabled ? Theme.colorText3
                              : _sel           ? Theme.colorAccent
                              :                  Theme.colorText2
            }

            PpPressable {
                id: chipMa
                // Disabled chips keep click handling (to report the block) but drop
                // the pointing-hand cursor and hover-grow.
                hoverScale:  chip._disabled ? 1.0 : 1.02
                cursorShape: chip._disabled ? Qt.ArrowCursor : Qt.PointingHandCursor
                onClicked:   chip._disabled ? root.blockedClicked(chip.modelData)
                                            : root.selectionChanged(chip.modelData)
            }
        }
    }
}
