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

// SPARKHEAD — the ONE quiet column head over a run of SparkRows (round 4; Replay asked for it so its rows
// need not mirror PpSparkRow's arithmetic): "pattern · shots 1–N · shot n · run", each label over its column,
// from the same roles.sparkGeo() the rows use. Give it the rows' width and the session's shot count.
// The labels come from the data: N = shots, n = the picked shot's ORDINAL (roles.shotNo).
Item {
    id: sh
    objectName: "sdSparkHead"
    property QtObject roles: PpDiagRoles {}
    property var readout: null
    property int shots: readout ? readout.shotCount : 0
    property bool showSparkline: roles.shows(2, width)
    property string nameLabel: "pattern"
    property string runLabel: "run"
    property bool atom: true
    property bool keepNext: true
    readonly property var keyKinds: []
    readonly property var geo: roles.sparkGeo(width, shots, showSparkline)
    implicitHeight: roles.u(18)
    height: implicitHeight
    Text { anchors.bottom: parent.bottom; anchors.bottomMargin: sh.roles.u(3)
           text: sh.nameLabel; font: sh.roles.fLabel; color: sh.roles.ink3 }
    Text { x: sh.geo.stripX; width: sh.geo.stripW + sh.geo.gap; anchors.bottom: parent.bottom; anchors.bottomMargin: sh.roles.u(3)
           text: sh.shots > 0 ? "shots 1–" + sh.shots : "shots"; font: sh.roles.fLabel; color: sh.roles.ink3 }
    Text { x: sh.geo.valX - sh.geo.gap; width: sh.geo.gap + sh.geo.valW; horizontalAlignment: Text.AlignRight
           anchors.bottom: parent.bottom; anchors.bottomMargin: sh.roles.u(3)
           text: sh.readout ? "shot " + sh.roles.shotNo(sh.readout) : "this shot"; font: sh.roles.fLabel; color: sh.roles.ink3 }
    Text { visible: sh.geo.spark; x: sh.geo.skX; anchors.bottom: parent.bottom; anchors.bottomMargin: sh.roles.u(3)
           text: sh.runLabel; font: sh.roles.fLabel; color: sh.roles.ink3 }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: sh.roles.hw; color: sh.roles.rule }
}
