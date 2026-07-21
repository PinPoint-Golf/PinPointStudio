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
                    + Math.max(list.contentHeight, Theme.sp(52))

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────────
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.leftMargin:   Theme.sp(18)
            Layout.rightMargin:  Theme.sp(16)
            Layout.topMargin:    Theme.sp(13)
            Layout.bottomMargin: Theme.sp(13)

            Text {
                text: qsTr("CHOOSE A SESSION")
                font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingLabel; color: Theme.colorText3
            }
            Item { Layout.fillWidth: true }
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
        Rectangle {
            Layout.fillWidth: true
            height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
        }

        // ── Session rows ────────────────────────────────────────────────────
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sessionReviewController.sessionsModel
            boundsBehavior: Flickable.StopAtBounds
            topMargin:    Theme.sp(6)
            bottomMargin: Theme.sp(6)
            leftMargin:   Theme.sp(8)
            rightMargin:  Theme.sp(8)
            delegate: sessionRow

            // Quiet invitation when there is no saved history yet — the live row
            // is always present (count 1), so "no saved sessions" means count ≤ 1.
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.sp(16)
                visible: list.count <= 1
                text: qsTr("No saved sessions yet")
                font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
                color: Theme.colorText3
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

            width:  ListView.view.width - Theme.sp(16)
            x:      Theme.sp(8)
            height: Theme.sp(58)
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
                anchors.fill: parent
                anchors.leftMargin:  Theme.sp(12)
                anchors.rightMargin: Theme.sp(12)
                spacing: Theme.sp(12)

                // Mini film-strip preview (thumbnail → ◑ placeholder, same idiom
                // as PpShotCard until real thumbnails are extracted).
                Row {
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.sp(2)
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
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    Row {
                        width: parent.width
                        spacing: Theme.sp(7)
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
                        parent: kebab
                        x: kebab.width - width            // right edges aligned
                        y: -height - Theme.sp(4)          // open upward, like the carousel
                        padding: Theme.sp(5)
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                        contentWidth: Math.max(exportRow.implicitWidth, trashRow.implicitWidth)
                                      + Theme.sp(28)
                        background: Rectangle {
                            color: Theme.colorSurface; radius: Theme.radiusLg
                            border.width: 1; border.color: Theme.colorBorderStrong
                        }

                        contentItem: Column {

                            Rectangle {   // Export session → shared carousel export sheet
                                width: parent.width
                                height: Theme.sp(34)
                                radius: Theme.radius
                                color: exportMa.containsMouse ? Theme.colorBg2 : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                                Row {
                                    id: exportRow
                                    anchors { left: parent.left; leftMargin: Theme.sp(10)
                                              verticalCenter: parent.verticalCenter }
                                    spacing: Theme.sp(10)
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
                                    anchors { left: parent.left; leftMargin: Theme.sp(10)
                                              verticalCenter: parent.verticalCenter }
                                    spacing: Theme.sp(10)
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
        }
    }

    // ── One right-aligned stat (number over micro label) ─────────────────────
    component Stat: Column {
        property string value: ""
        property string unit:  ""
        property color  valueColor: Theme.colorText

        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: Theme.sp(54)
        spacing: Theme.sp(1)

        Text {
            anchors.right: parent.right
            text: value
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzDataSm
            color: valueColor
        }
        Text {
            anchors.right: parent.right
            text: unit
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel; color: Theme.colorText3
        }
    }
}
