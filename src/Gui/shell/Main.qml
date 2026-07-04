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
import PinPointStudio

ApplicationWindow {
    id: root
    width:   appSettings.windowWidth
    height:  appSettings.windowHeight
    // Created hidden on purpose — Component.onCompleted positions the window on
    // the chosen display BEFORE it is shown. See the note there.
    visible: false
    title: qsTr("PinPoint Studio")
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

        // Geometry. Priority order:
        //   1. "cursor" mode — open on the screen under the cursor. This is an
        //      explicit per-launch intent, so it beats a remembered position
        //      (which would otherwise pin the window to a stale monitor). The
        //      remembered SIZE is still applied via the width/height bindings.
        //   2. Remembered exact position (when enabled and previously saved) —
        //      honoured for the "primary"/"screen:N" modes.
        //   3. The chosen display, centred.
        var screens = Qt.application.screens
        var mode    = appSettings.mainDisplayMode

        if (mode === "cursor" && Qt.platform.pluginName.startsWith("wayland")) {
            // Wayland: QCursor::pos() is unknowable before the app has a
            // focused window, so cursorScreenIndex() resolves to the wrong
            // screen, x/y are ignored, and root.screen is ignored too (mutter
            // disregards the xdg set_fullscreen output hint for a window whose
            // FIRST map is fullscreen — verified empirically). The compositor
            // places normally-mapped windows on the monitor with the pointer,
            // which is exactly cursor mode: set nothing here, and let the
            // show sequence below map the window normal-first.
        } else if (mode === "cursor") {
            var ci   = appSettings.cursorScreenIndex()
            var cscr = screens[(ci >= 0 && ci < screens.length) ? ci : 0]
            root.screen = cscr
            // Centre on the cursor's screen (guarantees full visibility
            // regardless of how the remembered size compares to this monitor).
            root.x = cscr.virtualX + Math.round((cscr.width  - root.width)  / 2)
            root.y = cscr.virtualY + Math.round((cscr.height - root.height) / 2)
        } else if (appSettings.rememberWindowGeometry
                   && appSettings.windowX >= 0 && appSettings.windowY >= 0) {
            // Exact saved position — preserves which screen the user last used.
            root.x = appSettings.windowX
            root.y = appSettings.windowY
        } else {
            // No saved position: place on the screen chosen in Display settings.
            var target = screens[0]   // index 0 is always the primary screen in Qt
            if (mode.indexOf("screen:") === 0) {
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

        // Show the window only AFTER its geometry is set. The window is declared
        // visible:false above for this reason: on Windows a visible:true window
        // is created on the PRIMARY monitor first, and relocating it afterwards
        // (setScreen + x/y) is unreliable under per-monitor DPI — it snaps back
        // to primary, so "cursor"/"screen:N" placement was never honoured.
        // Creating it hidden and showing it once positioned makes Windows open
        // the native window on the correct monitor from the start.
        if (appSettings.windowMaximized) {
            if (mode === "cursor" && Qt.platform.pluginName.startsWith("wayland")) {
                // Map the window NORMAL first, then fullscreen it. mutter
                // places a first-map-fullscreen window on a monitor of its own
                // choosing, ignoring both the pointer and the requested output
                // (verified: it always picked the laptop panel). A normally
                // mapped window is placed on the monitor with the pointer, and
                // fullscreening an already-placed window keeps it there.
                root.visible = true
                root.showFullScreen()
            } else {
                root.showFullScreen()
            }
        } else {
            root.visible = true
        }
    }

    // NOTE: on Wayland the compositor owns window placement — Qt6 applications
    // cannot set their own position after showNormal().  No workaround exists.
    function toggleFullscreen() {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            root.showNormal()
        else
            root.showFullScreen()
    }

    // F11 everywhere; Ctrl+Cmd+F is the macOS convention (Ctrl = Meta in Qt key names on macOS)
    Shortcut { sequence: "F11";         onActivated: root.toggleFullscreen() }
    Shortcut { sequence: "Meta+Ctrl+F"; onActivated: root.toggleFullscreen() }

    // ESC skips the post-shot capture replay, or exits Review back to Capture —
    // inert otherwise (enabled gating keeps Esc free for popups/text fields).
    Shortcut {
        sequence: "Esc"
        enabled: shotProcessor.isReplaying || SessionMode.mode === SessionMode.replay
        onActivated: {
            if (shotProcessor.isReplaying) shotProcessor.cancelReplay()
            else SessionMode.enterCapture()
        }
    }

    // ── Close interception while a session is active ─────────────────────────
    // Covers the WM close button and the header ✕ (routed through root.close()
    // via PpHeader.closeRequested — Qt.quit() would bypass onClosing). Known
    // gap: macOS Cmd+Q / app-menu Quit doesn't pass through onClosing.
    property bool _quitConfirmed: false
    onClosing: (close) => {
        if (sessionController.running && !root._quitConfirmed) {
            close.accepted = false
            closeConfirm.open()
        }
    }

    Popup {
        id: closeConfirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        closePolicy: Popup.CloseOnEscape          // Esc = cancel (safe default)
        padding: Theme.sp(20)
        width: Math.min(Theme.sp(420), root.width - Theme.sp(48))

        background: Rectangle {
            color: Theme.colorSurface
            radius: Theme.radiusLg
            border.width: 1
            border.color: Theme.colorAttention    // attention framing — interrupts a live session
        }

        contentItem: Column {
            spacing: Theme.sp(12)

            Text {
                width: parent.width
                text: qsTr("Session in progress")
                font.family: Theme.fontDisplay
                font.italic: Theme.fontDisplayItalic
                font.weight: Theme.fontDisplayWeight
                font.pixelSize: Math.min(Theme.sp(20), Theme.fontSzDisplay)
                color: Theme.colorAttention
                wrapMode: Text.WordWrap
            }
            Text {
                width: parent.width
                text: {
                    var t = sessionController.activeSessionType
                    var name = (t >= 0 && t + 1 < root.screenNames.length)
                                   ? root.screenNames[t + 1] : qsTr("current")
                    return qsTr("You're still mid-session — closing now will end your %1 session and stop capture. Close PinPoint Studio?").arg(name)
                }
                font.family: Theme.fontBody
                font.weight: Theme.fontBodyWeight
                font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText2
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }

            Item { width: 1; height: Theme.sp(4) }

            Row {
                anchors.right: parent.right
                spacing: Theme.sp(8)

                // End session & close — attention-styled primary
                Rectangle {
                    width: confirmCloseLbl.implicitWidth + Theme.sp(24)
                    height: Theme.sp(32); radius: Theme.radius
                    color: confirmCloseMa.containsMouse ? Theme.colorAttentionLight : "transparent"
                    border.width: 1; border.color: Theme.colorAttention
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Text {
                        id: confirmCloseLbl
                        anchors.centerIn: parent
                        text: qsTr("End session & close")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorAttention
                    }
                    PpPressable {
                        id: confirmCloseMa
                        onClicked: {
                            closeConfirm.close()
                            // Session-level teardown; the deeper shutdown (swing-save
                            // wait, capture-thread barriers, merger stop) runs in the
                            // controller destructors and the aboutToQuit hook.
                            cameraManager.stopCapture()
                            sessionController.endSession()
                            root._quitConfirmed = true
                            root.close()
                        }
                    }
                }

                // Cancel — neutral; the close was already rejected
                Rectangle {
                    width: cancelCloseLbl.implicitWidth + Theme.sp(24)
                    height: Theme.sp(32); radius: Theme.radius
                    color: cancelCloseMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
                    border.width: 1; border.color: Theme.colorBorderMid
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Text {
                        id: cancelCloseLbl
                        anchors.centerIn: parent
                        text: qsTr("Cancel")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText2
                    }
                    PpPressable {
                        id: cancelCloseMa
                        onClicked: closeConfirm.close()
                    }
                }
            }
        }
    }

    // Commit any in-progress text-field edit before the screen changes.
    // StackLayout keeps all screens alive (not destroyed on switch), so items in
    // hidden screens retain activeFocus and onActiveFocusChanged never fires.
    // Forcing focus to the root window content item steals it from any TextField,
    // triggering the onActiveFocusChanged handler in PpTextField.qml.
    Connections {
        target: navController
        function onCurrentIndexChanged() {
            root.contentItem.forceActiveFocus()
            // Mode + loaded swing PERSIST across navigation: leaving the session
            // screen (e.g. to Settings) and returning lands you back in the same
            // mode on the same swing. Live-capture record state is untouched by
            // navigation, so it is preserved for free.
        }
    }

    // Data-source transition. Loading a past session lands you in Replay on that
    // session (Capture is live-only); returning to live drops to Capture. Both
    // entries reset the focused swing and stop any lingering disk replay.
    Connections {
        target: sessionReviewController
        function onReviewActiveChanged() {
            if (sessionReviewController.reviewActive) SessionMode.enterLoadedSession()
            else                                      SessionMode.enterCapture()
        }
    }

    // Instant playback: when a shot finishes processing it is promoted straight onto
    // the Replay stage (disk replay of the swing just written). Only auto-promote
    // from Capture — if the user is already in Replay/Analyse studying an earlier
    // shot, the new one just lands in the carousel rather than yanking them away.
    // The `true` marks this as the post-shot auto-replay so it returns to Capture
    // when it finishes (see onPlaybackEnded below).
    Connections {
        target: shotProcessor
        function onShotProcessed(shotId, swingDir) {
            if (swingDir !== "" && SessionMode.mode === SessionMode.capture)
                SessionMode.enterReplay(shotId, swingDir, true)
        }
    }

    // …and when that auto-replay plays to its end, drop straight back to live
    // Capture — the user is at the mat ready to hit again. Gated on
    // autoReturnToCapture so a user-initiated Replay reaching its end stays put;
    // the source reports the fact, SessionMode owns the policy.
    Connections {
        target: shotReplay
        function onPlaybackEnded() {
            if (SessionMode.autoReturnToCapture && SessionMode.mode === SessionMode.replay)
                SessionMode.enterCapture()
        }
    }

    // Named StackLayout/navController indices — keep in sync with screenNames
    // and the ScreenXxx order in contentStack below. Session screens sit at
    // sessionType + 1 (see SessionController::Type).
    readonly property int screenHome:       0
    readonly property int screenSwing:      1
    readonly property int screenWrist:      2
    readonly property int screenGrf:        3
    readonly property int screenCoach:      4
    readonly property int screenPlay:       5
    readonly property int screenNewAthlete: 6
    readonly property int screenAthletes:   7
    readonly property int screenSystem:     8
    readonly property int screenSettings:   9
    readonly property int screenWizard:     10

    // Maps StackLayout index → header screen name
    readonly property var screenNames: [
        qsTr("Home"),             // screenHome
        qsTr("Swing"),            // screenSwing
        qsTr("Wrist"),            // screenWrist
        qsTr("Ground forces"),    // screenGrf
        qsTr("Coach"),            // screenCoach
        qsTr("Developer"),        // screenPlay
        qsTr("New athlete"),      // screenNewAthlete
        qsTr("Athletes"),         // screenAthletes
        qsTr("System"),           // screenSystem
        qsTr("Settings"),         // screenSettings
        qsTr("New session")       // screenWizard
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PpRail {
            id: rail
            Layout.fillHeight: true
            // implicitWidth declared in PpRail.qml drives the column width
            currentPageIndex: navController.currentIndex
            // Lock all session and home buttons while the wizard is open so the
            // user stays in the setup flow. System and Settings remain accessible
            // and use navigate() (not navigateRail()) to preserve wizard in history.
            locked: navController.currentIndex === root.screenWizard
            // While a session is active, mute everything except the active
            // session type's button (System/Settings stay interactive).
            // NavigationController enforces the same rule on every nav path.
            sessionLockIndex: navController.sessionLocked
                                  ? sessionController.activeSessionType + 1 : -1

            onPageRequested: function(index) {
                if (navController.currentIndex === root.screenWizard)
                    navController.navigate(index)   // preserves wizard in back-history
                else
                    navController.navigateRail(index)
            }
            onAvatarClicked: {
                if (navController.currentIndex !== root.screenWizard)
                    navController.navigateRail(root.screenAthletes)
            }
        }

        ColumnLayout {
            Layout.fillWidth:  true
            Layout.fillHeight: true
            spacing: 0

            PpHeader {
                id: appHeader
                Layout.fillWidth: true
                screenName: navController.currentIndex === root.screenNewAthlete
                            ? (athleteFormScreen.editUuid === "" ? qsTr("New athlete")
                                                                 : qsTr("Edit athlete"))
                            : (navController.currentIndex < screenNames.length
                               ? screenNames[navController.currentIndex] : "")
                showVersionPill: navController.currentIndex === root.screenSettings
                // Session screens (Swing/Wrist/GRF/Coach) gate the centred DETECT
                // cluster the header hosts during live Capture.
                sessionScreenActive: navController.currentIndex >= root.screenSwing
                                     && navController.currentIndex <= root.screenCoach
                isFullscreen: root.visibility === Window.FullScreen
                onFullscreenToggleRequested: root.toggleFullscreen()
                // Route through window.close() so the session-active confirm
                // (onClosing) intercepts the in-app ✕ too.
                onCloseRequested: root.close()

                // When on the wizard, ‹/› navigate steps; otherwise delegate to
                // navController. Step skipping (Triangulate/Calibrate/Confirm)
                // lives in the wizard's goBack()/goNext() — single source of truth.
                backEnabled: navController.currentIndex === root.screenWizard
                                 ? (sessionWizard.currentStep > 0 || navController.canGoBack)
                                 : navController.canGoBack
                forwardEnabled: navController.currentIndex === root.screenWizard
                                    ? sessionWizard.currentStep < sessionWizard.lastStep
                                    : navController.canGoForward

                onBackRequested: {
                    if (navController.currentIndex === root.screenWizard && sessionWizard.currentStep > 0) {
                        sessionWizard.goBack()
                    } else {
                        // ‹ on the wizard's first step exits the wizard — that
                        // abandons setup just like Cancel, so release any
                        // devices the wizard connected before leaving.
                        if (navController.currentIndex === root.screenWizard)
                            sessionWizard.releaseDevices()
                        navController.back()
                    }
                }
                onForwardRequested: {
                    if (navController.currentIndex === root.screenWizard && sessionWizard.currentStep < sessionWizard.lastStep)
                        sessionWizard.goNext("done")
                    else
                        navController.forward()
                }
            }

            StackLayout {
                id: contentStack
                Layout.fillWidth:  true
                Layout.fillHeight: true
                currentIndex: navController.currentIndex

                ScreenHome {                                               // screenHome — home / default
                    id: screenHome
                    onAddAthleteRequested: {
                        athleteFormScreen.editUuid = ""
                        navController.navigate(root.screenNewAthlete)
                    }
                    onAthletePickerRequested: navController.navigate(root.screenAthletes)
                    // Tapping the home avatar edits the current athlete's profile.
                    onEditCurrentAthleteRequested: {
                        athleteFormScreen.editUuid = athleteController.currentUuid
                        athleteFormScreen.loadForEdit()   // force a fresh load even if uuid is unchanged
                        navController.navigate(root.screenNewAthlete)
                    }
                    onStartSessionRequested: function(sessionTypeIndex) {
                        // Carry the Home club pick into the session; start() (fired
                        // at wizard completion) preserves an already-set activeClub.
                        sessionController.activeClub = screenHome.selectedClub
                        sessionWizard.reset(sessionTypeIndex)
                        navController.navigate(root.screenWizard)
                    }
                    // Coming-soon types jump straight to their placeholder rail
                    // screen (rail/stack index = sessionType + 1).
                    onOpenSessionScreenRequested: function(sessionTypeIndex) {
                        navController.navigate(sessionTypeIndex + 1)
                    }
                }
                ScreenPlaceholder { iconText: "◑"; titleText: qsTr("Swing"); ambientBackground: true }      // screenSwing — coming soon
                ScreenWrist {}                                             // screenWrist — Wrist Motion (sessionType 1)
                ScreenPlaceholder { iconText: "⇅"; titleText: qsTr("GRF"); ambientBackground: true }        // screenGrf — coming soon
                ScreenPlaceholder { iconText: "✦"; titleText: qsTr("Coach"); ambientBackground: true }      // screenCoach — coming soon
                PlayPage {}                                                // screenPlay — dev-hatch only
                ScreenAthleteForm {                                        // screenNewAthlete — new athlete form
                    id: athleteFormScreen
                    // Dismiss (Cancel / close ✕) returns to the origin screen, the
                    // same target the header ‹ arrow reaches.
                    onCancelled: {
                        athleteFormScreen.editUuid = ""
                        navController.back()
                    }
                    onSaved: {
                        const wasEdit = athleteFormScreen.editUuid !== ""
                        athleteFormScreen.editUuid = ""
                        // Edit: return to the origin (home or picker). Create: land on
                        // the picker so the new athlete is visible and selected.
                        if (wasEdit) navController.back()
                        else         navController.navigate(root.screenAthletes)
                    }
                    onSavedAndStarted: { athleteFormScreen.editUuid = ""; navController.navigate(root.screenAthletes) }
                }
                ScreenAthletePicker {                                      // screenAthletes — athlete picker
                    onAthleteSelected:     navController.navigate(root.screenHome)
                    onNewAthleteRequested: { athleteFormScreen.editUuid = ""; navController.navigate(root.screenNewAthlete) }
                    onEditAthleteRequested: function(uuid) {
                        athleteFormScreen.editUuid = uuid
                        navController.navigate(root.screenNewAthlete)
                    }
                }
                ScreenResourceMonitor {                                    // screenSystem — system resource monitor
                    onNavigateToSettings: function(panelIndex) {
                        settingsScreen.activeNavIndex = panelIndex
                        navController.navigate(root.screenSettings)
                    }
                }
                ScreenSettings {                                           // screenSettings — settings
                    id: settingsScreen
                    onResourceMonitorRequested: navController.navigate(root.screenSystem)
                }
                ScreenSessionWizard {                                      // screenWizard — session setup wizard
                    id: sessionWizard
                    onCancelled: {
                        // Abandoning setup releases any devices the wizard
                        // connected (cameras, IMUs, microphone via capture
                        // intent) — same teardown as End Session.
                        sessionWizard.releaseDevices()
                        sessionController.activeClub = ""   // drop the un-started club pick
                        navController.navigate(root.screenHome)
                    }
                    onSessionStartRequested: function(type, goals) {
                        var map = appSettings.sessionGoalsByType
                        map[type.toString()] = goals
                        appSettings.sessionGoalsByType = map
                        appSettings.lastSessionType    = type
                        // Navigate first: once start(type) runs, navigation is
                        // locked to this very screen (plus System/Settings).
                        navController.navigateRail(sessionWizard.sessionTypes[type].railIndex)
                        sessionController.start(type)   // session active → rail locks
                        cameraManager.startCapture()    // buffer capturing → SHOT arms
                    }
                    onNavigateToSettings: function(panelIndex) {
                        settingsScreen.activeNavIndex = panelIndex
                        navController.navigate(root.screenSettings)
                    }
                    onCameraRecalibrateRequested: {
                        // TODO: navigate to the stereo calibration screen when the
                        // pipeline lands. When calibration completes, call
                        // sessionWizard.reopenAtTriangulate() and
                        // navController.navigate(root.screenWizard).
                    }
                }
            }
        }
    }

    // ── Shot-pipeline failure toasts ─────────────────────────────────────────
    // Window-level overlays so a failure surfaces on whatever screen is
    // active, instead of dying quietly in the message log. Two independent
    // toasts because the workers run in parallel and can both fail: save
    // failure loses data (error red); analysis failure degrades the shot to
    // no-score (warn amber) but the media still lands. The copy action
    // carries the full error for bug reports.
    PpToast {
        id: saveErrorToast
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - Theme.sp(24)
        z: 100
        glyph: "⚠"
        severity: "error"
        showUndo: false
    }
    PpToast {
        id: analysisErrorToast
        anchors.horizontalCenter: parent.horizontalCenter
        // Stacks above the save toast when both are visible.
        y: saveErrorToast.y - (saveErrorToast.visible ? height + Theme.sp(10) : 0)
        z: 100
        glyph: "⚠"
        severity: "warn"
        showUndo: false
    }
    Connections {
        target: shotProcessor
        function onSwingSaveFailed(error) {
            saveErrorToast.copyText = error
            saveErrorToast.show(qsTr("Swing save failed — %1").arg(error))
        }
        function onAnalysisFailed(error) {
            analysisErrorToast.copyText = error
            analysisErrorToast.show(qsTr("Shot analysis failed — %1").arg(error))
        }
    }

    // ── Linux in-app update banner (design §5 surface A) ─────────────────────
    // Non-modal, bottom-centred, stacked above the error toasts. Suppressed during
    // a session, for skipped versions, and on the wizard. Inert off-Linux.
    PpUpdateBanner {
        id: updateBanner
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - Theme.sp(96), Theme.sp(620))
        allowed: navController.currentIndex !== root.screenWizard
        y: (analysisErrorToast.visible ? analysisErrorToast.y
            : saveErrorToast.visible   ? saveErrorToast.y
            : parent.height) - height - Theme.sp(12)
        z: 100
    }

    // Shot-detected confirmation chime — the same C8 ting as IMU calibration,
    // played once per committed shot (any modality) during a session.
    TingPlayer { id: shotTing; frequency: 4186.0 }
    Connections {
        target: shotController
        function onShotDetected(source, timestampUs, sessionType) {
            shotTing.play()
        }
    }
}
