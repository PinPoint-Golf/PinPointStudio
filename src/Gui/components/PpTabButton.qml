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

// One tab of a stock TabBar, drawn the app's way: body-face label, text when checked and text3
// otherwise. Two looks, the two tab bars the app has:
//   segment    the checked tab sits on a bg3 fill inside the bar's own frame (the IMU lab's tabs)
//   underline  bg2 tabs, the checked one on bg with an accent underline (the lab pages' top bar)
//
//   TabBar { PpTabButton { text: qsTr("Viz") }  PpTabButton { text: qsTr("Log") } }

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

TabButton {
    id: tab

    property string style: "segment"    // "segment" | "underline"

    contentItem: Text {
        text:                tab.text
        color:               tab.checked ? Theme.colorText : Theme.colorText3
        font.family:         Theme.fontBody
        font.pixelSize:      Theme.fontSzBody
        font.weight:         Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment:   Text.AlignVCenter
    }
    background: Rectangle {
        color:  tab.style === "underline" ? (tab.checked ? Theme.colorBg : Theme.colorBg2)
                                          : (tab.checked ? Theme.colorBg3 : "transparent")
        radius: tab.style === "underline" ? 0 : Theme.radius - 1
        Rectangle {
            visible: tab.style === "underline"
            anchors.bottom: parent.bottom
            width:  parent.width
            height: 2
            color:  tab.checked ? Theme.colorAccent : "transparent"
        }
    }
}
