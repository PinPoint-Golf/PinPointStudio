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

// Charts stage-panel: the focused replay's metric traces (PpMetricChart),
// scrubbing with the replay playhead, with split/overlay + chart-local segment
// selection + per-window summary. Bound to shotReplay. Hosted by PpModeStage as the
// "charts" panel; shows an empty-state until a swing is reviewed.

import QtQuick
import PinPointStudio

Item {
    id: root

    // Screen session type (Swing/Wrist/GRF/Coach), forwarded to the chart so it can
    // persist its collapsible-section state per screen+mode. Set by the host screen.
    property int sessionType: -1

    // All session screens live in the StackLayout at once (this screen is at index
    // sessionType+1). Without this gate every shotReplay span/position change re-ran
    // the chart maths in all 4 — 3 invisible. Only the visible screen's chart reacts.
    readonly property bool _screenActive: navController.currentIndex === root.sessionType + 1

    readonly property var  _detail: root._screenActive ? shotReplay.analysisDetail : null
    readonly property var  _series: (_detail && _detail.series) ? _detail.series : []

    PpMetricChart {
        anchors.fill: parent
        anchors.margins: Theme.sp(12)
        sessionType: root.sessionType
        visible:    root._series.length > 0
        seriesList: root._series
        phases:     (root._detail && root._detail.phases) ? root._detail.phases : []
        // Span/playhead gated too: an inactive screen feeds constants so no binding
        // churns its axis maths when shotReplay updates for a swing on another screen.
        startUs:    root._screenActive ? shotReplay.startUs    : 0
        endUs:      root._screenActive ? shotReplay.endUs      : 0
        impactUs:   root._screenActive ? shotReplay.impactUs   : 0
        playheadUs: root._screenActive ? shotReplay.positionUs : 0
        showPlayhead: true

        // Click/drag a plot to scrub — drives the shared replay playhead, so the
        // video, overlay, timeline and every other panel follow to that instant.
        seekable: true
        onSeekRequested: (t) => shotReplay.seekToUs(t)
        onScrubBegan:    shotReplay.beginScrub()
        onScrubEnded:    shotReplay.endScrub()
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.sp(6)
        visible: root._series.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: shotReplay.active ? qsTr("No metric traces for this swing")
                                    : qsTr("Select a swing to review")
            color: Theme.colorText2; font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Charts show the analyzed metric curves")
            color: Theme.colorText3; font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingData
        }
    }
}
