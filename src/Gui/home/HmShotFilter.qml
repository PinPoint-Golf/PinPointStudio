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

// Which shots Home's tabs read: one line of four quiet pickers, then Clear, then the shots it reads in words
// (docs/design/shot_outcomes_design.md §7; the plan's option A):
//
//   CLUB [Mid irons ▾]  STRIKE [Any ▾]  DIRECTION [Pull ▾]  FLIGHT [Any ▾]   Clear
//   Your pulled mid-iron shots: 11 over 5 sessions, against 31 others. Your swing was measured on 3 of them.
//
// It wraps onto a second line only when the width cannot hold it. A shot is read when it matches every choice.
//
//   clubScopes   [{ value, group, label, clubs: [{ value, label }] }] (workOns.clubScopes); the Club list is
//                All clubs, then each group followed by its clubs (a group of one club lists only the group)
//   clubScope    "all", a group's value or a club's value
//   options      { strike, direction, flight }: each [{ value, label, count }], "any" first (workOns.shotFilterOptions).
//                A row's count is what that choice would leave with the other choices kept, in the row's words
//                ("Pull  ·  11"); the closed field says only the choice. A count of 0 is listed, muted and inert.
//   shotFilter   "all" or "strike:<v>|direction:<v>|flight:<v>", each part "any" or a value (workOns.shotFilter)
//   line         workOns.shotFilterLine, "" under all
//   clubOnly     the Club picker alone, with groups only, for a page that already lists a row per club (Tendencies)
//
// objectNames: shotFilter (this), shotFilterClub, shotFilterStrike, shotFilterDirection, shotFilterFlight (the
// PpComboBoxes), shotFilterClear, shotFilterLine.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "shotFilter"

    property var    clubScopes: []
    property string clubScope:  "all"
    property var    options:    ({})
    property string shotFilter: "all"
    property string line:       ""
    property bool   clubOnly:   false

    signal clubScopeSelected(string key)
    signal shotFilterSelected(string key)

    readonly property var factorKeys: ["strike", "direction", "flight"]

    // The group the club choice sits in, or null under All clubs.
    readonly property var group: {
        for (const g of (root.clubScopes || []))
            if (g.value === root.clubScope || (g.clubs || []).some(c => c.value === root.clubScope)) return g
        return null
    }
    // With one group of one club, All clubs and that club are the same shots.
    readonly property var  groups: root.clubScopes || []
    readonly property bool hasClubChoice: root.groups.length > 1
                                          || (root.groups.length === 1 && (root.groups[0].clubs || []).length > 1)

    // The Club list: All clubs, each group, and under a group of more than one club its clubs (not under clubOnly).
    readonly property var clubList: {
        const out = [{ value: "all", label: qsTr("All clubs") }]
        for (const g of root.groups) {
            out.push({ value: g.value, label: g.label })
            const clubs = g.clubs || []
            if (!root.clubOnly && clubs.length > 1)
                for (const c of clubs) out.push({ value: c.value, label: c.label })
        }
        return out
    }
    // What the Club picker shows: under clubOnly a chosen club shows its group.
    readonly property string clubShown: root.clubOnly && root.group && root.clubScope !== root.group.value
                                        ? root.group.value : root.clubScope

    // The factor choices, each "any" or a value.
    function parse(key) {
        const out = { strike: "any", direction: "any", flight: "any" }
        if (!key || key === "all") return out
        for (const part of String(key).split("|")) {
            const i = part.indexOf(":")
            if (i > 0 && out[part.slice(0, i)] !== undefined) out[part.slice(0, i)] = part.slice(i + 1) || "any"
        }
        return out
    }
    function compose(m) {
        if (m.strike === "any" && m.direction === "any" && m.flight === "any") return "all"
        return "strike:" + m.strike + "|direction:" + m.direction + "|flight:" + m.flight
    }
    readonly property var chosen: root.parse(root.shotFilter)
    function factorOptions(f) { return root.options && root.options[f] ? root.options[f] : [] }
    // A picker with Any alone is a control whose one choice is the state it is already in.
    function factorShown(f) { return !root.clubOnly && (root.factorOptions(f).length > 1 || root.chosen[f] !== "any") }
    function pick(f, value) {
        const m = Object.assign({}, root.chosen)
        m[f] = String(value)
        root.shotFilterSelected(root.compose(m))
    }

    readonly property bool anyFactorShown: root.factorKeys.some(f => root.factorShown(f))
    readonly property bool shown: root.hasClubChoice || root.anyFactorShown
    readonly property bool anyChosen: root.clubScope !== "all" || (!root.clubOnly && root.shotFilter !== "all")
    function clear() {
        if (root.clubScope !== "all") root.clubScopeSelected("all")
        if (!root.clubOnly && root.shotFilter !== "all") root.shotFilterSelected("all")
    }

    visible: root.shown
    spacing: Theme.gap(6)

    // One label and its picker. options [{ value, label, count? }]; counts only where `counted`.
    component Picker: Row {
        id: picker
        property string label:   ""
        property string boxName: ""
        property var    options: []
        property string value:   ""
        property bool   counted: false
        property real   boxWidth: Theme.sp(140)
        signal picked(string value)

        readonly property int index: picker.options.findIndex(o => o.value === picker.value)
        spacing: Theme.gap(8)

        PpMicro {
            anchors.verticalCenter: parent.verticalCenter
            text: picker.label
        }
        PpComboBox {
            id: box
            objectName: picker.boxName
            anchors.verticalCenter: parent.verticalCenter
            width: picker.boxWidth
            model: picker.options.map(o => picker.counted && o.value !== "any" && o.count !== undefined
                                           ? o.label + "  ·  " + o.count : o.label)
            currentIndex: picker.index
            displayText:  picker.index >= 0 ? picker.options[picker.index].label
                          : picker.options.length > 0 ? picker.options[0].label : ""
            // A choice that would leave nothing is listed, muted and inert; Any and the current choice never are.
            itemEnabledFn: function (i) {
                const o = picker.options[i]
                return !picker.counted || !o || o.value === "any" || o.value === picker.value || (o.count || 0) > 0
            }
            onActivated: function (i) {
                const o = picker.options[i]
                // The control set currentIndex itself; follow the choice again.
                box.currentIndex = Qt.binding(function () { return picker.index })
                if (o && o.value !== picker.value) picker.picked(o.value)
            }
        }
    }

    Flow {
        width:   root.width
        spacing: Theme.gap(14)

        Picker {
            visible:  root.hasClubChoice
            label:    Theme.caps(qsTr("Club"))
            boxName:  "shotFilterClub"
            boxWidth: Theme.sp(150)
            options:  root.clubList
            value:    root.clubShown
            onPicked: (v) => root.clubScopeSelected(v)
        }
        Picker {
            visible:  root.factorShown("strike")
            label:    Theme.caps(qsTr("Strike"))
            boxName:  "shotFilterStrike"
            counted:  true
            options:  root.factorOptions("strike")
            value:    root.chosen.strike
            onPicked: (v) => root.pick("strike", v)
        }
        Picker {
            visible:  root.factorShown("direction")
            label:    Theme.caps(qsTr("Direction"))
            boxName:  "shotFilterDirection"
            boxWidth: Theme.sp(150)
            counted:  true
            options:  root.factorOptions("direction")
            value:    root.chosen.direction
            onPicked: (v) => root.pick("direction", v)
        }
        Picker {
            visible:  root.factorShown("flight")
            label:    Theme.caps(qsTr("Flight"))
            boxName:  "shotFilterFlight"
            boxWidth: Theme.sp(120)
            counted:  true
            options:  root.factorOptions("flight")
            value:    root.chosen.flight
            onPicked: (v) => root.pick("flight", v)
        }
        Item {                                  // as tall as a picker, so the link sits on the fields' middle
            visible: root.anyChosen
            width:   clearLink.implicitWidth
            height:  Theme.sp(34)
            PpLink {
                id: clearLink
                objectName: "shotFilterClear"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear")
                onClicked: root.clear()
            }
        }
    }

    PpCaption {
        objectName: "shotFilterLine"
        width:    root.width
        visible:  !root.clubOnly && text !== ""
        elide:    Text.ElideNone
        wrapMode: Text.WordWrap
        text:     root.line
    }
}
