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

// THE app's button. Every button look the app draws is a `style` here and every size a `size`, so a
// restyle is this one file. The looks below are today's looks, kept apart where the hand-built copies
// they replaced differed visibly; merging them is the typeset restyle's job, not a caller's.
//
//   style  (rest → hover; "active" is the held-lit state; ink is the label colour)
//     ""          the outline button, or primary / attention / destructive when those are set
//     outline     transparent → bg2, strong border, text ink
//     primary     accent fill (lighter on hover), bright ink           attention: the same in amber
//     destructive warn wash, warn border and ink
//     accent      accent fill that does not brighten, surface ink (the markup panel's lit button)
//     quiet       transparent → bg2, strong border → accent-mid, text2 ink; active: accent wash
//     faint       quiet with a mid border and text3 ink (the resource monitor's buttons)
//     wash        transparent → a faint accent wash, strong border → accent-mid, text2 ink; active:
//                 accent wash, border and ink; disabled draws a mid border and text3 at full opacity.
//                 A `tone` inks the label; a non-accent tone also takes a half-strength tone border.
//     washPlain   wash with no hover look (the lab Capture page's Scan)
//     subtle      bg2 → bg3, mid border, text2 ink (the neutral Cancel)
//     ghost       transparent → bg2, mid border, text2 ink
//     tone        transparent → the tone's wash, tone border and ink (strong border, text2 with none)
//     toneSoft    tone, with the accent-mid border
//     tonal       the tone's wash, tone border and ink; brightens on hover (Done)
//     bar         no border, transparent → bg2, bg3 active; tone ink or text2 (a toolbar's quiet item)
//     glyph       transparent → bg3, accent border when active, text2 ink (transport buttons)
//     scrim       transparent → a faint scrim wash, scrim ink (buttons drawn over video)
//     menu        transparent → bg3 with a mid border while hovered or active, text2 → text ink
//     labPrimary / labStop / labOutline / labText — the lab pages' stock-button looks: accent fill
//                 that darkens on press, warn wash with a half warn border, a plain outline, bare
//                 text with a press wash. Disabled greys the fill and ink; there is no hover look.
//
//   size   (height, horizontal padding, label face)
//     normal 34 · 24 · body     compact 28 · 20 · body2     micro 26 · 20 · micro     mini 18 · 16 · 10px
//     icon 34×34 glyph          iconSmall 20×18 glyph       lab: the label plus 12 · 16 (Basic Button)
//   A caller whose copy sat at another height or padding states `implicitHeight` / `horizontalPadding`.

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

