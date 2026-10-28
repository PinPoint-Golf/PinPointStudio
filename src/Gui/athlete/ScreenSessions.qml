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

// SESSIONS — the rail's Sessions (Main.qml, AppScreen.Sessions): the current golfer's sessions, every
// one, each with its report (docs/design/session_report_design.md §8.0), as a full-width table
// (docs/design/sessions_screen_redesign.md §2.1). The rail mutes Sessions with no golfer, so the list
// is always the current golfer's (main.cpp points sessionReportList at their library folder).
//
//   Sessions  10 sessions · 202 swings · 14.6 GB                       [Search work-ons, clubs]
//   RECORDED WITH (chips)   All reports | None 7 | Out of date 2 | Ready 1   Club: All ▾   ☐ Show archived (3)
//   the table (PpSessionReportList)
//   Click a row to select or deselect · … (until the first selection: appSettings.sessionsHintSeen)
//   [3 selected · 3.6 GB · 2 reports out of date · Clear   Make / remake reports  Email  Export…  Archive  Delete…]
//                                                 floating, while anything is selected or a bulk job runs
//
// Every filter is the model's (SessionReportListModel); this screen only draws their controls. The
// list owns the rows, the selection and the ⋯ menu; its requests leave through this screen's signals,
// which Main.qml handles. The selection bar (PpSessionsSelectionBar, §2.4) acts through the list too; a
// bulk job's outcome comes back as the list's notice(), shown here in a toast.

import QtQuick
import PinPointStudio

Item {
    id: root
    objectName: "sessionsScreen"

    // A row's Review; the links to where a missing detail is entered — Settings → Studio, or the
    // golfer's own page (their address, the photo approval).
    signal reviewSessionRequested(string sessionDir)
    signal studioSettingsRequested()
    signal golferDetailsRequested()

    // Back to the top: Main calls it on the way in.
    function toTop() { flick.contentY = 0 }

    readonly property var model: list.model
    readonly property int sidePad: Theme.sp(28)
    readonly property bool hintSeen: typeof appSettings !== "undefined" && appSettings
                                     && appSettings.sessionsHintSeen === true

    function bytesWords(b) {
        if (!(b > 0)) return "0 MB"
        const gb = b / (1024 * 1024 * 1024)
        if (gb >= 1) return gb.toFixed(1) + " GB"
        return Math.max(1, Math.round(b / (1024 * 1024))) + " MB"
    }
    function plural(n, one, many) { return n === 1 ? one.arg(n) : many.arg(n) }

    onVisibleChanged: if (visible) list.forceActiveFocus()

    Rectangle { anchors.fill: parent; color: Theme.colorBg }

    Flickable {
        id: flick
        anchors.fill:  parent
        contentWidth:  width
        contentHeight: mainCol.implicitHeight + Theme.sp(48) + (bar.visible ? bar.height + Theme.sp(24) : 0)
        clip:          true

        Column {
            id: mainCol
            x: root.sidePad
            width:   Math.max(0, root.width - 2 * root.sidePad)
            spacing: 0

            Item { width: 1; height: Theme.sp(36) }

            // ── Header: title, what is shown, search ─────────────────────────
            Item {
                width:  mainCol.width
                height: Math.max(title.implicitHeight, search.height)
                Row {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: Theme.gap(16)
                    Text {
                        id: title
                        objectName: "sessionsTitle"
                        text: qsTr("Sessions")
                        font.family:    Theme.fontDisplay
                        font.italic:    Theme.fontDisplayItalic
                        font.weight:    Theme.fontDisplayWeight
                        font.pixelSize: Theme.sp(Theme.fontSzTab)
                        color:          Theme.colorText
                    }
                    Text {
                        objectName: "sessionsSummary"
                        anchors.baseline: title.baseline
                        readonly property int n: root.model && root.model.shownCount !== undefined ? root.model.shownCount : 0
                        readonly property int sw: root.model && root.model.shownSwings !== undefined ? root.model.shownSwings : 0
                        text: [root.plural(n, qsTr("%1 session"), qsTr("%1 sessions")),
                               root.plural(sw, qsTr("%1 swing"), qsTr("%1 swings")),
                               root.bytesWords(root.model ? (root.model.shownBytes || 0) : 0)].join(" · ")
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzBody2
                        font.letterSpacing: Theme.trackingData
                        color: Theme.colorText3
                    }
                }
                PpTextField {
                    id: search
                    objectName: "sessionsSearch"
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    width: Math.min(Theme.sp(300), mainCol.width * 0.3)
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
            Item { width: 1; height: Theme.gap(18) }

            // ── Filters: recorded with · report · club · archived ────────────
            // One row; on a narrow window (or a large text scale) it wraps, the archived box staying at
            // the right. Each control is centred in a cell the combo's height, so they share a midline.
            Item {
                id: filters
                readonly property int cellH: clubBox.implicitHeight
                width:  mainCol.width
                height: Math.max(filterFlow.height, archivedBox.visible ? cellH : 0)
                Flow {
                    id: filterFlow
                    width: filters.width - (archivedBox.visible ? archivedBox.width + Theme.gap(18) : 0)
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
                }
                PpCheckRow {
                    id: archivedBox
                    objectName: "sessionsShowArchived"
                    anchors.right: parent.right
                    y: Math.round((filters.cellH - height) / 2)
                    readonly property int n: root.model && root.model.archivedCount !== undefined ? root.model.archivedCount : 0
                    readonly property bool on: !!(root.model && root.model.showArchived)
                    visible: n > 0 || on
                    width: implicitWidth
                    label:   qsTr("Show archived (%1)").arg(n)
                    checked: on
                    onToggled: if (root.model) root.model.showArchived = !on
                }
            }
            Item { width: 1; height: Theme.gap(16) }

            PpSessionReportList {
                id: list
                width: mainCol.width
                onReviewSessionRequested:  (dir) => root.reviewSessionRequested(dir)
                onStudioSettingsRequested: root.studioSettingsRequested()
                onGolferDetailsRequested:  root.golferDetailsRequested()
                onSelectedOnce: if (typeof appSettings !== "undefined" && appSettings && !appSettings.sessionsHintSeen)
                                    appSettings.sessionsHintSeen = true
            }

            // ── The hint, until the first selection ──────────────────────────
            Text {
                objectName: "sessionsHint"
                visible: !root.hintSeen && list.shown > 0
                width:   mainCol.width
                topPadding: Theme.gap(14)
                text: [qsTr("Click a row to select or deselect"), qsTr("Shift-click for a range"),
                       qsTr("Hover for Review, or double-click, to open in Live"), qsTr("Esc clears")].join("   ·   ")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText3
                wrapMode: Text.WordWrap
            }
        }
    }

    // ── The selection bar (§2.4): the summary and the bulk actions, or a job's progress ──
    PpSessionsSelectionBar {
        id: bar
        list: list
        x: root.sidePad
        width: Math.max(0, root.width - 2 * root.sidePad)
        height: implicitHeight
        anchors { bottom: parent.bottom; bottomMargin: Theme.sp(20) }
    }

    // A bulk job's outcome (the exported zips' paths to copy), above the bar.
    PpToast {
        id: toast
        objectName: "sessionsToast"
        showUndo: false
        anchors { horizontalCenter: parent.horizontalCenter; bottom: bar.visible ? bar.top : parent.bottom
                  bottomMargin: Theme.sp(16) }
    }
    Connections {
        target: list
        function onNotice(glyph, message, severity, copyText) {
            toast.copyText = copyText || ""
            toast.glyph    = glyph
            toast.severity = severity
            toast.show(message)
        }
    }
}
