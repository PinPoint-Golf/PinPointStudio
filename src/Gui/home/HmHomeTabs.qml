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

// Home's four views of the swing and the golfer's sessions, under one title-size tab bar
// (docs/design/ux_refactor_away_from_4_modes.md §2.3, 03_home_coaching.png):
//
//   Coaching     HmSwingSummary — what to do next, in the golfer's words (the default, D6)
//   Your swing   HmSwingAreas — how the whole swing stands, area by area
//   Tendencies   HmTendencies — each club's usual strike, direction and flight, cross-session
//   Matchups     HmGoesTogether — what travels together: today faults that move together; later
//                also pairs that work together (Mark, 28 Oct)
//   Sessions     HmSessions — every session with its report; the tab at the RIGHT of the bar (an `end` tab).
//                It was the rail's own screen until 11 Nov 2026 (Mark). It has its own filters, so the
//                shot filter line hides on it, and the bar's note is what it shows (sessions · swings · size).
//
// One filter line (HmShotFilter) sits above the tabs: Club (All, a group or one club), Strike, Direction and Flight,
// each a quiet picker with its counts, then Clear, then the shots it reads in words (workOns.shotFilterLine, empty
// under all). Both halves are remembered per athlete. The club is the controller's scope (workOns.clubScope) and the
// rest its shot filter (workOns.shotFilter, "strike:<v>|direction:<v>|flight:<v>", choices in workOns.shotFilterOptions
// for the current club choice), so Coaching, Your swing and Matchups read the same properties whichever is chosen, and
// dim while a new choice is worked out. Tendencies lists a row per club already, so it shows the Club picker alone,
// with groups only, and narrows to the group, the chosen club's group when a club is chosen; it ignores the rest.
//
// A fault on Coaching or Your swing opens its detail in place (HmFaultDetail); there is no page
// behind Home. The bar carries the tab's evidence line straight after the tabs (controller.tendencyNote
// on Tendencies when there is a table to back, else controller.summarySubtitle). The tab last chosen is remembered per athlete in appSettings.homeViewByAthlete[uuid].tab
// ("coaching" | "swing" | "tendencies" | "matchups"), the club choice in .clubScope and the strike, direction and
// flight choices in .shotFilter; HmSwingAreas keeps its own keys in the same entry. Both read and
// write it through hm_view_store.js.
//
// Signals go up to ScreenHome: Practise, an open fault's "Review the … session →", and the Sessions page's
// Review and links (Settings → Studio, the golfer's own page). ScreenHome floats the Sessions selection bar and
// toast over `sessions.list`.
//
// objectNames: homeTabs (this), homeTabBar (the PpTabs), homeShotFilter (the filter line; its pickers keep
// HmShotFilter's names), homeTabCoaching, homeTabSwing, homeTabMatchups and homeTabSessions (the pages),
// homeTabTendencies (HmTendencies' card, the third page), homeSessions (HmSessions, in homeTabSessions).

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
    signal studioSettingsRequested()
    signal golferDetailsRequested()

    // The Sessions page, for the selection bar and toast ScreenHome floats over it.
    readonly property alias sessions: sessions

    readonly property var tabKeys: ["coaching", "swing", "tendencies", "matchups", "sessions"]
    // The tab showing. Read from the settings for each athlete; "coaching" when none was chosen.
    property string current: "coaching"

    // The club filter: the controller's scope, and whether a new one is being worked out.
    readonly property bool   hasClubScope:  root.controller !== null && root.controller.clubScope !== undefined
    readonly property string clubScope:     root.hasClubScope ? root.controller.clubScope : "all"
    readonly property bool   readingScope:  root.hasClubScope && root.controller.clubScopeRefreshing
    readonly property bool   matchingScope: root.hasClubScope && root.controller.matchupsRefreshing
    readonly property bool   scopedTab:     root.current !== "tendencies" && root.current !== "sessions"
    // A stored choice is not "working", so it never flashes the page dim.
    readonly property bool   scopeWorking:  root.scopedTab && root.hasClubScope && root.controller.clubScopeWorking
                                            && (root.current === "matchups" ? root.matchingScope : root.readingScope)
    function selectClubScope(key) {
        key = String(key)
        if (root.hasClubScope) root.controller.clubScope = key
        ViewStore.write(root.settings, root.athleteUuid, "clubScope", key)
    }

    // The shot filter: strike, direction and flight, the choices for the current club choice and their line.
    readonly property bool   hasShotFilter:     root.controller !== null && root.controller.shotFilter !== undefined
    readonly property string shotFilter:        root.hasShotFilter ? root.controller.shotFilter : "all"
    readonly property var    shotFilterOptions: root.hasShotFilter && root.controller.shotFilterOptions !== undefined
                                                ? (root.controller.shotFilterOptions || ({})) : ({})
    readonly property string shotFilterLine:    root.hasShotFilter && root.controller.shotFilterLine !== undefined
                                                ? (root.controller.shotFilterLine || "") : ""
    function selectShotFilter(key) {
        key = String(key)
        if (root.hasShotFilter) root.controller.shotFilter = key
        ViewStore.write(root.settings, root.athleteUuid, "shotFilter", key)
    }

    function _read() {
        const tab = String(ViewStore.read(root.settings, root.athleteUuid, "tab", "coaching"))
        root.current = root.tabKeys.indexOf(tab) >= 0 ? tab : "coaching"
        if (root.hasClubScope) root.controller.clubScope = String(ViewStore.read(root.settings, root.athleteUuid, "clubScope", "all"))
        if (root.hasShotFilter) root.controller.shotFilter = String(ViewStore.read(root.settings, root.athleteUuid, "shotFilter", "all"))
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
               { key: "tendencies", label: qsTr("Tendencies") }, { key: "matchups", label: qsTr("Matchups") },
               { key: "sessions", label: qsTr("Sessions"), end: true }]
        current: root.current
        // The evidence each tab reads: "From N swings over M sessions"; Tendencies counts its shots.
        // With no table to back, Tendencies says why in its own card and the bar stays quiet. Sessions says
        // what its table shows.
        note: {
            if (root.current === "sessions") return sessions.summaryLine
            if (!root.controller) return ""
            if (root.current === "tendencies")
                return (root.controller.tendencyItems || []).length > 0 ? (root.controller.tendencyNote || "") : ""
            return root.controller.summarySubtitle || ""
        }
        onTabActivated: (key) => root.select(key)
    }

    HmShotFilter {
        id: filterLine
        objectName: "homeShotFilter"
        width:      root.width
        // Sessions has its own filters.
        visible:    root.current !== "sessions"
        clubOnly:   root.current === "tendencies"
        enabled:    !root.controller || !root.controller.paused
        clubScopes: root.hasClubScope ? (root.controller.clubScopes || []) : []
        clubScope:  root.clubScope
        options:    root.shotFilterOptions
        shotFilter: root.shotFilter
        line:       root.shotFilterLine
        onClubScopeSelected:  (key) => root.selectClubScope(key)
        onShotFilterSelected: (key) => root.selectShotFilter(key)
    }

    StackLayout {
        id: pages
        width: root.width
        // The last choice's results, until this one's land.
        opacity: root.scopeWorking ? 0.45 : 1
        enabled: !root.scopeWorking
        currentIndex: Math.max(0, root.tabKeys.indexOf(root.current))
        // As tall as the page showing, not the tallest.
        height: [coachingPage, swingPage, tendenciesPage, matchupsPage, sessionsPage][currentIndex].implicitHeight

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
        // The rows, and beside them the flow of the same clubs' shots (HmTendencyFlow; Mark, 10 Nov 2026: "alongside the
        // rows"). Narrower than sideBySide, the flow stacks above the table.
        Item {
            id: tendenciesPage
            readonly property bool sideBySide: width >= Theme.sp(1040)
            readonly property real flowW: sideBySide ? Math.round(width * 0.36) : width
            readonly property real pageGap: Theme.gap(16)
            implicitHeight: sideBySide ? Math.max(tendencies.implicitHeight, tendencyFlow.implicitHeight)
                                       : tendencyFlow.implicitHeight + pageGap + tendencies.implicitHeight
            HmTendencies {
                id: tendencies
                width: tendenciesPage.sideBySide ? tendenciesPage.width - tendenciesPage.flowW - tendenciesPage.pageGap
                                                 : tendenciesPage.width
                y: tendenciesPage.sideBySide ? 0 : tendencyFlow.implicitHeight + tendenciesPage.pageGap
                controller:  root.controller
                athleteUuid: root.athleteUuid
                group:       filterLine.group ? filterLine.group.group : ""
                onPractiseRequested: (conditionId, label) => root.practiseRequested(conditionId, label)
            }
            HmTendencyFlow {
                id: tendencyFlow
                width: tendenciesPage.flowW
                x: tendenciesPage.sideBySide ? tendenciesPage.width - width : 0
                controller: root.controller
                group:      tendencies.group
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
        Item {
            id: sessionsPage
            objectName: "homeTabSessions"
            implicitHeight: sessions.implicitHeight
            HmSessions {
                id: sessions
                width: sessionsPage.width
                onReviewSessionRequested:  (dir) => root.reviewSessionRequested(dir)
                onStudioSettingsRequested: root.studioSettingsRequested()
                onGolferDetailsRequested:  root.golferDetailsRequested()
            }
        }
    }

    // Declared last: children are destroyed in creation order, and the tab bar reads it to the end.
    PpTypeset { id: homeTs }
}
