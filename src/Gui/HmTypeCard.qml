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
import PinPoint

Rectangle {
    id: root

    property string imageSource:    ""
    property string iconText:       ""
    property real   iconSize:       Theme.sp(32)
    property string typeName:       ""
    property string description:    ""
    property int    camerasRequired:  2
    property int    imusRequired:     3
    property int    camerasCount:     0
    property int    imusCount:        0
    property bool   camerasOptional:  false
    property bool   camerasMet:       camerasOptional || camerasCount >= camerasRequired
    property bool   imusMet:          imusCount >= imusRequired
    property bool   isSelected:       false

    signal clicked()
    signal doubleClicked()

    implicitHeight: contentCol.implicitHeight + Theme.sp(24)
    radius: Theme.radiusLg
    color:  (isSelected || hoverArea.containsMouse) ? Theme.colorAccentLight : Theme.colorSurface
    border.width: 1
    border.color: isSelected            ? Theme.colorAccent
                : hoverArea.containsMouse ? Theme.colorAccentMid
                : Theme.colorBorderMid

    // Square illustration at the top of the tile, drawn at 50% opacity and
    // masked to the card's rounded top corners (square bottom edge).
    Image {
        id: tileImage
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height:   width
        source:   root.imageSource
        visible:  false                 // shown via tileImageMasked
        fillMode: Image.PreserveAspectFit
        smooth:   true
        mipmap:   true
        asynchronous: true
        layer.enabled: true
    }

    Item {
        id: tileImageMask
        anchors.fill: tileImage
        visible: false
        layer.enabled: true

        // Rounded top corners…
        Rectangle { anchors.fill: parent; radius: Theme.radiusLg }
        // …squared-off bottom (overrides the lower rounded corners).
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height / 2
        }
    }

    MultiEffect {
        id: tileImageMasked
        anchors.fill: tileImage
        source:      tileImage
        maskEnabled: true
        maskSource:  tileImageMask
        visible:     root.imageSource !== ""

        // Dormant → backlit: a selected tile lights up — its illustration rises
        // to near-full presence with a touch of added brightness and saturation,
        // as though the artwork is back-lit. Hover gives a gentler preview lift.
        // Behaviors animate the transition so selection feels like switching on.
        opacity: root.isSelected            ? (Theme.dark ? 0.95 : 1.0)
               : hoverArea.containsMouse    ? (Theme.dark ? 0.66 : 0.86)
               :                              (Theme.dark ? 0.5  : 0.75)   // lighter themes need more presence
        brightness: root.isSelected         ? 0.22
                  : hoverArea.containsMouse  ? 0.08
                  :                            0.0
        saturation: root.isSelected ? 0.15 : 0.0

        Behavior on opacity    { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on brightness { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on saturation { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }

    Column {
        id: contentCol
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.sp(12) }
        spacing: 0

        Item { width: 1; height: Theme.sp(4) }

        // Fixed-height icon slot: the glyph is vertically centred and sized
        // per-icon (iconSize) so different Unicode symbols share a common
        // visual height and a consistent gap to the title across tiles.
        Item {
            width:  parent.width
            height: Theme.sp(36)

            Text {
                anchors.left:           parent.left
                anchors.verticalCenter: parent.verticalCenter
                text:           root.iconText
                font.pixelSize: root.iconSize
                color:          Theme.colorText2
            }
        }

        Item { width: 1; height: Theme.sp(4) }

        Text {
            width:          parent.width
            text:           root.typeName
            font.family:    Theme.fontBody
            font.pixelSize: Math.round(Theme.fontSzBody * 1.5)
            font.weight:    Font.Normal
            color:          Theme.colorText
        }

        Item { width: 1; height: Theme.sp(4) }

        Text {
            width:             parent.width
            height:            Theme.sp(66)        // fixed reserve so icon/title align across tiles
            text:              root.description
            font.family:       Theme.fontBody
            font.pixelSize:    Theme.fontSzBody2
            font.weight:       Font.Normal
            color:             Theme.colorText3
            wrapMode:          Text.Wrap
            maximumLineCount:  4
            elide:             Text.ElideRight
            verticalAlignment: Text.AlignTop
            visible:           root.description !== ""
        }

        Item { width: 1; height: Theme.sp(6) }

        Row {
            spacing: Theme.sp(12)

            Text {
                text: {
                    if (root.camerasOptional) return qsTr("✓ Optional camera")
                    var prefix = root.camerasMet ? "✓ " : "⚠ "
                    var noun = root.camerasRequired === 1 ? qsTr("camera") : qsTr("cameras")
                    return prefix + root.camerasRequired + " " + noun
                }
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              root.camerasOptional
                                        ? (root.camerasCount >= 1 ? Theme.colorGood : Theme.colorWarn)
                                        : (root.camerasMet ? Theme.colorGood : Theme.colorWarn)
            }

            Text {
                text:               (root.imusMet ? "✓ " : "⚠ ") + root.imusRequired + " " + qsTr("IMUs")
                font.family:        Theme.fontData
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingData
                color:              root.imusMet ? Theme.colorGood : Theme.colorWarn
            }
        }

        Item { width: 1; height: Theme.sp(4) }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:       root.clicked()
        onDoubleClicked: root.doubleClicked()
    }
}
