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

// A label and its value, in an opened row: a micro label in a fixed column, then the value's
// words. Marks placed inside it (pips, a key) go in a row before the words; beside them the label
// sits on their centre, otherwise on the words' first baseline. Empty, it takes no room.

import QtQuick
import PinPointStudio

Item {
    id: fact

    property string label: ""
    property string text:  ""
    property int    labelWidth: Theme.sp(84)
    default property alias extra: valueRow.data

    visible: fact.text !== "" || valueRow.children.length > 0
    height:  visible ? Math.max(factLabel.implicitHeight, factText.visible ? factText.implicitHeight : 0,
                                valueRow.implicitHeight) : 0

    PpMicro {
        id: factLabel
        anchors.baseline: fact.text !== "" ? factText.baseline : undefined
        y: Math.round((valueRow.implicitHeight - implicitHeight) / 2)
        text: fact.label
    }
    Row {
        id: valueRow
        x: fact.labelWidth
        spacing: Theme.gap(8)
    }
    Text {
        id: factText
        visible: fact.text !== ""
        x: fact.labelWidth + (valueRow.children.length > 0 ? valueRow.implicitWidth + Theme.gap(12) : 0)
        width: fact.width - x
        text:  fact.text
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        font.weight:    Theme.fontBodyWeight
        color:          Theme.colorText2
        wrapMode:       Text.WordWrap
        lineHeight:     1.35
    }
}
