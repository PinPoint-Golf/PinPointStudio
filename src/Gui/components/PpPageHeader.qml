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

// A page's head: its eyebrow, its display title and the quiet line that says what the page is for.
//
//     PpPageHeader {
//         Layout.fillWidth: true
//         spacing:     root.contentSpacing
//         eyebrow:     Theme.caps(qsTr("Hardware"))
//         title:       qsTr("Cameras")
//         description: qsTr("All detected cameras are listed below. …")
//     }
//
// `style`:
//   "settings"  (default) a Settings panel: the body-face caps eyebrow (PpMicro "body"), the title at its own
//               width, the description PpCardNote "plain". `spacing` is the panel's contentSpacing (gap(20)
//               unless the panel sets its own), so the three sit as the panel's own rows did.
//   "screen"    a full screen (Choose athlete, the athlete form): the data-face eyebrow `eyebrowGap` over the
//               title (gap(10) unless set), no room between; the screen lays out its own lead below.
//   "setup"     a setup page or a calibration routine (ported from the wizard's StepIntro): eyebrow, title
//               at `titlePixelSize` wrapping to the width, the body in ink 2 (PpCardNote "setup") and a
//               hairline under it, gap(8) apart. The two Check pages set a larger title and `rule: false`:
//               their figure sits straight under the text. The calibration status column shows the eyebrow
//               and title only, at its own spacing.
//
// `flatTitle` draws the title as a plain display-font Text, never the brand gradient (the Ready page: its
// title carries a colour of its own, `titleColor` — amber for "Not quite ready"). `titleColor` is also the
// non-gradient colour of the default title (PpDisplayText.flatColor), and `titleObjectName` names
// whichever title is shown. The description is left out when it is empty.

import QtQuick
import PinPointStudio

Column {
    id: head

    property string style:       "settings"
    property string eyebrow:     ""
    property string title:       ""
    property string description: ""

    property real   titlePixelSize:  style === "setup" ? Math.min(Theme.sp(20), Theme.fontSzDisplay) : Theme.fontSzDisplay
    property bool   rule:            style === "setup"
    property bool   flatTitle:       false
    property color  titleColor:      Theme.colorText
    property string titleObjectName: ""
    property real   eyebrowGap:      style === "screen" ? Theme.gap(10) : 0

    readonly property bool _setup: style === "setup"

    spacing: _setup ? Theme.gap(8) : style === "screen" ? 0 : Theme.gap(20)

    PpMicro {
        kind:          head.style === "settings" ? "body" : ""
        text:          head.eyebrow
        bottomPadding: head.eyebrowGap
    }
    // Settings and screens: one line at the title's own width (the gradient spans the glyphs). Setup:
    // the full width, wrapping.
    PpDisplayText {
        objectName: head.flatTitle ? "" : head.titleObjectName
        visible:    !head.flatTitle
        width:      head._setup ? head.width : implicitWidth
        text:       head.title
        pixelSize:  head.titlePixelSize
        flatColor:  head.titleColor
        wrapMode:   head._setup ? Text.WordWrap : Text.NoWrap
    }
    Text {
        objectName:     head.flatTitle ? head.titleObjectName : ""
        visible:        head.flatTitle
        width:          head.width
        text:           head.title
        font.family:    Theme.fontDisplay
        font.italic:    Theme.fontDisplayItalic
        font.weight:    Theme.fontDisplayWeight
        font.pixelSize: head.titlePixelSize
        color:          head.titleColor
        wrapMode:       Text.WordWrap
    }
    PpCardNote {
        visible: head.description !== ""
        width:   head.width
        text:    head.description
        kind:    head._setup ? "setup" : "plain"
        color:   head._setup ? Theme.colorText2 : Theme.colorText3
    }
    Rectangle {
        visible: head.rule
        width:   head.width; height: 1
        color:   Theme.colorBorderMid
    }
}
