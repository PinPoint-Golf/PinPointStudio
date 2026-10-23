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

import QtQuick
import PinPointStudio

// THE REPORT'S PROFILE — one row per reading across P1…P8, printed (the session report's Posture and Wrist pages,
// PpPosturePage / PpWristPage; docs/design/session_report_design.md). Lifted out of PpPosturePage unchanged, so the
// Posture page draws exactly as it did; the Wrist page reads the same row keys (report_wrist.h maps the wrist rows onto
// them). Three kinds of pager atom, in one printGroup (its head, the column heads, repeats on a page that opens inside
// the list):
//   · the column heads: the position tags (ink3 where no swing reached it) and their names;
//   · one atom per reading: the label (the reading's own metric colour as a dot, its words, an optional style line —
//     posture's trail-knee style — and the camera / source with the unit); the plot — the zero line (address), the
//     band per position as a wash in the reading's OWN metric colour (identity, not judgement), the fault lines as
//     warn hairlines where the row has them, the session's box per position (PpChBoxCell → PpBoxPlot, the ONE box
//     plot, with box_stats' numbers from the presenter) on the row's own scale (yLo…yHi); the pole word (which way is
//     plus); and under each column the median ("-" where nothing was read), then the side of the band it falls on in
//     warn and underlined (a word, never colour alone) or how wide the middle half is, then a ⚠'s few words where the
//     cell has them; under a cell with an alt reading (posture's back line under trunk bend at P3–P5), a second,
//     smaller figure with its own ⚠ words;
//   · the caption, kept with the last row.
// What is optional is optional BY THE DATA: styleText "" draws no style line, a cell without faultHi / faultLo draws
// no fault line, a cell without alt draws no second reading, a cell without warn no ⚠ — the wrist rows carry none.
// Nothing is computed here: every word and number is the row's; the layout maths is the only arithmetic.
// INKS: `ro` (the report's palette and type, Folio light) and `roles` (the Charts print roles in the same fixed
// aesthetic, for the box): nothing reads a live theme token. The page that holds this keeps its PpChRoles LAST among
// its children (teardown: the boxes here go before their roles).
// objectNames, from `prefix`: <prefix>Profile, <prefix>Row (+ key), <prefix>Cell (+ tag, text, warned, outside),
// <prefix>AltValue (+ tag, rowKey, text, warned) — "reportPosture" keeps the Posture page's names.
Column {
    id: rp
    property QtObject ro: null
    property QtObject roles: null
    property string prefix: "reportPosture"
    property var rows: []
    property var positions: []
    property string caption: ""
    property bool on: true
    objectName: prefix + "Profile"
    readonly property var keyKinds: []
    property string printGroup: "posture"
    visible: on && rows.length > 0
    width: 600

    // the geometry: a label column, eight position columns, the pole word at the right
    readonly property real labelW: 122
    readonly property real poleW: 46
    readonly property real plotX: labelW
    readonly property real plotW: width - labelW - poleW
    readonly property real colW: plotW / 8
    readonly property real plotH: 50
    function cx(i) { return plotX + (i + 0.5) * colW }
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }

    Item {
        property bool atom: true
        width: rp.width; height: colHeads.height + 6
        Rectangle { anchors.fill: parent; color: rp.ro.paper }
        Item {
            id: colHeads
            width: rp.width; height: childrenRect.height
            Repeater {
                model: rp.positions
                delegate: Column {
                    required property var modelData
                    required property int index
                    x: rp.cx(index) - rp.colW / 2 + 2; width: rp.colW - 4
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData.tag || ""
                           font: rp.ro.fSmallM; color: modelData.available === false ? rp.ro.ink3 : rp.ro.ink }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; visible: text !== "" && text !== (modelData.tag || "")
                           text: modelData.name || ""; font: rp.ro.fMicro; color: rp.ro.ink3; wrapMode: Text.WrapAtWordBoundaryOrAnywhere }
                }
            }
        }
        Rectangle { x: rp.plotX; y: colHeads.height + 4; width: rp.plotW; height: rp.ro.hw; color: rp.ro.hair }
    }
    Repeater {
        model: rp.rows
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: rp.prefix + "Row"
            property bool atom: true
            readonly property var r: modelData
            readonly property string key: r.key || ""
            readonly property var cells: (r.cells || []).slice(0, 8)
            readonly property var boxes: (r.boxes || []).slice(0, 8)
            readonly property color hue: rp.ro.hue(r.hueKey || r.metricKey || "")
            readonly property real lo: rp.fin(r.yLo) ? r.yLo : -1
            readonly property real hi: rp.fin(r.yHi) ? r.yHi : 1
            function vy(v) { return 4 + (rp.plotH - 8) * (1 - (v - lo) / Math.max(1e-9, hi - lo)) }
            width: rp.width
            height: Math.max(labCol.height + 6, rp.plotH + nums.height + 12)

            // the label: the reading, the trail knee's style, the camera and unit
            Column {
                id: labCol
                y: 4; width: rp.labelW - 12; spacing: 2
                Row { spacing: 6
                    Rectangle { width: 7; height: 7; radius: 3.5; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { width: Math.min(implicitWidth, rp.labelW - 25); text: rw.r.label || ""; font: rp.ro.fBodyDM; color: rp.ro.ink; wrapMode: Text.WordWrap } }
                Text { visible: text !== ""; width: parent.width; text: rw.r.styleText || ""; font: rp.ro.fSmallM; color: rp.ro.ink2; wrapMode: Text.WordWrap }
                Text { width: parent.width; text: (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : ""); font: rp.ro.fMicro; color: rp.ro.ink3; wrapMode: Text.WordWrap }
            }
            // the plot: the zero line (address), the band per position as a wash in the reading's own colour, the
            // fault lines as hairlines, the session's box
            Rectangle { visible: rw.lo < 0 && rw.hi > 0; x: rp.plotX; width: rp.plotW; y: Math.round(rw.vy(0)); height: rp.ro.hw; color: Qt.alpha(rp.ro.ink3, 0.6) }
            Repeater {
                model: rw.cells
                delegate: Item {
                    required property var modelData
                    required property int index
                    readonly property var c: modelData
                    readonly property bool banded: c.wash === "band" && rp.fin(c.bandLo) && rp.fin(c.bandHi)
                    x: rp.cx(index) - rp.colW * 0.36; width: rp.colW * 0.72; height: rp.plotH
                    Rectangle { visible: parent.banded; width: parent.width
                                y: parent.banded ? rw.vy(Math.min(rw.hi, parent.c.bandHi)) : 0
                                height: parent.banded ? Math.max(1, rw.vy(Math.max(rw.lo, parent.c.bandLo)) - rw.vy(Math.min(rw.hi, parent.c.bandHi))) : 0
                                color: Qt.alpha(rw.hue, 0.16) }
                    Rectangle { visible: !!parent.c.faultHi && rp.fin(parent.c.faultHi.edge)
                                x: -rp.colW * 0.06; width: parent.width + rp.colW * 0.12; height: Math.max(0.75, rp.ro.hw)
                                y: visible ? Math.round(rw.vy(parent.c.faultHi.edge)) : 0; color: Qt.alpha(rp.ro.warn, 0.7) }
                    Rectangle { visible: !!parent.c.faultLo && rp.fin(parent.c.faultLo.edge)
                                x: -rp.colW * 0.06; width: parent.width + rp.colW * 0.12; height: Math.max(0.75, rp.ro.hw)
                                y: visible ? Math.round(rw.vy(parent.c.faultLo.edge)) : 0; color: Qt.alpha(rp.ro.warn, 0.7) }
                }
            }
            Repeater {
                model: rw.boxes
                delegate: PpChBoxCell {
                    required property var modelData
                    required property int index
                    vertical: true
                    x: rp.cx(index) - 10; width: 20; y: 4; height: rp.plotH - 8
                    lo: rw.lo; hi: rw.hi
                    roles: rp.roles; brush: null; interactive: false; showPicked: false
                    box: modelData ? modelData.box : null; values: modelData ? (modelData.values || []) : []; fired: []
                    color: rw.hue
                }
            }
            // the pole: which way is plus
            Text { x: rp.plotX + rp.plotW + 6; y: 2; width: rp.poleW - 6; text: rw.r.poleText || ""; font: rp.ro.fMicro; color: rp.ro.ink3
                   wrapMode: Text.WrapAtWordBoundaryOrAnywhere }
            // the numbers under each column: the median or "-"; then the side of the band it falls on (warn, a word)
            // or how wide the middle half is; then the ⚠ and its few words
            Row {
                id: nums
                x: rp.plotX; y: rp.plotH + 2
                Repeater {
                    model: rw.cells
                    delegate: Column {
                        id: nc
                        required property var modelData
                        required property int index
                        objectName: rp.prefix + "Cell"
                        readonly property var c: modelData
                        readonly property var bc: rw.boxes[index] || null
                        readonly property string tag: c.tag || ""
                        readonly property string text: bc ? (bc.medianText || "-") : (c.text || "-")
                        readonly property bool warned: !!c.warn
                        readonly property bool outside: c.outside === true
                        width: rp.colW
                        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: nc.text; font: rp.ro.fFigM
                               color: nc.outside ? rp.ro.warn : (nc.text !== "-" ? rp.ro.ink : rp.ro.ink3)
                               Rectangle { visible: nc.outside; anchors.horizontalCenter: parent.horizontalCenter; y: parent.height - 1
                                           width: parent.contentWidth; height: 1; color: rp.ro.warn } }
                        Text { visible: text !== ""; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                               text: nc.outside ? (nc.c.ringWord || "") : (nc.bc ? (nc.bc.wideText || "") : "")
                               font: rp.ro.fMicro; color: nc.outside ? rp.ro.warn : rp.ro.ink3 }
                        Text { visible: nc.warned; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                               text: (nc.c.warn || "") + (nc.c.warnShort ? " " + nc.c.warnShort : ""); font: rp.ro.fMicro; color: rp.ro.ink2 }
                        // the second reading: the back line under trunk bend at P3–P5, smaller, with its own ⚠ words
                        Item { visible: nc.c.alt === true; width: 1; height: 3 }
                        Text { objectName: rp.prefix + "AltValue"
                               readonly property string tag: nc.tag
                               readonly property string rowKey: rw.key
                               readonly property bool warned: !!nc.c.altWarn
                               visible: nc.c.alt === true; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                               text: nc.c.altText || ""; font: rp.ro.fSmall; color: nc.c.altAvailable ? rp.ro.ink2 : rp.ro.ink3 }
                        Text { visible: !!nc.c.altWarn; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                               text: (nc.c.altWarn || "") + (nc.c.altWarnShort ? " " + nc.c.altWarnShort : ""); font: rp.ro.fMicro; color: rp.ro.ink3 }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 4; x: 0; width: parent.width; height: rp.ro.hw; color: rp.ro.faint }
        }
    }
    Item {
        property bool atom: true
        property bool keepPrev: true
        visible: !!rp.caption
        width: rp.width; height: visible ? capText.height + 8 : 0
        Text { id: capText; y: 4; width: parent.width; text: rp.caption; font: rp.ro.fMicro; color: rp.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }
}
