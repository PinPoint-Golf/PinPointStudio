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
import PinPoint

Item {
    id: root

    // 60 % wider than the theme token; implicitWidth lets RowLayout size us correctly
    implicitWidth: Math.ceil(Theme.railWidth * 1.6)

    // Reflects contentStack.currentIndex in Main.qml
    property int currentPageIndex: 0

    // Emitted whenever a nav button is pressed; caller sets contentStack.currentIndex
    signal pageRequested(int index)

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
        Item { Layout.preferredHeight: 16; Layout.fillWidth: true }

        // ── Athlete avatar ───────────────────────────────────────────────────
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width:  32
            height: 32
            radius: 16
            color:  Theme.colorAccentLight
            border.width: 1
            border.color: Theme.colorAccentMid

            Text {
                anchors.centerIn: parent
                text:           "MC"
                font.family:    Theme.fontData
                font.pixelSize: 10
                color:          Theme.colorAccent
            }
        }

        // ── Divider ──────────────────────────────────────────────────────────
        Item { Layout.preferredHeight: 12; Layout.fillWidth: true }
        PpDivider {
            Layout.preferredWidth: 24
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: 12; Layout.fillWidth: true }

        // ── Mode buttons (pages 1–4) ──────────────────────────────────────────

        // Indices 1-4: mode placeholders; 5: Play dev-hatch (existing app tabs)
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "◑"
            labelText: "Swing"
            isActive:  root.currentPageIndex === 1
            onClicked: root.pageRequested(1)
        }

        Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⌖"
            labelText: "Wrist"
            isActive:  root.currentPageIndex === 2
            onClicked: root.pageRequested(2)
        }

        Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⇅"
            labelText: "GRF"
            isActive:  root.currentPageIndex === 3
            onClicked: root.pageRequested(3)
        }

        Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "✦"
            labelText: "Coach"
            isActive:  root.currentPageIndex === 4
            onClicked: root.pageRequested(4)
        }

        // ── Spacer ────────────────────────────────────────────────────────────
        Item { Layout.fillHeight: true; Layout.fillWidth: true }

        // ── Bottom section: Play dev-hatch + Settings ─────────────────────────
        PpDivider {
            Layout.preferredWidth: 24
            Layout.alignment: Qt.AlignHCenter
        }
        Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

        // Play — dev hatch to existing app tabs (index 5); lights up when there
        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "▶"
            labelText: "Play"
            isActive:  root.currentPageIndex === 5
            onClicked: root.pageRequested(5)
        }

        Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

        PpRailButton {
            Layout.alignment: Qt.AlignHCenter
            iconText:  "⚙"
            labelText: "Settings"
            isActive:  false
            onClicked: Theme.cycleTheme()
        }

        Item { Layout.preferredHeight: 16; Layout.fillWidth: true }
    }
}
