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

// Wrist Motion screen (contentStack index 2). Hosts the persistent session
// toolbar, then a View-driven layout: an optional timeline rail, the centre
// stage (camera tiles today; charts/dashboard/table land later), and an
// optional shot carousel. Which panels show, and how the stage packs them, is
// owned by the toolbar's View control (ViewLayout, per session type). The
// toolbar's device panels run calibration entirely in-panel, so this screen
// neither exposes nor routes any calibrate request.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
            sessionType: SessionController.Wrist
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
                PpCameraTiles { sessionType: SessionController.Wrist; showHittingArea: false }
            }
            chartsDelegate: Component { PpReviewCharts {} }
            // Table panel — read-only inspector of the focused swing.json. The
            // focused swing is the active replay, else the carousel's selection.
            tableDelegate: Component {
                PpDataViewer {
                    sessionType: SessionController.Wrist
                    swingDir: shotReplay.swingDir !== "" ? shotReplay.swingDir
                            : (wristCarousel.selectedCard ? wristCarousel.selectedCard.swingDir : "")
                }
            }
        }

        // Always-on Review transport (play/step/speed); works even when the
        // timeline panel is hidden. Capture/Analyse hide it.
        PpReviewTransport {
            Layout.fillWidth: true
            visible: SessionMode.mode === SessionMode.review
        }

        // Session-shot carousel — keys mirror the Wrist goal vocabulary
        // (goalDefsByType[1]); the stub model supplies placeholder values.
        PpShotCarousel {
            id: wristCarousel
            Layout.fillWidth: true
            visible: ViewLayout.isPanelOn(SessionMode.mode, "carousel")
            metricKeys: ["leadWristFlexExt", "leadWristRadUln", "forearmPronation", "leadArmFlexion"]
            traceLabel: qsTr("LEAD-WRIST FLEXION · ADDRESS → IMPACT")
        }
    }

    Component { id: reviewTimelineComp; PpReviewTimeline {} }
    Component { id: rmTimelineComp;     RmTimelineChart {} }
}
