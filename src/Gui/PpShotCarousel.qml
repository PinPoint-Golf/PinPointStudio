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

    property int  selectedShotId: -1
    property Item selectedCard:   null   // live delegate; nulled by QML when it is destroyed

    // The carousel shows the live shotModel normally, or a loaded past session's
    // private model while reviewing. Both are ShotListModel (identical roles +
    // mutation invokables), so the filter proxy, cards and panel work unchanged;
    // every mutation routes through activeModel so trash/rating hit the right one.
    readonly property bool reviewing: sessionReviewController.reviewActive
    readonly property var  activeModel: reviewing ? sessionReviewController.shots
                                                  : shotModel

    // Shared popup width for the sessions drawer (~⅓ of the carousel width,
    // clamped) — wide enough that its content doesn't wrap.
    readonly property real drawerWidth:
        Math.round(Math.max(Theme.sp(420), Math.min(width / 3, Theme.sp(560))))

    implicitHeight: Theme.carouselHeight

    // Opens the export options sheet for a set of swing dirs. The ⋯ "export all
    // selected" action routes through here, sharing one options panel, one
    // exporter call and one toast.
    function _openExportSheet(dirs, emptyMsg) {
        if (dirs.length === 0) {            // analysis-only shots have no on-disk files
            toast.showUndo = false
            toast.copyText = ""
            toast.glyph    = "ℹ"
            toast.show(emptyMsg)
            return
        }
        exportSheet.swingDirs   = dirs
        exportSheet.cameras     = swingExporter.camerasForShots(dirs)
        exportSheet.shotCount   = dirs.length
        exportSheet.includeJson = true
        exportSheet.open()
    }

    ShotFilterProxyModel {
        id: filterProxy
        sourceModel: root.activeModel
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

        // ── Session chip + filter combo + bulk-action menu ───────────────────
        RowLayout {
            Layout.alignment: Qt.AlignLeft
            spacing: Theme.sp(4)

            Rectangle {   // session chooser chip — live name or loaded-session label
                id: sessChip
                Layout.alignment: Qt.AlignVCenter
                implicitWidth:  sessRow.implicitWidth + Theme.sp(20)
                implicitHeight: Theme.sp(22)
                radius: Theme.radius
                color:  "transparent"
                border.width: 1
                border.color: sessionsPopup.opened || sessMa.containsMouse
                                  ? Theme.colorBorderMid : "transparent"
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                Row {
                    id: sessRow
                    anchors.centerIn: parent
                    spacing: Theme.sp(7)

                    Rectangle {   // live dot — only in live mode
                        visible: !root.reviewing
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.sp(7); height: Theme.sp(7); radius: Theme.sp(3.5)
                        color: Theme.colorError
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.reviewing
                                  ? (sessionReviewController.activeDayLabel
                                     + (sessionReviewController.activeTimeLabel
                                            ? " · " + sessionReviewController.activeTimeLabel : "")
                                     + qsTr(" · %1 shots").arg(sessionReviewController.activeShotCount))
                                  : qsTr("LIVE · %1 shots").arg(shotModel.activeCount)
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzBody2
                        color:          Theme.colorText
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
                    id: sessMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked: {
                        if (sessionsPopup.opened) {
                            sessionsPopup.close()
                        } else {
                            sessionReviewController.refresh()   // re-enumerate disk + live
                            sessionsPopup.open()
                        }
                    }
                }
            }

            Rectangle {   // hairline between the session chip and the filter pill
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 1; implicitHeight: Theme.sp(14)
                color: Theme.colorBorderMid; opacity: Theme.borderOpacityNormal
            }

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
                                    root._openExportSheet(
                                        root.activeModel.swingDirsForIds(filterProxy.visibleShotIds()),
                                        qsTr("No saved shots to export"))
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
                                    const n = root.activeModel.moveAllToTrash(ids)
                                    toast.showUndo = false   // OS trash is the recovery path, not an in-app undo
                                    toast.copyText = ""
                                    toast.glyph    = "🗑"
                                    toast.show(ids.length === 0 ? qsTr("No shots to trash")
                                               : n === ids.length ? qsTr("%1 shots moved to trash").arg(n)
                                               : qsTr("Moved %1 of %2 shots to trash").arg(n).arg(ids.length))
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
                // Highlight follows the swing currently on the stage (filmstrip
                // → loupe). Single click promotes this swing into Review.
                selected: shotId === SessionMode.focusedShotId
                onTapped: {
                    root.selectedShotId = shotId
                    root.selectedCard   = cardDelegate
                    SessionMode.enterReview(shotId, swingDir)
                }
                onRated: (n) => root.activeModel.setRating(shotId, n)
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

    // ── Sessions drawer — rises above the carousel, never reaches the toolbar ─
    Popup {
        id: sessionsPopup
        parent: root
        x: 0
        width: root.drawerWidth   // shared with the shot panel (siblings)
        y: -height - Theme.sp(10)

        // Clamp so the top stays below the toolbar. Window height (the overlay)
        // minus the header, toolbar and this carousel leaves the body band; the
        // drawer fills it less small gaps. Fits content when sessions are few.
        readonly property real _winH: Overlay.overlay ? Overlay.overlay.height : Theme.sp(600)
        readonly property real _bodyH: _winH - Theme.headerHeight - Theme.sp(60) - root.height
        height: Math.max(Theme.sp(120),
                         Math.min(sessDrawer.implicitHeight, _bodyH - Theme.sp(20)))

        padding: 0
        margins: Theme.sp(8)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }
        contentItem: PpSessionDrawer {
            id: sessDrawer
            onCloseRequested: sessionsPopup.close()
        }
    }

    // ── Bulk export options — opens upward over the left cap ─────────────────
    PpExportOptionsSheet {
        id: exportSheet
        parent: root
        x: Theme.sp(16)
        y: -height - Theme.sp(10)
        onConfirmed: (selectedVideoFiles, includeJson) =>
            swingExporter.exportShots(exportSheet.swingDirs, selectedVideoFiles, includeJson)
    }

    // Completion notice for the bulk export — the copy icon copies the zip path.
    Connections {
        target: swingExporter
        function onExportFinished(ok, zipPath, error) {
            toast.showUndo = false
            if (ok) {
                toast.glyph    = "✓"
                toast.copyText = zipPath
                toast.show(qsTr("Exported %1").arg(zipPath.split('/').pop()))
            } else {
                toast.glyph    = "⚠"
                toast.copyText = ""
                toast.show(qsTr("Export failed: %1").arg(error))
            }
        }
    }

    // ── Notice toast (export result / trash confirmation; no in-app undo) ─────
    PpToast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - Theme.sp(14)
        showUndo: false
    }
}
