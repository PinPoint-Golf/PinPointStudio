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

// THE app's choice chip: one word or two the user picks, alone or one of a row. PpChipGroup lays a
// row of them out from a list; a caller whose chips each do their own thing puts these in its own
// Repeater. Every chip look the app draws is a `style` here, so a restyle is this one file. They are
// today's looks, kept apart where the hand-built copies differed visibly. Selected / hover fills and
// borders come from Theme.choiceFill / choiceBorder where the look is the shared one.
//
//   style  (height · padding · face; rest → hover; selected)
//     ""          28 · 24 · body      clear → bg2, strong border → accent-mid; accent wash, border, ink
//     mono        24 · 20 · micro data   transparent → a faint accent wash; otherwise as ""
//     monoNeutral mono with a neutral hover (bg2 at 0.6)           (Settings › Storage)
//     body        30 · 28 · body2     as mono, in the body face
//     solid       24 · 16 · body2     surface, mid border → a faint accent wash; SOLID accent, bg ink.
//                                     `pending` (picked, not live yet) is the accent wash and ink.
//     solidLift   solid whose hover lifts the surface instead       (the lab camera chips)
//     solidFlat   solid with no hover look                         (the lab sensor chips)
//     capsule     26 · 20 · body2     a capsule; transparent → bg2, strong border, no fades
//     pill        24 · 18 · micro body   a capsule; transparent, mid border, no hover look
//     pillFilled  22 · 18 · micro body   pill on bg2
//     filter      22 · 16 · label body   a capsule; clear → bg2, mid border → accent; `count` after it
//     tag         18 · 10 · 9px data  a capsule in `tint`: transparent → a faint tint wash; tint wash,
//                                     border and ink when selected. Sits still (no grow or dip).
//     toggle      18 · 16 · 9px data  tag's capsule: clear → bg2, mid border → accent-mid; selected the
//                                     tint wash and half-tint border, unchanged by hover (the monitor's Deep)
//     tab         30 · 26 · body2     no border; bg2 when selected or hovered, a 2 px accent underline
//   `dimmed` quietens it (shown, still tappable). `blocked` keeps the taps but drops the grow and the
//   pointing hand, and "" its hover look too: a choice that cannot be taken just now, which says why.
//   A caller whose copy sat at another padding states `horizontalPadding`.
//   Not drawn here: PpMetricChart's segment chips, left on purpose while that chart is held for
//   retirement (C2).

import QtQuick
import PinPointStudio

