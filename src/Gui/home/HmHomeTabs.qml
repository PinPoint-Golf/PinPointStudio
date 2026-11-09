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

    function _read() {
        const tab = String(ViewStore.read(root.settings, root.athleteUuid, "tab", "coaching"))
        root.current = root.tabKeys.indexOf(tab) >= 0 ? tab : "coaching"
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

    StackLayout {
        id: pages
        width: root.width
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