Rectangle {
    id: root

    property string label:       ""
    // Optional leading glyph rendered in the symbol font (e.g. "▶" on Replay).
    property string glyph:       ""
    // Optional leading DRAWN icon, for the cases the symbol font cannot cover —
    // fontSymbol is Apple Symbols on macOS and Segoe UI Symbol on Windows, and
    // there are icons in neither (PpQrGlyph is the first). The component is
    // handed `contentColor` through the loader's own `color` property if it has
    // one, so it tracks the primary/outline/destructive palettes like the label
    // does. `iconSize` sizes the square it is given.
    property Component icon:     null
    property real   iconSize:    Theme.sp(15)
    property bool   primary:     false
    property bool   destructive: false
    // Bold amber call-to-action fill (the Theme.colorAttention framing). Filled,
    // like `primary`, but in the attention hue — used for the main action the user
    // should take next (e.g. Cancel during in-panel calibration).
    property bool   attention:   false

    property string style:       ""
    property string size:        "normal"
    // A Theme tone name ("accent", "error", "warn", "attention" …) for the tone, toneSoft, tonal,
    // wash and bar styles; "" is none.
    property string tone:        ""
    // Held lit: an open disclosure, a running calibration, a menu whose popup is up.
    property bool   active:      false
    // Grown even without hover (PpPressable.held) — a button whose popup is open.
    property alias  held:        press.held
    property alias  hoverScale:  press.hoverScale
    property alias  pressScale:  press.pressScale
    property alias  moves:       press.moves
    // The label in the data face (fontData) instead of the body face.
    property bool   mono:        false
    property int    fontWeight:  (_style === "quiet" || _style === "wash" || _style === "washPlain" || _style === "bar")
                                 && (size === "normal" || size === "compact") && !mono
                                 ? Theme.fontBodyWeight : Font.Normal
    // Dim words after the label in the micro data face (a shortcut, a menu's ▾).
    property string hint:        ""
    // A ▾ after the label in the symbol face: the button opens a menu.
    property bool   chevron:     false
    // Width measured from this text instead of the label, so a button whose label changes with its
    // state ("Scan" / "Scanning…") does not change width.
    property string sizeLabel:   ""
    property string tooltip:     ""
    // Handed the keyboard back after every click (the markup panel keeps its keys).
    property Item   focusOnClick: null
    property real   horizontalPadding: _style === "bar"     ? Theme.sp(16)
                                     : size === "compact"   ? Theme.sp(20)
                                     : size === "micro"     ? Theme.sp(20)
                                     : size === "mini"      ? Theme.sp(16)
                                     : size === "lab"       ? 16
                                     :                        Theme.sp(24)

    readonly property bool hovered: press.containsMouse
    readonly property bool pressed: press.pressed || _keyDown

    signal clicked()

    readonly property string _style: style !== "" ? style
                                   : primary      ? "primary"
                                   : attention    ? "attention"
                                   : destructive  ? "destructive"
                                   :                "outline"
    readonly property bool  _lab:      _style.indexOf("lab") === 0
    // The lab looks were stock controls: no colour fades, and they take the keyboard (Tab, Space).
    readonly property bool  _animates: !_lab && _style !== "scrim"
    property bool _keyDown: false
    readonly property bool  _toned:    Theme.hasTone(tone)
    readonly property color _toneInk:  Theme.toneColor(tone, Theme.colorText2)
    readonly property color _toneWash: Theme.toneLight(tone, Theme.colorAccentLight)
    readonly property bool  _lit:      hovered || active

    implicitWidth:  size === "icon"      ? Theme.sp(34)
                  : size === "iconSmall" ? Theme.sp(20)
                  : (sizeLabel !== "" ? sizer.implicitWidth : btnContent.implicitWidth) + horizontalPadding
    implicitHeight: size === "icon"      ? Theme.sp(34)
                  : size === "iconSmall" ? Theme.sp(18)
                  : size === "compact"   ? Theme.sp(28)
                  : size === "micro"     ? Theme.sp(26)
                  : size === "mini"      ? Theme.sp(18)
                  : size === "lab"       ? btnContent.implicitHeight + 12
                  :                        Theme.sp(34)
    radius:  size === "iconSmall" ? Theme.radius - 1 : Theme.radius
    opacity: root.enabled ? 1.0
           : (_lab || _style === "wash")            ? 1.0
           : _style === "bar"                       ? 0.35
           : (_style === "quiet" || _style === "faint") ? 0.5
           :                                          0.4

    // Resting fill. The outline variant rests transparent but RGB-matched to its
    // hover fill, so the hover ColorAnimation only ramps alpha — no colour flash
    // (the same lesson as the home-tile hover).
    color: {
        switch (_style) {
        case "primary":     return hovered ? Qt.lighter(Theme.colorAccent, 1.08) : Theme.colorAccent
        case "attention":   return hovered ? Qt.lighter(Theme.colorAttention, 1.08) : Theme.colorAttention
        case "destructive": return hovered ? Qt.lighter(Theme.colorWarnLight, 1.08) : Theme.colorWarnLight
        case "accent":      return Theme.colorAccent
        case "quiet":
        case "faint":       return Theme.choiceFill(active, hovered, "clear")
        case "washPlain":   return active ? Theme.colorAccentLight : "transparent"
        case "wash":        return Theme.choiceFill(active, hovered, "wash")
        case "subtle":      return hovered ? Theme.colorBg3 : Theme.colorBg2
        case "ghost":       return hovered ? Theme.colorBg2 : "transparent"
        case "tone":
        case "toneSoft":    return hovered ? _toneWash : "transparent"
        case "tonal":       return hovered ? Qt.lighter(_toneWash, 1.08) : _toneWash
        case "bar":         return active ? Theme.colorBg3 : hovered ? Theme.colorBg2 : "transparent"
        case "glyph":       return hovered ? Theme.colorBg3 : "transparent"
        case "scrim":       return hovered ? Qt.alpha(Theme.colorOnScrim, 0.16) : "transparent"
        case "menu":        return _lit ? Theme.colorBg3 : "transparent"
        case "labPrimary":  return !enabled ? Theme.colorBg3
                                 : pressed ? Qt.darker(Theme.colorAccent, 1.1) : Theme.colorAccent
        case "labStop":     return enabled ? Theme.colorWarnLight : Theme.colorBg3
        case "labOutline":  return "transparent"
        case "labText":     return pressed ? Theme.colorBg3 : "transparent"
        }
        return Theme.hoverFill(hovered, Theme.colorBg2)                         // outline
    }
    border.width: {
        switch (_style) {
        case "primary": case "attention": case "accent":
        case "bar": case "scrim": case "labPrimary": case "labText":
            return 0
        case "labStop":
            return enabled ? 1 : 0
        }
        return 1
    }
    border.color: {
        switch (_style) {
        case "destructive": return Theme.colorWarn
        case "quiet":       return !enabled ? Theme.colorBorderMid : Theme.choiceBorder(active, hovered)
        case "faint":       return (enabled && hovered) ? Theme.colorAccentMid : Theme.colorBorderMid
        case "washPlain":   return active ? Theme.colorAccent : Theme.colorBorderStrong
        case "wash":        return !enabled ? Theme.colorBorderMid
                                 : (!active && _toned && tone !== "accent") ? Qt.alpha(_toneInk, 0.5)
                                 : Theme.choiceBorder(active, hovered)
        case "subtle":
        case "ghost":       return Theme.colorBorderMid
        case "tone":        return _toned ? _toneInk : Theme.colorBorderStrong
        case "toneSoft":    return tone === "accent" ? Theme.colorAccentMid : Qt.alpha(_toneInk, 0.5)
        case "tonal":       return _toneInk
        case "glyph":       return active ? Theme.colorAccent : "transparent"
        case "menu":        return _lit ? Theme.colorBorderMid : "transparent"
        case "labStop":     return Qt.alpha(Theme.colorWarn, 0.5)
        case "labOutline":  return enabled ? Theme.colorBorderStrong : Theme.colorBorderMid
        }
        return Theme.colorBorderStrong
    }
    Behavior on color        { enabled: root._animates; ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { enabled: root._animates; ColorAnimation { duration: Theme.durationFast } }

    readonly property color contentColor: {
        switch (_style) {
        case "primary":
        case "attention":   return Theme.colorOnAccentBright
        case "destructive": return Theme.colorWarn
        case "accent":      return Theme.colorOnAccent
        case "quiet":       return !enabled ? Theme.colorText3 : active ? Theme.colorAccent : Theme.colorText2
        case "faint":       return Theme.colorText3
        case "washPlain":   return active ? Theme.colorAccent : Theme.colorText2
        case "wash":        return !enabled ? Theme.colorText3 : active ? Theme.colorAccent
                                 : _toned ? _toneInk : Theme.colorText2
        case "subtle":
        case "ghost":       return Theme.colorText2
        case "tone":
        case "toneSoft":
        case "tonal":
        case "bar":         return _toneInk
        case "glyph":       return Theme.colorText2
        case "scrim":       return Theme.colorOnScrim
        case "menu":        return _lit ? Theme.colorText : Theme.colorText2
        case "labPrimary":  return enabled ? Theme.colorBg : Theme.colorText3
        case "labStop":     return enabled ? Theme.colorWarn : Theme.colorText3
        case "labOutline":  return enabled ? Theme.colorText2 : Theme.colorText3
        case "labText":     return enabled ? Theme.colorText3 : Theme.colorBg3
        }
        return Theme.colorText                                                  // outline
    }

    readonly property int _labelPx: size === "compact" ? Theme.fontSzBody2
                                  : size === "micro"   ? Theme.fontSzMicro
                                  : size === "mini"    ? Theme.sp(10)
                                  :                      Theme.fontSzBody
    readonly property int _glyphPx: size === "icon"      ? Theme.sp(15)
                                  : size === "iconSmall" ? Theme.sp(11)
                                  : size === "micro"     ? Theme.fontSzMicro
                                  : size === "mini"      ? Theme.sp(10)
                                  : Math.round(_labelPx * Theme.symbolScale(root.glyph))

    Row {
        id: btnContent
        anchors.centerIn: parent
        spacing: (root._style === "bar" || root.size === "micro") ? Theme.gap(6) : Theme.gap(7)

        Loader {
            visible:  root.icon !== null
            active:   root.icon !== null
            width:    active ? root.iconSize : 0
            height:   root.iconSize
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: root.icon
            onLoaded: if (item && item.color !== undefined) item.color = Qt.binding(
                          function() { return root.contentColor })
        }
        Text {
            visible:        root.glyph !== ""
            anchors.verticalCenter: parent.verticalCenter
            text:           root.glyph
            font.family:    Theme.fontSymbol
            font.pixelSize: root._glyphPx
            color:          root.contentColor
            Behavior on color { enabled: root._animates; ColorAnimation { duration: Theme.durationFast } }
        }
        Text {
            visible:        root.label !== ""
            anchors.verticalCenter: parent.verticalCenter
            text:           root.label
            font.family:    root.mono ? Theme.fontData : Theme.fontBody
            font.pixelSize: root._labelPx
            font.weight:    root.fontWeight
            color:          root.contentColor
            Behavior on color { enabled: root._animates; ColorAnimation { duration: Theme.durationFast } }
        }
        Text {
            visible:        root.hint !== ""
            anchors.verticalCenter: parent.verticalCenter
            text:           root.hint
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
        }
        Text {
            visible:        root.chevron
            anchors.verticalCenter: parent.verticalCenter
            text:           "▾"
            font.family:    Theme.fontSymbol
            font.pixelSize: Theme.fontSzMicro
            color:          Theme.colorText3
        }
    }
    Text {   // measures sizeLabel; never drawn
        id: sizer
        visible:        false
        text:           root.sizeLabel
        font.family:    root.mono ? Theme.fontData : Theme.fontBody
        font.pixelSize: root._labelPx
        font.weight:    root.fontWeight
    }

    activeFocusOnTab: root._lab
    onActiveFocusChanged: if (!activeFocus) root._keyDown = false
    Keys.onPressed: (event) => {
        if (root._lab && event.key === Qt.Key_Space && !event.isAutoRepeat) { root._keyDown = true; event.accepted = true }
    }
    Keys.onReleased: (event) => {
        if (root._lab && event.key === Qt.Key_Space && !event.isAutoRepeat && root._keyDown) {
            root._keyDown = false; event.accepted = true; root.clicked()
        }
    }

    ToolTip.visible: root.tooltip !== "" && press.containsMouse
    ToolTip.text:    root.tooltip
    ToolTip.delay:   400

    // Standard hover-grow / press-dip motion (drives root.scale). Replaces the old
    // opacity-on-press blip; disabled dimming stays on root.opacity above.
    // The bar and lab looks sit still on hover (the bar keeps the press dip; the lab
    // buttons, stock controls before, have no motion at all).
    PpPressable {
        id: press
        enabled:    root.enabled
        hoverScale: root._style === "bar" ? 1.0 : 1.02
        moves:      !root._lab
        cursorShape: (root.enabled && !root._lab) ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (root._lab) root.forceActiveFocus()   // a stock Button took focus on click
            root.clicked()
            if (root.focusOnClick) root.focusOnClick.forceActiveFocus()
        }
    }
}
