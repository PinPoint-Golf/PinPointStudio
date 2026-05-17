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
    width: 800
    height: 700
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
            onPageRequested: function(index) { contentStack.currentIndex = index }
        }

        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            ScreenWelcome   {}                                         // 0 — opening / default
            ScreenPlaceholder { iconText: "◑"; titleText: "Swing"  }  // 1
            ScreenPlaceholder { iconText: "⌖"; titleText: "Wrist"  }  // 2
            ScreenPlaceholder { iconText: "⇅"; titleText: "GRF"    }  // 3
            ScreenPlaceholder { iconText: "✦"; titleText: "Coach"  }  // 4
            PlayPage {}                                                // 5 — Play dev-hatch only
        }
    }
}
