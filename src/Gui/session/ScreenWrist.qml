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
//
// Replay and Analyse use the Transit timeline, in one of two orientations
// (appSettings.timelineOrientation): HORIZONTAL in the top rail, VERTICAL in a
// fixed-width panel to the LEFT of the stage. Kept in lockstep with
// ScreenSessionMode. Capture has no timeline (RmTimelineChart is resource-monitor only).

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    readonly property bool _timelineOn:  ViewLayout.isPanelOn(SessionMode.mode, "timeline")
    // Replay AND Analyse use the Transit timeline; Capture has no timeline rail.
    readonly property bool _transitMode: SessionMode.mode === SessionMode.replay
                                         || SessionMode.mode === SessionMode.analyse
    readonly property bool _vertical:    appSettings.timelineOrientation === "vertical"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PpSessionToolbar {
            Layout.fillWidth: true
            sessionType: SessionController.Wrist
        }

        // Top rail — horizontal Transit timeline (Replay/Analyse only). Capture has
        // no timeline; the vertical orientation moves Transit to the left rail instead.
        Loader {
            id: topRail
            readonly property bool horizTransit: root._transitMode && root._timelineOn && !root._vertical
            Layout.fillWidth: true
            Layout.preferredHeight: horizTransit ? Theme.sp(150) : 0
            Behavior on Layout.preferredHeight {
                enabled: !Theme.reduceMotion
                NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
            }
            visible: Layout.preferredHeight > 0
            active: visible
            clip: true
            sourceComponent: transitHorizComp
        }

        // Stage row — optional vertical Transit panel (left) + the centre stage.
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 0

            Loader {
                id: leftRail
                readonly property bool transitVert: root._transitMode && root._vertical && root._timelineOn
                Layout.fillHeight: true
                Layout.preferredWidth: transitVert ? Theme.sp(184) : 0
                Behavior on Layout.preferredWidth {
                    enabled: !Theme.reduceMotion
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.InOutQuad }
                }
                visible: Layout.preferredWidth > 0
                active: visible
                clip: true
                sourceComponent: transitVertComp
            }

            // Centre stage — camera tiles + the Replay charts trace; dashboard/table
            // fall back to muted PpStagePanel placeholders until their producers land.
            // Packing (tabs/split/stage) is resolved by ViewLayout on the active mode.
            PpModeStage {
                Layout.fillWidth: true; Layout.fillHeight: true
                cameraDelegate: Component {
                    PpCameraTiles { sessionType: SessionController.Wrist; showHittingArea: false }
                }
                chartsDelegate: Component { PpReplayCharts { sessionType: SessionController.Wrist } }
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
        }

        // Replay transport (play/step/speed) for Replay and Analyse — both drive the
        // loaded swing's playhead. Works even when the timeline panel is hidden.
        // Capture has no replay, so it's hidden there.
        PpReplayTransport {
            Layout.fillWidth: true
            visible: root._transitMode
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

    Component {
        id: transitHorizComp
        PpTransitTimeline { orientation: "horizontal"; snapToPhases: appSettings.timelineSnapToPhases }
    }
    Component {
        id: transitVertComp
        PpTransitTimeline { orientation: "vertical"; snapToPhases: appSettings.timelineSnapToPhases }
    }
}
