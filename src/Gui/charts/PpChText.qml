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

// PPCHTEXT — one presenter Sentence ({ text, runs: [{ text, fig }] }) set as a paragraph: the words in the
// role given, the figure runs in the figure face (PpTypeset.figHtml). Wraps, never elides. Hidden when the
// sentence is empty, so a block can always place it. Held to the prose measure unless `measure` is false.
Text {
    id: tx
    property QtObject roles: null
    property var sentence: null
    property bool measure: true
    property var tone: undefined                 // a colour for the whole sentence (e.g. ink3 for a note)
    readonly property bool present: !!roles && roles.has(sentence)
    visible: present
    width: parent ? (measure && roles ? roles.measureW(parent.width) : parent.width) : 0
    wrapMode: Text.WordWrap
    textFormat: Text.RichText
    text: present ? roles.html(sentence) : ""
    font: roles ? roles.fSmall : Qt.font({})
    color: tone !== undefined ? tone : (roles ? roles.ink2 : "grey")
    lineHeight: roles ? roles.r.smallLeading : 1
}
