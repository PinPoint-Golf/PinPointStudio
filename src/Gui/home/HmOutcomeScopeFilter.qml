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

// Which shots Home's tabs read, by how they came off: All shots, Good shots, or one miss. One row of chips:
// All shots, then the controller's choices, Good first and the misses most common first.
//
//   options     [{ value, label, count }] (workOns.outcomeScopes, without All)
//   selected    "all", "good" or a miss's value ("miss:slice")
//
// objectNames: outcomeScopeFilter (this), outcomeScopeChips.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "outcomeScopeFilter"

    property var    options:  []
    property string selected: "all"
    signal scopeSelected(string key)

    PpChipGroup {
        objectName: "outcomeScopeChips"
        width:    root.width
        wrap:     true
        style:    "filter"
        options:  [{ label: qsTr("All shots"), value: "all" }].concat(root.options.map(o => ({ label: o.label, value: o.value })))
        selected: root.selected
        onSelectionChanged: (value) => root.scopeSelected(value)
    }
}
