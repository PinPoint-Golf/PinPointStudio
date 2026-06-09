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

// Shot review panel — the pop-over above a selected carousel card. Header
// (ordinal, timestamp · club, quality pill, ⋯ menu), trace chart, metrics
// grid, review actions, interactive rating, and a note field. Rating and note
// write straight to the shotModel invokables; replay/export/face-on bubble up
// through the carousel for the host screen to own. The ⋯ menu keeps Export
// and the warn-coloured Move-to-trash in the panel's coldest corner — trash
// is soft (reversible via the Undo toast), so there is no modal confirm.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PinPointStudio

Item {
    id: panel

    property int    shotId:         -1
    property int    ordinal:        0
    property string timestampLabel: ""
    property string club:           ""
    property int    score:          0
    property int    rating:         0
    property string note:           ""
    property var    tracePoints:    []
    property var    metrics:        ({})
    property var    metricKeys:     []
    property string traceLabel:     ""
    property var    analysisDetail: ({})

    // All analysis metric series; the multi-metric graph overlays them, else fall back to
    // the normalised tracePoints sparkline (stub / no-IMU shots).
    readonly property var _series: (analysisDetail && analysisDetail.series) ? analysisDetail.series : []
    readonly property bool _hasSeries: {
        for (let i = 0; i < _series.length; ++i)
            if (_series[i].t_us && _series[i].t_us.length > 1) return true
        return false
    }

    signal replayRequested(string mode)
    signal faceOnRequested()
    signal exportRequested()
    signal trashRequested()

    implicitWidth:  Theme.sp(468)
    implicitHeight: content.implicitHeight + Theme.sp(32)

    // The note field is deliberately unbound: its text is seeded per shot and
    // committed back via shotModel.setNote on focus loss / close, so typing
    // never fights a model binding.
    onShotIdChanged: noteArea.text = panel.note
    Component.onCompleted: noteArea.text = panel.note

    function commitNote() {
        if (panel.shotId >= 0 && noteArea.text !== panel.note)
            shotModel.setNote(panel.shotId, noteArea.text)
    }

    ColumnLayout {
        id: content
        anchors { left: parent.left; right: parent.right; top: parent.top
                  leftMargin: Theme.sp(17); rightMargin: Theme.sp(17); topMargin: Theme.sp(16) }
        spacing: 0

        // ── Header ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(10)

            Column {
                Layout.alignment: Qt.AlignTop
                spacing: Theme.sp(2)
                Text {
                    text:           qsTr("Shot #%1").arg(panel.ordinal)
                    font.family:    Theme.fontDisplay
                    font.pixelSize: Theme.fontSzHeading
                    font.weight:    Theme.fontDisplayWeight
                    font.italic:    Theme.fontDisplayItalic
                    color:          Theme.colorText
                }
                Text {
                    text:           panel.timestampLabel + " · " + panel.club
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingLabel
                    color:          Theme.colorText3
                }
            }

            Item { Layout.fillWidth: true }

            Column {
                Layout.alignment: Qt.AlignTop
                spacing: Theme.sp(3)
                Text {
                    anchors.right: parent.right
                    text:           qsTr("SHOT QUALITY")
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingLabel
                    color:          Theme.colorText3
                }
                PpQualityPill {
                    anchors.right: parent.right
                    score: panel.score
                    large: true
                }
            }

            Rectangle {   // ⋯ kebab — the coldest corner
                id: kebab
                Layout.alignment: Qt.AlignTop
                width:  Theme.sp(30)
                height: Theme.sp(30)
                radius: Theme.radius
                color:  menuPopup.opened || kebabMa.containsMouse ? Theme.colorBg2 : "transparent"
                border.width: 1
                border.color: menuPopup.opened ? Theme.colorBorderMid : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Text {
                    anchors.centerIn: parent
                    text:           "⋯"
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.sp(18)
                    color:          Theme.colorText2
                }
                MouseArea {
                    id: kebabMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    menuPopup.opened ? menuPopup.close() : menuPopup.open()
                }
            }
        }

        Item { Layout.preferredHeight: Theme.sp(13) }

        // ── Trace chart ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: traceCol.implicitHeight + Theme.sp(16)
            radius: Theme.radius
            color:  Theme.colorBg
            border.width: 1
            border.color: Theme.colorBorderMid

            Column {
                id: traceCol
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: Theme.sp(10); rightMargin: Theme.sp(10); topMargin: Theme.sp(8) }
                spacing: Theme.sp(2)
                Text {
                    text:           panel.traceLabel
                    font.family:    Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    font.letterSpacing: Theme.trackingLabel
                    color:          Theme.colorText3
                }
                PpMetricGraph {        // overlaid metric curves + filter chips + replay playhead
                    visible: panel._hasSeries
                    width:  parent.width
                    height: Theme.sp(96)
                    seriesList:   panel._series
                    phases:       (panel.analysisDetail && panel.analysisDetail.phases) ? panel.analysisDetail.phases : []
                    startUs:      shotProcessor.isReplaying ? shotProcessor.replayStartUs : 0
                    endUs:        shotProcessor.isReplaying ? shotProcessor.replayEndUs   : 0
                    impactUs:     shotProcessor.replayImpactUs
                    playheadUs:   shotProcessor.replayPositionUs
                    showPlayhead: shotProcessor.isReplaying
                }
                PpTrace {              // sparkline fallback when there's no analysis series
                    visible: !panel._hasSeries
                    width:  parent.width
                    height: Theme.sp(58)
                    points: panel.tracePoints
                }
            }
        }

        Item { Layout.preferredHeight: Theme.sp(14) }

        // ── Metrics grid (2 × 2 for the four per-screen keys) ────────────────
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing:    Theme.sp(14)
            columnSpacing: Theme.sp(26)

            Repeater {
                model: panel.metricKeys

                Column {
                    Layout.fillWidth: true
                    spacing: Theme.sp(3)
                    readonly property var metric: panel.metrics[modelData] !== undefined
                                                      ? panel.metrics[modelData]
                                                      : ({ label: modelData, value: "—" })
                    Text {
                        text:           metric.label.toUpperCase()
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzMicro
                        font.letterSpacing: Theme.trackingLabel
                        color:          Theme.colorText3
                    }
                    Text {
                        text:           metric.value
                        font.family:    Theme.fontData
                        font.pixelSize: Theme.fontSzDataSm
                        color:          Theme.colorText
                    }
                }
            }
        }

        Item { Layout.preferredHeight: Theme.sp(16) }

        Rectangle {   // hairline
            Layout.fillWidth: true
            height: 1
            color: Theme.colorBorderMid
            opacity: Theme.borderOpacityNormal
        }

        Item { Layout.preferredHeight: Theme.sp(14) }

        // ── Review actions ───────────────────────────────────────────────────
        Text {
            text:           qsTr("REVIEW")
            font.family:    Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingMicro
            color:          Theme.colorText3
        }

        Item { Layout.preferredHeight: Theme.sp(9) }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)

            PpButton {
                primary: true
                glyph:   "▶"
                label:   qsTr("Replay")
                onClicked: panel.replayRequested("normal")
            }
            PpButton {
                label: qsTr("½× Slow")
                onClicked: panel.replayRequested("slow")
            }
            PpButton {
                glyph: "◑"
                label: qsTr("Face-on")
                onClicked: panel.faceOnRequested()
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.preferredHeight: Theme.sp(16) }

        // ── Rating ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Text {
                text:           qsTr("YOUR RATING")
                font.family:    Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingMicro
                color:          Theme.colorText3
            }
            Item { Layout.fillWidth: true }
            PpStarRating {
                interactive: true
                value:       panel.rating
                starSize:    Theme.fontSzDataSm
                spacing:     Theme.sp(3)
                onRated: (n) => shotModel.setRating(panel.shotId, n)
            }
        }

        Item { Layout.preferredHeight: Theme.sp(9) }

        // ── Note ─────────────────────────────────────────────────────────────
        TextArea {
            id: noteArea
            Layout.fillWidth: true
            Layout.minimumHeight: Theme.sp(50)
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("Add a note for this shot…")
            placeholderTextColor: Theme.colorText3
            font.family:    Theme.fontBody
            font.pixelSize: Theme.fontSzBody2
            font.weight:    Theme.fontBodyWeight
            color:          Theme.colorText
            leftPadding:    Theme.sp(10)
            rightPadding:   Theme.sp(10)
            topPadding:     Theme.sp(8)
            bottomPadding:  Theme.sp(8)

            // Commit on focus loss — same auto-commit contract as PpTextField.
            onActiveFocusChanged: if (!activeFocus) panel.commitNote()

            background: Rectangle {
                radius:       Theme.radius
                color:        Theme.colorSurface
                border.width: 1
                border.color: noteArea.activeFocus ? Theme.colorAccent : Theme.colorBorderMid
            }
        }
    }

    // ── Overflow menu — Export, then the separated warn group ───────────────
    Popup {
        id: menuPopup
        parent: kebab
        y: kebab.height + Theme.sp(4)
        x: kebab.width - width
        padding: Theme.sp(5)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        // Widest row plus left inset and right margin — the popup sizes its
        // contentItem from contentWidth, so the labels never run tight to the
        // menu's right edge.
        contentWidth: Math.max(Theme.sp(168),
                               Math.max(exportRow.implicitWidth, trashRow.implicitWidth)
                               + Theme.sp(28))
        background: Rectangle {
            color: Theme.colorSurface; radius: Theme.radiusLg
            border.width: 1; border.color: Theme.colorBorderStrong
        }

        contentItem: Column {

            Rectangle {   // Export shot…
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
                        text: qsTr("Export shot…")
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color: Theme.colorText
                    }
                }
                MouseArea {
                    id: exportMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menuPopup.close()
                        panel.exportRequested()
                    }
                }
            }

            Rectangle {   // separator — destructive actions live in their own group
                width: parent.width - Theme.sp(12)
                anchors.horizontalCenter: parent.horizontalCenter
                height: 1
                color: Theme.colorBorderMid
                opacity: Theme.borderOpacityNormal
            }

            Rectangle {   // Move to trash (soft delete)
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
                MouseArea {
                    id: trashMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        menuPopup.close()
                        panel.trashRequested()
                    }
                }
            }
        }
    }
}