Rectangle {
    id: chip

    property string label:    ""
    // Dim words after the label in the micro data face (the filter's "what this would leave").
    property string count:    ""
    property bool   selected: false
    property bool   pending:  false
    property string style:    ""
    property color  tint:     Theme.colorAccent
    property bool   dimmed:   false
    property bool   blocked:  false
    property real   horizontalPadding: Theme.sp(_metric[1])
    property alias  hoverScale: press.hoverScale
    property alias  pressScale: press.pressScale
    property alias  moves:      press.moves
    readonly property bool hovered: press.containsMouse

    signal clicked()

    //                                    height, padding
    readonly property var _metric: _mono                  ? [24, 20]
                                 : style === "body"       ? [30, 28]
                                 : style === "capsule"    ? [26, 20]
                                 : style === "pill"       ? [24, 18]
                                 : style === "pillFilled" ? [22, 18]
                                 : style === "filter"     ? [22, 16]
                                 : style === "tag"        ? [18, 10]
                                 : style === "toggle"     ? [18, 16]
                                 : style === "tab"        ? [30, 26]
                                 : _solid                 ? [24, 16]
                                 :                          [28, 24]
    readonly property bool _mono:  style === "mono" || style === "monoNeutral"
    readonly property bool _solid: style === "solid" || style === "solidLift" || style === "solidFlat"
    readonly property bool _tagLike: style === "tag" || style === "toggle"
    readonly property bool _capsule: style === "capsule" || style === "pill" || style === "pillFilled"
                                     || style === "filter" || _tagLike
    readonly property bool _still:   style === "pill" || style === "pillFilled"
    // The hover look: a blocked plain chip has none.
    readonly property bool _hov: press.containsMouse && !(blocked && style === "")
    // ...nor a selected look.
    readonly property bool _sel: selected && !(blocked && style === "")

    readonly property real _countGap: Theme.gap(5)
    implicitWidth:  labelText.implicitWidth + (count !== "" ? _countGap + countText.implicitWidth : 0)
                    + horizontalPadding
    implicitHeight: Theme.sp(_metric[0])
    radius:  _capsule ? height / 2 : Theme.radius
    opacity: !dimmed ? 1.0
           : _solid                                   ? 0.5
           : (style === "capsule" || style === "filter") ? 0.45
           :                                            0.4

    color: {
        switch (style) {
        case "mono":
        case "body":       return Theme.choiceFill(selected, _hov, "wash")
        case "monoNeutral": return selected ? Theme.colorAccentLight
                                : _hov     ? Qt.alpha(Theme.colorBg2, 0.6) : "transparent"
        case "solid":      return selected ? Theme.colorAccent
                                : pending  ? Theme.colorAccentLight
                                : _hov     ? Qt.alpha(Theme.colorAccentLight, 0.6) : Theme.colorSurface
        case "solidLift":  return selected ? Theme.colorAccent
                                : pending  ? Theme.colorAccentLight
                                : _hov     ? Qt.lighter(Theme.colorSurface, 1.08) : Theme.colorSurface
        case "solidFlat":  return selected ? Theme.colorAccent
                                : pending  ? Theme.colorAccentLight : Theme.colorSurface
        case "capsule":    return selected ? Theme.colorAccentLight : _hov ? Theme.colorBg2 : "transparent"
        case "pill":       return selected ? Theme.colorAccentLight : "transparent"
        case "pillFilled": return selected ? Theme.colorAccentLight : Theme.colorBg2
        case "tag":        return selected ? Qt.alpha(tint, _hov ? 0.22 : 0.15)
                                : _hov     ? Qt.alpha(tint, 0.08) : "transparent"
        case "toggle":     return selected ? Qt.alpha(tint, 0.15) : Theme.hoverFill(_hov, Theme.colorBg2)
        case "tab":        return Theme.hoverFill(selected || _hov, Theme.colorBg2)
        }
        return Theme.choiceFill(_sel, _hov, "clear")                        // "" and filter
    }
    border.width: style === "tab" ? 0 : 1
    border.color: {
        switch (style) {
        case "solid":
        case "solidLift":  return Theme.choiceBorder(selected || pending, _hov, Theme.colorBorderMid)
        case "solidFlat":  return selected || pending ? Theme.colorAccent : Theme.colorBorderMid
        case "capsule":    return selected ? Theme.colorAccent : Theme.colorBorderStrong
        case "pill":
        case "pillFilled": return selected ? Theme.colorAccent : Theme.colorBorderMid
        case "filter":     return selected || _hov ? Theme.colorAccent : Theme.colorBorderMid
        case "tag":        return selected ? Qt.alpha(tint, _hov ? 0.55 : 0.4)
                                : _hov     ? Qt.alpha(tint, 0.4) : Theme.colorBorderMid
        case "toggle":     return selected ? Qt.alpha(tint, 0.4)
                                : _hov     ? Theme.colorAccentMid : Theme.colorBorderMid
        case "tab":        return "transparent"
        }
        return Theme.choiceBorder(_sel, _hov)                    // "", mono, monoNeutral, body
    }
    Behavior on color {
        enabled: chip.style !== "capsule" && !chip._still
        ColorAnimation { duration: Theme.durationFast }
    }
    Behavior on border.color {
        enabled: chip.style !== "capsule" && !chip._still && chip.style !== "filter" && !chip._tagLike
        ColorAnimation { duration: Theme.durationFast }
    }

    readonly property color _ink: {
        switch (style) {
        case "solid": case "solidLift": case "solidFlat":
                      return selected ? Theme.colorBg : pending ? Theme.colorAccent : Theme.colorText2
        case "tag":   case "toggle":
                      return selected ? tint : Theme.colorText3
        case "tab":   return selected ? Theme.colorText : Theme.colorText3
        case "":      return blocked ? Theme.colorText3 : selected ? Theme.colorAccent : Theme.colorText2
        }
        return selected ? Theme.colorAccent : Theme.colorText2
    }

    Rectangle {   // the tab's underline
        visible: chip.style === "tab"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 2
        color:  chip.selected ? Theme.colorAccent : "transparent"
    }

    // The label sits straight in the chip (centred, with the count beside it when there is one), so
    // anything that finds the chip by its words finds the chip as the words' parent.
    Text {
        id: labelText
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: countText.visible ? -(countText.implicitWidth + chip._countGap) / 2 : 0
        text: chip.label
        font.family: chip._mono || chip._tagLike ? Theme.fontData : Theme.fontBody
        font.pixelSize: {
            switch (chip.style) {
            case "mono": case "monoNeutral":
            case "pill": case "pillFilled":              return Theme.fontSzMicro
            case "filter":                               return Theme.fontSzLabel
            case "tag":  case "toggle":                  return Theme.sp(9)
            case "":                                     return Theme.fontSzBody
            }
            return Theme.fontSzBody2
        }
        font.weight: chip.style === "" ? (chip.selected ? Font.Normal : Theme.fontBodyWeight)
                   : (chip.style === "body" || chip.style === "pill") ? Theme.fontBodyWeight
                   : Font.Normal
        font.letterSpacing: chip._tagLike ? Theme.trackingMicro : 0
        color: chip._ink
        Behavior on color {
            enabled: chip._mono || chip.style === "body" || chip._solid
            ColorAnimation { duration: Theme.durationFast }
        }
    }
    Text {
        id: countText
        visible: chip.count !== ""
        anchors.left: labelText.right
        anchors.leftMargin: chip._countGap
        anchors.verticalCenter: parent.verticalCenter
        text: chip.count
        font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
        color: Theme.colorText3
    }

    PpPressable {
        id: press
        hoverScale:  chip.blocked || chip._still ? 1.0 : 1.02
        moves:       chip.style !== "tag"         // the tags have always sat still
        cursorShape: !chip.enabled || chip.blocked ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked:   chip.clicked()
    }
}
