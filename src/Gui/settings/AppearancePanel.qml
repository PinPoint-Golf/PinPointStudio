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
import PinPointStudio

Item {
    id: root

    // Each card previews ITS theme, never the current one, so the colours come from the theme
    // tables (Theme.themeInfo) rather than the live Theme.* tokens. One entry per themeIndex,
    // in themeIndex order — a new aesthetic in Theme.themeOrder gets its two cards for free.
    readonly property var themeData: {
        var d = []
        for (var i = 0; i < Theme.themeCount; ++i)
            d.push(Theme.themeInfo(i))
        return d
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
            spacing: Theme.gap(20)

            // ── Page header ───────────────────────────────────────────────────

            Text {
                text: qsTr("Configuration")
                font.family:         Theme.fontBody
                font.pixelSize:      Theme.fontSzMicro
                font.letterSpacing:  Theme.trackingMicro
                font.capitalization: Theme.capsFont
                color: Theme.colorText3
            }

            PpDisplayText {
                text: qsTr("Appearance")
            }

            Text {
                text: qsTr("Aesthetic, colour mode, type scale and interface behaviour.")
                font.family:    Theme.fontBody
                font.pixelSize: Theme.fontSzBody2
                font.weight:    Theme.fontBodyWeight
                color:          Theme.colorText3
                wrapMode:       Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Group 1 — Aesthetic & colour mode ────────────────────────────

            Text {
                text: qsTr("Aesthetic & colour mode")
                font.family:        Theme.fontBody
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                font.capitalization: Theme.capsFont
                color: Theme.colorText3
            }

            // 4-wide card grid, one card per themeIndex, wrapping to as many rows as the themes
            // need (twelve → 3 rows, fourteen → 4 with Folio light and dark on the last).
            Item {
                id: cardGrid
                objectName: "setting_aesthetic"
                readonly property int columns: 4
                readonly property int rows:    Math.ceil(root.themeData.length / columns)
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap(16)
                implicitHeight: Theme.sp(80) * rows + Theme.sp(10) * (rows - 1)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                Repeater {
                    model: root.themeData.length
                    delegate: Rectangle {
                        id: themeCard
                        required property int index

                        readonly property var   tData:      root.themeData[index]
                        readonly property bool  isSelected: Theme.themeIndex === index
                        readonly property real  cardW:      (cardGrid.width - (cardGrid.columns - 1) * Theme.sp(10)) / cardGrid.columns

                        x: (index % cardGrid.columns) * (cardW + Theme.sp(10))
                        y: Math.floor(index / cardGrid.columns) * (Theme.sp(80) + Theme.sp(10))
                        width:  cardW
                        height: Theme.sp(80)
                        radius: Theme.radius
                        clip:   true

                        // ── Top — colour preview (52 px) ──────────────────
                        Row {
                            anchors.left:  parent.left
                            anchors.right: parent.right
                            anchors.top:   parent.top
                            height: Theme.sp(52)

                            // Rail — narrow column with three accent dots
                            Rectangle {
                                width:  Theme.sp(18)
                                height: parent.height
                                color:  tData.railBg

                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.gap(4)
                                    Repeater {
                                        model: 3
                                        Rectangle {
                                            required property int index
                                            width:   Theme.sp(6)
                                            height:  Theme.sp(6)
                                            radius:  Theme.sp(3)
                                            color:   tData.dot
                                            opacity: index === 0 ? 1.0 : 0.3
                                        }
                                    }
                                }
                            }

                            // Sidenav — medium column with faint nav lines
                            Rectangle {
                                width:  Theme.sp(32)
                                height: parent.height
                                color:  tData.sidenavBg

                                // Separator from rail
                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    width: 1
                                    color: Qt.darker(tData.sidenavBg, 1.1)
                                }

                                Column {
                                    anchors {
                                        left: parent.left; leftMargin: Theme.gap(6)
                                        top:  parent.top;  topMargin:  Theme.gap(10)
                                    }
                                    spacing: Theme.gap(6)
                                    Repeater {
                                        model: 4
                                        Rectangle {
                                            width:  Theme.sp(18)
                                            height: Theme.sp(3)
                                            radius: 1
                                            color:  tData.dot
                                            opacity: 0.2
                                        }
                                    }
                                }
                            }

                            // Content area — fills remainder
                            Rectangle {
                                width:  parent.width - Theme.sp(18) - Theme.sp(32)
                                height: parent.height
                                color:  tData.contentBg
                            }
                        }

                        // ── Bottom — label row (28 px) ────────────────────
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                            height: Theme.sp(28)
                            color:  (themeCardPress.containsMouse && !themeCard.isSelected)
                                        ? Qt.lighter(Theme.colorSurface, 1.08) : Theme.colorSurface
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                            Rectangle {
                                anchors { top: parent.top; left: parent.left; right: parent.right }
                                height:  1
                                color:   Theme.colorBorderMid
                                opacity: Theme.borderOpacityNormal
                            }

                            Row {
                                anchors { left: parent.left; leftMargin: Theme.gap(8); verticalCenter: parent.verticalCenter }
                                spacing: Theme.gap(4)

                                Text {
                                    text:           tData.aesthetic
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody2
                                    color:          Theme.colorText2
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text:           tData.mode
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            // Selected checkmark circle
                            Rectangle {
                                visible: isSelected
                                anchors { right: parent.right; rightMargin: Theme.gap(8); verticalCenter: parent.verticalCenter }
                                width:  Theme.sp(14)
                                height: Theme.sp(14)
                                radius: Theme.sp(7)
                                color:  Theme.colorAccent

                                Text {
                                    anchors.centerIn: parent
                                    text:           "✓"
                                    font.family:    Theme.fontBody
                                    font.pixelSize: Theme.sp(8)
                                    color:          Theme.colorSurface
                                }
                            }
                        }

                        // Border overlay — rendered after all children so it isn't
                        // occluded by the preview/label Rectangles that fill to the edges.
                        Rectangle {
                            anchors.fill: parent
                            radius:       parent.radius
                            color:        "transparent"
                            border.width: themeCard.isSelected ? 2 : 1
                            border.color: themeCard.isSelected ? Theme.colorAccent
                                        : themeCardPress.containsMouse ? Theme.colorAccentMid
                                        : Theme.colorBorderStrong
                            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                        }

                        PpPressable {
                            id: themeCardPress
                            onClicked:    Theme.themeIndex = index
                        }
                    }
                }
            }

            // ── Divider ───────────────────────────────────────────────────────

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 1b — Metric colours ─────────────────────────────────────
            //
            // The twelve named colours every metric is drawn in (Theme.metricColor), as THIS theme
            // and mode paint them. Retuning one here changes it for this theme and mode only — the
            // name is what a metric is assigned, the colour is the theme's, and a retune is the
            // user's version of the theme's. Which metric uses which name is set per metric in
            // Diagnostic Model → Metrics.
            Text {
                text: qsTr("Metric colours")
                font.family:        Theme.fontBody
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                font.capitalization: Theme.capsFont
                color: Theme.colorText3
            }

            ColumnLayout {
                id: metricPalette
                objectName: "setting_metricPalette"
                Layout.fillWidth: true
                spacing: Theme.gap(12)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                // The name open in the editor below the grid. "" = none open.
                property string editing: ""

                function _isRetuned(name) {
                    const o = appSettings.metricPalette[Theme.paletteKey(name)]
                    return o !== undefined && o !== ""
                }
                readonly property int retunedCount: {
                    var n = 0
                    for (var i = 0; i < Theme.metricColorNames.length; ++i)
                        if (_isRetuned(Theme.metricColorNames[i])) ++n
                    return n
                }
                function setColour(name, hex) {
                    var m = {}
                    for (var k in appSettings.metricPalette) m[k] = appSettings.metricPalette[k]
                    if (hex === "" || hex.toUpperCase() === String(Theme.paletteDefault(name)).toUpperCase())
                        delete m[Theme.paletteKey(name)]
                    else
                        m[Theme.paletteKey(name)] = hex
                    appSettings.metricPalette = m
                }
                function resetAll() {
                    var m = {}
                    const prefix = Theme.aesthetic + "/" + (Theme.dark ? "dark" : "light") + "/"
                    for (var k in appSettings.metricPalette)
                        if (k.indexOf(prefix) !== 0) m[k] = appSettings.metricPalette[k]
                    appSettings.metricPalette = m
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(16)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.gap(3)
                        Text {
                            Layout.fillWidth: true
                            text:           qsTr("Colours for %1 · %2")
                                                .arg(Theme.aesthetic.charAt(0).toUpperCase() + Theme.aesthetic.slice(1))
                                                .arg(Theme.dark ? qsTr("dark") : qsTr("light"))
                            font.family:    Theme.fontBody
                            font.pixelSize: Theme.fontSzBody
                            color:          Theme.colorText
                        }
                        Text {
                            Layout.fillWidth: true
                            text:           qsTr("Each metric is always drawn in one of these. Retune one for this theme here; choose a metric's colour in Diagnostic Model → Metrics.")
                            wrapMode:       Text.WordWrap
                            font.family:    Theme.fontData
                            font.pixelSize: Theme.fontSzMicro
                            color:          Theme.colorText3
                        }
                    }

                    PpButton {
                        Layout.alignment: Qt.AlignVCenter
                        visible: metricPalette.retunedCount > 0
                        label:   qsTr("Reset %1").arg(metricPalette.retunedCount)
                        onClicked: { metricPalette.resetAll(); metricPalette.editing = "" }
                    }
                }

                // Twelve tiles on the theme's own surface, so a colour is judged where it is drawn.
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap(16)
                    spacing: Theme.gap(8)

                    Repeater {
                        model: Theme.metricColorNames
                        delegate: Rectangle {
                            id: tile
                            required property string modelData
                            objectName: "paletteTile_" + modelData
                            readonly property bool retuned: metricPalette._isRetuned(modelData)
                            readonly property bool open:    metricPalette.editing === modelData

                            width:  Theme.sp(132)
                            height: Theme.sp(44)
                            radius: Theme.radius
                            color:  Theme.colorSurface
                            border.width: 1
                            border.color: open ? Theme.colorAccent : Theme.colorBorderStrong

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: Theme.sp(10)
                                spacing: Theme.gap(10)

                                // A short trace, not a blob — a metric is a line on a chart, and a
                                // colour that works as a fill can still be too faint as a 2 px stroke.
                                Item {
                                    width:  Theme.sp(26)
                                    height: Theme.sp(20)
                                    anchors.verticalCenter: parent.verticalCenter
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width; height: Theme.sp(2); radius: 1
                                        color: Theme.paletteColor(tile.modelData)
                                        rotation: -24
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: Theme.sp(8); height: width; radius: width / 2
                                        color: Theme.paletteColor(tile.modelData)
                                    }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.gap(1)
                                    Text {
                                        text: tile.modelData.charAt(0).toUpperCase() + tile.modelData.slice(1)
                                        font.family:    Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody2
                                        color:          Theme.colorText
                                    }
                                    Text {
                                        text: String(Theme.paletteColor(tile.modelData)).toUpperCase()
                                              + (tile.retuned ? "  ·  " + qsTr("yours") : "")
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color: tile.retuned ? Theme.colorAccent : Theme.colorText3
                                    }
                                }
                            }

                            PpPressable {
                                hoverScale: 1.03
                                onClicked: metricPalette.editing = tile.open ? "" : tile.modelData
                            }
                        }
                    }
                }

                // The editor for the open name: a hex, the theme's value beside the user's, and a
                // reset. Typed rather than picked from a wheel — the app has no native dialogs, and a
                // hex is exact where a drag is not.
                RowLayout {
                    objectName: "paletteEditor"
                    visible: metricPalette.editing !== ""
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap(16)
                    spacing: Theme.gap(10)

                    Text {
                        text: metricPalette.editing.length
                              ? metricPalette.editing.charAt(0).toUpperCase() + metricPalette.editing.slice(1)
                              : ""
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }

                    PpTextField {
                        id: hexField
                        objectName: "paletteHexField"
                        implicitWidth: Theme.sp(110)
                        font.family: Theme.fontData
                        text: metricPalette.editing.length
                              ? String(Theme.paletteColor(metricPalette.editing)).toUpperCase() : ""
                        readonly property bool valid: /^#?[0-9A-Fa-f]{6}$/.test(text.trim())
                        hasError: !valid
                        onEditingFinished: {
                            if (!valid || metricPalette.editing === "") return
                            var t = text.trim()
                            if (t.charAt(0) !== "#") t = "#" + t
                            metricPalette.setColour(metricPalette.editing, t.toUpperCase())
                        }
                    }

                    Rectangle {
                        width: Theme.sp(22); height: width; radius: width / 2
                        color: hexField.valid
                               ? (hexField.text.trim().charAt(0) === "#" ? hexField.text.trim() : "#" + hexField.text.trim())
                               : "transparent"
                        border.width: 1
                        border.color: Theme.colorBorderStrong
                    }

                    Text {
                        text: qsTr("Theme: %1").arg(metricPalette.editing.length
                              ? String(Theme.paletteDefault(metricPalette.editing)).toUpperCase() : "")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }

                    Item { Layout.fillWidth: true }

                    PpButton {
                        visible: metricPalette.editing !== "" && metricPalette._isRetuned(metricPalette.editing)
                        label:   qsTr("Reset")
                        onClicked: metricPalette.setColour(metricPalette.editing, "")
                    }
                }
            }

            // ── Divider ───────────────────────────────────────────────────────

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 2 — Type scale ──────────────────────────────────────────

            Text {
                text: qsTr("Type scale")
                font.family:        Theme.fontBody
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                font.capitalization: Theme.capsFont
                color: Theme.colorText3
            }

            // Text size row
            RowLayout {
                objectName: "setting_textSize"
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)

                    Text {
                        Layout.fillWidth: true
                        text:           qsTr("Text size")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Scales all fonts and spacing proportionally · %1%").arg(Math.round(Theme.fontScale * 100))
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.gap(8)
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        text:           "A"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.sp(10)
                        color:          Theme.colorText3
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Slider {
                        id: fontScaleSlider
                        from: 0.8; to: 1.5; stepSize: 0.05
                        value: Theme.fontScale
                        implicitWidth: Theme.sp(160)
                        onMoved: Theme.fontScale = value

                        background: Rectangle {
                            x: fontScaleSlider.leftPadding
                            y: fontScaleSlider.topPadding + fontScaleSlider.availableHeight / 2 - height / 2
                            width:  fontScaleSlider.availableWidth
                            height: Theme.sp(3)
                            radius: Theme.sp(2)
                            color:  Theme.colorBg3

                            Rectangle {
                                width:  fontScaleSlider.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color:  Theme.colorAccent
                            }
                        }

                        handle: Rectangle {
                            x: fontScaleSlider.leftPadding + fontScaleSlider.visualPosition * (fontScaleSlider.availableWidth - width)
                            y: fontScaleSlider.topPadding + fontScaleSlider.availableHeight / 2 - height / 2
                            width:  Theme.sp(14)
                            height: Theme.sp(14)
                            radius: Theme.sp(7)
                            color:  Theme.colorAccent
                            border.color: Theme.colorSurface
                            border.width: 2
                        }
                    }

                    Text {
                        text:           "A"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.sp(18)
                        color:          Theme.colorText3
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // Live font scale preview
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gap(26)
                implicitHeight: Theme.sp(90)
                color:        Theme.colorSurface
                border.width: 1
                border.color: Theme.colorBorderMid
                radius:       Theme.radius
                clip:         true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Mini header bar
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.sp(28)
                        color: Theme.colorSurface

                        Rectangle {
                            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                            height:  1
                            color:   Theme.colorBorderMid
                            opacity: Theme.borderOpacityNormal
                        }

                        RowLayout {
                            anchors.fill:        parent
                            anchors.leftMargin:  Theme.gap(10)
                            anchors.rightMargin: Theme.gap(10)
                            spacing: Theme.gap(8)

                            Text {
                                text:        "Pinpoint"
                                font.family: Theme.fontDisplay
                                font.italic: Theme.fontDisplayItalic
                                font.weight: Theme.fontDisplayWeight
                                font.pixelSize: Theme.fontSzBody
                                color: Theme.colorText
                            }

                            Rectangle {
                                width:   1
                                height:  Theme.sp(12)
                                color:   Theme.colorBorderStrong
                                opacity: Theme.borderOpacityStrong
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                text:                "Swing analysis"
                                font.family:         Theme.fontBody
                                font.pixelSize:      Theme.fontSzLabel
                                font.capitalization: Theme.capsFont
                                font.letterSpacing:  Theme.trackingLabel
                                color: Theme.colorText3
                            }

                            Item { Layout.fillWidth: true }

                            Item {
                                implicitWidth:  pvLabel.implicitWidth + Theme.sp(10)
                                implicitHeight: pvLabel.implicitHeight + Theme.sp(6)
                                Layout.alignment: Qt.AlignVCenter

                                Rectangle {
                                    anchors.fill:  parent
                                    color:         "transparent"
                                    border.width:  1
                                    border.color:  Theme.colorBorderMid
                                    radius:        Theme.radius
                                }
                                Text {
                                    id: pvLabel
                                    anchors.centerIn: parent
                                    text:           "v0.9.2-alpha"
                                    font.family:    Theme.fontData
                                    font.pixelSize: Theme.fontSzMicro
                                    color:          Theme.colorText3
                                }
                            }
                        }
                    }

                    // Three metric cells
                    RowLayout {
                        Layout.fillWidth:  true
                        Layout.fillHeight: true
                        spacing: 0

                        Repeater {
                            model: [
                                { label: "Club speed",   value: "96.4",  unit: "mph" },
                                { label: "Attack angle", value: "−3.2°", unit: "deg" },
                                { label: "Face to path", value: "+1.8°", unit: "deg" }
                            ]

                            delegate: Item {
                                required property var modelData
                                required property int index

                                Layout.fillWidth:  true
                                Layout.fillHeight: true

                                Rectangle {
                                    visible: index > 0
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    width:   1
                                    color:   Theme.colorBorderMid
                                    opacity: Theme.borderOpacityNormal
                                }

                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: Theme.gap(1)

                                    Text {
                                        text:           modelData.label
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          Theme.colorText3
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text:        modelData.value
                                        font.family: Theme.fontDisplay
                                        font.italic: Theme.fontDisplayItalic
                                        font.weight: Theme.fontDisplayWeight
                                        font.pixelSize: Theme.fontSzHeading
                                        color: Theme.colorText
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text:           modelData.unit
                                        font.family:    Theme.fontData
                                        font.pixelSize: Theme.fontSzMicro
                                        color:          Theme.colorText3
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Divider ───────────────────────────────────────────────────────

            PpDivider { orientation: Qt.Horizontal; Layout.fillWidth: true }

            // ── Group 3 — Interface ───────────────────────────────────────────

            Text {
                text: qsTr("Interface")
                font.family:        Theme.fontBody
                font.pixelSize:     Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                font.capitalization: Theme.capsFont
                color: Theme.colorText3
            }

            // Density row
            RowLayout {
                objectName: "setting_density"
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)

                    Text {
                        Layout.fillWidth: true
                        text:           qsTr("Density")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Compact reduces rail and panel padding")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                PpChipGroup {
                    Layout.alignment: Qt.AlignVCenter
                    options:  ["Compact", "Default", "Spacious"]
                    selected: Theme.density.charAt(0).toUpperCase() + Theme.density.slice(1)
                    onSelectionChanged: (value) => Theme.density = value.toLowerCase()
                }
            }

            // Reduce motion row
            RowLayout {
                objectName: "setting_reduceMotion"
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)

                    Text {
                        Layout.fillWidth: true
                        text:           qsTr("Reduce motion")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Disables all transitions and animations")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                // Toggle — Behavior on x is intentionally exempt from reduceMotion
                // (it controls the thumb itself, not content)
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  Theme.reduceMotion ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.reduceMotion ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    Theme.reduceMotion = !Theme.reduceMotion
                    }
                }
            }

            // Gradient titles row
            RowLayout {
                objectName: "setting_gradientTitles"
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)

                    Text {
                        Layout.fillWidth: true
                        text:           qsTr("Gradient titles")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Warm→cool brand gradient on large display headings")
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                // Toggle — Behavior on x is intentionally exempt from reduceMotion
                // (it controls the thumb itself, not content)
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width:  Theme.sp(34)
                    height: Theme.sp(18)
                    radius: Theme.sp(9)
                    color:  Theme.gradientTitles ? Theme.colorAccent : Theme.colorBg3
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                    Rectangle {
                        width:  Theme.sp(12)
                        height: Theme.sp(12)
                        radius: Theme.sp(6)
                        color:  "white"
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.gradientTitles ? parent.width - width - Theme.sp(3) : Theme.sp(3)
                        Behavior on x { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    Theme.gradientTitles = !Theme.gradientTitles
                    }
                }
            }

            // Overlay opacity row
            RowLayout {
                objectName: "setting_overlayOpacity"
                Layout.fillWidth: true
                spacing: Theme.gap(16)
                property bool searchHighlight: false
                Rectangle { x: -Theme.sp(6); y: -Theme.sp(6); width: parent.width + Theme.sp(12); height: parent.height + Theme.sp(12); color: Theme.colorAccentLight; radius: Theme.radius; opacity: parent.searchHighlight ? 1.0 : 0.0; z: -1 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)

                    Text {
                        Layout.fillWidth: true
                        text:           qsTr("Overlay opacity")
                        font.family:    Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color:          Theme.colorText
                    }
                    Text {
                        text:           qsTr("Skeleton and angle guide overlays · %1%").arg(Math.round(Theme.overlayOpacity * 100))
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }

                Row {
                    spacing: Theme.gap(8)
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        text:           "0"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Slider {
                        id: opacitySlider
                        from: 0.0; to: 1.0; stepSize: 0.05
                        value: Theme.overlayOpacity
                        implicitWidth: Theme.sp(160)
                        onMoved: Theme.overlayOpacity = value

                        background: Rectangle {
                            x: opacitySlider.leftPadding
                            y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                            width:  opacitySlider.availableWidth
                            height: Theme.sp(3)
                            radius: Theme.sp(2)
                            color:  Theme.colorBg3

                            Rectangle {
                                width:  opacitySlider.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color:  Theme.colorAccent
                            }
                        }

                        handle: Rectangle {
                            x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width)
                            y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                            width:  Theme.sp(14)
                            height: Theme.sp(14)
                            radius: Theme.sp(7)
                            color:  Theme.colorAccent
                            border.color: Theme.colorSurface
                            border.width: 2
                        }
                    }

                    Text {
                        text:           "100"
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
