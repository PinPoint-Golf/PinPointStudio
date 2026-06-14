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

    readonly property var  _detail: shotReplay.analysisDetail
    readonly property var  _series: (_detail && _detail.series) ? _detail.series : []

    PpMetricChart {
        anchors.fill: parent
        anchors.margins: Theme.sp(12)
        visible:    root._series.length > 0
        seriesList: root._series
        phases:     (root._detail && root._detail.phases) ? root._detail.phases : []
        startUs:    shotReplay.startUs
        endUs:      shotReplay.endUs
        impactUs:   shotReplay.impactUs
        playheadUs: shotReplay.positionUs
        showPlayhead: true
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
