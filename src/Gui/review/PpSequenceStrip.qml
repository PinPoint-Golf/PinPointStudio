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

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import PinPointStudio

// PpSequenceStrip — the kinematic sequence's SENTENCE under the chart. Since 2026-09-18 the peaks,
// the leads between them and the out-of-sight bounds are drawn on the plot itself
// (PpChartPlot.sequence, from ChartMetrics.sequenceOverlay); what remains here is the header with
// the route, and the verdict. Was: the chips — restating the
// positions of the four curves in a row of boxes at a different scale, which read oddly beside them.
// Originally: the kinematic sequence, laid out as the one thing the golfer is told
// (docs/design/kinematic_sequence_design.md §8): the four segment chips in the order they PEAKED,
// the gap between neighbours, and the verdict — or the honest refusal of one.
//
// It is shown under the chart only while the METRICS preset is "Kinematic sequence", because the
// chips are the peaks of exactly the four curves that preset draws; a strip with no curves above
// it would be a number with nothing to check it against.
//
// EVERY STRING ON THIS STRIP IS C++'s. ChartMetrics.sequenceRows / sequenceVerdictText /
// sequenceRouteText carry the ordering walk and the string rules, so chart_metrics_test can assert
// them; nothing here derives — it binds. A chip's greyed state is the row's `placed` flag, which the
// producer decided from its own σ (a node less certain than the placement threshold is emitted
// unplaced, never dropped), so the reader sees the segment was attempted and from which view.
//
// The method glyph (I / T / P: inertial, triangulated, projected) is the directory's vocabulary
// for HOW a rung got its number. It is what tells a reader with one IMU which chip to trust.
ColumnLayout {
    id: root
    objectName: "sequenceStrip"

    property var  kinematicSequence: null      // analysisDetail.kinematicSequence (may be null)
    property real impactUs: -1                 // carried for parity with the summary; the rows are already impact-relative

    spacing: Theme.sp(6)

    ChartMetrics { id: cm }

    readonly property var    _ks:      root.kinematicSequence ? root.kinematicSequence : ({})
    readonly property var    _rows:    cm.sequenceRows(root._ks)
    readonly property string _verdict: cm.sequenceVerdictText(root._ks)
    readonly property string _route:   cm.sequenceRouteText(root._ks)
    // The nodes neither placed nor bounded, named — the peaks and bounds themselves are drawn on
    // the plot (PpChartPlot.sequence), so the strip is the sentence under them and nothing more.
    readonly property var    _overlay:  cm.sequenceOverlay(root._ks)
    readonly property string _chain:    root._overlay.chainText || ""

    // ── header — the same shape as the summary section's ──────────────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.sp(9)
        Text {
            objectName: "sequenceStripHeader"
            text: qsTr("SEQUENCE") + (root._route ? " · " + root._route : "")
            font.family: Theme.fontData; font.pixelSize: Theme.fontSzMicro
            font.letterSpacing: Theme.trackingLabel
            color: Theme.colorText3
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colorBorder }
    }

    // ── the verdict — one sentence, or the honest refusal of one ──────────────────────────────
    Text {
        objectName: "sequenceVerdict"
        Layout.fillWidth: true
        visible: root._verdict.length > 0
        // "Lead arm −87 ms → Club −4 ms (+83 ms) · placed nodes in order (2 of 4)": the order
        // with its leads first (a split view cannot bracket a lead between two facets, so the
        // line carries it), then the verdict.
        text: (root._chain.length > 0 ? root._chain + " · " : "") + root._verdict
        wrapMode: Text.WordWrap
        font.family: Theme.fontBody; font.pixelSize: Theme.fontSzBody2
        color: Theme.colorText2
    }
}
