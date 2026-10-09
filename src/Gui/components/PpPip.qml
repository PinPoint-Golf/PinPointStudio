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

// One session pip. Two vocabularies share it:
//
//   seen / unseen              the summary's: a dot in the tone where the session saw it, an empty
//                              ring where it did not
//   fired / clean /            PpTickRun's: a dot in the tone where it was a pattern, a short
//   notAssessable              dash in cleanTone where the session was clean, and an outlined ring
//                              where the session could not tell — present, never a gap
//
// The dash is centred on the dot's line, so a mixed row keeps one centre.

import QtQuick
import PinPointStudio

Item {
    id: pip

    property string kind:      "seen"
    property color  tone:      Theme.colorText3
    property color  cleanTone: Theme.colorGood
    property int    size:      Theme.sp(6)

    width: pip.size; height: pip.size

    Rectangle {
        readonly property bool clean: pip.kind === "clean"
        anchors.centerIn: parent
        width:  parent.width
        height: clean ? Math.max(2, Theme.sp(2)) : parent.height
        radius: height / 2
        color:  pip.kind === "fired" || pip.kind === "seen" ? pip.tone
              : clean                                       ? pip.cleanTone
              :                                               "transparent"
        border.width: pip.kind === "unseen" || pip.kind === "notAssessable" ? 1 : 0
        border.color: pip.kind === "unseen" ? Theme.colorBorderStrong : Theme.colorText3
    }
}
