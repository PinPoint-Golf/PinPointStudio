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

// A status pill: a state said in a word or two ("Connected", "Complete", "✓  Enabled"), in its
// tone on the tone's wash with a hairline in the tone. `tone` is a Theme tone name (good, warn,
// error, accent, …); any other word is the quiet pill (grey words on colorBg3). A caller with a
// palette of its own (the updater's, from update_states.js) sets fg / bg / edge instead.
//
// `style` keeps each look in use, until the restyle (#131) merges them:
//   box          26 px high, the theme's corner, mono micro words     Settings › General, About
//   round        22 px capsule, mono micro words                      calibration status
//   capsule      fits its words (9 px mono, micro tracking)           monitor phone card
//   capsuleSoft  capsule, its hairline at 30 %                        monitor device card
//   tag          14 px, square-cut, solid tone, 7 px surface words    Settings › Displays monitor
//   chip         20 px, 4 px corner, mono micro words, data tracking  setup check row
// `animated` fades the colours when they change (the setup check row's chip, as the connection goes).

import QtQuick
import PinPointStudio

Rectangle {
    id: pill

    property string text:  ""
    property string tone:  ""
    property string style: "box"
    property bool   animated: false

    readonly property bool _fit: style === "capsule" || style === "capsuleSoft"
    readonly property bool _tag: style === "tag"

    property color fg:   _tag ? Theme.colorSurface : Theme.toneColor(tone, Theme.colorText3)   // the words
    property color bg:   _tag ? Theme.toneColor(tone, Theme.colorText3) : Theme.toneLight(tone, Theme.colorBg3)
    property color edge: !Theme.hasTone(tone)      ? Theme.colorBorderMid
                       : style === "capsuleSoft"   ? Qt.alpha(fg, 0.3)
                       :                             fg

    implicitWidth:  label.implicitWidth + Theme.sp(_fit || style === "chip" ? 14 : _tag ? 8 : 16)
    implicitHeight: _fit ? label.implicitHeight + Theme.sp(6)
                         : Theme.sp(_tag ? 14 : style === "chip" ? 20 : style === "round" ? 22 : 26)
    radius: style === "box"   ? Theme.radius
          : style === "round" ? Theme.sp(11)
          : style === "chip"  ? Theme.sp(4)
          : _tag              ? 1
          :                     height / 2
    color:        pill.bg
    border.width: _tag ? 0 : 1
    border.color: pill.edge
    Behavior on color        { enabled: pill.animated; ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { enabled: pill.animated; ColorAnimation { duration: Theme.durationFast } }

    Text {
        id: label
        anchors.centerIn: parent
        text:               pill.text
        font.family:        Theme.fontData
        font.pixelSize:     pill._fit ? Theme.sp(9) : pill._tag ? Theme.sp(7) : Theme.fontSzMicro
        font.letterSpacing: pill._fit ? Theme.trackingMicro : pill.style === "chip" ? Theme.trackingData : 0
        color:              pill.fg
        Behavior on color { enabled: pill.animated; ColorAnimation { duration: Theme.durationFast } }
    }
}
