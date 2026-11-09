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

// Home's four views of the swing, under one title-size tab bar
// (docs/design/ux_refactor_away_from_4_modes.md §2.3, 03_home_coaching.png):
//
//   Coaching     HmSwingSummary — what to do next, in the golfer's words (the default, D6)
//   Your swing   HmSwingAreas — how the whole swing stands, area by area
//   Tendencies   HmTendencies — the usual miss with each club, cross-session
//   Matchups     HmGoesTogether — what travels together: today faults that move together; later
//                also pairs that work together (Mark, 28 Oct)
//
// One club filter (HmClubScopeFilter) sits above the tabs: All, a group or one club, remembered per athlete.
// It is the controller's scope (workOns.clubScope), so Coaching, Your swing and Matchups read the same properties
// whichever is chosen, and dim while a new choice is worked out. Tendencies lists a row per club already, so it
// shows only the top row and narrows to the group, the chosen club's group when a club is chosen.
//
// A fault on Coaching or Your swing opens its detail in place (HmFaultDetail); there is no page
// behind Home. The bar carries the tab's evidence line straight after the tabs (controller.tendencyNote
// on Tendencies when there is a table to back, else controller.summarySubtitle). The tab last chosen is remembered per athlete in appSettings.homeViewByAthlete[uuid].tab
// ("coaching" | "swing" | "tendencies" | "matchups"); HmSwingAreas keeps its own keys in the same entry. Both read and
// write it through hm_view_store.js.
//
// Signals go up to ScreenHome: Practise, and an open fault's "Review the … session →".
//
// objectNames: homeTabs (this), homeTabBar (the PpTabs), homeTabCoaching, homeTabSwing and
// homeTabMatchups (the pages), homeTabTendencies (HmTendencies' card, the third page).

import QtQuick
import QtQuick.Layouts
import PinPointStudio
import "hm_view_store.js" as ViewStore

Column {
    id: root
    objectName: "homeTabs"

    // Passed in rather than reached for, so a harness can drive the component.
    property var    controller:  (typeof workOns !== "undefined") ? workOns : null
    property var    settings:    (typeof appSettings !== "undefined") ? appSettings : null
    property string athleteUuid: (typeof athleteController !== "undefined" && athleteController)
                                 ? athleteController.currentUuid : ""

    signal practiseRequested(string conditionId, string label)
    signal reviewSessionRequested(string sessionDir)

    readonly property var tabKeys: ["coaching", "swing", "tendencies", "matchups"]
    // The tab showing. Read from the settings for each athlete; "coaching" when none was chosen.
    property string current: "coaching"

    // The club filter: the controller's scope, and whether a new one is being worked out.
    readonly property bool   hasClubScope:  root.controller !== null && root.controller.clubScope !== undefined
    readonly property string clubScope:     root.hasClubScope ? root.controller.clubScope : "all"
    readonly property bool   readingScope:  root.hasClubScope && root.controller.clubScopeRefreshing
    readonly property bool   matchingScope: root.hasClubScope && root.controller.matchupsRefreshing
    readonly property bool   scopedTab:     root.current !== "tendencies"
    // A stored choice is not "working", so it never flashes the page dim.
    readonly property bool   scopeWorking:  root.scopedTab && root.hasClubScope && root.controller.clubScopeWorking
                                            && (root.current === "matchups" ? root.matchingScope : root.readingScope)
    function selectClubScope(key) {
        key = String(key)
        if (root.hasClubScope) root.controller.clubScope = key
        ViewStore.write(root.settings, root.athleteUuid, "clubScope", key)
    }

    function _read() {
        const tab = String(ViewStore.read(root.settings, root.athleteUuid, "tab", "coaching"))
        root.current = root.tabKeys.indexOf(tab) >= 0 ? tab : "coaching"
        if (root.hasClubScope) root.controller.clubScope = String(ViewStore.read(root.settings, root.athleteUuid, "clubScope", "all"))
    }
    // Show a tab, and remember it for this athlete (nothing written when it is already the one).
    function select(key) {
        key = String(key)
        if (root.tabKeys.indexOf(key) < 0) return
        root.current = key
        ViewStore.write(root.settings, root.athleteUuid, "tab", key)
    }
    Component.onCompleted: root._read()
    onAthleteUuidChanged:  root._read()
    onSettingsChanged:     root._read()

    width:   parent ? parent.width : 0
    spacing: Theme.gap(16)

    PpTabs {
        id: bar
        objectName: "homeTabBar"
        width:  root.width
        roles:  homeTs
        size:   "title"
        noteAfterTabs: true
        tabs: [{ key: "coaching", label: qsTr("Coaching") }, { key: "swing", label: qsTr("Your swing") },
               { key: "tendencies", label: qsTr("Tendencies") }, { key: "matchups", label: qsTr("Matchups") }]
        current: root.current
        // The evidence each tab reads: "From N swings over M sessions"; Tendencies counts its shots.
        // With no table to back, Tendencies says why in its own card and the bar stays quiet.
        note: {
            if (!root.controller) return ""
            if (root.current === "tendencies")
                return (root.controller.tendencyItems || []).length > 0 ? (root.controller.tendencyNote || "") : ""
            return root.controller.summarySubtitle || ""
        }
        onTabActivated: (key) => root.select(key)
    }

    HmClubScopeFilter {
        id: scopeFilter
        objectName: "homeClubScope"
        visible:    hasChoice
        width:      root.width
        groupsOnly: !root.scopedTab
        enabled:    !root.controller || !root.controller.paused
        options:    root.hasClubScope ? root.controller.clubScopes : []
        selected:   root.clubScope
        onScopeSelected: (key) => root.selectClubScope(key)
    }

    StackLayout {
        id: pages
        width: root.width
        // The last choice's results, until this one's land.
        opacity: root.scopeWorking ? 0.45 : 1
        enabled: !root.scopeWorking
        currentIndex: Math.max(0, root.tabKeys.indexOf(root.current))
        // As tall as the page showing, not the tallest.
        height: [coachingPage, swingPage, tendenciesPage, matchupsPage][currentIndex].implicitHeight

        Item {
            id: coachingPage
            objectName: "homeTabCoaching"
            implicitHeight: summary.implicitHeight
            HmSwingSummary {
                id: summary
                width: coachingPage.width
                controller: root.controller
                onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
                onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
            }
        }
        Item {
            id: swingPage
            objectName: "homeTabSwing"
            implicitHeight: areas.implicitHeight
            HmSwingAreas {
                id: areas
                width: swingPage.width
                controller:  root.controller
                settings:    root.settings
                athleteUuid: root.athleteUuid
                onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
                onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
            }
        }
        Item {
            id: tendenciesPage
            implicitHeight: tendencies.implicitHeight
            HmTendencies {
                id: tendencies
                width: tendenciesPage.width
                controller:  root.controller
                athleteUuid: root.athleteUuid
                group:       scopeFilter.group ? scopeFilter.group.group : ""
                onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
            }
        }
        Item {
            id: matchupsPage
            objectName: "homeTabMatchups"
            implicitHeight: matchups.implicitHeight
            HmGoesTogether {
                id: matchups
                width: matchupsPage.width
                controller: root.controller
                onReviewSessionRequested: (dir) => root.reviewSessionRequested(dir)
            }
        }
    }

    // Declared last: children are destroyed in creation order, and the tab bar reads it to the end.
    PpTypeset { id: homeTs }
}
