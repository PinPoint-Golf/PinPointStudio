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

// Which clubs Home's tabs read: All, one group or one club. Two rows of chips: All and the groups, then
// the clubs of the chosen group when it has more than one.
//
//   options     [{ value, group, label, clubs: [{ value, label }] }] (workOns.clubScopes)
//   selected    "all", a group's value or a club's value
//   groupsOnly  the top row alone, for a page that already lists a row per club
//
// objectNames: clubScopeFilter (this), clubScopeGroups, clubScopeClubs.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "clubScopeFilter"

    property var    options:  []
    property string selected: "all"
    property bool   groupsOnly: false
    signal scopeSelected(string key)

    // The group the selection sits in, or null under All.
    readonly property var group: {
        for (const g of root.options)
            if (g.value === root.selected || g.clubs.some(c => c.value === root.selected)) return g
        return null
    }
    // With one group of one club, All and that club are the same shots.
    readonly property bool hasChoice: root.options.length > 1
                                      || (root.options.length === 1 && root.options[0].clubs.length > 1)

    spacing: Theme.gap(6)

    PpChipGroup {
        objectName: "clubScopeGroups"
        width:    root.width
        wrap:     true
        style:    "filter"
        options:  [{ label: qsTr("All"), value: "all" }].concat(root.options.map(g => ({ label: g.label, value: g.value })))
        selected: root.group ? root.group.value : "all"
        onSelectionChanged: (value) => root.scopeSelected(value)
    }
    PpChipGroup {
        objectName: "clubScopeClubs"
        visible:  !root.groupsOnly && root.group !== null && root.group.clubs.length > 1
        x:        Theme.gap(12)
        width:    root.width - x
        wrap:     true
        style:    "filter"
        options:  root.group ? [{ label: qsTr("All %1").arg(root.group.label.toLowerCase()), value: root.group.value }]
                               .concat(root.group.clubs.map(c => ({ label: c.label, value: c.value }))) : []
        selected: root.selected
        onSelectionChanged: (value) => root.scopeSelected(value)
    }
}
