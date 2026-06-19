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

import QtQuick
import QtQuick.Effects
import PinPointStudio

// Large display title with the brand warm→cool gradient fill (the site's
// signature). Falls back to a flat Text when Theme.gradientTitlesActive is
// false (reduce-motion / user preference). Sizes to its text, but tracks an
// externally-assigned width (Layout.fillWidth / explicit width) so wrapping
// titles wrap to the available space — implicitWidth/Height still report the
// natural glyph geometry so plain (un-stretched) placement is unchanged.
Item {
    id: root

    property alias  text:        glyphs.text
    property int    pixelSize:   Theme.fontSzDisplay
    property string fontFamily:  Theme.fontDisplay
    property int    fontWeight:  Theme.fontDisplayWeight
    property bool   italic:      Theme.fontDisplayItalic
    property real   letterSpacing: 0
    property color  flatColor:   Theme.colorText          // non-gradient colour
    property int    horizontalAlignment: Text.AlignLeft
    property alias  wrapMode:    glyphs.wrapMode
    property alias  lineHeight:  glyphs.lineHeight
    property alias  elide:       glyphs.elide
    property alias  maximumLineCount: glyphs.maximumLineCount

    implicitWidth:  glyphs.implicitWidth
    implicitHeight: glyphs.implicitHeight

    // Glyph geometry. In flat mode it is painted directly (flatColor). In
    // gradient mode it is pulled out of the scene by glyphMask.hideSource and
    // serves only as the alpha mask — so it MUST stay visible:true, because a
    // ShaderEffectSource captures an empty texture from a visible:false source.
    // width tracks root.width so wrapMode/Layout.fillWidth wrap correctly; when
    // no width is imposed, root.width == implicitWidth == the natural glyph
    // width, so the text stays on one line (no binding loop — implicitWidth is
    // intrinsic to the content, independent of the assigned width).
    Text {
        id: glyphs
        width:               root.width
        font.family:         root.fontFamily
        font.pixelSize:      root.pixelSize
        font.weight:         root.fontWeight
        font.italic:         root.italic
        font.letterSpacing:  root.letterSpacing
        horizontalAlignment: root.horizontalAlignment
        color:               root.flatColor
    }

    // Glyph alpha captured to a texture (the mask). hideSource removes glyphs
    // from the scene only while the gradient is active, so flat mode still
    // paints the Text above. The SES itself never paints (visible:false); it is
    // only a texture provider for the MultiEffect below.
    ShaderEffectSource {
        id: glyphMask
        anchors.fill: glyphs
        sourceItem:   glyphs
        hideSource:   Theme.gradientTitlesActive
        live:         Theme.gradientTitlesActive
        visible:      false
    }

    // Brand gradient, clipped to the glyph alpha. Horizontal ≈ the site's 96°.
    // maskThresholdMin cuts the transparent background (alpha 0) while keeping
    // glyph ink (alpha 1); maskSpreadAtMin keeps the glyph edges anti-aliased.
    Rectangle {
        anchors.fill: glyphs
        visible: Theme.gradientTitlesActive
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0;  color: Theme.gradientWarm }
            GradientStop { position: 0.55; color: Theme.gradientWarmLit }
            GradientStop { position: 1.0;  color: Theme.gradientCool }
        }
        layer.enabled: Theme.gradientTitlesActive
        layer.effect: MultiEffect {
            maskEnabled:      true
            maskSource:       glyphMask
            maskThresholdMin: 0.5
            maskSpreadAtMin:  0.4
        }
    }
}
