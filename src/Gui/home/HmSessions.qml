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

// SESSIONS — Home's Sessions tab (HmHomeTabs, the tab at the right of the bar; Mark, 11 Nov 2026 — it was the
// rail's own screen): the current golfer's sessions, every one, each with its report
// (docs/design/session_report_design.md §8.0), as a full-width table (docs/design/sessions_screen_redesign.md §2.1).
// Home shows the tabs only with a golfer, so the list is always the current golfer's (main.cpp points
// sessionReportList at their library folder).
//
//   (the tab bar's note: 10 sessions · 202 swings · 14.6 GB — summaryLine)
//   RECORDED WITH (chips)   All reports | None 7 | Out of date 2 | Ready 1   Club: All ▾   ☐ Show archived (3)   [Search]
//   the table (PpSessionReportList)
//   Click a row to select or deselect · … (until the first selection: appSettings.sessionsHintSeen)
//
// The selection bar (PpSessionsSelectionBar, §2.4) and the bulk jobs' toast float over the whole of Home, so
// ScreenHome hosts them over `list`. Every filter is the model's (SessionReportListModel); this page only
// draws their controls. The list owns the rows, the selection and the ⋯ menu; its requests leave through
// this page's signals.

import QtQuick
import PinPointStudio

Column {
    id: root
    objectName: "homeSessions"

    // A row's Review; the links to where a missing detail is entered — Settings → Studio, or the
    // golfer's own page (their address, the photo approval).
    signal reviewSessionRequested(string sessionDir)
    signal studioSettingsRequested()
    signal golferDetailsRequested()

    // The table, for the selection bar and toast ScreenHome floats over it.
    readonly property alias list: list
    readonly property var model: list.model
    readonly property bool hintSeen: typeof appSettings !== "undefined" && appSettings
                                     && appSettings.sessionsHintSeen === true

    // What is shown, for the tab bar's note.
    readonly property string summaryLine: {
        const m = root.model
        const n  = m && m.shownCount  !== undefined ? m.shownCount  : 0
        const sw = m && m.shownSwings !== undefined ? m.shownSwings : 0
        return [Theme.plural(n, qsTr("%1 session"), qsTr("%1 sessions")),
                Theme.plural(sw, qsTr("%1 swing"), qsTr("%1 swings")),
                Theme.formatBytes(m ? (m.shownBytes || 0) : 0)].join(" · ")
    }

    // The keys (Esc, Ctrl/⌘-A, the arrows) go to the table when the page shows — unless a tab has the focus,
    // so the bar's arrows keep moving between tabs.
    onVisibleChanged: {
        if (!visible) return
        const f = Window.activeFocusItem
        if (!f || f.objectName !== "sdTab") list.forceActiveFocus()
    }

    spacing: 0

    // ── Filters: recorded with · report · club · archived, and the search at the right ──
    // One row; on a narrow window (or a large text scale) it wraps. Each control is centred in a cell the
    // combo's height, so they share a midline.
    Item {
        id: filters
        readonly property int cellH: clubBox.implicitHeight
        width:  root.width
        height: Math.max(filterFlow.height, cellH)
        Flow {
            id: filterFlow
            width: filters.width - search.width - Theme.gap(18)
            spacing: Theme.gap(14)

            Item {
                // The bar's own rule (a device any session used), not its `visible`, which
                // follows this cell's and would hold it hidden.
                visible: !!root.model && !!root.model.availableDevices && root.model.availableDevices.length > 0
                width: deviceBar.implicitWidth
                height: filters.cellH
                PpDeviceFilterBar {
                    id: deviceBar
                    objectName: "sessionListDeviceFilter"
                    anchors.verticalCenter: parent.verticalCenter
                    model: root.model && root.model.availableDevices !== undefined ? root.model : null
                    showCount: false
                }
            }

            Item {
                width: reportSeg.width; height: filters.cellH
                PpSegmentedControl {
                    id: reportSeg
                    objectName: "sessionsReportFilter"
                    anchors.verticalCenter: parent.verticalCenter
                    solid: false
                    readonly property var counts: root.model && root.model.reportCounts ? root.model.reportCounts : ({})
                    readonly property var keys: ["", "none", "stale", "ready"]
                    options: [qsTr("All reports"),
                              qsTr("None %1").arg(counts.none || 0),
                              qsTr("Out of date %1").arg(counts.stale || 0),
                              qsTr("Ready %1").arg(counts.ready || 0)]
                    selected: options[Math.max(0, keys.indexOf(root.model && root.model.reportFilter ? root.model.reportFilter : ""))]
                    width: Theme.sp(340)
                    onActivated: (value) => {
                        const k = reportSeg.keys[reportSeg.options.indexOf(value)]
                        if (root.model && k !== undefined) root.model.reportFilter = k
                    }
                }
            }

            PpComboBox {
                id: clubBox
                objectName: "sessionsClubFilter"
                readonly property var clubs: root.model && root.model.clubCounts ? root.model.clubCounts : []
                readonly property string chosen: root.model && root.model.clubFilter ? root.model.clubFilter : ""
                implicitWidth: Theme.sp(140)
                model: [qsTr("All clubs")].concat(clubs.map(c => c.club + "  ·  " + c.count))
                currentIndex: chosen === "" ? 0 : 1 + clubs.findIndex(c => c.club === chosen)
                displayText: chosen === "" ? qsTr("Club: All") : qsTr("Club: %1").arg(chosen)
                onActivated: (i) => { if (root.model) root.model.clubFilter = i <= 0 ? "" : clubs[i - 1].club }
            }

            Item {
                visible: archivedBox.n > 0 || archivedBox.on
                width: archivedBox.width; height: filters.cellH
                PpCheckRow {
                    id: archivedBox
                    objectName: "sessionsShowArchived"
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property int n: root.model && root.model.archivedCount !== undefined ? root.model.archivedCount : 0
                    readonly property bool on: !!(root.model && root.model.showArchived)
                    width: implicitWidth
                    label:   qsTr("Show archived (%1)").arg(n)
                    checked: on
                    onToggled: if (root.model) root.model.showArchived = !on
                }
            }
        }
        PpSearchField {
            id: search
            objectName: "sessionsSearch"
            anchors.right: parent.right
            y: Math.round((filters.cellH - height) / 2)
            width: Math.min(Theme.sp(300), root.width * 0.3)
            placeholderText: qsTr("Search work-ons, clubs")
            text: root.model && root.model.search !== undefined ? root.model.search : ""
            onTextEdited: if (root.model) root.model.search = text
            Keys.onEscapePressed: (event) => {
                if (text !== "") { text = ""; if (root.model) root.model.search = "" }
                list.forceActiveFocus()
                event.accepted = true
            }
        }
    }
    Item { width: 1; height: Theme.gap(16) }

    PpSessionReportList {
        id: list
        width: root.width
        onReviewSessionRequested:  (dir) => root.reviewSessionRequested(dir)
        onStudioSettingsRequested: root.studioSettingsRequested()
        onGolferDetailsRequested:  root.golferDetailsRequested()
        onSelectedOnce: if (typeof appSettings !== "undefined" && appSettings && !appSettings.sessionsHintSeen)
                            appSettings.sessionsHintSeen = true
    }

    // ── The hint, until the first selection ──────────────────────────
    PpCardNote {
        objectName: "sessionsHint"
        kind:       "plain"
        visible:    !root.hintSeen && list.shown > 0
        width:      root.width
        topPadding: Theme.gap(14)
        text:       [qsTr("Click a row to select or deselect"), qsTr("Shift-click for a range"),
                     qsTr("Hover for Review, or double-click, to open in Live"), qsTr("Esc clears")].join("   ·   ")
    }
}
