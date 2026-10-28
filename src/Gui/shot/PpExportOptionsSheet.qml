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

// Bulk-export options popover — opened from the shot carousel's ⋯ menu, and from the Sessions screen
// (one zip per session, PpSessionReportList). Lets the user pick which cameras' video to include and
// whether to bundle the JSON data, then emits confirmed() with the selection. The host calls
// swingExporter.exportShots() and shows the completion toast. thumb.jpg is always included; raw frames
// never are — so the only choices are cameras + JSON.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPointStudio

Popup {
    id: root

    // [{ file, alias }] from swingExporter.camerasForShots(...) — one checkbox each.
    property var  cameras: []
    // Absolute swing dirs to export — held for the host to read in onConfirmed.
    property var  swingDirs: []
    // Shot count, for the header caption only.
    property int  shotCount: 0
    // The header caption in place of "N shots · zipped to your home folder" (the Sessions screen's
    // "3 sessions, 87 swings · one zip each").
    property string caption: ""
    property string heading: qsTr("Export shots")
    // Initial JSON-toggle state; the live state resets from this on each open.
    property bool includeJson: true

    signal confirmed(var selectedVideoFiles, bool includeJson)
    signal cancelled()

    padding: Theme.gap(16)
    margins: Theme.gap(8)
    modal:   false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // file -> bool selection map, re-assigned wholesale so bindings re-evaluate;
    // and the live JSON-toggle state. Both reset from the public props on open.
    property var  _checked: ({})
    property bool _json:    true

    onAboutToShow: {
        var sel = {}
        for (var i = 0; i < cameras.length; ++i)
            sel[cameras[i].file] = true        // default: every camera on
        _checked = sel
        _json    = includeJson
    }

    function _toggle(file) {
        var c = Object.assign({}, _checked)
        c[file] = !c[file]
        _checked = c
    }
    function _selectedFiles() {
        var out = []
        for (var i = 0; i < cameras.length; ++i)
            if (_checked[cameras[i].file])
                out.push(cameras[i].file)
        return out
    }

    background: PpPopoverCard {}

    // The square checkbox rows are components/PpCheckRow.qml.

    contentItem: ColumnLayout {
        spacing: Theme.gap(14)

        // ── header ───────────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.gap(3)
            PpMicro { text: Theme.caps(root.heading) }
            Text {
                visible: root.shotCount > 0 || root.caption !== ""
                text:    root.caption !== "" ? root.caption
                                             : qsTr("%n shot(s) · zipped to your home folder", "", root.shotCount)
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.italic:    true
                color:          Theme.colorText3
            }
        }

        // ── cameras ───────────────────────────────────────────────────────────
        PpMicro { text: Theme.caps(qsTr("Cameras")) }
        Column {
            Layout.fillWidth: true
            spacing: Theme.gap(4)
            Repeater {
                model: root.cameras
                delegate: PpCheckRow {
                    required property var modelData
                    width:   parent.width
                    label:   modelData.alias
                    checked: root._checked[modelData.file] === true
                    onToggled: root._toggle(modelData.file)
                }
            }
            PpCardNote {   // no video tracks in the selected shots
                visible: root.cameras.length === 0
                text:    qsTr("No camera video in these shots")
            }
        }

        Rectangle {   // divider
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.colorBorder
        }

        // ── data ─────────────────────────────────────────────────────────────
        PpCheckRow {
            Layout.fillWidth: true
            label:   qsTr("Include JSON data")
            checked: root._json
            onToggled: root._json = !root._json
        }

        // ── footer ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.gap(4)
            spacing: Theme.gap(8)
            Item { Layout.fillWidth: true }
            PpButton {
                label: qsTr("Cancel")
                onClicked: { root.cancelled(); root.close() }
            }
            PpButton {
                label:   qsTr("Export")
                glyph:   "⤓"
                primary: true
                onClicked: {
                    root.confirmed(root._selectedFiles(), root._json)
                    root.close()
                }
            }
        }
    }
}
