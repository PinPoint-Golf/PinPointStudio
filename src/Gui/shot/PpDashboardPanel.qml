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

// PpDashboardPanel — the configurable post-shot dashboard. It joins the metric
// catalogue (identity / availability / normative corridors) to the focused shot's
// analysisDetail (measured values) BY KEY, and composes four glanceable zones —
// Verdict, Setup, Motion, Sequence — chosen by a per-session-type preset. Read at
// distance: no charts, headline numbers only. The preset control lives in this
// panel's header (never the toolbar); editing it updates the per-type store so the
// in-app panel and any cast wall window stay in lock-step.
//
// The dashboard is a pure DISPLAY SURFACE: the config (per session type) is
// swing-agnostic, and each zone renders its configured metrics showing the metric
// layer's value, or "NA" (live metric with no value this swing) / "soon" (planned,
// no producer yet). Availability is the pipeline's concern, not the dashboard's.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // Set by the host screen (Swing 0, Wrist 1, GRF 2, Coach 3), like the other
    // stage delegates (PpReplayCharts { sessionType: … }).
    property int sessionType: -1

    // The focused shot's flattened analysis. Recomputed shotCtx tracks it.
    readonly property var detail: shotReplay.analysisDetail
    readonly property var shotCtx: root._ctx(detail, sessionType)
    readonly property bool hasShot: shotReplay.active && detail && detail.overall !== undefined

    // shotCtx is a function result, so bind it through a helper that reads the two
    // tracked properties (detail, sessionType) to re-evaluate when either changes.
    function _ctx(d, st) { return shotReplay.shotContext(st) }

    // The catalogue façade — one instance per panel (QML_ELEMENT, not a singleton).
    // NB: id must NOT be `catalog` — the zones have a `catalog` property, and an
    // unqualified `catalog: catalog` binding would resolve to the zone's OWN (null)
    // property (id/property shadowing — see CLAUDE.md / commit 39857cc).
    MetricCatalog { id: metricCat }

    // Wrist findings/strengths for the Verdict zone (wrist-only; empty elsewhere).
    // id must NOT be `wristModel` (same shadowing trap as `catalog` above).
    WristDiagnosticsModel {
        id: wristDx
        analysisDetail: root.detail
        playheadUs:     shotReplay.positionUs
    }

    // Active preset id + its display label for the header chip.
    readonly property string _activeId: ViewLayout.dashboardActive(sessionType)
    function _presetLabel(id) {
        if (id === "custom") return qsTr("Custom")
        var cat = ViewLayout.dashboardPresetCatalog()
        for (var i = 0; i < cat.length; ++i)
            if (cat[i].id === id) return cat[i].label
        return id   // a saved preset — show its name
    }

    // ── Header: title · right-aligned preset chip + edit affordance ─────────────
    Rectangle {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.sp(30)
        color: "transparent"

        Text {
            anchors { left: parent.left; leftMargin: Theme.sp(12); verticalCenter: parent.verticalCenter }
            text: qsTr("Dashboard")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.capitalization: Font.AllUppercase; font.letterSpacing: Theme.trackingMicro
            color: Theme.colorText3
        }

        // Preset chip — reads the active preset (with a '*' when Custom); click opens
        // the editor popover anchored here.
        Rectangle {
            id: presetChip
            anchors { right: parent.right; rightMargin: Theme.sp(12); verticalCenter: parent.verticalCenter }
            implicitWidth: chipRow.implicitWidth + Theme.sp(14)
            implicitHeight: Theme.sp(24)
            radius: Theme.radius
            color: chipMa.containsMouse || editor.opened ? Theme.colorBg3 : "transparent"
            border.width: Theme.borderWidth
            border.color: chipMa.containsMouse || editor.opened ? Theme.colorBorderMid : Theme.colorBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: Theme.sp(5)
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root._presetLabel(root._activeId)
                          + (root._activeId === "custom" ? " *" : "")
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                    color: Theme.colorText2
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                    color: Theme.colorText3
                }
            }
            PpPressable {
                id: chipMa
                hoverScale: 1.0
                onClicked: editor.opened ? editor.close() : editor.open()
            }
        }
    }

    // ── Empty state ─────────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: !root.hasShot
        text: shotReplay.active ? qsTr("No analysis for this shot")
                                : qsTr("Select a shot to review")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        color: Theme.colorText3
    }

    // ── Zone column ─────────────────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom
                  topMargin: Theme.sp(6) }
        visible: root.hasShot
        contentHeight: zoneCol.implicitHeight + Theme.sp(24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: zoneCol
            x: Theme.sp(12)
            width: flick.width - Theme.sp(24)
            spacing: Theme.sp(12)

            Repeater {
                model: ViewLayout.dashboardZones(root.sessionType)
                delegate: Loader {
                    required property var modelData      // zone key string
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 0
                    sourceComponent: root._zoneComp(modelData)
                }
            }
        }
    }

    function _zoneComp(key) {
        switch (key) {
        case "verdict":  return verdictComp
        case "setup":    return setupComp
        case "motion":   return motionComp
        case "sequence": return sequenceComp
        }
        return null
    }

    // Inline wrappers — each binds the shared join inputs down to a zone file and
    // resolves that zone's pinned metric list from the per-type preset store.
    Component {
        id: verdictComp
        PpDashboardVerdictZone {
            width: parent ? parent.width : 0
            detail: root.detail; wristModel: wristDx
        }
    }
    Component {
        id: setupComp
        PpDashboardSetupZone {
            width: parent ? parent.width : 0
            catalog: metricCat; shotCtx: root.shotCtx; detail: root.detail
            sessionType: root.sessionType
            pinnedMetrics: ViewLayout.dashboardZoneMetrics(root.sessionType, "setup")
        }
    }
    Component {
        id: motionComp
        PpDashboardMotionZone {
            width: parent ? parent.width : 0
            catalog: metricCat; shotCtx: root.shotCtx; detail: root.detail
            sessionType: root.sessionType
            pinnedMetrics: ViewLayout.dashboardZoneMetrics(root.sessionType, "motion")
        }
    }
    Component {
        id: sequenceComp
        PpDashboardSequenceZone {
            width: parent ? parent.width : 0
            catalog: metricCat; shotCtx: root.shotCtx; detail: root.detail
            sessionType: root.sessionType
        }
    }

    // ── Preset editor popover ────────────────────────────────────────────────────
    PpDashboardPresetEditor {
        id: editor
        sessionType: root.sessionType
        catalog: metricCat
        shotCtx: root.shotCtx
        // Anchor under the header chip.
        parent: root
        x: Math.max(Theme.sp(8), presetChip.x + presetChip.width - width)
        y: header.height + Theme.sp(4)
    }
}
