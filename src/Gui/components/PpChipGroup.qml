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

// A row of choice chips (PpChoiceChip) from a list, one picked. One line, sized to its chips; with
// `wrap` it takes the width it is given and wraps onto further lines (a Flow sized by a RowLayout would
// feed its own shrunken width back after a text-size change and stay wrapped, so wrapping is opt-in).
// `options` are words, or objects
// { label, value, count, tint, disabled } where the chip says one thing and stands for another (value
// defaults to the label). `style` is any PpChoiceChip style.
//
// `multiSelect` makes each chip a toggle: `selectedValues` lists the ones on, and selectionChanged()
// names the one tapped for the caller to add or drop.

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

Item {
    id: root

    property var    options:  []
    property string selected: ""
    property bool   multiSelect:    false
    property var    selectedValues: []
    property string style:    ""
    // Chip labels that render greyed and non-selectable. A click on one emits
    // blockedClicked() instead of selectionChanged(). Default [] = all selectable
    // (backward-compatible with existing callers).
    property var    disabledOptions: []

    signal selectionChanged(string value)
    signal blockedClicked(string value)

    property real   spacing:  Theme.gap(6)
    property bool   wrap:     false

    implicitWidth:  flow.implicitWidth
    implicitHeight: flow.implicitHeight

    Flow {
        id: flow
        width:   root.wrap ? root.width : undefined
        spacing: root.spacing

        Repeater {
            model: root.options

            PpChoiceChip {
                id: chip
                required property var modelData

                readonly property bool   _obj:  typeof modelData === "object" && modelData !== null
                readonly property string value: !_obj                       ? String(modelData)
                                              : modelData.value !== undefined ? String(modelData.value)
                                              :                               String(modelData.label)
                readonly property bool   _disabled: (_obj && modelData.disabled === true)
                                                    || root.disabledOptions.indexOf(value) >= 0

                label:    _obj ? String(modelData.label) : String(modelData)
                count:    _obj && modelData.count !== undefined ? String(modelData.count) : ""
                tint:     _obj && modelData.tint !== undefined ? modelData.tint : Theme.colorAccent
                style:    root.style
                selected: root.multiSelect ? root.selectedValues.indexOf(value) >= 0 : value === root.selected
                // Disabled chips keep click handling (to report the block) but drop
                // the pointing-hand cursor and hover-grow.
                dimmed:   _disabled
                blocked:  _disabled
                onClicked: chip._disabled ? root.blockedClicked(chip.value) : root.selectionChanged(chip.value)
            }
        }
    }
}
