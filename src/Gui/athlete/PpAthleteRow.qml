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
import PinPointStudio

Item {
    id: root

    property var  athleteData: ({})
    property bool isSelected:  false
    property int  rowIndex:    0

    // The hover "Switch" chip shows on rows other than the current golfer, and never while a
    // session runs (its swings are being saved to the current golfer; the picker says so).
    property bool canSwitch: true

    signal clicked()
    signal editRequested()
    signal switchRequested()

    height: Theme.sp(44)

    Rectangle {
        anchors.fill: parent
        readonly property color _restColor: root.rowIndex % 2 === 0 ? Theme.colorSurface : Theme.colorBg
        color: root.isSelected
                   ? Theme.colorAccentLight
                   : (rowPress.containsMouse ? Qt.lighter(_restColor, 1.08) : _restColor)
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }

    Row {
        anchors {
            left:           parent.left
            right:          parent.right
            verticalCenter: parent.verticalCenter
            leftMargin:     Theme.gap(12)
            rightMargin:    Theme.gap(12)
        }
        spacing: Theme.gap(12)
        // Above rowPress so the Switch chip takes its own press; the rest of the row has no
        // mouse handling, so its presses still fall through to rowPress.
        z: 1

        // Avatar circle
        PpAvatar {
            anchors.verticalCenter: parent.verticalCenter
            uuid:     root.athleteData.uuid || ""
            initials: root.athleteData.initials || ""
        }

        // Name — fills remaining space, with the hover Switch chip just to its right
        Item {
            id: nameCell
            width:  parent.width - Theme.sp(28) - Theme.sp(60) - Theme.sp(60) - Theme.sp(60) - Theme.sp(72) - Theme.sp(16) - parent.spacing * 5
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: nameText
                anchors.verticalCenter: parent.verticalCenter
                // Leave room for the chip so a long name elides rather than running under it.
                width:          Math.min(implicitWidth,
                                         nameCell.width - (switchChip.visible ? switchChip.width + Theme.gap(10) : 0))
                text:           athleteData.name || ""
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          Theme.colorText
                elide:          Text.ElideRight
            }

            PpRowAction {
                id: switchChip
                objectName: "athleteSwitch"
                x:       nameText.width + Theme.gap(10)
                anchors.verticalCenter: parent.verticalCenter
                visible: root.canSwitch && (rowPress.containsMouse || switchChip.hovered)
                text:    qsTr("Switch")
                tooltip: qsTr("Switch to %1 and go to Home").arg(athleteData.name || "")
                onClicked: root.switchRequested()
            }
        }

        // Handedness
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           (athleteData.handedness || "Right")[0] + "H"
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Handicap
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           Theme.formatHandicap(athleteData.handicap)
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Sessions
        Text {
            width:          Theme.sp(60)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            text:           (athleteData.sessionCount || 0) + " sess"
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
        }

        // Last seen
        Text {
            width:          Theme.sp(72)
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
            font.family:    Theme.fontData
            font.pixelSize: Theme.sp(10)
            color:          Theme.colorText3
            text: {
                const ts = athleteData.lastSessionAt || 0
                if (ts === 0) return qsTr("never")
                const days = Math.floor((Date.now() / 1000 - ts) / 86400)
                if (days === 0) return qsTr("today")
                if (days === 1) return qsTr("yesterday")
                return qsTr("%1d ago").arg(days)
            }
        }

        // Edit affordance — pencil-on-hover (display only; the hit-target is the
        // root-level chevronMa below, stacked above rowPress).
        Item {
            width:  Theme.sp(16)
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text: (chevronMa.containsMouse || rowPress.containsMouse) ? "✎" : "›"
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody
                color:          chevronMa.containsMouse ? Theme.colorAccent : Theme.colorText3
            }
        }
    }

    PpPressable {
        id:              rowPress
        hoverScale:      1.0
        onClicked:       root.clicked()
        onDoubleClicked: root.editRequested()
    }

    // Dedicated edit hit-target over the chevron column. Declared AFTER rowPress
    // so it sits on top: a click on the pencil routes to edit, while clicks
    // anywhere else on the row fall through to rowPress and select.
    MouseArea {
        id: chevronMa
        anchors {
            right:  parent.right
            top:    parent.top
            bottom: parent.bottom
        }
        width:        Theme.sp(28)
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.editRequested()
    }
}
