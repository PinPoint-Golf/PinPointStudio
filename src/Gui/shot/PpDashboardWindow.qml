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

    // True only for the TEMPORARY post-shot pop that closes itself after the dwell
    // (postShotDisplayMode == "window"). Set by Main.qml at creation.
    property bool autoClose: false

    // The interactive layer is off for exactly one case: an auto-closing cast. That
    // surface is a glance — arming hover/seek on something about to vanish under the
    // pointer is a trap. PANEL and KIOSK are PERSISTENT casts: they sit there for the
    // whole session and are the surface a coach actually works against, so they get
    // the full layer. (An earlier cut gated on `kiosk`, which silently disabled every
    // affordance on the most-used configuration.)
    readonly property bool interactive: !autoClose

    // Held open while the presenter is reading (pointer over the panel) or has
    // pinned it. Main.qml's castDwellTimer re-arms instead of closing while true.
    property bool pinned: false
    readonly property bool dwellHeld: pinned || dash.hovered

    // Viewing-distance zoom. Rather than a second set of "big" dimensions (which
    // would fork the component and break the one-render guardrail), the panel is
    // laid out at 1/contentScale of the window and then scaled up: every token —
    // type, tile, corridor, dot — grows together, so the proportions are identical
    // at every setting. Text stays crisp through Qt's distance-field rasteriser.
    //
    // The MAGNITUDE is a user preference, not a constant: a 13" laptop mirrored at
    // arm's length and a 65" TV across a bay want very different type, and there is
    // no correct answer to pick for them. appSettings.dashboardScale chooses.
    // Kiosk (the true wall case) is stepped up relative to a framed desk window.
    readonly property real _scaleStep: appSettings.dashboardScale === "small"  ? 0.78
                                     : appSettings.dashboardScale === "large"  ? 1.35
                                     :                                           1.0
    property real contentScale: (kiosk ? 1.45 : 1.0) * _scaleStep

    // Mirror wrapper — flips horizontally about the centre when mirror is set.
    Item {
        anchors.fill: parent
        transform: Scale { origin.x: width / 2; xScale: win.mirror ? -1 : 1 }

        // A dashboard should FILL its display — the old centred 720px column left
        // most of a 2560-wide wall empty. The inset is now just enough to keep the
        // card borders off the bezel.
        //
        // The panel itself is NOT transformed: it applies `uiScale` internally, to
        // the zone content only. Its header — the preset pill and its popover —
        // therefore renders at native size, identical to the session toolbar,
        // while the data below zooms for the room.
        PpDashboardPanel {
            id: dash
            x: Theme.sp(8); y: Theme.sp(8)
            width:  parent.width  - Theme.sp(16)
            height: parent.height - Theme.sp(16)
            uiScale: win.contentScale
            sessionType: win.sessionType
            interactive: win.interactive
        }
    }

    // Pin — freezes the auto-close outright. Chromeless until hovered (or pinned) so
    // it costs the resting wall render nothing.
    Rectangle {
        visible: win.interactive
        anchors { right: parent.right; top: parent.top
                  rightMargin: Theme.sp(12); topMargin: Theme.sp(12) }
        implicitWidth: pinTxt.implicitWidth + Theme.sp(16)
        implicitHeight: Theme.sp(24)
        radius: Theme.radius
        color: win.pinned ? Theme.colorBg3 : "transparent"
        border.width: Theme.borderWidth
        border.color: win.pinned ? Theme.colorBorderMid
                    : (pinHover.hovered ? Theme.colorBorder : "transparent")
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        Text {
            id: pinTxt
            anchors.centerIn: parent
            text: win.pinned ? qsTr("PINNED") : qsTr("PIN")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color: win.pinned ? Theme.colorText2
                 : (pinHover.hovered ? Theme.colorText3 : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        HoverHandler { id: pinHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: win.pinned = !win.pinned }
    }
}
