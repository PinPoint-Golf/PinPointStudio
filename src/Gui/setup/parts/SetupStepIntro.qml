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
// Named Setup… because several other files still declare an inline StepIntro / CheckRow /
// TogglePill of their own; a module type of the same name would sit beside each of them.
import QtQuick
import PinPointStudio

Column {
    property string eyebrow: ""
    property string heading: ""
    property string body:    ""
    spacing: Theme.sp(8)

    Text {
        text:               eyebrow
        font.family:        Theme.fontData
        font.pixelSize:     Theme.fontSzMicro
        font.letterSpacing: Theme.trackingMicro
        color:              Theme.colorText3
    }
    PpDisplayText {
        width:          parent.width
        text:           heading
        pixelSize:      Math.min(Theme.sp(20), Theme.fontSzDisplay)
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
        width: parent.width; height: 1
        color: Theme.colorBorderMid
    }
}
