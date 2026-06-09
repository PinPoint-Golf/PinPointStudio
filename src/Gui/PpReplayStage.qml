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

// Main-screen replay stage — the large, persistent, interactive surface for a
// disk-backed shot replay (target "screen"). Bound entirely to the global
// `shotReplay` controller: one VideoOutput per camera (face-on + DTL), the synced
// metric graph, and a full transport bar (to-start, frame-step, play/pause, scrub
// back and forth, speed toggle, close). Stays open and interactive until the user
// closes it — it does NOT auto-dismiss like the post-shot auto-replay. The host
// (a rail screen) overlays one instance on its body and gates `visible`.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import PinPointStudio

Item {
    id: stage

    // Only render (and bind sinks) when this surface is the active target, so we
    // never steal the player's single sink from the in-panel inline replay.
    readonly property bool onScreen: shotReplay.active && shotReplay.target === "screen"
    readonly property int  n: Math.max(1, shotReplay.streamCount)

    function _fmt(us) { return (Math.max(0, us) / 1e6).toFixed(2) + "s" }

    // Compact icon button used across the transport bar.
    component TBtn: Rectangle {
        property string glyph: ""
        property bool   highlight: false
        signal acted()
        width: Theme.sp(34); height: Theme.sp(34); radius: Theme.radius
        color: tbma.containsMouse ? Theme.colorBg3 : "transparent"
        border.width: 1
        border.color: highlight ? Theme.colorAccent : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Text {
            anchors.centerIn: parent
            text: parent.glyph
            font.family: Theme.fontSymbol
            font.pixelSize: Theme.sp(15)
            color: Theme.colorText2
        }
        MouseArea {
            id: tbma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.acted()
        }
    }

    Rectangle { anchors.fill: parent; color: Theme.colorBg2 }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.sp(12)
        spacing: Theme.sp(8)

        // ── Header ──────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(8)
            Text {
                text: qsTr("REPLAY")
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                font.letterSpacing: Theme.trackingLabel
                color: Theme.colorText3
            }
            Item { Layout.fillWidth: true }

            // Close — outlined in the attention hue (not a solid fill).
            Rectangle {
                implicitWidth:  closeRow.implicitWidth + Theme.sp(24)
                implicitHeight: Theme.sp(34)
                radius: Theme.radius
                color: closeMa.containsMouse
                           ? Qt.rgba(Theme.colorAttention.r, Theme.colorAttention.g,
                                     Theme.colorAttention.b, 0.12)
                           : "transparent"
                border.width: 1
                border.color: Theme.colorAttention
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }

                Row {
                    id: closeRow
                    anchors.centerIn: parent
                    spacing: Theme.sp(7)
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✕"
                        font.family: Theme.fontSymbol
                        font.pixelSize: Math.round(Theme.fontSzBody * Theme.symbolScale("✕"))
                        color: Theme.colorAttention
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Close")
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontSzBody
                        color: Theme.colorAttention
                    }
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: shotReplay.stop()
                }
            }
        }

        // ── Video (one VideoOutput per camera, side by side) ────────────────
        Item {
            id: videoArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Row {
                anchors.fill: parent
                spacing: Theme.sp(6)
                Repeater {
                    // Gate on the screen target so the panel's Repeater keeps the
                    // sink when the inline surface is the active one.
                    model: stage.onScreen ? shotReplay.streamCount : 0
                    delegate: Rectangle {
                        required property int index
                        width:  (videoArea.width - (stage.n - 1) * Theme.sp(6)) / stage.n
                        height: videoArea.height
                        color:  "#000000"
                        radius: Theme.radius
                        clip:   true
                        VideoOutput {
                            id: vo
                            anchors.fill: parent
                            fillMode: VideoOutput.PreserveAspectFit
                            Component.onCompleted: shotReplay.setVideoSink(index, vo.videoSink)
                        }
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

        // ── Synced metric graph ─────────────────────────────────────────────
        PpMetricGraph {
            readonly property bool _has: !!(shotReplay.analysisDetail
                                            && shotReplay.analysisDetail.series
                                            && shotReplay.analysisDetail.series.length > 0)
            Layout.fillWidth: true
            Layout.preferredHeight: _has ? Theme.sp(120) : 0
            visible:      _has
            seriesList:   (shotReplay.analysisDetail && shotReplay.analysisDetail.series) ? shotReplay.analysisDetail.series : []
            phases:       (shotReplay.analysisDetail && shotReplay.analysisDetail.phases) ? shotReplay.analysisDetail.phases : []
            startUs:      shotReplay.startUs
            endUs:        shotReplay.endUs
            impactUs:     shotReplay.impactUs
            playheadUs:   shotReplay.positionUs
            showPlayhead: true
        }

        // ── Transport controls ──────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.sp(4)

            TBtn { glyph: "⏮"; onActed: shotReplay.seekToUs(shotReplay.startUs) }
            TBtn { glyph: "◂"; onActed: shotReplay.stepFrame(-1) }
            TBtn { glyph: shotReplay.playing ? "⏸" : "▶"; highlight: true
                   onActed: shotReplay.togglePlay() }
            TBtn { glyph: "▸"; onActed: shotReplay.stepFrame(1) }

            Text {
                Layout.leftMargin: Theme.sp(6)
                text: stage._fmt(shotReplay.positionUs - shotReplay.startUs)
                      + " / " + stage._fmt(shotReplay.endUs - shotReplay.startUs)
                font.family: Theme.fontData
                font.pixelSize: Theme.fontSzMicro
                color: Theme.colorText3
            }

            Item { Layout.fillWidth: true }

            // Speed toggle ¼× ↔ ⅛×.
            Rectangle {
                width: Theme.sp(40); height: Theme.sp(34); radius: Theme.radius
                color: spdMa.containsMouse ? Theme.colorBg3 : "transparent"
                border.width: 1; border.color: Theme.colorBorderMid
                Text {
                    anchors.centerIn: parent
                    text: shotReplay.mode === "slow" ? "⅛×" : "¼×"
                    font.family: Theme.fontData
                    font.pixelSize: Theme.fontSzMicro
                    color: Theme.colorText2
                }
                MouseArea {
                    id: spdMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: shotReplay.setMode(shotReplay.mode === "slow" ? "normal" : "slow")
                }
            }
        }

        // ── Scrub bar — full-width, prominent; drag to scrub-search (works
        //    while paused). A bigger track + handle make it easy to grab. ─────
        Slider {
            id: scrub
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.sp(20)
            from: 0; to: 1
            Binding {
                target: scrub; property: "value"; when: !scrub.pressed
                value: shotReplay.endUs > shotReplay.startUs
                           ? (shotReplay.positionUs - shotReplay.startUs) / (shotReplay.endUs - shotReplay.startUs)
                           : 0
            }
            onPressedChanged: pressed ? shotReplay.beginScrub() : shotReplay.endScrub()
            onMoved: shotReplay.seekToFraction(value)

            background: Rectangle {
                x: scrub.leftPadding
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: scrub.availableWidth; height: Theme.sp(6); radius: height / 2
                color: Theme.colorBorderMid
                Rectangle {
                    width: scrub.visualPosition * parent.width; height: parent.height
                    radius: height / 2; color: Theme.colorAccent
                }
            }
            handle: Rectangle {
                x: scrub.leftPadding + scrub.visualPosition * (scrub.availableWidth - width)
                y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                width: Theme.sp(18); height: Theme.sp(18); radius: width / 2
                color: scrub.pressed ? Qt.lighter(Theme.colorAccent, 1.15) : Theme.colorAccent
                border.width: 2; border.color: Theme.colorBg
            }
        }
    }
}
