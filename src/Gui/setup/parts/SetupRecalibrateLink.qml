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

// The line under a setup Check page's live figure: a muted question, then the accent
// "Recalibrate" link that sends the golfer back to the Calibrate step (WizardPage.redoCalibration).
// `linkObjectName` names the link itself, so a test can click the words and not the question.
import QtQuick
import PinPointStudio

Row {
    id: recal

    property string prompt: ""
    property alias  linkObjectName: link.objectName
    signal activated()

    spacing: Theme.gap(6)

    Text {
        text:           recal.prompt
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody2
        color:          Theme.colorText3
        anchors.verticalCenter: parent.verticalCenter
    }
    PpLink {
        id: link
        text:        qsTr("Recalibrate")
        font.weight: Theme.fontBodyWeight
        hoverTint:   false
        anchors.verticalCenter: parent.verticalCenter
        onClicked:   recal.activated()
    }
}
