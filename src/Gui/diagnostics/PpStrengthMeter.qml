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

// HOW FAR OUT, in five steps — the answer the state pill beside it cannot give.
//
// A firing drawn in one colour says THAT a condition was out of its corridor and never BY
// HOW MUCH, and a reading a hair past the edge and one a mile past it are different coaching
// problems wearing the same red. Step 0 is the middle of the corridor and step 5 is well
// outside it; the ramp runs green through amber to red, so the meter agrees with the colour
// the rest of the panel is already using for the same reading.
//
// THE STEP IS THE MODEL'S. diagnostic_ledger.h's severityLevel() reads it off the row's
// corridor excess and SessionDiagnosticsModel publishes it — this file positions and paints,
// and there is no arithmetic in here beyond laying out five rectangles (§6.2). What is
// decided here is only how a step LOOKS.
//
// `known` FALSE DRAWS NOTHING, and that is the same rule the outlined tick keeps one layer
// down: step 0 is the best news on the meter, so a measure the capture could not assess — or
// one graded against an authored number rather than a band — must never be able to land on
// it. No reading, no meter.
//
// A RISING LADDER rather than five equal bars, because a run of equal bars on a card that
// already carries PpTickRun's run of equal bars is two different claims drawn as one shape.
// The stair says "a scale"; the run says "a sequence".

import QtQuick
import QtQuick.Controls
import PinPointStudio

Item {
    id: root

    // SessionDiagnosticsModel's `strength` block, off any card, node, cell or chip:
    //   strength       0..5
    //   strengthKnown  whether there is a reading behind it
    //   strengthText   the one sentence, number included
    property int    level:   0
    property bool   known:   false
    property string caption: ""
    // The panel's fit scale. See PpSessionDiagnosticsBody._fitFor().
    property real fit: 1.0

    objectName: "sdStrengthMeter"

    readonly property int steps: 5

    // The design's metrics at k = 1: 3 px wide, 1.5 px gap, and a stair from 4 to 10 px.
    // Through Theme.fontScale and then the panel's fit — the two factors every metric on
    // this panel carries. Not Theme.sp(), which would round the 1.5 away before the fit.
    readonly property real _unit: Theme.fontScale * root.fit
    readonly property real _barW: 3.0 * _unit
    readonly property real _gap:  1.5 * _unit
    readonly property int  _minH: Math.max(1, Math.round(4.0 * _unit))
    readonly property int  _maxH: Math.max(2, Math.round(10.0 * _unit))

    // One colour for the whole lit run, not one per step: the meter makes a single statement
    // about a single reading, and a stair that graded itself internally would read as five.
    readonly property color _lit: Theme.strengthColor(root.level)

    // AT STEP 0 THE EMPTY LADDER IS GREEN. Nothing is lit, so without this the meter would
    // look the same as a meter with no reading behind it — and those two are opposites.
    readonly property color _track: root.level === 0
        ? Qt.rgba(Theme.colorGood.r, Theme.colorGood.g, Theme.colorGood.b, 0.35)
        : Theme.colorBorderMid

    visible: root.known
    implicitWidth:  steps * _barW + (steps - 1) * _gap
    implicitHeight: _maxH

    Accessible.role: Accessible.Indicator
    Accessible.name: root.caption

    HoverHandler { id: meterHover; enabled: root.caption !== "" }
    ToolTip.visible: meterHover.hovered
    ToolTip.text: root.caption
    ToolTip.delay: 400

    Repeater {
        model: root.steps

        Rectangle {
            required property int index

            objectName: "sdStrengthBar"

            readonly property bool on: index < root.level

            x: index * (root._barW + root._gap)
            width: root._barW
            height: root._minH
                    + Math.round((root._maxH - root._minH) * (index / (root.steps - 1)))
            y: root._maxH - height
            radius: Math.max(1, Math.round(root._unit))

            color: on ? root._lit : root._track
            // The unlit steps are outlined rather than filled, so a meter reading 2 cannot be
            // mistaken for one reading 5 in a faint colour at a glance.
            opacity: on ? 1.0 : 0.5
        }
    }
}
