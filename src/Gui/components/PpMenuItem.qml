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

// ONE LINE OF A POPUP MENU — the Popup + PpPopoverCard menus (the Sessions row ⋯ menu, the session
// drawer's row menu, the shot action bar's "all N shown ▾"). A 34-px hover row the menu's width: its
// words, an optional quiet note after them, an optional leading glyph and a right-hand count tail;
// inactive (greyed, no press) or destructive (the warn ink and the warn hover wash).
//
//   Popup {
//       background: PpPopoverCard {}
//       contentItem: Column {
//           PpMenuItem { glyph: "⤓"; text: qsTr("Export session"); onTriggered: { menu.close(); … } }
//           PpMenuRule {}
//           PpMenuItem { glyph: "🗑"; text: qsTr("Move to trash"); warn: true; onTriggered: … }
//       }
//   }
//
// A menu that sizes itself to its lines reads `contentImplicitWidth` (glyph + words, no margins).
//
// style "compact" is the model editor's ⋯ drawer row (ModelTools): 30 px and square-cornered, the
// smaller words cut short rather than wrapped, `tail` in the data face after them, the hover wash
// without a fade, and the error ink (not the warn) for a destructive row. No glyph, no note.
// It is sized by its layout (an implicit height), as the drawer is a ColumnLayout.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: it

    property string text:     ""
    property string note:     ""        // quiet words after the text: "· why not"
    property string glyph:    ""        // a leading symbol (fontSymbol), "" for none
    property string tail:     ""        // a right-aligned count in the micro face, "" for none
    property bool   active:   true
    property bool   warn:     false     // destructive: the warn ink and hover wash
    property bool   linkNote: false     // the note is the way to fix it (accent)
    property int    fontWeight: Theme.fontBodyWeight
    property string style:    ""        // "" | "compact"
    signal triggered()

    readonly property bool _compact: style === "compact"

    readonly property real contentImplicitWidth: (lead.visible ? lead.implicitWidth + line.spacing : 0)
                                                 + words.implicitWidth

    width: parent ? parent.width : 0
    height: it._compact ? Theme.sp(30) : Math.max(Theme.sp(34), words.implicitHeight + Theme.sp(14))
    implicitHeight: it._compact ? Theme.sp(30) : 0
    radius: it._compact ? 0 : Theme.radius
    color: it._compact ? (compactHover.hovered ? Theme.colorBg2 : "transparent")
         : !it.active ? "transparent"
         : itPress.containsMouse ? (it.warn ? Theme.colorWarnLight : Theme.colorBg2) : "transparent"
    Behavior on color { enabled: !it._compact; ColorAnimation { duration: Theme.durationFast } }

    Row {
        id: line
        visible: !it._compact
        anchors { left: parent.left; leftMargin: Theme.gap(10); verticalCenter: parent.verticalCenter }
        spacing: Theme.gap(10)
        Text {
            id: lead
            visible: it.glyph !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: it.glyph
            font.family: Theme.fontSymbol
            font.pixelSize: Theme.fontSzBody
            color: it.warn ? (it.active ? Theme.colorWarn : Qt.alpha(Theme.colorWarn, 0.5))
                 : it.active ? Theme.colorText2 : Theme.colorText3
        }
        Text {
            id: words
            anchors.verticalCenter: parent.verticalCenter
            width: it.width - 2 * Theme.gap(10) -(lead.visible ? lead.width + line.spacing : 0)
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            readonly property color ink: it.warn ? (it.active ? Theme.colorWarn : Qt.alpha(Theme.colorWarn, 0.5))
                                       : it.active ? Theme.colorText : Theme.colorText3
            readonly property color noteInk: it.linkNote ? Theme.colorAccent : Theme.colorText3
            text: it.text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                  + (it.note !== "" ? " <font color=\"" + noteInk + "\">· " + it.note.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>" : "")
            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
            font.weight: it.fontWeight
            color: ink
        }
    }
    PpMicro {   // count tail
        visible: it.tail !== "" && !it._compact
        anchors { right: parent.right; rightMargin: Theme.gap(10); verticalCenter: parent.verticalCenter }
        text: it.tail
        font.letterSpacing: Theme.trackingData
        color:   it.warn ? Theme.colorWarn : Theme.colorText3
        opacity: it.warn ? 0.7 : 1.0
    }
    HoverHandler { id: compactHover; enabled: it._compact }
    RowLayout {   // the compact row: words, then the tail
        visible: it._compact
        anchors.fill: parent
        anchors.leftMargin:  Theme.gap(14)
        anchors.rightMargin: Theme.gap(14)
        spacing: Theme.gap(8)

        Text {
            Layout.fillWidth: true
            text: it.text
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    it.fontWeight
            color:          it.warn ? Theme.colorError : Theme.colorText
            elide:          Text.ElideRight
        }
        Text {
            text:    it.tail
            visible: text.length > 0
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          it.warn ? Theme.colorError : Theme.colorText3
            opacity:        it.warn ? 0.75 : 1.0
        }
    }
    PpPressable {
        id: itPress
        enabled: it.active
        hoverScale: 1.0       // full-width menu item — press-dip only
        onClicked: it.triggered()
    }
}
