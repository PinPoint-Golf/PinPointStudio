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

// PPCHREADINGPICKER — choose a reading, IN PLACE (never a pop-up or a menu; design §2.3 "quiet Up / Across
// pickers that open in place"): a quiet question, then each family with its readings flowed as quiet links,
// the current one in ink. It pushes what is below it down and closes when the register sets `open` false (a
// pick, the control again). The catalogue is ChartsRelationsPresenter relations.catalogue.
// objectName: chReadingPicker; each reading chReadingOption (+ readingId).
Column {
    id: vp
    objectName: "chReadingPicker"
    property QtObject roles: null
    property bool open: false
    property string title: ""
    property string current: ""
    property string exclude: ""
    property var catalogue: []                   // [{ name, readings: [{ readingId, short, tag, text, label }] }]
    property bool interactive: true
    readonly property var keyKinds: []
    signal picked(string readingId)
    visible: open
    spacing: roles ? roles.u(4) : 4
    topPadding: roles ? roles.u(6) : 6; bottomPadding: roles ? roles.u(10) : 10
    Text { text: vp.title; font: vp.roles ? vp.roles.fLabel : Qt.font({}); color: vp.roles ? vp.roles.ink3 : "grey" }
    Repeater {
        model: vp.open && vp.roles ? vp.catalogue : []
        delegate: Row {
            required property var modelData
            width: vp.width; spacing: vp.roles.u(10)
            readonly property real nameW: Math.round(9 * vp.roles.em)
            Text { width: parent.nameW; text: modelData.name; font: vp.roles.fSmall; color: vp.roles.ink3; wrapMode: Text.WordWrap }
            Flow {
                width: vp.width - parent.nameW - vp.roles.u(10); spacing: vp.roles.u(10)
                Repeater {
                    model: modelData.readings.filter(function (r) { return r.readingId !== vp.exclude })
                    delegate: Text {
                        objectName: "chReadingOption"
                        required property var modelData
                        readonly property string readingId: modelData.readingId
                        readonly property bool on: modelData.readingId === vp.current
                        text: modelData.text
                        font: on || !vp.interactive ? vp.roles.fSmallM : vp.roles.fSmallL; color: on ? vp.roles.ink : vp.roles.link
                        MouseArea { anchors.fill: parent; anchors.margins: -2; enabled: vp.interactive; cursorShape: Qt.PointingHandCursor; onClicked: vp.picked(parent.readingId) }
                    }
                }
            }
        }
    }
}
