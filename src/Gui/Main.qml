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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPoint

ApplicationWindow {
    id: root
    height: 700
    width: Math.round(700 * 1.6)
    visible: true
    title: qsTr("PinPoint")
    color: Theme.colorBg
    font.family: Theme.fontBody

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PpRail {
            id: rail
            Layout.fillHeight: true
            // implicitWidth declared in PpRail.qml drives the column width
            currentPageIndex: contentStack.currentIndex
            onPageRequested: function(index) {
                // Wrist (2) and GRF (3) require at least one athlete; redirect to Welcome if none exist
                if ((index === 2 || index === 3 || index === 4) && athleteController.athletes.length === 0)
                    contentStack.currentIndex = 0
                else
                    contentStack.currentIndex = index
            }
            onAvatarClicked:  contentStack.currentIndex = 7
            onSystemClicked:  contentStack.currentIndex = 8
        }

        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            ScreenHome {                                               // 0 — home / default
                onAddAthleteRequested:    contentStack.currentIndex = 6
                onAthletePickerRequested: contentStack.currentIndex = 7
                onStartSessionRequested:  console.log("Session start — not yet implemented")
                onSystemRequested:        contentStack.currentIndex = 8
            }
            VideoPage       {}                                         // 1 — Swing
            ScreenPlaceholder { iconText: "⌖"; titleText: "Wrist"  }  // 2
            ScreenPlaceholder { iconText: "⇅"; titleText: "GRF"    }  // 3
            ScreenPlaceholder { iconText: "✦"; titleText: "Coach"  }  // 4
            PlayPage {}                                                // 5 — Play dev-hatch only
            ScreenAthleteForm {                                        // 6 — new athlete form
                onCancelled:       contentStack.currentIndex = 0
                onSaved:           contentStack.currentIndex = 7
                onSavedAndStarted: contentStack.currentIndex = 7
            }
            ScreenAthletePicker {                                      // 7 — athlete picker
                onAthleteSelected:    contentStack.currentIndex = 0
                onNewAthleteRequested: contentStack.currentIndex = 6
            }
            ScreenResourceMonitor {}                                   // 8 — system resource monitor
        }
    }
}
