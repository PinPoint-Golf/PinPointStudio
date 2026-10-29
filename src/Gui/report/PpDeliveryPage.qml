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

// CLUB DELIVERY — the Club delivery page of the session report (docs/design/club_delivery_chart_design.md §4.8, stage 4):
// the Charts Club delivery section's SESSION reading, printed. After the Posture and Wrist pages and before "Between now
// and next time"; absent when the map has no `delivery` (report_delivery.h: no swing of the session reads the swing
// plane). Blocks, in the Session view's order:
//   · the head: the kicker, "Club delivery", the session's sentence;
//   · "The plane at each position": one box per P-position, P1…P7, over the session's swings on one shared scale
//     (PpChBoxCell → PpBoxPlot, the ONE box plot, in the swing plane curve's OWN metric colour), the median and ± half
//     the middle half under each ("-" where nothing was read), then where it varies most. Under five swings with the
//     curve: the sentence saying so, no boxes;
//   · the usual set-up: address, shoulders, the hands' steepening;
//   · side by side: "Plane and your ball flight" — the day's fit as a sentence (and the attack's when its own fit
//     holds), the scatter (PpChScatter, the one scatter, in the Charts print roles: the day's swings with both readings,
//     this session's in ink, the day's others faint; the fitted line only when the fit holds; its axis from the payload's
//     ticks, as paper has no ticker) and its legend — and "Shot shape" (PpChShotShapeGrid, every swing; what it was read
//     from, and the commonest shape);
//   · "How far can I trust this?": the trust lines, open (paper has no fold), said once — in two columns across the full
//     width (the Order page's side-by-side idiom), so the whole reading sits on ONE page (Mark, 29 Oct).
// FLOWED as the Posture and Wrist pages: the section starts a fresh page (breakBefore), every block is one atom, a heading
// keeps with what follows, a continued page says "Club delivery, continued" (the pager). Every sentence and figure is the
// map's `delivery` (report_delivery.h); the register sets only the fixed headings. FOLIO LIGHT through `ro` (the
// report's palette and type) and `cr` (the Charts print roles in the same fixed aesthetic, for the boxes, the scatter and
// the grid): nothing reads a live theme token. No affordance: nothing here is a link or a fold.
// objectNames: reportDelivery (the head), reportDeliveryBand (+ reportDeliveryBandCol: tag, text), reportDeliveryBandWhy,
// reportDeliveryVaries, reportDeliveryUsual (+ reportDeliveryUsualItem: label, text), reportDeliveryFlight
// (+ reportDeliveryFlightText, reportDeliveryScatter), reportDeliveryShape (+ the grid's chShotShapeGrid), reportDeliveryTrust
// (+ reportDeliveryTrustLine).
Column {
    id: dp
    property QtObject ro: null
    property var dl: null
    readonly property var m: dl || ({})
    readonly property bool on: !!dl
    readonly property var band: m.band || ({})
    readonly property var positions: (band.positions || []).slice(0, 7)          // the map's P1…P7
    readonly property var usual: (m.usual || []).slice(0, 3)
    readonly property var fl: m.flight || ({})
    readonly property var sc: fl.scatter || null
    readonly property var sh: m.shape || ({})
    readonly property var trust: (m.trust || []).slice(0, 12)
    visible: on
    width: 600

    readonly property real labelW: 122                  // the band's label column (the Posture profile's)
    readonly property real gap: 28                      // between the ball flight and the shot shape
    readonly property real shapeW: 214                  // the grid's column: its row labels and three cells

    // ═══ the head ═══
    Column {
        objectName: "reportDelivery"
        property bool atom: true
        property bool keepNext: true
        property bool breakBefore: true
        property string section: "Club delivery"
        visible: dp.on
        width: dp.width
        Text { width: parent.width; text: dp.m.kicker || ""; font: dp.ro.fKicker; color: dp.ro.accent; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
        Text { width: parent.width; text: "Club delivery"; font: dp.ro.fH1; color: dp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.08 }
        Item { width: 1; height: 10 }
        Text { width: parent.width; text: dp.m.head || ""; font: dp.ro.fStand; color: dp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: "The plane at each position"; font: dp.ro.fH3; color: dp.ro.ink; wrapMode: Text.WordWrap }
        Item { width: 1; height: 3 }
        Text { objectName: "reportDeliveryBandWhy"; width: parent.width; visible: text !== ""; text: dp.band.sub || ""
               font: dp.ro.fSmall; color: dp.ro.ink3; wrapMode: Text.WordWrap }
        Item { width: 1; height: 8 }
    }

    // ═══ the band: a box per position on one scale, the median and ± half the middle half under each ═══
    Item {
        id: bandBlock
        objectName: "reportDeliveryBand"
        property bool atom: true
        visible: dp.on && dp.band.shown === true && dp.positions.length > 0
        readonly property real colW: (dp.width - dp.labelW) / Math.max(1, dp.positions.length)
        readonly property real boxH: 70
        readonly property color hue: dp.ro.hue("swingPlaneCurve")
        width: dp.width
        height: visible ? cols.height + (variesText.visible ? variesText.height + 8 : 0) + 10 : 0

        // the label: the reading in its own colour, its camera and unit
        Column {
            y: 30 + bandBlock.boxH / 2 - 14; width: dp.labelW - 12; spacing: 2
            Row { spacing: 6
                Rectangle { width: 7; height: 7; radius: 3.5; color: bandBlock.hue; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Swing plane"; font: dp.ro.fBodyDM; color: dp.ro.ink } }
            Text { width: parent.width; text: "down-the-line camera, degrees"; font: dp.ro.fMicro; color: dp.ro.ink3; wrapMode: Text.WordWrap }
        }
        Row {
            id: cols
            x: dp.labelW
            Repeater {
                model: bandBlock.visible ? dp.positions : []
                delegate: Column {
                    id: bcol
                    required property var modelData
                    objectName: "reportDeliveryBandCol"
                    readonly property string tag: modelData.tag || ""
                    readonly property string text: modelData.medianText || "-"
                    width: bandBlock.colW
                    spacing: 2
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.tag; font: dp.ro.fSmallM; color: dp.ro.ink }
                    // the position's name ("address", "the top", "impact"); a blank line where it is only its tag, so every
                    // column's box and figures sit on the same lines
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideNone
                           text: (bcol.modelData.name || "") !== bcol.tag ? (bcol.modelData.name || "") : " "
                           font: dp.ro.fMicro; color: dp.ro.ink3 }
                    Item { width: 1; height: 2 }
                    PpChBoxCell {
                        vertical: true
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 22; height: bandBlock.boxH
                        lo: dp.band.lo; hi: dp.band.hi
                        roles: cr; brush: null; interactive: false; showPicked: false
                        box: bcol.modelData.box || null; values: bcol.modelData.values || []; fired: []
                        color: bandBlock.hue
                    }
                    Item { width: 1; height: 2 }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.text; font: dp.ro.fFigM
                           color: bcol.text !== "-" ? dp.ro.ink : dp.ro.ink3 }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.modelData.halfText || "-"; font: dp.ro.fMicro; color: dp.ro.ink3 }
                }
            }
        }
        Text {
            id: variesText
            objectName: "reportDeliveryVaries"
            visible: text !== ""
            y: cols.height + 8; width: dp.width; wrapMode: Text.WordWrap
            text: dp.band.varies || ""; font: dp.ro.fBody; color: dp.ro.ink; lineHeight: 1.25
        }
    }

    // ═══ the usual set-up: the address line, the shoulder plane, the hands' steepening ═══
    Item {
        objectName: "reportDeliveryUsual"
        property bool atom: true
        visible: dp.on && dp.usual.length > 0
        width: dp.width; height: visible ? usualFlow.height + 12 : 0
        Text { id: usualLab; y: 8; text: "Your usual set-up"; font: dp.ro.fLabel; color: dp.ro.ink3 }
        Flow {
            id: usualFlow
            x: usualLab.width + 14; y: 6; width: dp.width - x; spacing: 18
            Repeater {
                model: dp.usual
                delegate: Row {
                    required property var modelData
                    objectName: "reportDeliveryUsualItem"
                    readonly property string label: modelData.label || ""
                    readonly property string text: modelData.text || "-"
                    spacing: 6
                    Text { text: parent.label; font: dp.ro.fSmall; color: dp.ro.ink3; anchors.baseline: fig.baseline }
                    Text { id: fig; text: parent.text; font: dp.ro.fFigB; color: parent.text !== "-" ? dp.ro.ink : dp.ro.ink3 }
                }
            }
        }
    }

    // ═══ plane and your ball flight | shot shape, side by side ═══
    Item {
        id: pair
        property bool atom: true
        visible: dp.on
        width: dp.width
        readonly property real flW: dp.width - dp.gap - dp.shapeW
        height: visible ? Math.max(flCol.height, shCol.height) + 4 : 0

        Column {
            id: flCol
            objectName: "reportDeliveryFlight"
            width: pair.flW
            Item { width: 1; height: 12 }
            Text { width: parent.width; text: "Plane and your ball flight"; font: dp.ro.fH3; color: dp.ro.ink; wrapMode: Text.WordWrap }
            Item { width: 1; height: 4 }
            Text { objectName: "reportDeliveryFlightText"; width: parent.width; visible: text !== ""; text: dp.fl.text || ""
                   font: dp.ro.fBody; color: dp.ro.ink; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            Text { width: parent.width; visible: text !== ""; topPadding: 3; text: dp.fl.attackText || ""
                   font: dp.ro.fBody; color: dp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25 }
            Item { width: 1; height: 6; visible: !!dp.sc }
            PpChScatter {
                objectName: "reportDeliveryScatter"
                visible: !!dp.sc
                width: Math.min(parent.width, 400)
                height: visible ? implicitHeight : 0
                roles: cr; brush: null; ticker: tk
                scatter: dp.sc
                xKey: ""; yKey: ""
                interactive: false
                hoverLabels: false
                showMarginals: false
                showCaption: false
                plotH: 104
            }
            Text { width: parent.width; visible: text !== ""; topPadding: 2; text: dp.fl.legend || ""
                   font: dp.ro.fMicro; color: dp.ro.ink3; wrapMode: Text.WordWrap; lineHeight: 1.2 }
        }
        Column {
            id: shCol
            objectName: "reportDeliveryShape"
            x: pair.flW + dp.gap
            width: dp.shapeW
            Item { width: 1; height: 12 }
            Text { width: parent.width; text: "Shot shape"; font: dp.ro.fH3; color: dp.ro.ink; wrapMode: Text.WordWrap }
            Item { width: 1; height: 3 }
            Text { width: parent.width; visible: text !== ""; text: dp.sh.sub || ""; font: dp.ro.fSmall; color: dp.ro.ink3; wrapMode: Text.WordWrap }
            Item { width: 1; height: 8; visible: dp.sh.shown === true }
            PpChShotShapeGrid {
                visible: dp.sh.shown === true
                roles: cr; interactive: false
                shot: null
                marks: dp.sh.marks || []
                line: ""
            }
            Text { width: parent.width; visible: text !== "" && dp.sh.shown === true; topPadding: 4; text: dp.sh.line || ""
                   font: dp.ro.fSmall; color: dp.ro.ink2; wrapMode: Text.WordWrap }
        }
    }

    // ═══ how far can I trust this? said once, open ═══
    Column {
        id: trustBlock
        objectName: "reportDeliveryTrust"
        property bool atom: true
        visible: dp.on && dp.trust.length > 0
        width: dp.width
        readonly property real colGap: 18                 // the Order page's side-by-side columns (PpOrderPage ColsRow)
        readonly property real colW: (width - colGap) / 2
        // the lines in their order, split where the two columns come out most nearly level (by length: the columns share
        // one width and one type, so a line's length stands for its height)
        readonly property int split: {
            const n = dp.trust.length
            if (n < 2) return n
            const len = dp.trust.map(function (t) { return String(t).length + 60 })   // + a line's own padding and ragged end
            const total = len.reduce(function (a, b) { return a + b }, 0)
            let best = 1, bestD = Infinity, acc = 0
            for (let i = 1; i < n; ++i) {
                acc += len[i - 1]
                const d = Math.abs(total - 2 * acc)
                if (d < bestD) { bestD = d; best = i }
            }
            return best
        }
        Item { width: 1; height: 8 }
        Text { width: parent.width; text: "How far can I trust this?"; font: dp.ro.fH3; color: dp.ro.ink }
        Item { width: 1; height: 5 }
        // two columns across the full width, each under a hairline, as the Order page sets its short lists side by side; in
        // the report's note style (fMicro at 1.25, the captions' and legends'), its smallest — never smaller
        Row {
            spacing: trustBlock.colGap
            Repeater {
                model: [dp.trust.slice(0, trustBlock.split), dp.trust.slice(trustBlock.split)]
                delegate: Column {
                    required property var modelData
                    width: trustBlock.colW; spacing: 4
                    Rectangle { width: parent.width; height: dp.ro.hw; color: dp.ro.hair }
                    Repeater {
                        model: modelData
                        delegate: Text {
                            required property var modelData
                            objectName: "reportDeliveryTrustLine"
                            width: trustBlock.colW
                            text: String(modelData); font: dp.ro.fMicro; color: dp.ro.ink2; wrapMode: Text.WordWrap; lineHeight: 1.25
                        }
                    }
                }
            }
        }
        Text { width: parent.width; visible: text !== ""; topPadding: 3; text: dp.m.trustMore || ""; font: dp.ro.fMicro; color: dp.ro.ink3; wrapMode: Text.WordWrap }
    }

    // the scatter's axes: the payload's ticks (PpChScatter asks a ticker; paper has no presenter to ask). The x axis is
    // asked with the x range's ends, the y axis with the y range's.
    QtObject {
        id: tk
        function ticks(lo, hi, n, unit) {
            const s = dp.sc
            if (!s || !s.xRange || !s.yRange) return []
            return Math.abs(lo - s.xRange.lo) <= Math.abs(lo - s.yRange.lo) ? (s.xTicks || []) : (s.yTicks || [])
        }
    }
    // the Charts print roles in Folio light: the boxes', the scatter's and the grid's design values and inks. LAST in the
    // Column: children are destroyed first-declared first, so everything above goes before its roles (see PpPosturePage).
    PpChRoles { id: cr; medium: "print"; fixedAesthetic: "folio" }
}
