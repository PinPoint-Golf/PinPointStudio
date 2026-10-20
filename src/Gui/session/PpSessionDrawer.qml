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

// Session chooser drawer — the body of the "CHOOSE A SESSION" popup raised from
// the carousel's session chip. Lists every session on disk for the current
// athlete plus the synthesized live session (pinned first, flagged isLive), via
// sessionReviewController.sessionsModel. Selecting a row enters review
// (loadSession) or returns to live (resumeLive); the host popup closes on the
// emitted closeRequested(). Hosted as a Popup contentItem, so it reports its
// natural size through implicitHeight and the popup clamps it below the toolbar.
//
// Each row names the devices its swings were recorded with (cameras by view,
// HackMotion, IMU, launch monitor), and the chips under the header filter the
// saved sessions to those that used EVERY chosen device. The filter is the
// model's (SessionListModel::deviceFilter), so it outlives the drawer closing.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: drawer

    // Raised when a row is chosen (or the ✕ tapped) — the host popup closes.
    signal closeRequested()

    // A session row was chosen (live or past) — the host clears the carousel
    // filter so the freshly selected session shows all of its shots.
    signal sessionSelected()

    // Per-row '...' menu actions, handled by the host carousel (which owns the
    // shared export sheet, exporter and toast). sessionId is the row's absolute
    // session dir; never emitted for the live row (it carries no '...' menu).
    signal exportRequested(string sessionId)
    signal trashRequested(string sessionId)

    // Natural content height the host popup uses to size itself (it clamps this
    // so the drawer never reaches the toolbar). list.contentHeight is independent
    // of the view's own height, so there is no binding loop with the popup.
    implicitWidth:  Theme.sp(440)
    implicitHeight: headerRow.implicitHeight + Theme.sp(26)
                    + (deviceBar.visible ? deviceBar.implicitHeight + Theme.sp(10) : 0)
                    + Math.max(list.contentHeight, Theme.sp(52))
                    // Room for the empty-list note below the live row, which it would otherwise sit on.
                    + (emptyNote.visible ? emptyNote.implicitHeight + Theme.gap(40) : 0)

    readonly property var  _sessions:     sessionReviewController.sessionsModel
    readonly property bool _filterActive: _sessions.deviceFilter.length > 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────────
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.leftMargin:   Theme.gap(18)
            Layout.rightMargin:  Theme.gap(16)
            Layout.topMargin:    Theme.gap(13)
            Layout.bottomMargin: Theme.gap(13)

            PpMicro { text: Theme.caps(qsTr("Choose a session")) }
            Item { Layout.fillWidth: true }
            Text {   // how much of the library the device filter is showing
                visible: drawer._filterActive
                text: qsTr("%1 of %2").arg(drawer._sessions.shownSavedCount)
                                      .arg(drawer._sessions.savedCount)
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                color: Theme.colorText3
            }
            Item {   // ✕ close (padded hit area)
                Layout.preferredWidth:  Theme.sp(20)
                Layout.preferredHeight: Theme.sp(20)
                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.family: Theme.fontSymbol; font.pixelSize: Theme.fontSzBody2
                    color: closeMa.containsMouse ? Theme.colorText2 : Theme.colorText3
                }
                PpPressable {
                    id: closeMa
                    anchors.margins: -Theme.sp(6)
                    onClicked: drawer.closeRequested()
                }
            }
        }
        // ── Device filter — one chip per device any saved session used ──────
        //    Multi-select, AND: two chips on = sessions that had both. Each chip's
        //    count is what adding it would leave. Tap again to drop one; Clear
        //    drops them all.
        Flow {
            id: deviceBar
            objectName: "deviceFilterBar"
            visible: drawer._sessions.availableDevices.length > 0
            Layout.fillWidth: true
            Layout.leftMargin:   Theme.gap(18)
            Layout.rightMargin:  Theme.gap(16)
            Layout.bottomMargin: Theme.gap(10)
            spacing: Theme.gap(6)

            PpMicro {
                height: Theme.sp(22)
                verticalAlignment: Text.AlignVCenter
                text: Theme.caps(qsTr("Recorded with"))
            }

            Repeater {
                model: drawer._sessions.availableDevices

                Rectangle {
                    id: devChip
                    required property var modelData
                    readonly property bool _on: drawer._sessions.deviceFilter.indexOf(modelData.id) >= 0

                    objectName: "deviceChip_" + modelData.id
                    // Nothing would be left if this were added — quietened, still tappable.
                    opacity: !_on && modelData.count === 0 ? 0.45 : 1.0
                    width:  devRow.implicitWidth + Theme.sp(16)
                    height: Theme.sp(22)
                    radius: height / 2
                    color:  _on ? Theme.colorAccentLight
                          : devMa.containsMouse ? Theme.colorBg2
                          : Qt.rgba(Theme.colorBg2.r, Theme.colorBg2.g, Theme.colorBg2.b, 0)
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    border.width: 1
                    border.color: _on || devMa.containsMouse ? Theme.colorAccent : Theme.colorBorderMid

                    Row {
                        id: devRow
                        anchors.centerIn: parent
                        spacing: Theme.gap(5)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: devChip.modelData.label
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzLabel
                            color: devChip._on ? Theme.colorAccent : Theme.colorText2
                        }
                        Text {   // sessions left if this chip were on too (AND)
                            anchors.verticalCenter: parent.verticalCenter
                            text: devChip.modelData.count
                            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                            color: Theme.colorText3
                        }
                    }
                    PpPressable {
                        id: devMa
                        onClicked: drawer._sessions.toggleDevice(devChip.modelData.id)
                    }
                }
            }

            PpLink {
                visible: drawer._filterActive
                height: Theme.sp(22)
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Clear")
                pressMargin: Theme.sp(4)
                onClicked: drawer._sessions.clearDeviceFilter()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1; color: Theme.colorBorder
        }

        // ── Session rows ────────────────────────────────────────────────────
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: drawer._sessions
            boundsBehavior: Flickable.StopAtBounds
            topMargin:    Theme.gap(6)
            bottomMargin: Theme.gap(6)
            leftMargin:   Theme.gap(8)
            rightMargin:  Theme.gap(8)
            delegate: sessionRow

            // Quiet invitation when there is no saved history yet — the live row
            // is always present (count 1), so "no saved sessions" means count ≤ 1.
            // With the device filter on, the same space says nothing matched.
            PpCardNote {
                id: emptyNote
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.gap(16)
                visible: list.count <= 1
                text: drawer._filterActive ? qsTr("No saved sessions used all of those")
                                           : qsTr("No saved sessions yet")
            }
        }
    }

    // ── Row delegate ────────────────────────────────────────────────────────
    Component {
        id: sessionRow

        Rectangle {
            id: rowBg

            required property int    index
            required property string sessionId
            required property string dayLabel
            required property string timeLabel
            required property string clubMix
            required property int    shotCount
            required property string lengthLabel
            required property int    avgQuality
            required property bool   isLive
            required property var    previewThumbs
            required property bool   indexed
            required property var    devices
            required property var    deviceLabels

            width:  ListView.view.width - Theme.sp(16)
            x:      Theme.sp(8)
            // A third line for the device tags when there are any to show.
            height: deviceLabels.length > 0 ? Theme.sp(76) : Theme.sp(58)
            readonly property real _mainH: deviceLabels.length > 0 ? Theme.sp(54) : height
            radius: Theme.radius
            color:  isLive ? Theme.colorAccentLight
                  : (rowMa.containsMouse || kebabMa.containsMouse || rowMenu.opened)
                        ? Theme.colorBg : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            // Declared before the visuals so the kebab's own MouseArea (inside the
            // RowLayout below, drawn on top) wins for its slot, while clicks
            // anywhere else on the row fall through here to load the session.
            PpPressable {
                id: rowMa
                hoverScale: 1.0           // full-width list row — press-dip only
                onClicked: {
                    if (rowBg.isLive) sessionReviewController.resumeLive()
                    else              sessionReviewController.loadSession(rowBg.sessionId)
                    drawer.sessionSelected()
                    drawer.closeRequested()
                }
            }

            RowLayout {
                id: rowMain
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: rowBg._mainH
                anchors.leftMargin:  Theme.gap(12)
                anchors.rightMargin: Theme.gap(12)
                spacing: Theme.gap(12)

                // Mini film-strip preview (thumbnail → ◑ placeholder, same idiom
                // as PpShotCard until real thumbnails are extracted).
                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.gap(2)
                    Repeater {
                        model: 4
                        delegate: Rectangle {
                            required property int index
                            readonly property string thumb:
                                (rowBg.previewThumbs && index < rowBg.previewThumbs.length)
                                    ? rowBg.previewThumbs[index] : ""
                            width: Theme.sp(26); height: Theme.sp(16); radius: Theme.sp(3)
                            color: Theme.colorBg3
                            clip: true
                            Image {
                                anchors.fill: parent
                                visible: parent.thumb !== ""
                                source: parent.thumb
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: parent.thumb === ""
                                text: "◑"
                                font.family: Theme.fontSymbol
                                font.pixelSize: Math.round(Theme.sp(11) * Theme.symbolScale("◑"))
                                color: Theme.colorText3
                            }
                        }
                    }
                }

                // Title + club mix — both elide so a long label never clips the
                // stats at the narrow (~⅓) drawer width.
                Column {
                    id: titleCol
                    Layout.fillWidth: true
                    spacing: Theme.gap(3)
                    Row {
                        width: parent.width
                        spacing: Theme.gap(7)
                        Rectangle {
                            visible: rowBg.isLive
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                            color: Theme.colorError
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            // Row drops the dot when not live; subtract dot+spacing only then.
                            width: parent.width - (rowBg.isLive ? Theme.sp(14) : 0)
                            elide: Text.ElideRight
                            text: rowBg.dayLabel
                                  + (rowBg.timeLabel ? " · " + rowBg.timeLabel : "")
                                  + (rowBg.isLive ? qsTr(" · live") : "")
                            font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody
                            color: Theme.colorText
                        }
                    }
                    Text {
                        visible: rowBg.clubMix !== ""
                        width: parent.width
                        text: rowBg.clubMix
                        font.family: Theme.fontData; font.pixelSize: Theme.fontSzLabel
                        color: Theme.colorText3
                        elide: Text.ElideRight
                    }
                }

                // Right-aligned stats: shots / length / avg quality. Shot count comes from
                // a directory listing so it is always known; length and quality need the
                // session's summaries, which are built when the session is first opened —
                // show "—" rather than a misleading 0 until then.
                Stat { value: rowBg.shotCount;                    unit: qsTr("shots") }
                Stat { value: rowBg.indexed ? (rowBg.lengthLabel || "—") : "—"
                       unit: qsTr("length") }
                Stat { value: rowBg.indexed ? rowBg.avgQuality : "—"; unit: qsTr("avg q")
                       valueColor: rowBg.indexed ? Theme.qualityColor(rowBg.avgQuality)
                                                 : Theme.colorText3 }

                // ── Per-row '...' menu (Export session / Move to trash) ──────
                // Excluded from the layout on the live row (no on-disk session),
                // so live rows keep their original alignment. The glyph is hidden
                // at rest and revealed on row hover to keep the list chromeless.
                Item {
                    id: kebab
                    visible: !rowBg.isLive
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth:  Theme.sp(22)
                    Layout.preferredHeight: Theme.sp(22)

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: rowMenu.opened || kebabMa.containsMouse
                                   ? Theme.colorBg3 : "transparent"
                        opacity: (rowMa.containsMouse || kebabMa.containsMouse
                                  || rowMenu.opened) ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                        Text {
                            anchors.centerIn: parent
                            text: "⋯"
                            font.family: Theme.fontData
                            font.pixelSize: Theme.fontSzBody
                            color: Theme.colorText2
                        }
                    }
                    PpPressable {
                        id: kebabMa
                        held: rowMenu.opened       // stay grown while the menu is up
                        onClicked: rowMenu.opened ? rowMenu.close() : rowMenu.open()
                    }

                    // Reuses the carousel bulk-menu pattern (Popup → Column of
                    // hover rows, destructive action below a separator).
                    Popup {
                        id: rowMenu
                        objectName: "rowMenu"
                        parent: kebab
                        x: kebab.width - width            // right edges aligned
                        y: -height - Theme.sp(4)          // open upward, like the carousel
                        padding: Theme.gap(5)
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        contentWidth: Math.max(exportRow.implicitWidth, trashRow.implicitWidth)
                                      + Theme.sp(28)
                        background: PpPopoverCard {}

                        contentItem: Column {

                            Rectangle {   // Export session → shared carousel export sheet
                                width: parent.width
                                height: Theme.sp(34)
                                radius: Theme.radius
                                color: exportMa.containsMouse ? Theme.colorBg2 : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                Row {
                                    id: exportRow
                                    anchors { left: parent.left; leftMargin: Theme.gap(10)
                                              verticalCenter: parent.verticalCenter }
                                    spacing: Theme.gap(10)
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "⤓"
                                        font.family: Theme.fontSymbol
                                        font.pixelSize: Theme.fontSzBody
                                        color: Theme.colorText2
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Export session")
                                        font.family: Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody
                                        color: Theme.colorText
                                    }
                                }
                                PpPressable {
                                    id: exportMa
                                    hoverScale: 1.0       // full-width menu item — press-dip only
                                    onClicked: {
                                        rowMenu.close()
                                        drawer.exportRequested(rowBg.sessionId)
                                    }
                                }
                            }

                            Rectangle {   // separator — destructive action in its own group
                                width: parent.width - Theme.sp(12)
                                anchors.horizontalCenter: parent.horizontalCenter
                                height: 1
                                color: Theme.colorBorderMid
                                opacity: Theme.borderOpacityNormal
                            }

                            // "Move to trash", not "Delete" — recoverable via the OS
                            // trash, the same wording as the carousel's shot trash.
                            Rectangle {   // Move to trash (soft, recoverable)
                                width: parent.width
                                height: Theme.sp(34)
                                radius: Theme.radius
                                color: trashMa.containsMouse ? Theme.colorWarnLight : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                Row {
                                    id: trashRow
                                    anchors { left: parent.left; leftMargin: Theme.gap(10)
                                              verticalCenter: parent.verticalCenter }
                                    spacing: Theme.gap(10)
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "🗑"
                                        font.family: Theme.fontSymbol
                                        font.pixelSize: Theme.fontSzBody
                                        color: Theme.colorWarn
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Move to trash")
                                        font.family: Theme.fontBody
                                        font.pixelSize: Theme.fontSzBody
                                        color: Theme.colorWarn
                                    }
                                }
                                PpPressable {
                                    id: trashMa
                                    hoverScale: 1.0       // full-width menu item — press-dip only
                                    onClicked: {
                                        rowMenu.close()
                                        drawer.trashRequested(rowBg.sessionId)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // What recorded it — grey outlines, the ones the filter asked for in the
            // accent. Its own line under the title, running to the row's right edge
            // (under the stats) so the tags are not squeezed into the title column.
            Row {
                objectName: "deviceTags"
                visible: rowBg.deviceLabels.length > 0
                x: rowMain.x + titleCol.x
                y: rowBg._mainH - Theme.sp(4)
                width: rowBg.width - x - Theme.gap(12)
                height: Theme.sp(18)
                clip: true
                spacing: Theme.gap(4)
                Repeater {
                    model: rowBg.deviceLabels
                    PpChip {
                        required property string modelData
                        required property int    index
                        text:   modelData
                        tinted: drawer._sessions.deviceFilter.indexOf(rowBg.devices[index]) >= 0
                        tone:   tinted ? Theme.colorAccent : Theme.colorText2
                    }
                }
            }
        }
    }

    // ── One right-aligned stat (number over micro label) ─────────────────────
    component Stat: Column {
        property string value: ""
        property string unit:  ""
        property color  valueColor: Theme.colorText

        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Theme.sp(54)
        spacing: Theme.gap(1)

        Text {
            anchors.right: parent.right
            text: value
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
            color: valueColor
        }
        PpMicro {
            anchors.right: parent.right
            text: unit
        }
    }
}
