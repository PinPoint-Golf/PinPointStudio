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

// A micro heading: the app's caps micro type, quiet by default — the eyebrow over a page or a group,
// a field's head, a pill's small label. Callers tint it (a card's title in the card's tone) or loosen
// its tracking (asides and counts use trackingData).
//
// `kind` (Text's own `style` is its outline effect) picks the face and the case:
//   ""            the data face; the caller spells capitals through Theme.caps, as every caps label does
//   "caps"        the data face, set in capitals by the theme (Theme.capsFont): Settings → Cameras / Motion
//                 Sensors / Storage field heads, PpFactStrip keys, PpFlowIndicator group labels
//   "body"        the body face, set in capitals by the theme: a settings section head, the swing model's
//                 heads, the Resource monitor's heads
//   "bodyWeight"  "body" at the theme's body weight: the settings index's section heads, the swing model's
//                 rail heads and table header, PpRowAction's label

import QtQuick
import PinPointStudio

Text {
    property string kind: ""

    readonly property bool _body: kind === "body" || kind === "bodyWeight"

    font.family:         _body ? Theme.fontBody : Theme.fontData
    font.pixelSize:      Theme.fontSzMicro
    font.weight:         kind === "bodyWeight" ? Theme.fontBodyWeight : Font.Normal
    font.letterSpacing:  Theme.trackingMicro
    font.capitalization: kind === "" ? Font.MixedCase : Theme.capsFont
    color:               Theme.colorText3
}
