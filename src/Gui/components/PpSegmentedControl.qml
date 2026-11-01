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

// THE app's segmented selector: a row of joined choices, one active. `options` are words, or objects
// { label, value, disabled } where the segment shows one thing and stands for another (a glyph, a
// speed); a disabled segment is dimmed and takes no click (as are any whose value is in
// `disabledOptions`). Contiguous segments brighten on hover, never scale: a scale would break the
// shared seam. Every look is a `style`, so a restyle is this one file:
//
//   style
//     ""        a recessed capsule track filling its width, equal segments, the active one filled
//               (`solid`: accent with bright ink; otherwise raised bg3 with accent ink). 30 px, body2.
//     compact   the same track at 108 × 24, glyph segments in the symbol face, active raised bg3
//               (the Motion panel's off / frame / fan / trace)
//     speed     a 26 px outline capsule hugging its segments, micro data words, the active one on a
//               faint accent capsule (the replay speeds)
//     unit      separate 36 × 34 cells with a shared outer radius on bg2, active on the accent wash
//               inside an accent border (mph / km/h, ft / cm)

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Rectangle {
    id: seg

    property var    options:  []
    property string selected: ""
    property bool   solid:    true     // solid accent fill vs recessed/raised
    property string style:    ""
    property var    disabledOptions: []
    signal activated(string value)

    // speed and unit size to their segments; "" shares its track out (a RowLayout). compact is drawn
    // through the Row too, with each cell a quarter of the track, so its cells keep their old sizes at
    // every text size.
    readonly property bool _hug:   style === "speed" || style === "unit"
    readonly property bool _rowed: _hug || style === "compact"

    implicitWidth:  style === "compact" ? Theme.sp(108)
                  : style === "speed"   ? cells.implicitWidth + Theme.sp(8)
                  : style === "unit"    ? cells.implicitWidth
                  :                       0
    implicitHeight: Theme.sp(style === "compact" ? 24 : style === "speed" ? 26 : style === "unit" ? 34 : 30)
    radius: style === "unit" ? 0 : height / 2
    color:  _hug ? "transparent" : Theme.colorBg      // recessed below the popup surface
    border.width: style === "unit" ? 0 : 1
    border.color: Theme.colorBorderMid

    RowLayout {
        visible: !seg._rowed
        anchors.fill: parent
        anchors.margins: Theme.gap(3)
        spacing: Theme.gap(2)

        Repeater {
            model: seg._rowed ? [] : seg.options
            delegate: Segment {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    Row {
        id: cells
        visible: seg._rowed
        anchors.fill:     seg.style === "compact" ? parent : undefined
        anchors.centerIn: seg.style === "compact" ? undefined : parent
        anchors.margins:  Theme.gap(2)
        spacing: seg.style === "speed" ? Theme.gap(2) : seg.style === "compact" ? Theme.gap(1) : 0

        Repeater {
            model: seg._rowed ? seg.options : []
            delegate: Segment { anchors.verticalCenter: parent ? parent.verticalCenter : undefined }
        }
    }

    component Segment: Rectangle {
        id: cell
        required property var modelData
        required property int index

        readonly property bool   _obj:  typeof modelData === "object" && modelData !== null
        readonly property string value: !_obj                       ? String(modelData)
                                      : modelData.value !== undefined ? String(modelData.value)
                                      :                               String(modelData.label)
        readonly property bool active:    value === seg.selected
        readonly property bool available: !(_obj && modelData.disabled === true)
                                          && seg.disabledOptions.indexOf(value) < 0
        readonly property bool _unit:  seg.style === "unit"
        readonly property bool _first: index === 0
        readonly property bool _last:  index === seg.options.length - 1

        implicitWidth:  seg.style === "speed"   ? cellText.implicitWidth + Theme.sp(12)
                      : seg.style === "compact" ? (seg.width - Theme.sp(4) - Theme.sp(3)) / 4
                      : _unit                   ? Theme.sp(36) : 0
        implicitHeight: seg.style === "speed"   ? Theme.sp(20)
                      : seg.style === "compact" ? seg.height - 2 * Theme.gap(2)
                      : _unit                   ? Theme.sp(34) : 0
        radius: height / 2
        // unit: the outer corners take the radius, the joins none.
        topLeftRadius:     _unit ? (_first ? Theme.radius : 0) : radius
        bottomLeftRadius:  _unit ? (_first ? Theme.radius : 0) : radius
        topRightRadius:    _unit ? (_last  ? Theme.radius : 0) : radius
        bottomRightRadius: _unit ? (_last  ? Theme.radius : 0) : radius
        opacity: available ? 1.0 : 0.35
        color: {
            switch (seg.style) {
            case "speed": return active ? Qt.alpha(Theme.colorAccent, 0.16)
                               : cellMa.containsMouse ? Theme.colorBg3 : "transparent"
            case "unit":  return active ? Theme.colorAccentLight
                               : cellMa.containsMouse ? Theme.colorBg3 : Theme.colorBg2
            }
            return active ? (seg.solid && seg.style === "" ? Theme.colorAccent : Theme.colorBg3)
                          : Theme.hoverFill(cellMa.containsMouse, Theme.colorBg3)
        }
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        // unit: the border drawn over each cell, so neighbours' borders do not fight.
        Rectangle {
            visible: cell._unit
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: cell.active ? Theme.colorAccent : Theme.colorBorderStrong
            topLeftRadius:     cell.topLeftRadius
            bottomLeftRadius:  cell.bottomLeftRadius
            topRightRadius:    cell.topRightRadius
            bottomRightRadius: cell.bottomRightRadius
        }

        Text {
            id: cellText
            anchors.centerIn: parent
            text: cell._obj ? String(cell.modelData.label) : String(cell.modelData)
            font.family: seg.style === "compact" ? Theme.fontSymbol
                       : cell._unit || seg.style === "speed" ? Theme.fontData : Theme.fontBody
            font.pixelSize: cell._unit || seg.style === "speed" ? Theme.fontSzMicro : Theme.fontSzBody2
            font.weight: seg.style !== "" ? Font.Normal
                       : cell.active      ? Font.Medium : Theme.fontBodyWeight
            font.letterSpacing: seg.style === "speed" ? Theme.trackingData : 0
            color: cell.active ? (seg.style === "" && seg.solid ? Theme.colorOnAccentBright : Theme.colorAccent)
                 : cell._unit || seg.style === "speed" ? Theme.colorText3
                 : Theme.colorText2
        }
        MouseArea {
            id: cellMa
            anchors.fill: parent
            enabled: cell.available
            hoverEnabled: true
            cursorShape: cell.available ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: seg.activated(cell.value)
        }
    }
}
