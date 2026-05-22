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
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PinPoint

Item {
    id: root

    // Build a model of connected screens for use in ComboBoxes.
    // Qt.application.screens returns the list of QScreen objects available at runtime.
    readonly property var screenModel: {
        var list = []
        var screens = Qt.application.screens
        for (var i = 0; i < screens.length; i++) {
            var s = screens[i]
            list.push({
                label: qsTr("Display %1 — %2 · %3×%4")
                           .arg(i + 1)
                           .arg(s.name)
                           .arg(s.width)
                           .arg(s.height),
                value: "screen:" + i
            })
        }
        return list
    }

    // Helper: find the ComboBox index for a stored value string
    function mainDisplayIndexFor(stored) {
        if (stored === "primary") return 0
        if (stored === "cursor")  return 1
        for (var i = 0; i < root.screenModel.length; i++) {
            if (root.screenModel[i].value === stored) return 2 + i
        }
        return 0
    }

    function secondaryDisplayIndexFor(stored) {
        if (stored === "none") return 0
        for (var i = 0; i < root.screenModel.length; i++) {
            if (root.screenModel[i].value === stored) return 1 + i
        }
        return 0
    }

    // ── Search scroll-to support ──────────────────────────────────────────────

    property string lastHighlightId: ""

    function findChild(parent, name) {
        for (var i = 0; i < parent.children.length; i++) {
            var child = parent.children[i]
            if (child.objectName === name) return child
            var found = findChild(child, name)
            if (found) return found
        }
        return null
    }

    function scrollToItem(itemId) {
        if (!itemId) return true
        var target = findChild(contentCol, itemId)
        if (!target) return false
        var mapped = target.mapToItem(contentCol, 0, 0)
        scrollView.contentItem.contentY = Math.max(0, Math.min(
            mapped.y - Theme.sp(24),
            scrollView.contentItem.contentHeight - scrollView.height
        ))
        target.searchHighlight = true
        lastHighlightId = itemId
        highlightTimer.restart()
        return true
    }

    Timer {
        id: highlightTimer
        interval: 1800
        onTriggered: {
            var target = findChild(contentCol, lastHighlightId)
            if (target) target.searchHighlight = false
        }
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: contentCol.y + contentCol.implicitHeight + Theme.sp(28)

        ColumnLayout {
            id: contentCol
            x: Theme.sp(32)
            y: Theme.sp(28)
            width: parent.width - Theme.sp(64)
            spacing: Theme.sp(20)

            // ── Page header ───────────────────────────────────────────────────

            Text {
                text: qsTr("CONFIGURATION")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            Text {
                text: qsTr("Displays")
                font.family:    Theme.fontDisplay
                font.italic:    Theme.fontDisplayItalic
                font.pixelSize: Theme.fontSzDisplay
                color: Theme.colorText
            }

            Text {
                text: qsTr("Configure which monitors Pinpoint uses and how post-shot content is presented.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Font.Light
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Monitor diagram ───────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                implicitHeight: monRow.implicitHeight + legend.implicitHeight + Theme.sp(8)

                Row {
                    id: monRow
                    spacing: Theme.sp(12)
                    anchors.top: parent.top
                    anchors.left: parent.left

                    Repeater {
                        model: Qt.application.screens

                        delegate: Column {
                            required property var modelData
                            required property int index

                            readonly property bool isMain:
                                appSettings.mainDisplayMode === "screen:" + index
                                || (appSettings.mainDisplayMode === "primary" && index === 0)
                                || (appSettings.mainDisplayMode === "cursor"  && index === 0)
                            readonly property bool isSecondary: appSettings.secondaryDisplayMode === "screen:" + index

                            spacing: Theme.sp(4)
                            anchors.bottom: parent.bottom

                            Rectangle {
                                width:  Theme.sp(80)
                                height: Theme.sp(50)
                                radius: Theme.radius
                                color:  isMain      ? Theme.colorAccentLight
                                      : isSecondary ? Theme.colorGoodLight
                                      : Theme.colorBg3
                                border.width: 1
                                border.color: isMain      ? Theme.colorAccent
                                            : isSecondary ? Theme.colorGood
                                            : Theme.colorBorderStrong
                                Behavior on color        { ColorAnimation { duration: Theme.durationFast } }
                                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                                Rectangle {
                                    visible: isMain || isSecondary
                                    anchors.top:     parent.top
                                    anchors.right:   parent.right
                                    anchors.margins: Theme.sp(4)
                                    implicitWidth:  badgeText.implicitWidth + Theme.sp(8)
                                    implicitHeight: Theme.sp(14)
                                    radius: 1
                                    color:  isMain ? Theme.colorAccent : Theme.colorGood

                                    Text {
                                        id: badgeText
                                        anchors.centerIn: parent
                                        text:           isMain ? qsTr("Pinpoint") : qsTr("Post-shot")
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.sp(7)
                                        color:          Theme.colorSurface
                                    }
                                }

                                Text {
                                    anchors.left:    parent.left
                                    anchors.bottom:  parent.bottom
                                    anchors.margins: Theme.sp(4)
                                    text:            (index + 1).toString()
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color: isMain || isSecondary ? Theme.colorAccent : Theme.colorText3
                                }
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width:  Theme.sp(12)
                                height: Theme.sp(5)
                                color:  Theme.colorBorderStrong
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width:  Theme.sp(22)
                                height: Theme.sp(2)
                                radius: 1
                                color:  Theme.colorBorderStrong
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text:           modelData.name + " · " + modelData.width + "×" + modelData.height
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.sp(8)
                                color:          Theme.colorText3
                            }
                        }
                    }
                }

                Row {
                    id: legend
                    anchors.top:        monRow.bottom
                    anchors.topMargin:  Theme.sp(10)
                    anchors.left:       parent.left
                    spacing: Theme.sp(16)

                    Repeater {
                        model: [
                            { color: Theme.colorAccent,       label: qsTr("Pinpoint main window") },
                            { color: Theme.colorGood,         label: qsTr("Post-shot display")    },
                            { color: Theme.colorBorderStrong, label: qsTr("Available")            }
                        ]
                        delegate: Row {
                            required property var modelData
                            spacing: Theme.sp(5)
                            Rectangle {
                                width:  Theme.sp(8)
                                height: Theme.sp(8)
                                radius: 1
                                color:  modelData.color
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text:           modelData.label
                                font.family:    Theme.fontData
                                font.pixelSize: Theme.fontSzMicro
                                color:          Theme.colorText3
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            // ── Group 1 — Main window ─────────────────────────────────────────

            Text {
                text: qsTr("MAIN WINDOW")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Launch on row
            RowLayout {
                objectName: "setting_launchOn"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Launch on")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Which display opens the main Pinpoint window")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                ComboBox {
                    id: launchOnCombo
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Theme.sp(220)

                    readonly property var mainDisplayLabels: {
                        var fixed = [
                            qsTr("Primary monitor"),
                            qsTr("Monitor with cursor at launch")
                        ]
                        return fixed.concat(root.screenModel.map(function(s) { return s.label }))
                    }

                    model: mainDisplayLabels

                    Component.onCompleted: currentIndex = root.mainDisplayIndexFor(appSettings.mainDisplayMode)

                    onActivated: (index) => {
                        if (index === 0)      appSettings.mainDisplayMode = "primary"
                        else if (index === 1) appSettings.mainDisplayMode = "cursor"
                        else                  appSettings.mainDisplayMode = root.screenModel[index - 2].value
                    }

                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Font.Light

                    contentItem: Text {
                        leftPadding: Theme.sp(10)
                        text:           launchOnCombo.displayText
                        font:           launchOnCombo.font
                        color:          Theme.colorText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        color:        Theme.colorSurface
                        border.width: 1
                        border.color: Theme.colorBorderStrong
                        radius:       Theme.radius
                    }

                    indicator: Text {
                        x: launchOnCombo.width - width - Theme.sp(10)
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "⌄"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText3
                    }

                    popup: Popup {
                        y: launchOnCombo.height
                        width: launchOnCombo.width
                        padding: 0

                        background: Rectangle {
                            color:        Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorderStrong
                            radius:       Theme.radius
                        }

                        contentItem: ListView {
                            implicitHeight: contentHeight
                            model: launchOnCombo.delegateModel
                            clip: true
                        }
                    }

                    delegate: ItemDelegate {
                        required property string modelData
                        required property int    index

                        width: launchOnCombo.width
                        highlighted: launchOnCombo.highlightedIndex === index

                        contentItem: Text {
                            leftPadding: Theme.sp(10)
                            text:           modelData
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Font.Light
                            color:          Theme.colorText
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.highlighted ? Theme.colorAccentLight : "transparent"
                        }
                    }
                }
            }

            // Remember window geometry row
            RowLayout {
                objectName: "setting_rememberGeometry"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.windowMaximized ? 0.4 : 1.0
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Remember window size and position")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Restores last session geometry on next launch")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: rememberGeomToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.rememberWindowGeometry

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: rememberGeomToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.rememberWindowGeometry = !appSettings.rememberWindowGeometry
                    }
                }
            }

            // Launch in full screen row
            RowLayout {
                objectName: "setting_fullScreen"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Launch in full screen")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Overrides saved window size if enabled")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: fullScreenToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.windowMaximized

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: fullScreenToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.windowMaximized = !appSettings.windowMaximized
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 2 — Post-shot display ───────────────────────────────────

            Text {
                text: qsTr("POST-SHOT DISPLAY")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Secondary display row
            RowLayout {
                objectName: "setting_secondaryDisplay"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Secondary display")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Golfer-facing screen shown after each swing")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                ComboBox {
                    id: secondaryDisplayCombo
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Theme.sp(220)

                    readonly property var secondaryDisplayLabels: {
                        var fixed = [ qsTr("None") ]
                        return fixed.concat(root.screenModel.map(function(s) { return s.label }))
                    }

                    model: secondaryDisplayLabels

                    Component.onCompleted: currentIndex = root.secondaryDisplayIndexFor(appSettings.secondaryDisplayMode)

                    onActivated: (index) => {
                        if (index === 0) appSettings.secondaryDisplayMode = "none"
                        else             appSettings.secondaryDisplayMode = root.screenModel[index - 1].value
                    }

                    font.family:    Theme.fontBody
                    font.pixelSize: Theme.fontSzBody2
                    font.weight:    Font.Light

                    contentItem: Text {
                        leftPadding: Theme.sp(10)
                        text:           secondaryDisplayCombo.displayText
                        font:           secondaryDisplayCombo.font
                        color:          Theme.colorText
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        color:        Theme.colorSurface
                        border.width: 1
                        border.color: Theme.colorBorderStrong
                        radius:       Theme.radius
                    }

                    indicator: Text {
                        x: secondaryDisplayCombo.width - width - Theme.sp(10)
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "⌄"
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText3
                    }

                    popup: Popup {
                        y: secondaryDisplayCombo.height
                        width: secondaryDisplayCombo.width
                        padding: 0

                        background: Rectangle {
                            color:        Theme.colorSurface
                            border.width: 1
                            border.color: Theme.colorBorderStrong
                            radius:       Theme.radius
                        }

                        contentItem: ListView {
                            implicitHeight: contentHeight
                            model: secondaryDisplayCombo.delegateModel
                            clip: true
                        }
                    }

                    delegate: ItemDelegate {
                        required property string modelData
                        required property int    index

                        width: secondaryDisplayCombo.width
                        highlighted: secondaryDisplayCombo.highlightedIndex === index

                        contentItem: Text {
                            leftPadding: Theme.sp(10)
                            text:           modelData
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody2
                            font.weight:    Font.Light
                            color:          Theme.colorText
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            color: parent.highlighted ? Theme.colorAccentLight : "transparent"
                        }
                    }
                }
            }

            // Post-shot content row (dimmed when no secondary display)
            RowLayout {
                objectName: "setting_postShotContent"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }
                opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Post-shot content")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("What the golfer sees on the secondary display")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    options: [qsTr("Replay"), qsTr("Metrics"), qsTr("Replay + metrics")]
                    selected: {
                        if (appSettings.postShotContent === "replay")   return qsTr("Replay")
                        if (appSettings.postShotContent === "metrics")  return qsTr("Metrics")
                        return qsTr("Replay + metrics")
                    }
                    onSelectionChanged: (value) => {
                        if (value === qsTr("Replay"))         appSettings.postShotContent = "replay"
                        else if (value === qsTr("Metrics"))   appSettings.postShotContent = "metrics"
                        else                                  appSettings.postShotContent = "replay+metrics"
                    }
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Display delay row (dimmed when no secondary display)
            RowLayout {
                objectName: "setting_postShotDelay"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Display delay")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Pause before showing post-shot content")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    options: [qsTr("None"), qsTr("0.5 s"), qsTr("1.0 s"), qsTr("2.0 s")]
                    selected: {
                        var d = appSettings.postShotDelay
                        if (d < 0.1)  return qsTr("None")
                        if (d < 0.75) return qsTr("0.5 s")
                        if (d < 1.5)  return qsTr("1.0 s")
                        return qsTr("2.0 s")
                    }
                    onSelectionChanged: (value) => {
                        if (value === qsTr("None"))           appSettings.postShotDelay = 0.0
                        else if (value === qsTr("0.5 s"))     appSettings.postShotDelay = 0.5
                        else if (value === qsTr("1.0 s"))     appSettings.postShotDelay = 1.0
                        else                                  appSettings.postShotDelay = 2.0
                    }
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Mirror main window row (dimmed when no secondary display)
            RowLayout {
                objectName: "setting_mirrorMain"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                opacity: appSettings.secondaryDisplayMode === "none" ? 0.4 : 1.0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Mirror main window")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Shows the full Pinpoint interface rather than the simplified post-shot view")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: mirrorToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.postShotMirror

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: mirrorToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.postShotMirror = !appSettings.postShotMirror
                    }
                }
            }

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 3 — Rendering ───────────────────────────────────────────

            Text {
                text: qsTr("RENDERING")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Font.AllUppercase
                color: Theme.colorText3
            }

            // Frame rate cap row
            RowLayout {
                objectName: "setting_frameRateCap"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Frame rate cap")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("UI rendering — independent of camera capture rate")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    options: [qsTr("30 fps"), qsTr("60 fps"), qsTr("Match display")]
                    selected: {
                        if (appSettings.uiFrameRateCap === "30") return qsTr("30 fps")
                        if (appSettings.uiFrameRateCap === "60") return qsTr("60 fps")
                        return qsTr("Match display")
                    }
                    onSelectionChanged: (value) => {
                        if (value === qsTr("30 fps"))      appSettings.uiFrameRateCap = "30"
                        else if (value === qsTr("60 fps")) appSettings.uiFrameRateCap = "60"
                        else                               appSettings.uiFrameRateCap = "display"
                    }
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // Hardware acceleration row
            RowLayout {
                objectName: "setting_hwAccel"
                Layout.fillWidth: true
                spacing: Theme.sp(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Text {
                        text:           qsTr("Hardware acceleration")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Uses GPU for video decode and overlay rendering")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Rectangle {
                    id: hwAccelToggle
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  checked ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    property bool checked: appSettings.hardwareAcceleration

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: hwAccelToggle.checked ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    appSettings.hardwareAcceleration = !appSettings.hardwareAcceleration
                    }
                }
            }
        }
    }
}
