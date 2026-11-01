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

// The notice card both notification surfaces draw: the window's notification centre
// (PpNotificationHost, one per live notice) and a panel's own toast (PpToast). The elevated fill
// with a hairline in the notice's tone, a glyph, the words, and the actions after hairlines —
// copy (a ⧉ that flashes a ✓), an action in the label face, a ✕ to dismiss. It only draws and
// reports: who shows it, for how long and what an action does stay with the host.
//
//   ""     the centre's: the glyph set apart from a title with an optional quieter detail line
//          under it; grows to fit the two lines, never shorter than 40 px
//   toast  the glyph tight against one line of words; always 40 px tall

import QtQuick
import PinPointStudio

Rectangle {
    id: card

    property string style:       ""
    property string glyph:       ""
    property color  glyphColor:  Theme.colorText2
    property color  tone:        Theme.colorBorderStrong   // the hairline
    property string title:       ""
    property string detail:      ""
    // When non-empty, a copy icon copies this text to the clipboard on tap.
    property string copyText:    ""
    property string actionText:  ""      // "" for no action
    property bool   dismissible: false   // a ✕ at the end

    signal copied()
    signal actionClicked()
    signal dismissClicked()

    readonly property bool _toast: style === "toast"

    implicitWidth:  content.implicitWidth + Theme.sp(30)
    implicitHeight: _toast ? Theme.sp(40) : Math.max(Theme.sp(40), content.implicitHeight + Theme.sp(16))
    radius: Theme.radiusLg
    color:  Theme.colorBg3
    border.width: 1
    border.color: card.tone

    // The glyph and the words. The toast holds them as one tight group, centred as a unit; the centre
    // spaces them like the actions, each centred on its own (the two round their centring differently,
    // so each keeps its own arrangement).
    component Glyph: Text {
        anchors.verticalCenter: parent.verticalCenter
        font.family:    Theme.fontSymbol
        font.pixelSize: Theme.fontSzBody
    }
    component Words: Column {
        id: words
        property string title:  ""
        property string detail: ""
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gap(2)
        Text {
            text:           words.title
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody
            color:          Theme.colorText
        }
        Text {
            visible:        words.detail.length > 0
            text:           words.detail
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText2
            elide:          Text.ElideMiddle
            width:          Math.min(implicitWidth, Theme.sp(420))
        }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.gap(14)

        Row {
            visible: card._toast
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.gap(9)
            Glyph { text: card.glyph; color: card.glyphColor }
            Words { title: card.title; detail: card.detail }
        }
        Glyph { visible: !card._toast; text: card.glyph; color: card.glyphColor }
        Words { visible: !card._toast; title: card.title; detail: card.detail }

        Rectangle {   // hairline separator before the copy action
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBorderStrong
            visible: card.copyText.length > 0
        }

        Text {        // copy-to-clipboard action; flashes a check as feedback
            anchors.verticalCenter: parent.verticalCenter
            visible:        card.copyText.length > 0
            text:           copyConfirm.running ? "✓" : "⧉"
            font.family:    Theme.fontSymbol
            font.pixelSize: Theme.fontSzBody
            color:          copyConfirm.running ? Theme.colorGood : Theme.colorAccent

            Timer { id: copyConfirm; interval: 1200 }

            PpPressable {
                anchors.margins: -Theme.sp(6)
                onClicked: {
                    clipboard.setText(card.copyText)
                    copyConfirm.restart()
                    card.copied()
                }
            }
        }

        Rectangle {   // hairline separator before the action
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBorderStrong
            visible: card.actionText.length > 0
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible:        card.actionText.length > 0
            text:           card.actionText
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzBody2
            font.letterSpacing: Theme.trackingLabel
            color:          Theme.colorAccent

            PpPressable {
                anchors.margins: -Theme.sp(6)
                onClicked: card.actionClicked()
            }
        }

        Rectangle {   // hairline separator before dismiss
            anchors.verticalCenter: parent.verticalCenter
            width: 1; height: Theme.sp(18)
            color: Theme.colorBorderStrong
            visible: card.dismissible
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible:        card.dismissible
            text:           "✕"
            font.family:    Theme.fontSymbol
            font.pixelSize: Theme.fontSzBody
            color:          Theme.colorText2
            PpPressable {
                anchors.margins: -Theme.sp(6)
                onClicked: card.dismissClicked()
            }
        }
    }
}
