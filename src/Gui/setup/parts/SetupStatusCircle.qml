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

// The status circle at the left of a SetupCheckRow. Ported verbatim from the retired ScreenSessionWizard.qml's
// inline `component StatusCircle` (l.2309–2328).
import QtQuick
import PinPointStudio

Rectangle {
    property bool ok:    false
    property bool warn:  false   // amber — found but not ready (e.g. assigned but not connected)
    property bool error: false   // red   — hard failure (not found / not assigned)
    width: Theme.sp(17); height: Theme.sp(17); radius: Theme.sp(9)
    color:        ok    ? Theme.colorGoodLight  :
                  warn  ? Theme.colorWarnLight  :
                  error ? Theme.colorErrorLight : "transparent"
    border.width: 1
    border.color: ok    ? Theme.colorGood  :
                  warn  ? Theme.colorWarn  :
                  error ? Theme.colorError : Theme.colorBorderMid
    Text {
        anchors.centerIn: parent
        visible:        parent.ok
        text:           "✓"
        font.pixelSize: Theme.sp(10)
        color:          Theme.colorGood
    }
}
