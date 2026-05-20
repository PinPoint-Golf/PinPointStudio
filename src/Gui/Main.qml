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
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPoint

ApplicationWindow {
    id: root
    width:   appSettings.windowWidth
    height:  appSettings.windowHeight
    visible: true
    title: qsTr("PinPoint")
    color: Theme.colorBg
    font.family: Theme.fontBody


    // Debounce geometry writes — 500 ms after the last move or resize.
    // Saves all four values together so they stay consistent in QSettings.
    Timer {
        id: geometryTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (root.visibility !== Window.Windowed) return
            if (!appSettings.rememberWindowGeometry) return
            appSettings.windowX      = root.x
            appSettings.windowY      = root.y
            appSettings.windowWidth  = root.width
            appSettings.windowHeight = root.height
        }
    }

    onXChanged:      { if (visibility === Window.Windowed) geometryTimer.restart() }
    onYChanged:      { if (visibility === Window.Windowed) geometryTimer.restart() }
    onWidthChanged:  { if (visibility === Window.Windowed) geometryTimer.restart() }
    onHeightChanged: { if (visibility === Window.Windowed) geometryTimer.restart() }

    Component.onCompleted: {
        // Font scale
        if (appSettings.fontScale > 0) {
            Theme.fontScale = appSettings.fontScale
        } else {
            var w = Screen.desktopAvailableWidth
            Theme.fontScale = w >= 3840 ? 1.5 : w >= 2560 ? 1.25 : 1.0
        }

        // Geometry: restore saved position, or place on the chosen display.
        if (appSettings.rememberWindowGeometry
                && appSettings.windowX >= 0 && appSettings.windowY >= 0) {
            // Exact saved position — preserves which screen the user last used.
            root.x = appSettings.windowX
            root.y = appSettings.windowY
        } else {
            // No saved position: place on the screen chosen in Display settings.
            var screens = Qt.application.screens
            var target  = screens[0]   // index 0 is always the primary screen in Qt
            var mode    = appSettings.mainDisplayMode

            if (mode === "cursor") {
                var ci = appSettings.cursorScreenIndex()
                if (ci >= 0 && ci < screens.length)
                    target = screens[ci]
            } else if (mode.indexOf("screen:") === 0) {
                var idx = parseInt(mode.substring(7))
                if (!isNaN(idx) && idx >= 0 && idx < screens.length)
                    target = screens[idx]
            }
            // "primary" falls through — target is already screens[0]

            root.screen = target
            // Centre the window on the chosen screen.
            root.x = target.virtualX + Math.round((target.width  - root.width)  / 2)
            root.y = target.virtualY + Math.round((target.height - root.height) / 2)
        }

        if (appSettings.windowMaximized)
            root.showFullScreen()
    }

    function toggleFullscreen() {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            root.showNormal()
        else
            root.showFullScreen()
    }

    // F11 everywhere; Ctrl+Cmd+F is the macOS convention (Ctrl = Meta in Qt key names on macOS)
    Shortcut { sequence: "F11";         onActivated: root.toggleFullscreen() }
    Shortcut { sequence: "Meta+Ctrl+F"; onActivated: root.toggleFullscreen() }

    // Maps StackLayout index → header screen name
    readonly property var screenNames: [
        qsTr("Home"),             // 0
        qsTr("Swing"),            // 1
        qsTr("Wrist"),            // 2
        qsTr("Ground forces"),    // 3
        qsTr("Coach"),            // 4
        qsTr("Play"),             // 5
        qsTr("New athlete"),      // 6
        qsTr("Athletes"),         // 7
        qsTr("System resources"), // 8
        qsTr("Settings")          // 9
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
                showVersionPill: navController.currentIndex === 9
                isFullscreen: root.visibility === Window.FullScreen
                onFullscreenToggleRequested: root.toggleFullscreen()
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
                ScreenSettings {}                                          // 9 — settings
            }
        }
    }
}
