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

    // Reflects contentStack.currentIndex in Main.qml
    property int  currentPageIndex: 0

    // When true, only System and Settings are interactive; all other buttons are muted.
    property bool locked: false

    // Active-session lock: the rail/stack index of the running session type
    // (-1 = no session). While set, every mode button except this index is
    // muted (System/Settings stay interactive). Visual affordance only — the
    // enforcement lives in NavigationController::blockedDuringSession().
    property int sessionLockIndex: -1

    // Shared mute rule for the mode buttons (Home/Swing/Wrist/GRF/Coach/Play).
    function _modeMuted(idx) {
        return locked || (sessionLockIndex >= 0 && sessionLockIndex !== idx)
    }

    // Emitted whenever a nav button is pressed; caller sets contentStack.currentIndex
    signal pageRequested(int index)

    // Emitted when the athlete avatar is clicked
    signal avatarClicked()

    // Emitted when the System button is clicked
    signal systemClicked()

    // Background — Instrument uses colorBg2; Studio and Editorial use colorBg
    Rectangle {
        anchors.fill: parent
        color: Theme.aesthetic === "instrument" ? Theme.colorBg2 : Theme.colorBg
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
            border.color: athleteController.hasCurrentAthlete
                              ? Theme.colorAccentMid : Theme.colorBorderStrong

            // Dashed border for the "no athlete" state — overlay a dotted Rectangle
            Rectangle {
                anchors.fill:  parent
                radius:        parent.radius
                color:         "transparent"
                border.width:  1
                border.color:  Theme.colorBorderStrong
                visible:       !athleteController.hasCurrentAthlete
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

            MouseArea {
                anchors.fill: parent
                // Athlete picker (index 7) is unreachable while the wizard is
                // open or a session is active.
                enabled:      !root.locked && root.sessionLockIndex < 0
                cursorShape:  enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked:    root.avatarClicked()
            }
        }

        // ── Divider ──────────────────────────────────────────────────────────
        Item { Layout.preferredHeight: Theme.sp(12); Layout.fillWidth: true }
        PpDivider {
            Layout.preferredWidth: Theme.sp(24)
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: Theme.sp(12); Layout.fillWidth: true }

        // ── Mode buttons (pages 0–4) ──────────────────────────────────────────

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⌂"
            labelText: qsTr("Home")
            isActive:  root.currentPageIndex === 0
            isMuted:   root._modeMuted(0)
            onClicked: root.pageRequested(0)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "◑"
            labelText: qsTr("Swing")
            isActive:  root.currentPageIndex === 1
            isMuted:   root._modeMuted(1)
            onClicked: root.pageRequested(1)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⌖"
            labelText: qsTr("Wrist")
            isActive:  root.currentPageIndex === 2
            isMuted:   root._modeMuted(2)
            onClicked: root.pageRequested(2)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⇅"
            labelText: qsTr("GRF")
            isActive:  root.currentPageIndex === 3
            isMuted:   root._modeMuted(3)
            onClicked: root.pageRequested(3)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "✦"
            labelText: qsTr("Coach")
            isActive:  root.currentPageIndex === 4
            isMuted:   root._modeMuted(4)
            onClicked: root.pageRequested(4)
        }

        // ── Spacer ────────────────────────────────────────────────────────────
        Item { Layout.fillHeight: true; Layout.fillWidth: true }

        // ── Bottom section: Play dev-hatch + Settings ─────────────────────────
        PpDivider {
            Layout.preferredWidth: Theme.sp(24)
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        // Play — dev hatch to existing app tabs (index 5); lights up when there
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "▶"
            labelText: qsTr("Play")
            isActive:  root.currentPageIndex === 5
            isMuted:   root._modeMuted(5)
            onClicked: root.pageRequested(5)
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        // System and Settings always active — allowed during wizard
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "◈"
            labelText: qsTr("System")
            isActive:  root.currentPageIndex === 8
            onClicked: root.systemClicked()
        }

        Item { Layout.preferredHeight: Theme.sp(8); Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⚙"
            labelText: qsTr("Settings")
            isActive:  root.currentPageIndex === 9
            onClicked: root.pageRequested(9)
        }

        Item { Layout.preferredHeight: Theme.sp(16); Layout.fillWidth: true }
    }
}
