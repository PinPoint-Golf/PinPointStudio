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

// One searchable setting in a settings panel: its content stacked in a column, and the search
// highlight drawn BEHIND it rather than inside the column — a layout would give the highlight a slot
// of its own (and its spacing), so a highlight Rectangle dropped into a RowLayout row is laid out as
// one more cell, not drawn behind the row.
//
// THE SEARCH CONTRACT. SettingsIndex lists each setting by its `objectName` ("setting_archiveAge");
// SettingsPanelBase.scrollToItem finds the item with that objectName, scrolls to it and sets its
// `searchHighlight` for 1.8 s. So the objectName goes on the SettingsRow itself, and the row is what
// lights up:
//
//     SettingsRow {                                  // a title over a field: stacked
//         objectName: "setting_archiveLocation"
//         SettingsRowTitle { title: qsTr("Folder"); subtitle: qsTr("…") }
//         PpTextField { Layout.fillWidth: true; … }
//     }
//
//     SettingsRow {                                  // a title beside its control: one RowLayout inside
//         objectName: "setting_archiveKeepRaw"
//         RowLayout {
//             Layout.fillWidth: true
//             spacing: Theme.gap(16)
//             SettingsRowTitle { title: qsTr("Keep raw sensor frames in the archive"); subtitle: qsTr("…") }
//             PpSwitch { Layout.alignment: Qt.AlignVCenter; checked: …; onToggled: (v) => … }
//         }
//     }
//
// `spacing` is the gap between the stacked children (gap(6) unless set). Extra properties and
// functions a row needs (a commit(), an error string) can be declared on the SettingsRow as usual.
// The row is Layout.fillWidth, so it spans the panel's content column.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: sr

    property bool searchHighlight: false
    property real spacing: Theme.gap(6)
    default property alias content: srCol.data

    Layout.fillWidth: true
    implicitWidth: srCol.implicitWidth
    implicitHeight: srCol.implicitHeight

    Rectangle {
        x: -Theme.sp(6); y: -Theme.sp(6)
        width: sr.width + Theme.sp(12); height: sr.height + Theme.sp(12)
        color: Theme.colorAccentLight; radius: Theme.radius
        opacity: sr.searchHighlight ? 1.0 : 0.0
    }
    ColumnLayout { id: srCol; width: sr.width; spacing: sr.spacing }
}
