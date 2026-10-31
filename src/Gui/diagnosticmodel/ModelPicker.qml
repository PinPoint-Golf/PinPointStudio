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

pragma ComponentBehavior: Bound

import QtQuick
import PinPointStudio

// Type-ahead for every link. Click, type three characters, Enter — never a picker dialog and never
// a scroll through 140 rows.
//
// The candidate list is PRE-FILTERED to legal targets in C++ (acyclicity, axis and
// corroborates-shadowing rules), so an illegal edit cannot be constructed. A control that offers a
// choice it will then reject is a worse control than one that does not offer it, and it also teaches
// the author that the rules are arbitrary.
//
// The type-ahead itself is ModelTypeAhead, which ModelCausePicker is also built on; this is that
// with nothing to create and nothing below the list.
ModelTypeAhead {
    id: root

    // candidateSource: a function (searchText) -> [{ id, label, detail, tone }], asked again on every
    // keystroke so the filtering stays in C++. `title` is the caps line above the field.

    signal picked(string id)

    width: Theme.sp(360)

    placeholderText:  qsTr("Type to narrow…")
    countText:        qsTr("%n legal target(s)", "", root._candidates.length)
    borderColor:      Theme.colorBorderStrong
    countBottomGap:   Theme.gap(2)
    listMinHeight:    Theme.sp(32)
    listBottomMargin: Theme.gap(8)
    detailWidthFraction: 0.4

    onChosen: (row) => root.picked(row.id)
}
