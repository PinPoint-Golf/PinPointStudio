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
    width:   appSettings.windowWidth
    height:  appSettings.windowHeight
    visible: true
    title: qsTr("PinPoint")
    color: Theme.colorBg
    font.family: Theme.fontBody

    onWidthChanged:  appSettings.windowWidth  = width
    onHeightChanged: appSettings.windowHeight = height

    Component.onCompleted: {
        if (appSettings.fontScale > 0) {
            Theme.fontScale = appSettings.fontScale
        } else {
            var w = Screen.desktopAvailableWidth
            Theme.fontScale = w >= 3840 ? 1.5 : w >= 2560 ? 1.25 : 1.0
        }
    }

    // Maps StackLayout index → header screen name
    readonly property var screenNames: [
        "Home",             // 0
        "Swing",            // 1
        "Wrist",            // 2
        "Ground forces",    // 3
        "Coach",            // 4
        "Play",             // 5
        "New athlete",      // 6
        "Athletes",         // 7
        "System resources"  // 8
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PpRail {
            id: rail
            Layout.fillHeight: true
            // implicitWidth declared in PpRail.qml drives the column width
            currentPageIndex: navController.currentIndex
            onPageRequested: function(index) { navController.navigateRail(index) }
            onAvatarClicked: navController.navigateRail(7)
            onSystemClicked: navController.navigateRail(8)
        }

        ColumnLayout {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            spacing: 0

            PpHeader {
                id: appHeader
                Layout.fillWidth: true
                screenName: navController.currentIndex < screenNames.length
                            ? screenNames[navController.currentIndex] : ""
            }

            StackLayout {
                id: contentStack
                Layout.fillWidth:  true
                Layout.fillHeight: true
                currentIndex: navController.currentIndex

                ScreenHome {                                               // 0 — home / default
                    onAddAthleteRequested:    navController.navigate(6)
                    onAthletePickerRequested: navController.navigate(7)
                    onStartSessionRequested:  console.log("Session start — not yet implemented")
                    onSystemRequested:        navController.navigate(8)
                }
                VideoPage       {}                                         // 1 — Swing
                ScreenPlaceholder { iconText: "⌖"; titleText: "Wrist"  }  // 2
                ScreenPlaceholder { iconText: "⇅"; titleText: "GRF"    }  // 3
                ScreenPlaceholder { iconText: "✦"; titleText: "Coach"  }  // 4
                PlayPage {}                                                // 5 — Play dev-hatch only
                ScreenAthleteForm {                                        // 6 — new athlete form
                    onCancelled:       navController.navigate(0)
                    onSaved:           navController.navigate(7)
                    onSavedAndStarted: navController.navigate(7)
                }
                ScreenAthletePicker {                                      // 7 — athlete picker
                    onAthleteSelected:     navController.navigate(0)
                    onNewAthleteRequested: navController.navigate(6)
                }
                ScreenResourceMonitor {}                                   // 8 — system resource monitor
            }
        }
    }
}
