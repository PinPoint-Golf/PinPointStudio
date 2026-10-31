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

// PPVIEWFLICK — one register view's own named Flickable (D8, §0.2-0.3), shared by the Charts register and the
// Session diagnostics Analyse and Replay registers. It hangs under `below` (the tab row, or what opens under it)
// and fills the rest of its parent. The view (a Component) is built on first show — the first time the register's
// `shown` tab is this flick's `key` — and never torn down, so scroll and hover survive a republish and the panel
// can save and restore its place by the flick's objectName.
// A register specialises it once (its roles, its shown tab, its own measures in u: `gap` above the view, `foot`
// of room after it) and names one per view. `scrolls` false takes the drag away (the auto-closing cast);
// `lockWhenFits` false leaves a view that fits still taking the drag (Analyse).
Flickable {
    id: vf
    property QtObject roles: null
    property string key: ""
    property Component view: null
    property string shown: ""
    property Item below: null
    property real gap: 0
    property real foot: 0
    property bool scrolls: true
    property bool lockWhenFits: true
    readonly property bool on: shown === key
    readonly property Item content: ld.item
    property bool built: false
    onOnChanged: if (on) built = true
    Component.onCompleted: if (on) built = true
    function _u(n) { return roles ? roles.u(n) : n }
    visible: on
    anchors { top: below ? below.bottom : undefined; left: parent ? parent.left : undefined; right: parent ? parent.right : undefined
              bottom: parent ? parent.bottom : undefined; topMargin: _u(gap) }
    contentWidth: width; contentHeight: (ld.item ? ld.item.height : 0) + _u(foot)
    clip: true; boundsBehavior: Flickable.StopAtBounds
    interactive: scrolls && (!lockWhenFits || contentHeight > height)
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    Loader { id: ld; active: vf.built; width: vf.width - vf._u(8); sourceComponent: vf.view }
}
