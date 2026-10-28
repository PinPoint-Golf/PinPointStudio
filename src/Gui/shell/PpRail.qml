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
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // 60 % wider than the theme token; implicitWidth lets RowLayout size us correctly
    implicitWidth: Math.ceil(Theme.railWidth * 1.6)

    // Reflects contentStack.currentIndex in Main.qml (an AppScreen value)
    property int  currentPageIndex: AppScreen.Home

    // Wizard lock: while true every destination above the spacer is muted; Settings stays
    // interactive (System sits beneath it).
    property bool locked: false

    // Shared mute rule for the destinations above the spacer.
    function _muted(screen) { return locked }

    // True while the wizard locks the rail — i.e. some buttons are muted. Enabled buttons tint
    // amber (attention) in this state and revert to the standard scheme once everything is
    // interactive.
    readonly property bool anyLocked: locked

    // Emitted whenever a nav button is pressed; caller sets contentStack.currentIndex
    signal pageRequested(int index)

    // Emitted when the athlete avatar is clicked
    signal avatarClicked()

    // Background — the theme's rail ground (Instrument's colorBg2, colorBg elsewhere)
    Rectangle {
        anchors.fill: parent
        color: Theme.colorRail
    }

    // Right-edge hairline separator
    Rectangle {
        anchors.right:  parent.right
        anchors.top:    parent.top
        anchors.bottom: parent.bottom
        width:   1
        color:   Theme.colorBorderMid
        opacity: Theme.borderOpacityNormal
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Top padding ──────────────────────────────────────────────────────
        Item { Layout.preferredHeight: Theme.sp(16); Layout.fillWidth: true }

        // ── Athlete avatar ───────────────────────────────────────────────────
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width:  Theme.sp(32)
            height: Theme.sp(32)
            radius: Theme.sp(16)
            color:  athleteController.hasCurrentAthlete
                        ? Theme.colorAccentLight : "transparent"
            border.width: athleteController.hasCurrentAthlete ? 1 : 1
            // Brighten the border on hover, matching the home-screen avatar.
            border.color: athleteController.hasCurrentAthlete
                              ? (railAvatarPress.containsMouse ? Theme.colorAccent
                                                              : Theme.colorAccentMid)
                              : (railAvatarPress.containsMouse ? Theme.colorText3
                                                              : Theme.colorBorderStrong)
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

            // Dashed border for the "no athlete" state — overlay a dotted Rectangle
            Rectangle {
                anchors.fill:  parent
                radius:        parent.radius
                color:         "transparent"
                border.width:  1
                border.color:  railAvatarPress.containsMouse ? Theme.colorText3
                                                            : Theme.colorBorderStrong
                visible:       !athleteController.hasCurrentAthlete
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                // QML doesn't support dashed borders natively; solid low-opacity suffices
            }

            Text {
                anchors.centerIn: parent
                text:           athleteController.hasCurrentAthlete
                                    ? athleteController.currentInitials : "＋"
                font.family:    Theme.fontData
                font.pixelSize: Theme.sp(10)
                color:          athleteController.hasCurrentAthlete
                                    ? Theme.colorAccent : Theme.colorText3
            }

            PpPressable {
                // Athlete picker (AppScreen.Athletes) is unreachable while the
                // wizard is open. A running session locks nothing here: the picker
                // itself refuses to switch golfer until it ends.
                id:             railAvatarPress
                enabled:        !root.locked
                confirmOnClick: true
                onClicked:      root.avatarClicked()
            }
        }

        // ── Divider ──────────────────────────────────────────────────────────
        Item { Layout.preferredHeight: Theme.sp(12); Layout.fillWidth: true }
        PpDivider {
            Layout.preferredWidth: Theme.sp(24)
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: Theme.sp(12); Layout.fillWidth: true }

        // ── Destinations ──────────────────────────────────────────────────────
        // No mode buttons: one session screen, the rail's Live, always there
        // (docs/design/ux_refactor_away_from_4_modes.md §2.2). Each button is active on its own
        // screen. No Work-ons button: Home's tabs carry the faults, each opening in place (Mark,
        // 28 Oct). The Swing model has no button either: it is Settings → Reference.

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⌂"
            labelText: qsTr("Home")
            isActive:  root.currentPageIndex === AppScreen.Home
            isMuted:   root._muted(AppScreen.Home)
            attention: root.anyLocked
            onClicked: root.pageRequested(AppScreen.Home)
        }

        // Live — the session screen, always available: the running session, a past one opened
        // for review, or the cameras before a session starts.
        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "▶"
            labelText: qsTr("Live")
            isActive:  root.currentPageIndex === AppScreen.Session
            isMuted:   root._muted(AppScreen.Session)
            attention: root.anyLocked
            onClicked: root.pageRequested(AppScreen.Session)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        // Sessions — the current golfer's sessions. Muted with no golfer, as the avatar's edit is:
        // with no athlete the form would open as a blank "New athlete" form.
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "◑"
            labelText: qsTr("Sessions")
            isActive:  root.currentPageIndex === AppScreen.Sessions
            isMuted:   root._muted(AppScreen.Sessions) || !athleteController.hasCurrentAthlete
            attention: root.anyLocked
            onClicked: root.pageRequested(AppScreen.Sessions)
        }


        // ── Spacer ────────────────────────────────────────────────────────────
        Item { Layout.fillHeight: true; Layout.fillWidth: true }

        // ── Bottom section: Settings ──────────────────────────────────────────
        PpDivider {
            Layout.preferredWidth: Theme.sp(24)
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        // Settings always active — allowed during the wizard. Stays
        // highlighted on the Developer and System (resource monitor) screens, which
        // live under Settings.
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⚙"
            labelText: qsTr("Settings")
            isActive:  root.currentPageIndex === AppScreen.Settings
                       || root.currentPageIndex === AppScreen.System
                       || root.currentPageIndex === AppScreen.Developer
            attention: root.anyLocked
            onClicked: root.pageRequested(AppScreen.Settings)
        }

        Item { Layout.preferredHeight: Theme.sp(16); Layout.fillWidth: true }
    }
}
