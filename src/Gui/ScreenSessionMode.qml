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

        // Timeline rail — content resolves by mode: Review shows the scrub +
        // clickable phase pills; Capture/Analyse keep the session placeholder.
        Loader {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(56)
            visible: ViewLayout.isPanelOn(SessionMode.mode, "timeline")
            active: visible
            sourceComponent: SessionMode.mode === SessionMode.review
                             ? reviewTimelineComp : rmTimelineComp
        }

        // Centre stage — camera tiles + the Review charts trace; dashboard/table
        // fall back to muted PpStagePanel placeholders until their producers land.
        // Packing (tabs/split/stage) is resolved by ViewLayout on the active mode.
        PpModeStage {
            Layout.fillWidth: true; Layout.fillHeight: true
            cameraDelegate: Component {
                PpCameraTiles { sessionType: root.sessionType }
            }
            chartsDelegate: Component { PpReviewCharts {} }
            // Table panel — read-only inspector of the focused swing.json. The
            // focused swing is the active replay, else the carousel's selection.
            tableDelegate: Component {
                PpDataViewer {
                    sessionType: root.sessionType
                    swingDir: shotReplay.swingDir !== "" ? shotReplay.swingDir
                            : (modeCarousel.selectedCard ? modeCarousel.selectedCard.swingDir : "")
                }
            }
        }

        // Always-on Review transport (play/step/speed); works even when the
        // timeline panel is hidden. Capture/Analyse hide it.
        PpReviewTransport {
            Layout.fillWidth: true
            visible: SessionMode.mode === SessionMode.review
        }

        // Session-shot carousel — stub model for now (per-mode goal vocabulary
        // lands with the real analyzers).
        PpShotCarousel {
            id: modeCarousel
            Layout.fillWidth: true
            visible: ViewLayout.isPanelOn(SessionMode.mode, "carousel")
            metricKeys: []
            traceLabel: qsTr("SESSION SHOTS")
        }
    }

    Component { id: reviewTimelineComp; PpReviewTimeline {} }
    Component { id: rmTimelineComp;     RmTimelineChart {} }
}
