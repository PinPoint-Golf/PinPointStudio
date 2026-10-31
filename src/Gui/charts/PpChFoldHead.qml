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

// PPCHFOLDHEAD — a crosshead that folds its section (CONTRACT Addendum: "Collapsible sections keep their saved
// state"; today's CONTROLS / SUMMARY / SEQUENCE folds, persisted per screen × mode in AppSettings
// sectionCollapse by the panel). Quiet (a PpSectionHead): the section's title in the section role, an ink3 caret,
// and while folded a one-line `state` so nothing it held is silently gone (simplify = quieten). Not foldable in print.
// objectName: chFold:<key>.
Item {
    id: fh
    property QtObject roles: null
    property string key: ""
    property string title: ""
    property string sub: ""
    property string state_: ""                   // what the folded section still says, on its head line
    property bool collapsed: false
    property bool foldable: true
    property bool interactive: true
    readonly property var keyKinds: []
    property bool atom: true
    property bool keepNext: true
    signal toggled()
    objectName: "chFold:" + key
    implicitHeight: roles ? Math.max(roles.u(30), fl.height + roles.u(16)) : 30
    height: implicitHeight
    PpSectionHead {
        id: fl
        y: fh.roles ? fh.roles.u(12) : 12
        width: fh.width
        roles: fh.roles; paged: false; pad: 0; gap: 8
        foldable: fh.foldable && fh.interactive; folded: fh.collapsed
        title: fh.title
        sub: fh.collapsed ? "" : fh.sub; subWrap: false
        note: fh.collapsed ? fh.state_ : ""
    }
    MouseArea { anchors.fill: parent; enabled: fh.foldable && fh.interactive; cursorShape: Qt.PointingHandCursor; onClicked: fh.toggled() }
}
