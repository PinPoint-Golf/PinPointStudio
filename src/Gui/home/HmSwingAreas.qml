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

// YOUR SWING, as areas (docs/design/ux_refactor_away_from_4_modes.md §2.3, §5.2 D1): one table card,
// a row per area (HmAreaRow) rated by counting its habits, in the controller's display order
// (habits, then occasional, then the worst share; an area this rig cannot measure last).
//
//   chevron · Area · Habits · Trend · Biggest fault · Work-ons
//
// Paint-only: every row is WorkOnsController.areaItems. Before the first areas land it says it is
// reading; with no session to read it says "Nothing yet"; a recompute after that leaves the table
// standing and says nothing (HmSwingSummary's states).
//
// WHICH AREAS ARE OPEN is the golfer's, per athlete, in appSettings.homeViewByAthlete[uuid]:
//   expanded       [group…]  the open areas, any number
//   expandedSeen   bool      set by the first write; until then the weakest measurable area (the
//                            first measurable one, since areas arrive in display order) opens on
//                            first show. After it, a table the golfer closed stays closed.
// The map is read once per write and a modified copy written back (a QVariantMap property read is a
// deep copy); the entry's other keys (the tab, ScreenHome's) are kept. With no map on the settings
// object the state is this component's alone.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "swingAreas"

    // Passed in rather than reached for, so a harness can drive the component.
    property var    controller:  (typeof workOns !== "undefined") ? workOns : null
    property var    settings:    (typeof appSettings !== "undefined") ? appSettings : null
    property string athleteUuid: (typeof athleteController !== "undefined" && athleteController)
                                 ? athleteController.currentUuid : ""

    signal practiseRequested(string conditionId, string label)
    signal allInAreaRequested(string group)

    readonly property var  areas:      root.controller && root.controller.areaItems ? root.controller.areaItems : []
    readonly property bool ready:      root.areas.length > 0
    readonly property bool catchingUp: root.controller ? !!root.controller.updating : false

    // The open areas, as groups.
    property var openGroups: []
    property bool _seen: false
    property string _loadedFor: "\u0001"     // the uuid openGroups was read for; never a real one

    function isOpen(group) { return root.openGroups.indexOf(group) >= 0 }

    function _hasStore() {
        return root.settings && root.settings.homeViewByAthlete !== undefined && root.athleteUuid !== ""
    }
    function _write(groups) {
        root.openGroups = groups
        root._seen = true
        if (!root._hasStore()) return
        const map  = root.settings.homeViewByAthlete || {}       // read ONCE
        const next = Object.assign({}, map)
        const entry = Object.assign({}, next[root.athleteUuid] || {})
        entry.expanded     = groups.slice()
        entry.expandedSeen = true
        next[root.athleteUuid] = entry
        root.settings.homeViewByAthlete = next
    }
    function toggle(group) {
        const list = root.openGroups.slice()
        const i = list.indexOf(group)
        if (i >= 0) list.splice(i, 1)
        else        list.push(group)
        root._write(list)
    }
    function sync() {
        if (root._loadedFor !== root.athleteUuid) {
            root._loadedFor = root.athleteUuid
            let groups = [], seen = false
            if (root._hasStore()) {
                const map = root.settings.homeViewByAthlete || {}
                const e = map[root.athleteUuid]
                if (e) {
                    groups = e.expanded ? Array.from(e.expanded).map(String) : []
                    seen   = !!e.expandedSeen
                }
            }
            root.openGroups = groups
            root._seen = seen
        }
        // Not while the controller is catching up: the areas showing may still be the last athlete's.
        // root.areas, not root.ready: in onAreasChanged the `ready` binding may not have caught up.
        const areas = root.areas || []
        if (root._seen || areas.length === 0 || root.catchingUp) return
        for (const a of areas) {
            if (a.measurable) { root._write([String(a.group)]); return }
        }
    }
    Component.onCompleted:  root.sync()
    onAreasChanged:         root.sync()
    onAthleteUuidChanged:   root.sync()
    onCatchingUpChanged:    root.sync()
    onSettingsChanged:      { root._loadedFor = "\u0001"; root.sync() }

    width:   parent ? parent.width : 0
    spacing: 0

    // The first reading of a golfer's sessions, said once and quietly.
    Item {
        visible: root.catchingUp && !root.ready
        width:   root.width
        height:  visible ? Theme.sp(40) : 0
        PpMicro {
            objectName: "swingAreasUpdating"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("reading your sessions…")
            font.letterSpacing: Theme.trackingData
        }
    }

    // Nothing to read yet. Not said before the controller has been pointed at the athlete.
    Item {
        visible: !root.ready && !root.catchingUp
                 && root.controller !== null && (root.controller.athleteDir || "") !== ""
        width:   root.width
        height:  visible ? Theme.sp(40) : 0
        Text {
            objectName: "swingAreasEmpty"
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            text: qsTr("Nothing yet. Your swing is summed up here once you have hit some shots.")
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText3
        }
    }

    PpCard {
        id: table
        objectName: "swingAreasTable"
        visible: root.ready
        width:   root.width
        height:  visible ? implicitHeight : 0
        table:   true
        columns: [{ title: "",                       width: Theme.sp(14) },
                  { title: qsTr("Area"),             width: Theme.sp(170) },
                  { title: qsTr("Habits"),           fill: 1.3 },
                  { title: qsTr("Trend"),            width: Theme.sp(48), align: "right" },
                  { title: qsTr("Biggest fault"),    fill: 1 },
                  { title: qsTr("Work-ons"),         width: Theme.sp(130), align: "right" }]

        Repeater {
            model: root.areas
            HmAreaRow {
                required property var modelData
                area:       modelData
                autoToggle: false
                expanded:   root.isOpen(String(modelData.group))
                onToggled:  root.toggle(String(modelData.group))
                onPractiseRequested:  (conditionId, label) => root.practiseRequested(conditionId, label)
                onAllInAreaRequested: (group) => root.allInAreaRequested(group)
            }
        }
    }
}
