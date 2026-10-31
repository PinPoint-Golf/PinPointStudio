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

// A settings panel's section heading: quiet caps micro type over a group of rows ("SPACE",
// "SENDING REPORTS"). The caller spells the words through Theme.caps, as every caps label does:
//
//     SettingsSectionHead { text: Theme.caps(qsTr("Archive location")) }
//
// Settings → Archiving and Studio; the other panels' hand-typed copies of this Text move here as they
// are converted onto SettingsPanelBase.

import QtQuick
import PinPointStudio

Text {
    font.family:         Theme.fontBody
    font.pixelSize:      Theme.fontSzMicro
    font.letterSpacing:  Theme.trackingMicro
    font.capitalization: Theme.capsFont
    color:               Theme.colorText3
}
