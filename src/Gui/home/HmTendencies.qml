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

// TENDENCIES — the usual miss with each club, the body of Home's Tendencies tab (HmHomeTabs;
// docs/design/shot_outcomes_design.md §7.2). One table card, a row per club and swing length, optionally of one group
// (HmTendencyRow):
//
//   chevron · Club · Usual distance · Usual miss · Your shots · Seen with · Trend · Work-on
//
// One row open at a time. Held here and not on the row: each publish of the tendencies rebuilds them.
//
// Paint-only: every row is WorkOnsController.tendencyItems. Reading, nothing yet and a table with
// building rows are told apart in words; the counts are the tab bar's evidence line, never a row's.
//
// This card is the page's named item (homeTabTendencies), so the page Item in HmHomeTabs has no name.

import QtQuick
import PinPointStudio

PpCard {
    id: root
    objectName: "homeTabTendencies"

    // Passed in rather than reached for, so a harness can drive the component.
    property var    controller:  (typeof workOns !== "undefined") ? workOns : null
    property string athleteUuid: ""
    property string group: ""       // a clubGroup, or "" for every club

    signal practiseRequested(string conditionId, string label)

    readonly property var    tendencies: root.controller && root.controller.tendencyItems ? root.controller.tendencyItems : []
    readonly property bool   hasRows:    root.tendencies.length > 0
    readonly property var    shownRows:  root.group === "" ? root.tendencies : root.tendencies.filter(t => t.clubGroup === root.group)
    readonly property string note:       root.controller && root.controller.tendencyNote ? root.controller.tendencyNote : ""
    readonly property bool   reading:    !root.hasRows && root.controller !== null
                                         && (!!root.controller.tendenciesUpdating || !!root.controller.updating)

    property string openKey: ""
    onAthleteUuidChanged: root.openKey = ""
    onGroupChanged:       root.openKey = ""

    function caption(title) { return root.hasRows ? title : "" }

    width:  parent ? parent.width : 0
    table:  true
    columns: [{ title: "",                          width: Theme.sp(14) },     // the chevron's
              { title: caption(qsTr("Club")),           fill: 0.85 },
              { title: caption(qsTr("Usual distance")), fill: 1.0 },
              { title: caption(qsTr("Usual miss")),     fill: 0.9 },
              { title: caption(qsTr("Your shots")),     fill: 1.75 },
              { title: caption(qsTr("Seen with")),      fill: 1.5 },
              { title: caption(qsTr("Trend")),          width: Theme.sp(84) },
              { title: caption(qsTr("Work-on")),        width: Theme.sp(84) }]

    Repeater {
        model: root.shownRows
        HmTendencyRow {
            id: tendencyRow
            required property var modelData
            tendency:   tendencyRow.modelData
            controller: root.controller
            autoToggle: false
            expanded:   root.openKey !== "" && root.openKey === tendencyRow.key
            onToggled:  root.openKey = tendencyRow.expanded ? "" : tendencyRow.key
            onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
        }
    }

    // The first reading of a golfer's sessions, said once and quietly.
    PpTableRow {
        id: readingRow
        visible: root.reading
        hoverEnabled: false
        PpMicro {
            objectName: "tendenciesUpdating"
            width: readingRow.cellBox.width
            text:  qsTr("reading your sessions…")
            font.letterSpacing: Theme.trackingData
        }
    }

    // Nothing to read yet, or nothing that holds. Not said before the controller has an athlete.
    PpTableRow {
        id: emptyRow
        visible: root.shownRows.length === 0 && !root.reading && root.controller !== null
                 && (root.note !== "" || (root.controller.athleteDir || "") !== "")
        hoverEnabled: false
        height: Math.max(Theme.sp(Theme.rowH), emptyNote.implicitHeight + Theme.gap(18))
        PpCaption {
            id: emptyNote
            objectName: "tendenciesEmpty"
            width:    emptyRow.cellBox.width
            elide:    Text.ElideNone
            wrapMode: Text.WordWrap
            text:     root.hasRows ? qsTr("No tendencies with these clubs yet.")
                      : root.note !== "" ? root.note : qsTr("Nothing yet. This fills in as your sessions build up.")
        }
    }

    PpTableRow {
        id: footRow
        visible: root.hasRows
        hoverEnabled: false
        height: Math.max(Theme.sp(Theme.rowH), footNote.implicitHeight + Theme.gap(18))
        PpCardNote {
            id: footNote
            width: footRow.cellBox.width
            text:  qsTr("Part swings are detected from backswing length and club speed. Shots you marked as intended shapes are left out.")
        }
    }
}
