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

// One row of the closing page's summary table: dot, label, value. Ported verbatim from
// the retired ScreenSessionWizard.qml's inline `component SummaryRow` (l.2473–2503), ready for the Stage 5b
// closing page, which renders SetupFlow.summaryRows through it.
import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: sr
    property string rowLabel: ""
    property string rowValue: ""
    property bool   good:     true
    // "neutral": neither in place nor missing (optional kit that is not there) — drawn muted.
    // Added at Stage 5c; "" keeps the good / warn colours.
    property string tone:     ""
    readonly property color _color: tone === "neutral" ? Theme.colorText3
                                                       : (good ? Theme.colorGood : Theme.colorWarn)
    height: Theme.sp(44)

    RowLayout {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.gap(14) }
        spacing: Theme.gap(10)

        Rectangle {
            width: Theme.sp(6); height: Theme.sp(6); radius: Theme.sp(3)
            color: sr._color
        }
        Text {
            Layout.fillWidth: true
            text:           sr.rowLabel
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            color:          Theme.colorText
        }
        Text {
            text:               sr.rowValue
            font.family:        Theme.fontData
            font.pixelSize:     Theme.fontSzMicro
            font.letterSpacing: Theme.trackingData
            color:              sr._color
        }
    }
}
