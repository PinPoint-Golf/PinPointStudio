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

// PpDashboardWindow — the wall-cast surface for the post-shot dashboard. A top-level
// window placed on the secondary display, hosting the SAME PpDashboardPanel as the
// in-app stage (bound to the shared shotReplay + per-type preset store, so a header
// edit updates both live). Kiosk = frameless full-screen; window = a centred overlay
// the presenter auto-closes after the dwell. postShotMirror horizontally flips the
// content for a coach standing opposite the athlete.

import QtQuick
import QtQuick.Window
import PinPointStudio

Window {
    id: win

    property int  sessionType: sessionController.activeSessionType
    property bool mirror: false
    property bool kiosk: false
    property var  targetScreen: null      // Qt.application.screens[i] (Screen info) or null

    // Screen metrics — the Qt.application.screens elements expose the Screen-info API
    // (width/height/virtualX/virtualY), NOT a QScreen `geometry` rect. Guard virtualX/Y
    // (older wrappers may omit them) so windowed centring never resolves to NaN.
    readonly property var  _scr: targetScreen ? targetScreen
                 : (Qt.application.screens.length > 0 ? Qt.application.screens[0] : null)
    readonly property real _sw: _scr ? _scr.width  : 1280
    readonly property real _sh: _scr ? _scr.height : 720
    readonly property real _sx: (_scr && _scr.virtualX !== undefined) ? _scr.virtualX : 0
    readonly property real _sy: (_scr && _scr.virtualY !== undefined) ? _scr.virtualY : 0

    // Placement is driven imperatively by Main.qml (_openCast), NOT via a
    // `visibility` binding, because a kiosk needs the map-normal-THEN-fullscreen
    // dance: a window whose FIRST map is fullscreen is placed by the compositor on
    // ITS choice of monitor (mutter ignores the output hint — see Main.qml note),
    // whereas fullscreening an already-mapped window keeps it where it landed. On
    // Wayland the compositor owns placement outright (it uses the pointer's
    // monitor); `screen` + x/y are best-effort hints honoured on X11.
    screen: targetScreen ? targetScreen : null
    color: Theme.colorBg
    title: qsTr("PinPoint — Post-shot")
    flags: kiosk ? (Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint) : Qt.Window

    // Windowed-mode geometry (kiosk fullscreen overrides these via showFullScreen()).
    width:  Math.round(_sw * 0.55)
    height: Math.round(_sh * 0.72)
    x: _sx + Math.round((_sw - width)  / 2)
    y: _sy + Math.round((_sh - height) / 2)

    // Mirror wrapper — flips horizontally about the centre when mirror is set.
    Item {
        anchors.fill: parent
        transform: Scale { origin.x: width / 2; xScale: win.mirror ? -1 : 1 }

        PpDashboardPanel {
            anchors.centerIn: parent
            width:  Math.min(parent.width - Theme.sp(64), Theme.sp(720))
            height: parent.height - Theme.sp(48)
            sessionType: win.sessionType
        }
    }
}
