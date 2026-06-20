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

// Shared dropdown selector. A thin styling layer over the Basic ComboBox so every
// drop-down across the app shares one look, one (text-scaled) height, and one place
// to evolve "app flair" later. Callers drive it like a normal ComboBox — set
// `model`, `currentIndex`, `onActivated`, `implicitWidth`, `enabled`, `visible` — and
// keep any external sync logic (Component.onCompleted / Connections) on the instance.
//
// Extras over the stock control:
//   displaySuffix  — text appended to every label, closed view and rows (e.g. " Hz")
//   itemEnabledFn  — optional function(index) -> bool to disable individual rows
//                    (e.g. an already-assigned placement); disabled rows are inert
//                    and muted. Read whatever reactive state you need inside it so
//                    QML tracks the dependency.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import PinPointStudio

ComboBox {
    id: root

    property string displaySuffix: ""
    property var    itemEnabledFn: null

    implicitHeight: Theme.sp(34)

    font.family:    Theme.fontBody
    font.pixelSize: Theme.fontSzBody2
    font.weight:    Theme.fontBodyWeight

    contentItem: Text {
        leftPadding:       Theme.sp(10)
        rightPadding:      Theme.sp(26)   // clear the chevron
        text:              root.displayText + root.displaySuffix
        font:              root.font
        color:             root.enabled ? Theme.colorText : Theme.colorText3
        verticalAlignment: Text.AlignVCenter
        elide:             Text.ElideRight
    }

    background: Rectangle {
        color:        Theme.colorSurface
        radius:       Theme.radius
        border.width: 1
        border.color: root.activeFocus ? Theme.colorAccent
                    : root.enabled     ? Theme.colorBorderStrong
                    :                     Theme.colorBorderMid
    }

    indicator: Text {
        x: root.width - width - Theme.sp(10)
        anchors.verticalCenter: parent.verticalCenter
        text:           "⌄"
        font.family:    Theme.fontBody
        font.pixelSize: Theme.fontSzBody
        color:          root.enabled ? Theme.colorText3 : Theme.colorBorderStrong
    }

    popup: Popup {
        y:       root.height + Theme.sp(2)
        width:   root.width
        padding: 0
        // Cap tall lists; +2 accounts for the 1px top/bottom borders.
        implicitHeight: Math.min(listView.contentHeight + 2, Theme.sp(280))

        background: Rectangle {
            color:        Theme.colorSurface
            radius:       Theme.radius
            border.width: 1
            border.color: Theme.colorBorderStrong
        }

        contentItem: ListView {
            id: listView
            clip:           true
            implicitHeight: contentHeight
            model:          root.delegateModel
            currentIndex:   root.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var modelData
        required property int index

        width:          root.width
        implicitHeight: Theme.sp(34)
        enabled:        root.itemEnabledFn ? root.itemEnabledFn(index) : true
        highlighted:    root.highlightedIndex === index

        contentItem: Text {
            leftPadding:       Theme.sp(10)
            rightPadding:      Theme.sp(10)
            text:              itemDelegate.modelData + root.displaySuffix
            font.family:       Theme.fontBody
            font.pixelSize:    root.font.pixelSize
            font.weight:       Theme.fontBodyWeight
            color:             itemDelegate.enabled ? Theme.colorText : Theme.colorText3
            verticalAlignment: Text.AlignVCenter
            elide:             Text.ElideRight
        }

        background: Rectangle {
            color: (itemDelegate.highlighted || itemDelegate.hovered) && itemDelegate.enabled
                       ? Theme.colorAccentLight : "transparent"
        }
    }
}
