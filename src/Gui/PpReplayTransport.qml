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

// Always-on Replay transport chrome: play/pause, frame-step and speed. Slim bar
// shown under the camera panel in Replay/Analyse, so video-only review still works
// even when the timeline panel (scrub + phase pills) is hidden. There is no Close
// button — exit via the toolbar mode switch / Capture / Esc.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    implicitHeight: Theme.sp(40)

    function _fmt(us) { return (Math.max(0, us) / 1e6).toFixed(2) + "s" }

    RowLayout {
        anchors.centerIn: parent
        spacing: Theme.sp(4)

        TBtn { glyph: "⏮"; onActed: shotReplay.seekToUs(shotReplay.startUs) }
        TBtn { glyph: "◂"; onActed: shotReplay.stepFrame(-1) }
        TBtn { glyph: shotReplay.playing ? "⏸" : "▶"; highlight: true
               onActed: shotReplay.togglePlay() }
        TBtn { glyph: "▸"; onActed: shotReplay.stepFrame(1) }

        // Playback speed (capture-time multiplier; 1× = real time) — honoured by
        // the running replay immediately.
        PpSpeedSelector {
            Layout.leftMargin: Theme.sp(8)
            current: shotReplay.speed
            onSelected: (speed) => shotReplay.setSpeed(speed)
        }

        Text {
            Layout.leftMargin: Theme.sp(8)
            text: root._fmt(shotReplay.positionUs - shotReplay.startUs)
                  + " / " + root._fmt(shotReplay.endUs - shotReplay.startUs)
            font.family: Theme.fontData
            font.pixelSize: Theme.fontSzMicro
            color: Theme.colorText3
        }
    }

    // Compact icon button (mirrors the old PpReplayStage transport).
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
}
