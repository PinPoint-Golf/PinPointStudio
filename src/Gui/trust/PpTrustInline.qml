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
import PinPointStudio

// PPTRUSTINLINE — the in-place home of a reason when the Trust panel is off (TrustFocus): the same readings
// (PpTrustReading) expanded directly under the claim that was clicked, full width, until closed. A quiet
// left rule ties it to the claim above; "Close" and "Show in the Trust panel" sit on its head line.
// objectName: trustInline.
Item {
    id: ti
    objectName: "trustInline"
    property QtObject roles: null
    property var readings: []                    // [READING] to show (one, or a block's several)
    property string swingDir: ""
    signal closed()
    visible: (readings || []).length > 0
    width: parent ? parent.width : 0
    implicitHeight: visible ? body.height + (roles ? roles.u(10) : 10) : 0
    height: implicitHeight

    Rectangle { x: 0; y: 0; width: ti.roles ? ti.roles.u(2) : 2; height: body.height; color: ti.roles ? ti.roles.rule : "grey" }
    Column {
        id: body
        x: ti.roles ? ti.roles.u(12) : 12
        width: ti.width - x
        spacing: ti.roles ? ti.roles.u(8) : 8
        Row {
            spacing: ti.roles ? ti.roles.u(14) : 14
            Text { text: "Why"; font: ti.roles ? ti.roles.fSection : Qt.font({}); color: ti.roles ? ti.roles.ink : "black" }
            Text {
                objectName: "trustInlineToPanel"
                text: "Show in the Trust panel"
                font: ti.roles ? ti.roles.fSmallL : Qt.font({}); color: ti.roles ? ti.roles.link : "grey"
                MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor
                            onClicked: { ViewLayout.setPanel(SessionMode.mode, "trust", true)
                                         const r = (ti.readings || [])[0]
                                         Qt.callLater(function () { TrustFocus.open(r ? r.subject : "", ti.swingDir); ti.closed() }) } }
            }
            Text {
                objectName: "trustInlineClose"
                text: "Close"
                font: ti.roles ? ti.roles.fSmallL : Qt.font({}); color: ti.roles ? ti.roles.link : "grey"
                MouseArea { anchors.fill: parent; anchors.margins: -2; cursorShape: Qt.PointingHandCursor; onClicked: ti.closed() }
            }
        }
        Repeater {
            model: ti.readings || []
            delegate: PpTrustReading {
                required property var modelData
                width: body.width
                roles: ti.roles
                reading: modelData
                foldable: (ti.readings || []).length > 1
            }
        }
    }
}
