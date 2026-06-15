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

// Read-only swing.json inspector: region presets, a coverage strip, and a gap-aware
// detail table. Hosted as PpModeStage's tableDelegate. Backed by SwingDataSource.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    property int    sessionType: -1
    property string swingDir: ""

    readonly property bool hasSwing: root.swingDir !== "" && src.loaded

    // All session screens live in the StackLayout simultaneously (the session screen
    // sits at index sessionType+1), so without this gate EVERY swing reload re-parsed
    // swing.json in all of them — 3 invisible viewers doing ~230 ms of work each on
    // the UI thread. Only the visible screen feeds its source. Mirrors PpCameraTiles.
    readonly property bool _screenActive: navController.currentIndex === root.sessionType + 1
    // Left gutter for the control-row labels so REGION / SEGMENT / RESOLVES TO and
    // their wrapped chip rows all hang-indent to the same edge.
    readonly property int ctrlGutter: Theme.sp(82)

    // Per-section collapse state (default expanded), persisted per screen+mode
    // via AppSettings (key "<sessionType>:<mode>:<section>"). Restored on creation
    // and whenever the screen+mode key changes — so a surviving instance re-reads
    // its layout when the mode flips (e.g. Replay↔Analyse) — and written on toggle.
    property bool scopeCollapsed:    false
    property bool coverageCollapsed: false
    property bool tableCollapsed:    false

    readonly property string _sectionKeyBase: root.sessionType + ":" + SessionMode.mode + ":"
    on_SectionKeyBaseChanged: root._restoreSections()
    Component.onCompleted:    root._restoreSections()

    function _restoreSections() {
        var m = appSettings.sectionCollapse, b = root._sectionKeyBase
        root.scopeCollapsed    = m[b + "scope"]    === true
        root.coverageCollapsed = m[b + "coverage"] === true
        root.tableCollapsed    = m[b + "table"]    === true
    }
    function _persistSection(name, val) {
        if (root.sessionType < 0) return            // transient instance — don't persist
        var m = {}
        for (var k in appSettings.sectionCollapse) m[k] = appSettings.sectionCollapse[k]
        m[root._sectionKeyBase + name] = val
        appSettings.sectionCollapse = m
    }

    // Reusable collapsible section header (caret + title, click to toggle).
    component SectionHeader: Rectangle {
        id: sh
        property string title: ""
        property bool   collapsed: false
        signal toggled()
        implicitHeight: Theme.sp(26)
        color: shMa.containsMouse ? Theme.colorBg3 : "transparent"
        Row {
            anchors { left: parent.left; leftMargin: Theme.sp(10); verticalCenter: parent.verticalCenter }
            spacing: Theme.sp(7)
            Text { text: sh.collapsed ? "▸" : "▾"; color: Theme.colorText3
                   font.pixelSize: Theme.fontSzBody2; anchors.verticalCenter: parent.verticalCenter }
            Text { text: sh.title; color: Theme.colorText3; font.family: Theme.fontData
                   font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
                   anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: shMa; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: sh.toggled() }
    }

    SwingDataSource {
        id: src
        // Gated: an inactive screen feeds "" so it never parses; it picks up the real
        // swing the moment its screen becomes visible (one parse, lazily).
        swingDir: root._screenActive ? root.swingDir : ""
        sessionType: root.sessionType
        imuPlacement: appSettings.imuPlacement   // settings fallback when device.role absent
        // Persisted explicit choice for this session type, else the content-aware
        // default (a region that actually surfaces this swing's IMUs). Explicit
        // choices persist via the chip handler; the default is not written back, so
        // it keeps adapting per swing until the user picks.
        region: {
            var m = appSettings.dataRegionByType, k = String(root.sessionType)
            return (m && m[k] !== undefined) ? m[k] : src.bestRegion
        }
    }

    // Persist an explicit region choice for this session type.
    function persistRegion(name) {
        var m = {}
        for (var kk in appSettings.dataRegionByType) m[kk] = appSettings.dataRegionByType[kk]
        m[String(root.sessionType)] = name
        appSettings.dataRegionByType = m
    }

    // colorKey → instrument-palette hex (Theme lacks per-source hues)
    function srcColor(key) {
        switch (key) {
            case "teal":    return "#7EBFAA"
            case "blue":    return "#6FA8D6"
            case "purple":  return "#9F8AD6"
            case "purple2": return "#B58AC9"
            case "amber":   return Theme.colorAttention
            case "coral":   return "#C98B6E"
        }
        return Theme.colorText3
    }

    Rectangle { anchors.fill: parent; color: Theme.colorBg2; radius: Theme.radius
                border.width: 1; border.color: Theme.colorBorderMid }

    // ── empty state ──────────────────────────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: !root.hasSwing
        text: qsTr("Select a shot to inspect its data")
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        color: Theme.colorText3
    }

    ColumnLayout {
        anchors { fill: parent; margins: 1 }
        spacing: 0
        visible: root.hasSwing

        // ── header ──────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.sp(10)
            Text {
                text: qsTr("DATA TABLE"); font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro; font.letterSpacing: Theme.trackingMicro
                color: Theme.colorText3
            }
            Item { Layout.fillWidth: true }
            // fill toggle
            PpSegmentedControl {
                Layout.preferredWidth: Theme.sp(170)
                options: [qsTr("show gaps"), qsTr("nearest")]
                selected: src.fillMode === "off" ? qsTr("show gaps") : qsTr("nearest")
                solid: false
                onActivated: (value) => src.fillMode = (value === qsTr("nearest")) ? "nearest" : "off"
            }
            Rectangle {
                Layout.preferredHeight: Theme.sp(28)
                implicitWidth: propsLbl.implicitWidth + Theme.sp(22)
                radius: Theme.radius; color: propsMa.containsMouse ? Theme.colorBg3 : "transparent"
                border.width: 1; border.color: Theme.colorBorderMid
                Row {
                    anchors.centerIn: parent; spacing: Theme.sp(5)
                    Text { text: "ⓘ"; color: Theme.colorText2; font.pixelSize: Theme.fontSzBody2
                           anchors.verticalCenter: parent.verticalCenter }
                    Text { id: propsLbl; text: qsTr("Properties"); color: Theme.colorText2
                           font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                           anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea { id: propsMa; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor; onClicked: propsPopup.open() }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal }

        // ── scope (region + segment + resolved tags) ────────────────────────
        SectionHeader {
            Layout.fillWidth: true; title: qsTr("SCOPE"); collapsed: root.scopeCollapsed
            onToggled: { root.scopeCollapsed = !root.scopeCollapsed
                         root._persistSection("scope", root.scopeCollapsed) }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.sp(10); Layout.rightMargin: Theme.sp(10)
            Layout.bottomMargin: Theme.sp(8)
            spacing: Theme.sp(8)
            visible: !root.scopeCollapsed
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.sp(8)
                Text {
                    text: qsTr("REGION"); Layout.preferredWidth: root.ctrlGutter
                    Layout.alignment: Qt.AlignTop
                    height: Theme.sp(28); verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                }
                Flow {
                    Layout.fillWidth: true; spacing: Theme.sp(7)
                    Repeater {
                        model: src.regionOptions
                        delegate: Rectangle {
                            required property string modelData
                            readonly property bool active: modelData === src.region
                            // Dim a region that resolves to nothing for this swing
                            // (Custom has no count → never dimmed).
                            readonly property bool empty: src.regionLaneCounts[modelData] === 0
                            height: Theme.sp(28); radius: height / 2
                            width: rcLbl.implicitWidth + Theme.sp(24)
                            opacity: (empty && !active) ? 0.45 : 1.0
                            color: active ? Theme.colorAccentLight : "transparent"
                            border.width: 1; border.color: active ? Theme.colorAccentMid : Theme.colorBorderMid
                            Text { id: rcLbl; anchors.centerIn: parent; text: modelData
                                   font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                                   color: active ? Theme.colorAccent : Theme.colorText2 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { src.region = modelData; root.persistRegion(modelData) } }
                        }
                    }
                }
            }

            // ── phase segments — window the table vertically ────────────────
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.sp(8)
                visible: src.segments.length > 1
                Text {
                    text: qsTr("SEGMENT"); Layout.preferredWidth: root.ctrlGutter
                    Layout.alignment: Qt.AlignTop
                    height: Theme.sp(24); verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                }
                Flow {
                    Layout.fillWidth: true; spacing: Theme.sp(6)
                    Repeater {
                        model: src.segments
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool active: src.windowStartUs === modelData.startUs
                                                         && src.windowEndUs === modelData.endUs
                            height: Theme.sp(24); radius: height / 2
                            width: segLbl.implicitWidth + Theme.sp(18)
                            color: active ? Theme.colorAccentLight : "transparent"
                            border.width: 1; border.color: active ? Theme.colorAccentMid : Theme.colorBorderMid
                            Text { id: segLbl; anchors.centerIn: parent; text: modelData.label
                                   font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                                   font.letterSpacing: Theme.trackingMicro
                                   color: active ? Theme.colorAccent : Theme.colorText2 }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: src.setWindow(modelData.startUs, modelData.endUs) }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: Theme.sp(8)
                Text {
                    text: qsTr("RESOLVES TO"); Layout.preferredWidth: root.ctrlGutter
                    Layout.alignment: Qt.AlignTop
                    height: Theme.sp(24); verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingMicro; color: Theme.colorText3
                }
                Flow {
                    Layout.fillWidth: true; spacing: Theme.sp(6)
                    Repeater {
                        model: src.resolvedSources
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            height: Theme.sp(24); radius: Theme.radius
                            width: tagRow.implicitWidth + Theme.sp(16)
                            color: Theme.colorBg
                            Row {
                                id: tagRow; anchors.centerIn: parent; spacing: Theme.sp(6)
                                Rectangle { width: Theme.sp(6); height: Theme.sp(6); radius: width / 2
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: root.srcColor(modelData.colorKey) }
                                Text { text: modelData.label; color: Theme.colorText
                                       anchors.verticalCenter: parent.verticalCenter
                                       font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2 }
                                Text { visible: modelData.removable === true; text: "×"
                                       color: Theme.colorText3; font.pixelSize: Theme.fontSzBody2
                                       anchors.verticalCenter: parent.verticalCenter
                                       MouseArea { anchors.fill: parent; anchors.margins: -4
                                                   cursorShape: Qt.PointingHandCursor
                                                   onClicked: src.removeSource(index) } }
                            }
                        }
                    }
                    Text {
                        visible: src.resolvedSources.length === 0
                        height: Theme.sp(24); verticalAlignment: Text.AlignVCenter
                        text: qsTr("no sources present for this region")
                        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                        color: Theme.colorText3
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal }

        // ── coverage strip ──────────────────────────────────────────────────
        SectionHeader {
            Layout.fillWidth: true; title: qsTr("COVERAGE"); collapsed: root.coverageCollapsed
            onToggled: { root.coverageCollapsed = !root.coverageCollapsed
                         root._persistSection("coverage", root.coverageCollapsed) }
        }
        PpCoverageStrip {
            Layout.fillWidth: true
            visible: !root.coverageCollapsed
            showTitle: false
            model:  src.coverage
            phases: src.phases
            spanUs: src.spanUs
            windowStartUs: src.windowStartUs
            windowEndUs:   src.windowEndUs
            colorFn: root.srcColor
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal }

        // ── detail table ────────────────────────────────────────────────────
        SectionHeader {
            Layout.fillWidth: true; title: qsTr("TABLE"); collapsed: root.tableCollapsed
            onToggled: { root.tableCollapsed = !root.tableCollapsed
                         root._persistSection("table", root.tableCollapsed) }
        }
        Item {
            id: tableArea
            Layout.fillWidth: true; Layout.fillHeight: !root.tableCollapsed
            visible: !root.tableCollapsed
            Layout.margins: Theme.sp(8)

            // Keyboard cursor row (highlighted; driven by arrows / Home·End / Page).
            property int currentRow: 0
            readonly property int rowH: Theme.sp(22)
            onCurrentRowChanged: table.positionViewAtRow(currentRow, TableView.Contain)

            // Replay playhead drives the current row — cross-highlight in lockstep
            // with the Transit timeline + charts cursor. One µs domain (window-
            // relative), so positionUs maps directly. rowForTimeUs returns -1 when the
            // playhead is outside a phase-windowed table; we leave the row alone then,
            // and keyboard nav still moves it between playhead updates.
            Connections {
                target: shotReplay
                function onPositionChanged() {
                    if (!shotReplay.active) return
                    var r = src.table.rowForTimeUs(shotReplay.positionUs)
                    if (r >= 0) tableArea.currentRow = r
                }
            }

            HorizontalHeaderView {
                id: header
                anchors { top: parent.top; left: parent.left; right: parent.right }
                syncView: table
                clip: true
                delegate: Rectangle {
                    implicitWidth: Theme.sp(90); implicitHeight: Theme.sp(24)
                    color: "transparent"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 2
                                color: root.srcColor(src.table.columnColorKey(index)) }
                    Text { anchors.centerIn: parent; text: display
                           font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                           color: Theme.colorText2 }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                color: Theme.colorBorderMid }
                }
            }

            TableView {
                id: table
                anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                model: src.table
                clip: true
                columnSpacing: 0; rowSpacing: 0
                boundsBehavior: Flickable.StopAtBounds
                focus: true; activeFocusOnTab: true
                ScrollBar.vertical: ScrollBar {}

                // keep the cursor valid as the windowed row set changes
                onRowsChanged: tableArea.currentRow =
                    Math.max(0, Math.min(tableArea.currentRow, rows - 1))

                Keys.onPressed: function (event) {
                    var n = table.rows
                    if (n <= 0) return
                    var page = Math.max(1, Math.floor(table.height / tableArea.rowH) - 1)
                    var r = tableArea.currentRow
                    switch (event.key) {
                    case Qt.Key_Up:       r = Math.max(0, r - 1); break
                    case Qt.Key_Down:     r = Math.min(n - 1, r + 1); break
                    case Qt.Key_Home:     r = 0; break
                    case Qt.Key_End:      r = n - 1; break
                    case Qt.Key_PageUp:   r = Math.max(0, r - page); break
                    case Qt.Key_PageDown: r = Math.min(n - 1, r + page); break
                    case Qt.Key_Left:
                        table.contentX = Math.max(0, table.contentX - Theme.sp(90))
                        event.accepted = true; return
                    case Qt.Key_Right:
                        table.contentX = Math.min(Math.max(0, table.contentWidth - table.width),
                                                  table.contentX + Theme.sp(90))
                        event.accepted = true; return
                    default: return
                    }
                    tableArea.currentRow = r
                    event.accepted = true
                }

                delegate: Rectangle {
                    required property int row
                    readonly property bool cursor: row === tableArea.currentRow
                    implicitWidth: Theme.sp(90); implicitHeight: Theme.sp(22)
                    // cursor row wins over the state tint (text colour still reads state)
                    color: cursor ? Qt.rgba(0.49, 0.75, 0.67, 0.14)
                         : state === SwingSeriesModel.Missing ? Qt.rgba(0.77, 0.41, 0.41, 0.08)
                         : (state === SwingSeriesModel.Held || state === SwingSeriesModel.Resync
                            || state === SwingSeriesModel.DerivedHeld)
                           ? Qt.rgba(0.91, 0.71, 0.29, 0.08) : "transparent"
                    Text {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.sp(8); anchors.leftMargin: Theme.sp(8)
                        horizontalAlignment: align === Qt.AlignLeft ? Text.AlignLeft : Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        text: display
                        font.family: align === Qt.AlignLeft ? Theme.fontBody : Theme.fontData  // mono for numbers
                        font.pixelSize: Theme.fontSzBody2
                        color: state === SwingSeriesModel.Missing ? Theme.colorText3
                             : (state === SwingSeriesModel.Held || state === SwingSeriesModel.Resync
                                || state === SwingSeriesModel.LowConf || state === SwingSeriesModel.DerivedHeld)
                               ? Theme.colorAttention : Theme.colorText
                        font.underline: state === SwingSeriesModel.DerivedHeld
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1
                                color: Theme.colorBorder; opacity: 0.5 }
                    // click to move the cursor + take keyboard focus
                    TapHandler { onTapped: { tableArea.currentRow = row; table.forceActiveFocus() } }
                }
            }
        }

        // Keep collapsed headers tight at the top: only the table fills, so when it
        // is collapsed nothing absorbs the slack — this spacer does, just then.
        Item { Layout.fillWidth: true; Layout.fillHeight: root.tableCollapsed
               visible: root.tableCollapsed }
    }

    // properties / provenance
    Popup {
        id: propsPopup
        parent: root
        width: Math.min(Theme.sp(320), root.width - Theme.sp(24))
        height: root.height - Theme.sp(24); x: root.width - width - Theme.sp(12); y: Theme.sp(12)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { color: Theme.colorSurface; radius: Theme.radiusLg
                                border.width: 1; border.color: Theme.colorBorderStrong }
        contentItem: PpPropertiesPanel { metadata: src.metadata }
    }
}
