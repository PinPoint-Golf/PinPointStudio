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

// RUNNINGHEAD — print furniture: "PinPoint Studio · Session diagnostics" on the left, golfer · date ·
// club · shot n of N on the right, a half-pixel rule under it. The PpPager draws it on every page.
Item {
    id: rh
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var source: null
    property var readout: null
    property string title: "PinPoint Studio  ·  Session diagnostics"
    readonly property var meta: roles.sessionMeta(source, readout, false)
    implicitHeight: lt.height + roles.u(6)
    height: implicitHeight
    Text { id: lt; text: rh.title; font: rh.roles.fMicro; color: rh.roles.ink2 }
    Text {
        anchors.right: parent.right; anchors.baseline: lt.baseline
        text: [rh.meta.golfer, rh.meta.date, rh.meta.club, rh.meta.shot].filter(s => !!s).join("  ·  ")
        font: rh.roles.fMicro; color: rh.roles.ink2
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: rh.roles.hw; color: rh.roles.rule }
}
