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

// The app's search field: a PpTextField (its `size`s, its type, its focus border) with a search's
// chrome. What typing does — filtering, Esc, arrow keys — stays with the caller, on the field itself.
//
// `style`:
//   "box"   (default) PpTextField's box at the field's `size`.
//   "line"  no box: a ⌕ glyph (colorText3, colorText2 while focused) then the text, and a 1 px rule
//           6 px above the bottom (colorBorderMid, the accent while focused), both easing. Made to fill
//           a strip: the glyph and the text sit together in the middle of it, and only the text's band
//           takes a press. Settings' search.
//
//     PpSearchField {
//         style: "line"
//         anchors.fill: parent
//         placeholderText: qsTr("Search settings…")
//         onTextChanged: root.searchQuery = text.toLowerCase()
//     }
//
// Settings (line), the athlete picker (compact), Sessions, the Swing model's search and its
// type-ahead pickers (ModelTypeAhead).

import QtQuick
import PinPointStudio

PpTextField {
    id: root

    property string style: "box"   // "box" | "line"

    readonly property bool _line: root.style === "line"
    boxed:  !root._line
    padHit: !root._line
    // Line style, laid out as Settings' Row of glyph and bare field was: the glyph and a band one
    // line tall (_textH — the text or the placeholder, whichever is taller) centre as one row, the
    // taller of the two setting its height; the band sits at the top of that row and the text is
    // centred in it.
    readonly property real _textH: Math.max(root.contentHeight, root._lineH)
    readonly property real _rowH:  Math.max(glyph.implicitHeight, root._textH)

    // Where a centring anchor puts the middle of an item `h` tall: a whole pixel, rounded down from
    // the half when the height is odd (QQuickAnchors' alignWhenCentered).
    function _centre(h) { return (Math.floor(h) % 2) ? (h + 1) / 2 : h / 2 }

    leftPadding:   root._line ? glyph.width + Theme.gap(6) : root._padX
    rightPadding:  root._line ? 0 : root._padX
    topPadding:    root._line ? root._centre(root.height) - root._centre(root._rowH) : 0
    bottomPadding: root._line ? root.height - root.topPadding - root._textH : 0
    font.pixelSize: root._line ? Theme.fontSzBody2 : root._fontPx

    Text {
        id: glyph
        visible: root._line
        y:       root._centre(root.height) - root._centre(height)
        text:    "⌕"
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzHeading
        color: root.activeFocus ? Theme.colorText2 : Theme.colorText3
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    // The line style's rule, in place of the box.
    Rectangle {
        visible: root._line
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: Theme.gap(6) }
        height: 1
        color: root.activeFocus ? Theme.colorAccent : Theme.colorBorderMid
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
