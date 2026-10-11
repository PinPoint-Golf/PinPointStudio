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

// STANDFIRST — B's head block: a kicker, a display head, a standfirst sentence built from the data,
// and (screen only) quiet filter links. size "head" for a view, "feature" for a masthead or a detail
// opening. The standfirst is held to the prose measure. The sentence comes from the panel's own helper
// (Session diagnostics: PpDiagRoles.shotStandfirst / sessionStandfirst / watchStandfirst(…, long));
// long = roles.shows("longStandfirst", width).
Column {
    id: sf
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property string kicker: ""
    property string head: ""
    property string standfirst: ""              // rich text
    property string size: "head"                // "head" | "feature"
    property var chips: []                      // [{ key, label, n, on }] — screen only
    property bool interactive: medium === "screen"
    property string section: ""                 // print: a standfirst that opens a section names it ("<section>, continued")
    property bool atom: true
    property bool keepNext: true
    readonly property var keyKinds: []
    signal chipActivated(string key)

    objectName: "sdStandfirst"
    spacing: roles.u(7)
    Text {
        visible: sf.kicker.length > 0
        width: parent.width; wrapMode: Text.Wrap
        text: sf.kicker; font: sf.roles.fKicker; color: sf.roles.ink3
    }
    Text {
        visible: sf.head.length > 0
        width: parent.width; wrapMode: Text.Wrap
        text: sf.head; font: sf.size === "feature" ? sf.roles.fFeature : sf.roles.fTitle
        color: sf.roles.ink; lineHeight: 1.08
    }
    Text {
        visible: sf.standfirst.length > 0
        width: sf.roles.measureW(parent.width); wrapMode: Text.Wrap; textFormat: Text.RichText
        topPadding: sf.roles.u(2)
        text: sf.standfirst; font: sf.roles.fStand; color: sf.roles.ink2; lineHeight: sf.roles.r.leading
    }
    Flow {
        visible: (sf.chips || []).length > 0 && sf.medium === "screen"
        width: parent.width; spacing: sf.roles.u(12); topPadding: sf.roles.u(2)
        Repeater {
            model: sf.roles ? (sf.chips || []) : []
            delegate: Text {
                required property var modelData
                objectName: "sdFilterTool"
                readonly property string key: modelData.key || ""
                readonly property bool on: !!modelData.on
                text: modelData.label + (modelData.n !== undefined ? " " + modelData.n : "")
                font: modelData.on ? sf.roles.fLabel : sf.roles.fLabelL
                color: modelData.on ? sf.roles.ink : sf.roles.link
                MouseArea { enabled: sf.interactive; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sf.chipActivated(parent.modelData.key) }
            }
        }
    }
}
