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

// DETAILBAR — Detail's chrome: "‹ Back" (never goes), the breadcrumb (from compact up), and the focus
// toggle on the right (its long form from compact up). Screen only. Wire backActivated → body._closeDetail(),
// focusActivated → body._declareFocus(id, !focused). Round 4: `interactive` (false: drawn, not pressed;
// the focus toggle then reads as a plain statement and Back stays — it never goes).
Item {
    id: db
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property string crumb: ""                    // the condition's name
    property bool focused: false
    property string backLabel: "Back"
    signal backActivated()
    signal focusActivated()
    readonly property var keyKinds: []
    implicitHeight: roles.u(30)
    height: implicitHeight
    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: db.roles.u(14)
        Text {
            id: bk
            objectName: "sdDetailBack"
            text: "‹ " + db.backLabel; font: db.roles.fBodyL; color: db.roles.link
            MouseArea { enabled: db.interactive; anchors.fill: parent; anchors.margins: -db.roles.u(4); cursorShape: Qt.PointingHandCursor; onClicked: db.backActivated() }
        }
        Text { objectName: "sdDetailTitle"
               visible: db.roles.shows("breadcrumb", db.width) && db.crumb.length > 0
               text: "Session diagnostics  ·  " + db.crumb; font: db.roles.fSmall; color: db.roles.ink3; anchors.baseline: bk.baseline }
    }
    Text {
        objectName: "sdDetailFocus"
        readonly property bool focused: db.focused
        visible: db.interactive || db.focused
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        text: db.focused ? (db.interactive ? "Focused · clear" : "Focused") : (db.roles.atLeast(db.width, "compact") ? "Focus on this" : "Focus")
        font: db.interactive ? db.roles.fSmallL : db.roles.fSmall; color: db.interactive ? db.roles.link : db.roles.ink2
        MouseArea { enabled: db.interactive; anchors.fill: parent; anchors.margins: -db.roles.u(4); cursorShape: Qt.PointingHandCursor; onClicked: db.focusActivated() }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: db.roles.hw; color: db.roles.hair }
}
