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

// A notice: words in a tone, on the tone's wash inside a hairline in the tone — "the raw frames
// are big", "everything's ready", "release on open canvas". `tone` is a Theme tone name (warn,
// good, error, accent, …). Anything declared inside it sits on top (a PpPressable, to make the
// notice a way in).
//
// `style` keeps each look in use, until the restyle (#131) merges them:
//   warning     ⚠ and a caps `title` over body words in colorText2, hairline at 40 %   Home (resources)
//   strip       mono micro words in the tone, full width, 10 px over the words          Settings › Storage
//   para        body words in the tone, 1.5 line height, 12 px all round               setup Ready
//   bubble      body words in the tone, centred, sized to them up to `maxWidth`        model graph (drag)
//   bubbleSlim  bubble, 2 px shorter                                                   model graph (link hint)
//   chip        26 px, micro body words in the tone, no hairline                       model editor (validation)
// strip and para take their width from the caller and claim none of their own.

import QtQuick
import PinPointStudio

Rectangle {
    id: notice

    property string tone:  "warn"
    property string text:  ""
    property string style: "strip"
    property string title: Theme.caps(qsTr("Warning"))       // warning: the eyebrow over the words
    property real   maxWidth: Number.POSITIVE_INFINITY       // bubble: never wider than this

    readonly property color toneColor: Theme.toneColor(tone)
    readonly property bool  _warning: style === "warning"
    readonly property bool  _top:     style === "strip" || style === "para"
    readonly property bool  _bubble:  style === "bubble" || style === "bubbleSlim"

    implicitWidth:  _warning ? warnRow.implicitWidth + Theme.sp(28)
                  : _bubble  ? Math.min(centred.implicitWidth + Theme.sp(24), maxWidth)
                  : _top     ? 0
                  :            centred.implicitWidth + Theme.sp(18)
    implicitHeight: _warning ? warnRow.implicitHeight + Theme.sp(22)
                  : _top     ? words.implicitHeight + Theme.sp(20)
                  : _bubble  ? centred.implicitHeight + Theme.sp(style === "bubbleSlim" ? 14 : 16)
                  :            Theme.sp(26)
    radius:       Theme.radius
    color:        Theme.toneLight(tone)
    border.width: style === "chip" ? 0 : 1
    border.color: _warning ? Qt.alpha(notice.toneColor, 0.4) : notice.toneColor

    // warning: the glyph, then the eyebrow over the words.
    Row {
        id: warnRow
        visible: notice._warning
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.margins: Theme.gap(11)
        anchors.topMargin: Theme.gap(11)
        spacing: Theme.gap(10)

        Text {
            text: "⚠"
            font.pixelSize: Theme.sp(14)
            color: notice.toneColor
            anchors.top: parent.top
        }

        Column {
            spacing: Theme.gap(3)
            width: parent.width - Theme.sp(24)

            PpMicro {
                text: notice.title
                color: notice.toneColor
            }

            Text {
                text: notice.text
                wrapMode: Text.WordWrap
                width: parent.width
                font.family: Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight: Theme.fontBodyWeight
                color: Theme.colorText2
            }
        }
    }

    // strip, para: the words across the top, the box as tall as they are.
    Text {
        id: words
        visible: notice._top
        anchors {
            left: parent.left; right: parent.right; top: parent.top
            leftMargin: Theme.gap(12); rightMargin: Theme.gap(12)
            topMargin: Theme.gap(notice.style === "para" ? 12 : 10)
        }
        text:           notice._top ? notice.text : ""
        font.family:    notice.style === "para" ? Theme.fontBody : Theme.fontData
        font.weight:    notice.style === "para" ? Theme.fontBodyWeight : Font.Normal
        font.pixelSize: notice.style === "para" ? Theme.fontSzBody2 : Theme.fontSzMicro
        lineHeight:     notice.style === "para" ? 1.5 : 1.0
        color:          notice.toneColor
        wrapMode:       Text.WordWrap
    }

    // bubble, chip: the words in the middle, the box sized to them.
    Text {
        id: centred
        visible: !notice._warning && !notice._top
        anchors.centerIn: parent
        width:    notice._bubble ? parent.width - Theme.sp(24) : implicitWidth
        text:     !notice._warning && !notice._top ? notice.text : ""
        font.family:    Theme.fontBody
        font.weight:    notice.style === "chip" ? Theme.fontBodyWeight : Font.Normal
        font.pixelSize: notice.style === "chip" ? Theme.fontSzMicro : Theme.fontSzBody2
        color:          notice.toneColor
        wrapMode:       notice._bubble ? Text.WordWrap : Text.NoWrap
        horizontalAlignment: notice._bubble ? Text.AlignHCenter : Text.AlignLeft
    }
}
