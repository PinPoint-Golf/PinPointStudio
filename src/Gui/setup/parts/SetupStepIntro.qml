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

// A setup page's eyebrow, heading and body, over a hairline. Ported verbatim from the wizard's
// inline `component StepIntro` (the retired ScreenSessionWizard.qml l.2244–2277). The pages bind `eyebrow`
// to WizardPage.stepLabel, which the flow keeps in step with the indicator's numbering.
//
// The two Check pages (CheckArmPage, CheckTrunkPage) set a larger heading and no hairline: their
// figure sits straight under the text.
//
// The Ready page sets `flatHeading`: its heading is a plain display-font Text, never the brand
// gradient, because it carries a colour of its own (`headingColor` — amber for "Not quite ready").
// `headingColor` is also the non-gradient colour of the default heading (PpDisplayText.flatColor),
// and `headingObjectName` names whichever heading is shown.
import QtQuick
import PinPointStudio

Column {
    property string eyebrow: ""
    property string heading: ""
    property string body:    ""
    property real   headingPixelSize: Math.min(Theme.sp(20), Theme.fontSzDisplay)
    property bool   rule:    true       // the hairline under the body
    property bool   flatHeading: false  // a plain Text heading, never the gradient
    property color  headingColor: Theme.colorText
    property string headingObjectName: ""
    spacing: Theme.gap(8)

    Text {
        text:               eyebrow
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }
    PpDisplayText {
        objectName:     flatHeading ? "" : headingObjectName
        visible:        !flatHeading
        width:          parent.width
        text:           heading
        pixelSize:      headingPixelSize
        flatColor:      headingColor
        wrapMode:       Text.WordWrap
    }
    Text {
        objectName:     flatHeading ? headingObjectName : ""
        visible:        flatHeading
        width:          parent.width
        text:           heading
        font.family:    Theme.fontDisplay
        font.italic:    Theme.fontDisplayItalic
        font.weight:    Theme.fontDisplayWeight
        font.pixelSize: headingPixelSize
        color:          headingColor
        wrapMode:       Text.WordWrap
    }
    Text {
        width:          parent.width
        text:           body
        font.family:    Theme.fontBody
        font.weight:    Theme.fontBodyWeight
        font.pixelSize: Theme.fontSzBody2
        color:          Theme.colorText2
        wrapMode:       Text.WordWrap
        lineHeight:     1.65
    }
    Rectangle {
        visible: rule
        width: parent.width; height: 1
        color: Theme.colorBorderMid
    }
}
