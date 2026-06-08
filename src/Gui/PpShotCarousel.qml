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

// Session-shot carousel — the film-strip rail docked at the bottom of the
// mode screens. Reusable: each screen supplies its own metricKeys (the panel's
// summary metrics) and traceLabel; the shot data itself is the session-global
// `shotModel` context property, filtered per-screen through this carousel's
// own ShotFilterProxyModel. Rating/note/trash go straight to the shotModel
// invokables; replay/export/face-on are the host screen's to wire.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root

    // Ordered metric keys the review panel shows for this screen
    // (e.g. Wrist: wristAngleTop, impactConditions, trailWristExtension, transition).
    property var    metricKeys: []
    // Caption over the panel's trace chart (e.g. "LEAD-WRIST FLEXION · ADDRESS → IMPACT").
    property string traceLabel: ""

    signal replayRequested(int shotId, string mode)
    signal exportRequested(int shotId)
    signal exportAllRequested(var shotIds)
    signal faceOnRequested(int shotId)

    property int  selectedShotId: -1
    property Item selectedCard:   null   // live delegate; nulled by QML when it is destroyed

    // Selected card's visual x within the carousel — tracks list scrolling.
    readonly property real selectedCardX:
        selectedCard ? railCol.x + strip.x + selectedCard.x - strip.contentX : 0

    implicitHeight: Theme.carouselHeight

    // A filtered-out or trashed selection destroys its delegate — drop the panel with it.
    onSelectedCardChanged: if (!selectedCard && panelPopup.opened) panelPopup.close()

    ShotFilterProxyModel {
        id: filterProxy
        sourceModel: shotModel
    }

    Rectangle {   // rail background
        anchors.fill: parent
        color: Theme.colorBg2
    }
    Rectangle {   // top hairline
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1; color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
    }

    ColumnLayout {
        id: railCol
        anchors { fill: parent
                  leftMargin: Theme.sp(16); rightMargin: Theme.sp(16)
                  topMargin: Theme.sp(7);   bottomMargin: Theme.sp(8) }
        spacing: Theme.sp(7)

        // ── Filter combo + bulk-action menu ──────────────────────────────────
        RowLayout {
            Layout.alignment: Qt.AlignLeft
            spacing: Theme.sp(4)

            Rectangle {   // "N shots" / "N of M shots" when filtered
                id: filterPill
                implicitWidth:  pillRow.implicitWidth + Theme.sp(12)
                implicitHeight: Theme.sp(22)
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: filterProxy.filterActive || pillMa.containsMouse
                                  ? Theme.colorBorderMid : "transparent"
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: Theme.sp(6)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           filterProxy.countLabel
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          filterProxy.filterActive ? Theme.colorText : Theme.colorText3
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text:           "▾"
                        font.family:    Theme.fontSymbol
                        font.pixelSize: Theme.fontSzMicro
                        color:          Theme.colorText3
                    }
                }
                MouseArea {
                    id: pillMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    filterPopup.opened ? filterPopup.close() : filterPopup.open()
                }
            }

            Rectangle {   // ⋯ bulk actions on the filtered set
                id: bulkKebab
                visible: filterProxy.filterActive
                implicitWidth:  Theme.sp(22)
                implicitHeight: Theme.sp(22)
                radius: Theme.radius
                color:  bulkMenu.opened || bulkMa.containsMouse ? Theme.colorBg3 : "transparent"
                border.width: 1
                border.color: bulkMenu.opened ? Theme.colorBorderMid : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    anchors.centerIn: parent
                    text:           "⋯"
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzBody
                    color:          Theme.colorText2
                }
                MouseArea {
                    id: bulkMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    bulkMenu.opened ? bulkMenu.close() : bulkMenu.open()
                }

                Connections {   // clearing the filter hides the kebab — drop its menu too
                    target: filterProxy
                    function onFilterChanged() {
                        if (!filterProxy.filterActive)
                            bulkMenu.close()
                    }
                }

                // The bulk set is the filtered set — no filter, no menu.
                Popup {
                    id: bulkMenu
                    parent: bulkKebab
                    y: -height - Theme.sp(4)
                    x: 0
                    padding: Theme.sp(5)
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                    // Widest row plus left inset and right margin — the popup sizes
                    // its contentItem from contentWidth, so the labels never run
                    // tight to the menu's right edge.
                    contentWidth: Math.max(bulkExportRow.implicitWidth, bulkTrashRow.implicitWidth)
                                  + Theme.sp(28)
                    background: Rectangle {
                        color: Theme.colorSurface; radius: Theme.radiusLg
                        border.width: 1; border.color: Theme.colorBorderStrong
                    }

                    contentItem: Column {

                        Rectangle {   // Export all selected
                            width: parent.width
                            height: Theme.sp(34)
                            radius: Theme.radius
                            color: bulkExportMa.containsMouse ? Theme.colorBg2 : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            Row {
                                id: bulkExportRow
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
                                    text: qsTr("Export all selected")
                                    font.family: Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    color: Theme.colorText
                                }
                            }
                            MouseArea {
                                id: bulkExportMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    bulkMenu.close()
                                    root.exportAllRequested(filterProxy.visibleShotIds())
                                }
                            }
                        }

                        Rectangle {   // separator — destructive actions in their own group
                            width: parent.width - Theme.sp(12)
                            anchors.horizontalCenter: parent.horizontalCenter
                            height: 1
                            color: Theme.colorBorderMid
                            opacity: Theme.borderOpacityNormal
                        }

                        // "Move … to trash", not "Delete …" — project convention:
                        // "Delete" is reserved for permanent removal (e.g. Delete
                        // athlete); trash language signals the action is recoverable.
                        Rectangle {   // Move all selected to trash (soft, undoable)
                            width: parent.width
                            height: Theme.sp(34)
                            radius: Theme.radius
                            color: bulkTrashMa.containsMouse ? Theme.colorWarnLight : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            Row {
                                id: bulkTrashRow
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
                                    text: qsTr("Move all selected to trash")
                                    font.family: Theme.fontBody
                                    font.pixelSize: Theme.fontSzBody
                                    color: Theme.colorWarn
                                }
                            }
                            MouseArea {
                                id: bulkTrashMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const ids = filterProxy.visibleShotIds()
                                    bulkMenu.close()
                                    panelPopup.close()
                                    shotModel.moveAllToTrash(ids)
                                    toast.show(qsTr("%1 shots moved to trash").arg(ids.length))
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Film strip ───────────────────────────────────────────────────────
        ListView {
            id: strip
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(Theme.sp(139) * 9 / 16)
            orientation: ListView.Horizontal
            spacing:     Theme.sp(5)
            clip:        true
            model:       filterProxy

            delegate: PpShotCard {
                id: cardDelegate
                selected: shotId === root.selectedShotId && panelPopup.opened
                onTapped: {
                    root.selectedShotId = shotId
                    root.selectedCard   = cardDelegate
                    panelPopup.open()
                }
                onRated: (n) => shotModel.setRating(shotId, n)
            }
        }
    }

    // ── Review panel — opens upward, anchored over the selected card ────────
    Popup {
        id: panelPopup
        parent: root
        y: -height - Theme.sp(10)
        x: Math.max(Theme.sp(8),
                    Math.min(root.selectedCardX - width / 2 + Theme.sp(70),
                             root.width - width - Theme.sp(8)))
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong

            Rectangle {   // pointer arrow aligned to the selected card
                x: Math.max(Theme.sp(14),
                            Math.min(root.selectedCardX + Theme.sp(70) - panelPopup.x - width / 2,
                                     parent.width - width - Theme.sp(14)))
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -Theme.sp(7)
                width:  Theme.sp(13); height: Theme.sp(13)
                rotation: 45
                color: Theme.colorSurface
                border.width: 1; border.color: Theme.colorBorderStrong
            }
            Rectangle {   // mask the arrow's top half so only the tip shows
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                anchors.margins: 1
                height: Theme.sp(10)
                radius: Theme.radiusLg
                color:  Theme.colorSurface
            }
        }
        onClosed: {
            shotPanel.commitNote()
            root.selectedShotId = -1
            root.selectedCard   = null
        }

        contentItem: PpShotPanel {
            id: shotPanel
            shotId:         root.selectedCard ? root.selectedCard.shotId         : -1
            ordinal:        root.selectedCard ? root.selectedCard.ordinal        : 0
            timestampLabel: root.selectedCard ? root.selectedCard.timestampLabel : ""
            club:           root.selectedCard ? root.selectedCard.club           : ""
            score:          root.selectedCard ? root.selectedCard.score          : 0
            rating:         root.selectedCard ? root.selectedCard.rating         : 0
            note:           root.selectedCard ? root.selectedCard.note           : ""
            tracePoints:    root.selectedCard ? root.selectedCard.tracePoints    : []
            metrics:        root.selectedCard ? root.selectedCard.metrics        : ({})
            analysisDetail: root.selectedCard ? root.selectedCard.analysisDetail : ({})
            metricKeys:     root.metricKeys
            traceLabel:     root.traceLabel

            onReplayRequested: (mode) => root.replayRequested(shotPanel.shotId, mode)
            onFaceOnRequested: root.faceOnRequested(shotPanel.shotId)
            onExportRequested: {
                root.exportRequested(shotPanel.shotId)
                panelPopup.close()
            }
            onTrashRequested: {
                const trashedOrdinal = shotPanel.ordinal
                const trashedId      = shotPanel.shotId
                shotPanel.commitNote()
                panelPopup.close()
                shotModel.moveToTrash(trashedId)
                toast.show(qsTr("Shot #%1 moved to trash").arg(trashedOrdinal))
            }
        }
    }

    // ── Filter popover — opens upward over the left cap ──────────────────────
    Popup {
        id: filterPopup
        parent: root
        x: Theme.sp(16)
        y: -height - Theme.sp(10)
        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpShotFilter { proxy: filterProxy }
    }

    // ── Undo toast ───────────────────────────────────────────────────────────
    PpToast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - Theme.sp(14)
        onUndoClicked: shotModel.restoreLastTrashed()
    }
}
