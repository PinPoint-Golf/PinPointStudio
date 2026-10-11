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

// FOLIO — print furniture at the foot of a page: the page's key (only the marks this page uses, C), a
// hairline, a quiet footer (the final session state; generated <date>) and "Section · Page n of N".
Item {
    id: fo
    property QtObject roles: PpTypeset {}           // the register passes its one PpTypeset; this default only covers creation order
    property var readout: null
    property string footer: ""
    property string section: ""
    property int page: 1
    property int pageCount: 1
    property var keyKinds: []
    implicitHeight: col.height
    height: implicitHeight
    Column {
        id: col
        width: parent.width
        spacing: fo.roles.u(5)
        PpPageKey { visible: (fo.keyKinds || []).length > 0; width: parent.width; roles: fo.roles; readout: fo.readout; kinds: fo.keyKinds; spacing: fo.roles.u(10) }
        Rectangle { width: parent.width; height: fo.roles.hw; color: fo.roles.hair }
        Item {
            width: parent.width; height: ft.height
            Text { id: ft; width: parent.width - pn.width - fo.roles.u(20); wrapMode: Text.WordWrap
                   text: fo.footer + "  ·  generated " + Qt.formatDate(new Date(), "d MMM yyyy"); font: fo.roles.fMicro; color: fo.roles.ink3 }
            Text { id: pn; anchors.right: parent.right; anchors.baseline: ft.baseline
                   text: (fo.section ? fo.section + "    " : "") + "Page " + fo.page + " of " + fo.pageCount; font: fo.roles.fMicro; color: fo.roles.ink2 }
        }
    }
}
