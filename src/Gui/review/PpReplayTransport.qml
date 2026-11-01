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

// Always-on Replay transport chrome: play/pause, frame-step and speed. Slim control
// that rides in the shot carousel's top strip in Replay/Analyse, so video-only review
// still works even when the timeline panel (scrub + phase pills) is hidden. There is no
// Close button — exit via the toolbar mode switch / Capture / Esc.
//
// Content-sized (implicitWidth follows the button row) so the host can right-align it in
// the carousel strip; it does not stretch to fill.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    implicitWidth:  bar.implicitWidth
    implicitHeight: Theme.sp(40)

    function _fmt(us) { return (Math.max(0, us) / 1e6).toFixed(2) + "s" }

    RowLayout {
        id: bar
        anchors.centerIn: parent
        spacing: Theme.gap(4)

        PpButton { style: "glyph"; size: "icon"; glyph: "⏮"; onClicked: shotReplay.seekToUs(shotReplay.startUs) }
        PpButton { style: "glyph"; size: "icon"; glyph: "◂"; onClicked: shotReplay.stepFrame(-1) }
        PpButton { style: "glyph"; size: "icon"; glyph: shotReplay.playing ? "⏸" : "▶"; active: true
                   onClicked: shotReplay.togglePlay() }
        PpButton { style: "glyph"; size: "icon"; glyph: "▸"; onClicked: shotReplay.stepFrame(1) }

        // Playback speed (capture-time multiplier; 1× = real time) — honoured by
        // the running replay immediately.
        // All speeds visible, slowest → full speed; the active one highlighted by
        // proximity so float representation never breaks the match.
        PpSegmentedControl {
            readonly property var speeds: [0.1, 0.25, 0.5, 1.0]
            Layout.leftMargin: Theme.gap(8)
            style:    "speed"
            options:  speeds.map(sp => ({ label: (sp >= 1 ? "1" : Number(sp).toString()) + "×", value: String(sp) }))
            selected: {
                const near = speeds.filter(sp => Math.abs(shotReplay.speed - sp) < 0.01)
                return near.length > 0 ? String(near[0]) : ""
            }
            onActivated: (value) => shotReplay.setSpeed(parseFloat(value))
        }

        PpMicro {
            Layout.leftMargin: Theme.gap(8)
            text: root._fmt(shotReplay.positionUs - shotReplay.startUs)
                  + " / " + root._fmt(shotReplay.endUs - shotReplay.startUs)
            font.letterSpacing: Theme.trackingData
        }
    }
}
