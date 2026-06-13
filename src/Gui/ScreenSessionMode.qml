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

// Reusable session-mode screen (Swing, GRF and Coach rail screens; the Wrist
// screen has its own ScreenWrist). Hosts the persistent session toolbar then a
// View-driven layout — an optional timeline rail, the centre stage (camera tiles
// today; charts/dashboard/table land later), and an optional shot carousel —
// each gated by the toolbar's View control (ViewLayout, per session type).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    // Retained for Main.qml's per-instance labelling; the body is now View-driven.
    property string iconText:  ""
    property string titleText: ""
    // SessionController::Type of this screen (set per instance in Main.qml).
    property int sessionType: -1

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
            sessionType: root.sessionType
        }

        // Timeline rail (placeholder until the real producer lands).
        RmTimelineChart {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(48)
            visible: ViewLayout.isPanelOn(root.sessionType, "timeline")
        }

        // Centre stage — camera tiles wired to real content; charts/dashboard/
        // table fall back to muted PpStagePanel placeholders until their
        // producers land. Packing (tabs/split/stage) is resolved by ViewLayout.
        PpModeStage {
            Layout.fillWidth: true; Layout.fillHeight: true
            sessionType: root.sessionType
            cameraDelegate: Component {
                PpCameraTiles { sessionType: root.sessionType }
            }
        }

        // Session-shot carousel — stub model for now (per-mode goal vocabulary
        // lands with the real analyzers).
        PpShotCarousel {
            Layout.fillWidth: true
            visible: ViewLayout.isPanelOn(root.sessionType, "carousel")
            metricKeys: []
            traceLabel: qsTr("SESSION SHOTS")
        }
    }
}
