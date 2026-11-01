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

// The app's one-line text field: a surface box, radius, a 1 px border that is colorBorderStrong at
// rest, colorWarn on `hasError` and the accent while it has focus; body type; colorText3 placeholder.
// It emits editingFinished whenever it loses focus (`commitOnBlur`), so a settings field commits when
// the user navigates away without pressing Enter.
//
// `size` — the box, as each copy drew it:
//   "regular"  34 px, 10 px in from the sides, body size and weight (every form and settings field)
//   "compact"  24 px, 8 px in, fontSzBody2 at normal weight (Settings → Cameras crop origin/size, the
//              athlete picker's search, which sets its own 28 px height)
// `monospace` sets the data face (the crop numbers). `animateFocus` eases the border colour in and out
// of focus (the crop fields did). `boxed: false` draws no box (PpSearchField's "line" style).
// `padHit: false` — only the text's band takes a press and shows the I-beam, as on the copies that
// were a bare input inside a box (the crop fields, the athlete picker's search, Settings' search).
//
// The fields that were bare TextInputs keep a TextInput's behaviour with `editMenu: false` (no
// right-click Cut/Copy/Paste menu), `activeFocusOnTab: false`, `commitOnBlur: false` (Qt's own
// editingFinished on focus loss only, not a second one from here) and, where it matters, a TapHandler
// holding the right and middle buttons (a TextInput took the left button only).
//
//     PpTextField {
//         Layout.fillWidth: true
//         placeholderText: qsTr("Device alias…")
//         text: dev.alias
//         onEditingFinished: dev.alias = text
//     }
//
// Searches use PpSearchField, which is this field with a search's chrome.

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

TextField {
    id: root

    property bool   hasError:     false
    property string size:         "regular"   // "regular" | "compact"
    property bool   monospace:    false
    property bool   animateFocus: false
    property bool   commitOnBlur: true
    property bool   editMenu:     true
    property bool   boxed:        true
    property bool   padHit:       true

    readonly property bool _compact: root.size === "compact"
    // What the size gives, for PpSearchField's styles to fall back on.
    readonly property real _padX:   root._compact ? Theme.gap(8) : Theme.gap(10)
    readonly property int  _fontPx: root._compact ? Theme.fontSzBody2 : Theme.fontSzBody
    // The height of a line of placeholder in the field's font (lineMetric, below).
    readonly property real _lineH:  lineMetric.implicitHeight

    implicitHeight: root._compact ? Theme.sp(24) : Theme.sp(34)
    leftPadding:    root._padX
    rightPadding:   root._padX
    topPadding:     0
    bottomPadding:  0
    verticalAlignment: TextInput.AlignVCenter

    font.family:    root.monospace ? Theme.fontData : Theme.fontBody
    font.pixelSize: root._fontPx
    font.weight:    root.size === "regular" ? Theme.fontBodyWeight : Font.Normal
    color:          Theme.colorText

    placeholderTextColor: Theme.colorText3

    // Commit the current text whenever this field loses keyboard focus —
    // covers the common case where the user navigates away from the settings
    // screen without pressing Enter or clicking elsewhere first.
    onActiveFocusChanged: if (!activeFocus && root.commitOnBlur) root.editingFinished()

    // Measures a line of the placeholder, so PpSearchField's line style can place its text as
    // Settings' Row of glyph and field did. Never drawn.
    Text {
        id: lineMetric
        visible: false
        text: root.placeholderText
        font: root.font
    }

    containmentMask: root.padHit ? null : textBand
    Item {
        id: textBand
        x: root.leftPadding
        y: root.topPadding
        width:  root.width - root.leftPadding - root.rightPadding
        height: root.height - root.topPadding - root.bottomPadding
    }

    // The style's own edit menu is set on construction; a field without one drops it here.
    Component.onCompleted: if (!root.editMenu) ContextMenu.menu = null

    background: Rectangle {
        visible:      root.boxed
        radius:       Theme.radius
        color:        Theme.colorSurface
        border.width: 1
        border.color: root.activeFocus
                          ? Theme.colorAccent
                          : (root.hasError ? Theme.colorWarn : Theme.colorBorderStrong)
        Behavior on border.color { enabled: root.animateFocus; ColorAnimation { duration: Theme.durationFast } }
    }
}
