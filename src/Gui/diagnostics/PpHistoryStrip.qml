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

// HISTORYSTRIP — "Across sessions": per session, its shots as clean marks, the middle half as a bar and
// the median as a short rule, sessions in date order on the same value axis as the run.
// PUBLIC API (stable; internals belong to the fault-forward figure work):
//   history    detail.history  (columns[{label, values[{f, stack}], fQ1, fQ3, fMedian, current}])
//   scale      a PpFigureScale shared with the other panels of its PpFigureRow (required)
//   title, roles, brush, medium
//   signal sessionActivated(sessionDir)
Item {
    id: hs
    objectName: "sdHistoryStrip"
    property QtObject roles: PpDiagRoles {}           // the register passes its one PpDiagRoles; this default only covers creation order
    property var brush: null
    property string medium: roles ? roles.medium : "screen"
    property bool interactive: medium === "screen"
    property var history: null
    property var scale: null
    property string title: "Across sessions"
    readonly property var keyKinds: ["clean", "fault", "corridor"]
    signal sessionActivated(string sessionDir)
    readonly property var h: history || ({})
    readonly property var cols: h.columns || []
    implicitHeight: scale ? scale.plotTop + scale.plotHeight + roles.u(26) : 0

    Text { text: hs.title; font: hs.roles.fSmall; color: hs.roles.ink }
    PpZonePlot { anchors.fill: parent; roles: hs.roles; scale: hs.scale }
    Repeater {
        model: hs.roles ? (hs.scale ? hs.cols : []) : []
        delegate: Item {
            id: hc
            objectName: "sdHistoryColumn"
            required property var modelData
            required property int index
            readonly property bool current: !!modelData.current
            readonly property real cx: (index + 0.5) / hs.cols.length * hs.width
            Repeater {
                model: hs.roles ? (hc.modelData.values || []) : []
                delegate: PpMark {
                    required property var modelData
                    roles: hs.roles; fired: false
                    cx: Math.round((hc.cx - hs.roles.u(14) + (modelData.stack || 0) * hs.roles.u(6)) * 2) / 2; cy: Math.round(hs.scale.y(modelData.f) * 2) / 2
                }
            }
            Rectangle { x: hc.cx + hs.roles.u(8); width: Math.max(2, hs.roles.u(2)); y: hs.scale.y(hc.modelData.fQ3)
                        height: hs.scale.y(hc.modelData.fQ1) - hs.scale.y(hc.modelData.fQ3); color: hs.roles.ink2 }
            Rectangle { objectName: "sdHistoryMedian"; x: hc.cx + hs.roles.u(4); width: hs.roles.u(10); y: Math.round(hs.scale.y(hc.modelData.fMedian)); height: 1.5; color: hs.roles.ink }
            Text { text: hc.modelData.label; font: hs.roles.fFigLabel; color: hc.modelData.current ? hs.roles.ink : hs.roles.ink3
                   x: hc.cx - width / 2; y: hs.scale.plotTop + hs.scale.plotHeight + hs.roles.u(8) }
            MouseArea {
                enabled: hs.interactive && !hc.modelData.current; visible: enabled
                x: hc.cx - hs.roles.u(20); width: hs.roles.u(40); height: hs.height; cursorShape: Qt.PointingHandCursor
                onClicked: hs.sessionActivated(hc.modelData.sessionDir || "")
            }
        }
    }
}
