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
import QtMultimedia
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
    property string swingDir:       ""
    property bool   hasVideo:       false

    // A shot can be replayed from disk once its media + swing.json exist on disk
    // (swingDir set, and it actually has video — analysis-only shots don't).
    // True while THIS shot is the one the disk replay is driving.
    readonly property bool canReplay:     panel.swingDir !== "" && panel.hasVideo
    // The inline (in-panel) replay surface is active only when the replay targets
    // the panel — a popped-out replay (target "screen") renders on the rail body.
    readonly property bool _diskReplaying: shotReplay.active && shotReplay.shotId === panel.shotId
                                           && shotReplay.target === "panel"

    // All analysis metric series; the multi-metric graph overlays them, else fall back to
    // the normalised tracePoints sparkline (stub / no-IMU shots).
    readonly property var _series: (analysisDetail && analysisDetail.series) ? analysisDetail.series : []
    readonly property bool _hasSeries: {
        for (let i = 0; i < _series.length; ++i)
            if (_series[i].t_us && _series[i].t_us.length > 1) return true
        return false
    }

    signal replayRequested(string mode)
    signal popOutRequested()
    signal faceOnRequested()
    signal exportRequested()
    signal trashRequested()

    implicitWidth:  Theme.sp(468)
    implicitHeight: content.implicitHeight + Theme.sp(32)

    // The note field is deliberately unbound: its text is seeded per shot and
    // committed back via shotModel.setNote on focus loss / close, so typing
    // never fights a model binding.
    onShotIdChanged: {
        noteArea.text = panel.note
        // An inline replay belongs to the previously selected shot; a popped-out
        // screen replay is independent of the panel and keeps running.
        if (shotReplay.target === "panel")
            shotReplay.stop()
    }
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
                // ── Disk-backed replay video (shown only while replaying) ──────
                // One VideoOutput per recorded camera (face-on + DTL), side by
                // side. The controller drives every player from a single capture-
                // time clock, so all cameras stay in sync. Sinks bind after start()
                // sets streamCount — setVideoSink rebinds the live players.
                Rectangle {
                    id: replayBox
                    width:   parent.width
                    readonly property int n: Math.max(1, shotReplay.streamCount)
                    height:  panel._diskReplaying ? Math.round(width / replayBox.n * 9 / 16) : 0
                    visible: panel._diskReplaying
                    clip:    true
                    radius:  Theme.radius
                    color:   "#000000"

                    Row {
                        anchors.fill: parent
                        spacing: 1
                        Repeater {
                            // Bind sinks only while THIS (panel) surface drives the
                            // replay, so a popped-out screen replay keeps them.
                            model: panel._diskReplaying ? shotReplay.streamCount : 0
                            delegate: VideoOutput {
                                required property int index
                                width:  (replayBox.width - (replayBox.n - 1)) / replayBox.n
                                height: replayBox.height
                                fillMode: VideoOutput.PreserveAspectFit
                                Component.onCompleted: shotReplay.setVideoSink(index, videoSink)
                            }
                        }
                    }

                    // Click the video to play/pause — no on-video icon.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shotReplay.togglePlay()
                    }
                }

                Item { height: panel._diskReplaying ? Theme.sp(4) : 0; width: 1 }

                PpMetricGraph {        // overlaid metric curves + filter chips + replay playhead
                    visible: panel._hasSeries || panel._diskReplaying
                    width:  parent.width
                    height: panel._diskReplaying ? Theme.sp(76) : Theme.sp(96)
                    seriesList:   (panel._diskReplaying && shotReplay.analysisDetail && shotReplay.analysisDetail.series)
                                      ? shotReplay.analysisDetail.series : panel._series
                    phases:       panel._diskReplaying
                                      ? ((shotReplay.analysisDetail && shotReplay.analysisDetail.phases) ? shotReplay.analysisDetail.phases : [])
                                      : ((panel.analysisDetail && panel.analysisDetail.phases) ? panel.analysisDetail.phases : [])
                    startUs:      panel._diskReplaying ? shotReplay.startUs : 0
                    endUs:        panel._diskReplaying ? shotReplay.endUs   : 0
                    impactUs:     panel._diskReplaying ? shotReplay.impactUs : 0
                    playheadUs:   panel._diskReplaying ? shotReplay.positionUs : 0
                    showPlayhead: panel._diskReplaying
                }

                // Scrub bar — drag to seek; tracks the playhead while replaying.
                Slider {
                    id: scrub
                    visible: panel._diskReplaying
                    width:   parent.width
                    height:  panel._diskReplaying ? implicitHeight : 0
                    from: 0; to: 1
                    Binding {
                        target: scrub; property: "value"; when: !scrub.pressed
                        value: shotReplay.endUs > shotReplay.startUs
                                   ? (shotReplay.positionUs - shotReplay.startUs) / (shotReplay.endUs - shotReplay.startUs)
                                   : 0
                    }
                    onMoved: shotReplay.seekToFraction(value)

                    background: Rectangle {
                        x: scrub.leftPadding
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: scrub.availableWidth; height: Theme.sp(3); radius: height / 2
                        color: Theme.colorBorderMid
                        Rectangle {
                            width: scrub.visualPosition * parent.width; height: parent.height
                            radius: height / 2; color: Theme.colorAccent
                        }
                    }
                    handle: Rectangle {
                        x: scrub.leftPadding + scrub.visualPosition * (scrub.availableWidth - width)
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: Theme.sp(11); height: Theme.sp(11); radius: width / 2
                        color: Theme.colorAccent
                    }
                }

                PpTrace {              // sparkline fallback when there's no analysis series
                    visible: !panel._hasSeries && !panel._diskReplaying
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
                glyph:   panel._diskReplaying ? "■" : "▶"
                label:   panel._diskReplaying ? qsTr("Stop") : qsTr("Replay")
                enabled: panel.canReplay
                onClicked: {
                    if (panel._diskReplaying) { shotReplay.stop(); return }
                    shotReplay.start(panel.shotId, panel.swingDir, "normal")
                    panel.replayRequested("normal")
                }
            }
            PpButton {
                label: qsTr("½× Slow")
                enabled: panel.canReplay
                onClicked: {
                    shotReplay.start(panel.shotId, panel.swingDir, "slow")
                    panel.replayRequested("slow")
                }
            }
            PpButton {
                glyph: "⤢"
                label: qsTr("Pop out")
                enabled: panel.canReplay
                // Replay on the big interactive main-screen stage, then close the
                // popup. The carousel's onClosed leaves screen-targeted replays
                // running (only panel ones stop on close).
                onClicked: {
                    shotReplay.start(panel.shotId, panel.swingDir, "normal", "screen")
                    panel.popOutRequested()
                }
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
