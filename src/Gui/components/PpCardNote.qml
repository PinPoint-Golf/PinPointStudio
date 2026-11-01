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

// The app's quiet body line: a card with nothing in it says so in one quiet line, so the card keeps
// its shape; a page says what it is for under its title; an empty list says why.
//
// `kind` (Text's own `style` is its outline effect) sets the leading, the one thing the copies differ in:
//   ""       the card note, lineHeight 1.3
//   "plain"  no extra leading: a settings page's description (PpPageHeader) and its quiet paragraphs, the
//            Sessions hint, the report list's empty lines
//   "aside"  "plain" in italic: an empty device list, Home's "rise and fall together" lead
//   "list"   lineHeight 1.4: Home's first-run feature list
//   "form"   lineHeight 1.5: the athlete form's lead
//   "setup"  lineHeight 1.65: a setup page's body (PpPageHeader "setup", in ink 2) and its notes

import QtQuick
import PinPointStudio

Text {
    property string kind: ""

    font.family:    Theme.fontBody
    font.pixelSize: Theme.fontSzBody2
    font.weight:    Theme.fontBodyWeight
    font.italic:    kind === "aside"
    color:          Theme.colorText3
    wrapMode:       Text.WordWrap
    lineHeight:     kind === "plain" || kind === "aside" ? 1.0
                  : kind === "list"  ? 1.4
                  : kind === "form"  ? 1.5
                  : kind === "setup" ? 1.65
                  :                    1.3
}
