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

// What recorded a session — one PpChip per device, grey outlines, the ones the device filter asked for
// (PpDeviceFilterBar) in the accent. A row of any session list: the carousel's drawer, the golfer's
// sessions on Home and the golfer page.

import QtQuick
import PinPointStudio

Row {
    id: tags
    objectName: "deviceTags"

    property var tokens:   []      // session_summary.h device tokens, display order
    property var labels:   []      // their short names, same order
    property var selected: []      // the filter's tokens

    visible: !!labels && labels.length > 0
    height: Theme.sp(18)
    spacing: Theme.gap(4)

    Repeater {
        model: tags.labels
        PpChip {
            required property string modelData
            required property int    index
            text:   modelData
            tinted: !!tags.selected && !!tags.tokens && tags.selected.indexOf(tags.tokens[index]) >= 0
            tone:   tinted ? Theme.colorAccent : Theme.colorText2
        }
    }
}
