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

// A micro heading: the app's mono caps, quiet by default. Callers tint it (a card's title in the
// card's tone) or loosen its tracking (asides and counts use trackingData).

import QtQuick
import PinPointStudio

Text {
    font.family:        Theme.fontData
    font.pixelSize:     Theme.fontSzMicro
    font.letterSpacing: Theme.trackingMicro
    color:              Theme.colorText3
}
