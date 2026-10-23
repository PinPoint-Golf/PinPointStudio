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

// PPCHPOSTUREMOVED — "WHAT MOVED" (docs/design/posture_chart_design.md §5.3): at the top and at impact, one line
// each, built by the core from the rows read (hips in, head up / down, bend lost or gained and what it went to,
// the trail leg's style once). Side by side when the width allows, stacked below that. The "when" ("At the top")
// seeks to that position. Every word is the presenter's MOVED ({ when, line, seekUs }).
// objectName: chPostureMoved; each line chPostureMovedLine (+ tag).
Item {
    id: mv
    objectName: "chPostureMoved"
    property QtObject roles: null
    property var moved: ({})                     // { top: MOVED, impact: MOVED }
    property bool interactive: true
    signal seek(real us)
    readonly property bool ok: !!roles
    readonly property var parts: ok ? [moved && moved.top ? moved.top : null, moved && moved.impact ? moved.impact : null]
                                      .filter(function (m) { return !!m && !!m.line }) : []
    readonly property real headW: ok && roles.atLeast(width, "medium") ? roles.u(150) : 0
    readonly property bool two: ok && roles.columns(width - headW, 22, roles.u(28)) >= 2
    readonly property real colW: two ? (width - headW - roles.u(28)) / 2 : width - headW
    visible: parts.length > 0
    height: !ok || parts.length === 0 ? 0 : Math.max(hd.height, grid.y + grid.height)

    Text { id: hd; visible: mv.ok; width: mv.headW > 0 ? mv.headW - (mv.ok ? mv.roles.u(10) : 10) : mv.width
           text: "What moved"; font: mv.ok ? mv.roles.fSection : Qt.font({}); color: mv.ok ? mv.roles.ink : "black" }
    Flow {
        id: grid
        x: mv.headW; y: mv.headW > 0 ? 0 : hd.height + (mv.ok ? mv.roles.u(6) : 6)
        width: mv.width - mv.headW
        spacing: mv.ok ? mv.roles.u(28) : 28
        Repeater {
            model: mv.parts
            delegate: Column {
                required property var modelData
                objectName: "chPostureMovedLine"
                readonly property string tag: modelData.tag || ""
                width: mv.colW
                spacing: mv.roles.u(3)
                Text { readonly property bool live: mv.interactive && modelData.seekUs >= 0
                       text: modelData.when || ""; font: live ? mv.roles.fSmallL : mv.roles.fSmallM; color: live ? mv.roles.link : mv.roles.ink2
                       MouseArea { anchors.fill: parent; enabled: parent.live; cursorShape: Qt.PointingHandCursor; onClicked: mv.seek(modelData.seekUs) } }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: modelData.line || ""; font: mv.roles.fBody; color: mv.roles.ink }
            }
        }
    }
}
