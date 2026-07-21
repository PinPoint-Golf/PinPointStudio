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

// Categorical-state chip — the approved way to render a bare categorical word
// (e.g. wrist "bowed", pattern "blended · bowed") on the dashboard: a bare word
// is a build error here, because colour + a dot are the channel that let a
// RAG-trained eye triage a state at a glance rather than reading text. `band`
// selects the RAG tint (green/yellow/red/none, same mapping everywhere);
// `caption` is an optional small label above (e.g. the metric name); `emphasis`
// sizes up for the Verdict zone's headline pattern read.

import QtQuick
import PinPointStudio

Item {
    id: root

    property string state_:   ""
    property string band:     ""
    property string caption:  ""
    property bool   emphasis: false

    // Same tiny local function every dashboard zone uses — keep it in sync.
    function _bandColor(b) {
        return b === "green"  ? Theme.colorRagGood
             : b === "yellow" ? Theme.colorRagWatch
             : b === "red"    ? Theme.colorRagFault
             :                  Theme.colorRagNone
    }

    // Dot/fill/border always read as "the band colour" (colorRagNone doubles as
    // the neutral/no-data tint). The word itself falls back to colorText2 when
    // band is unset so the state text stays legible rather than reading muted.
    readonly property color _bandColor_: _bandColor(root.band)
    readonly property color _textColor:  root.band.length > 0 ? _bandColor_ : Theme.colorText2

    readonly property int _padH: Theme.sp(10)
    readonly property int _padV: Theme.sp(6)

    implicitWidth:  col.implicitWidth
    implicitHeight: col.implicitHeight

    Column {
        id: col
        spacing: Theme.sp(3)

        Text {
            id: capText
            visible: root.caption.length > 0
            text: root.caption.toUpperCase()
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:              Theme.colorText3
        }

        Rectangle {
            id: chip
            implicitWidth:  chipRow.implicitWidth + root._padH * 2
            implicitHeight: chipRow.implicitHeight + root._padV * 2
            radius:         Theme.radius
            color:          Qt.alpha(root._bandColor_, 0.14)
            border.width:   Theme.borderWidth
            border.color:   Qt.alpha(root._bandColor_, 0.5)

            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: Theme.sp(6)

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width:  Theme.sp(6)
                    height: Theme.sp(6)
                    radius: width / 2
                    color:  root._bandColor_
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.state_.toUpperCase()
                    font.family:        Theme.fontData
                    font.pixelSize:     root.emphasis ? Theme.fontSzBody2 : Theme.fontSzLabel
                    font.letterSpacing: Theme.trackingMicro
                    color:              root._textColor
                }
            }
        }
    }
}
