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
// the carousel strip; it does not stretch to fill. Its width holds still through playback
// (the time readout is sized to the clip's duration), so the host's layout never jumps.

import QtQuick
import QtQuick.Layouts
import PinPointStudio

Item {
    id: root
    implicitWidth:  bar.implicitWidth
    implicitHeight: Theme.sp(40)

    function _fmt(us) { return (Math.max(0, us) / 1e6).toFixed(2) + "s" }
    property string _posText: _fmt(shotReplay.positionUs - shotReplay.startUs)
    property string _durText: _fmt(shotReplay.endUs - shotReplay.startUs)

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

        // Position / duration. Sized to the duration's text in both halves, so the readout (and
        // the whole transport) keeps one width through playback: the position never has more
        // digits than the duration, and the data face's digits are fixed-width in every theme
        // (monospaced, or tabular in Folio). Without this a clip of 10 s or more widened by a
        // digit as the playhead passed 10 s, and the carousel's fit test could move the transport
        // between its rows mid-replay.
        PpMicro {
            id: readout
            objectName: "transportTime"
            Layout.leftMargin: Theme.gap(8)
            Layout.preferredWidth: Math.max(implicitWidth, readoutSizer.implicitWidth)
            horizontalAlignment: Text.AlignRight   // the duration stays still as the position gains a digit
            text: root._posText + " / " + root._durText
            font.letterSpacing: Theme.trackingData
        }
    }
    PpMicro {   // measures the readout at its widest; never drawn
        id: readoutSizer
        visible: false
        text: root._durText + " / " + root._durText
        font.letterSpacing: Theme.trackingData
    }
}
