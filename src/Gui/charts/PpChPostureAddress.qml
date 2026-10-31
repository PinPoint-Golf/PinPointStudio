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

// PPCHPOSTUREADDRESS — "AT ADDRESS" (docs/design/posture_chart_design.md §7.4.2): the rig-sensitive address readings
// (forward bend, trail-knee bend, arm hang, head pitch, heel-to-toe balance, side lean — the pack's `rigSensitive`
// measures), under the profile and above What moved, on This shot and on Session. Mark, 22 Oct: every reading we have
// is SHOWN, with a ⚠ and short honest words where it may be unreliable; the full reason is said once, in the trust fold.
// One row per reading the capture can produce:
//   · the golfer's name for it, its camera and unit;
//   · on the row's own scale: the good-player band as a wash in the reading's metric colour (identity, not judgement);
//     the upper lane this session's box (PpChBoxCell → PpBoxPlot, the ONE box plot) with — This shot — this swing's dot;
//     the lower lane the golfer's EARLIER sessions from the same set-up as a box, or the words why there is none;
//   · the figures under it: the number ("-" when this shot has none), the band, both boxes' medians and n;
//   · the ⚠ and its few words, "depends on where the camera stands", under every row.
// Every word, number and scale is ChartsPosturePresenter's ADDRESSBLOCK (address: { title, sub, rows, legend }); QML
// maps values to pixels only.
// ON PAPER (the session report's Posture page, PpPosturePage): set `ro` (the report's palette and type, Folio light) and
// roles to the page's Charts print roles. The block is then drawn in the report's own type and inks at the report's
// geometry (the label column of PpReportProfile, lanes of 13 px), every row's hue read through `ro` (PpChRoles.hue reads
// the live theme), and its pieces are PAGER ATOMS: the heading (kept with the first reading), one atom per reading, the
// legend (kept with the last). `cap` caps the rows (the report's 8); `prefix` names them (reportPostureAddress…).
// objectNames, from `prefix` (default chPostureAddress): the block; each row <prefix>Row (+ measureId, text, warned,
// hasHistory, historyN, banded); <prefix>Band, <prefix>Dot, <prefix>SessionBox, <prefix>History (the history box),
// <prefix>NoHistory (its words), <prefix>Figures, <prefix>Warn.
Column {
    id: ad
    property string prefix: "chPostureAddress"
    objectName: prefix
    property QtObject roles: null
    property QtObject ro: null                   // paper: the report's palette and type (see the header)
    property var brush: null
    property var block: null
    property string mode: "shot"                 // "shot" | "session"
    property bool interactive: true
    property int cap: 0                          // the rows drawn at most; 0 = every row
    readonly property bool ok: !!roles
    readonly property bool isPrint: !!ro
    readonly property var rows: block && block.rows ? (cap > 0 ? block.rows.slice(0, cap) : block.rows) : []
    readonly property bool shot: mode === "shot"
    readonly property bool sideLabel: isPrint || (ok && roles.atLeast(width, "medium"))
    // paper: the report's label column (PpReportProfile's), lanes and gaps in the report's px
    readonly property real labelW: isPrint ? 122 : sideLabel ? roles.u(176) : 0
    readonly property real laneW: isPrint ? 92 : ok ? roles.u(96) : 96
    readonly property real plotX: labelW
    readonly property real plotW: isPrint ? width - labelW - laneW : Math.max(ok ? roles.u(140) : 140, width - plotX - laneW)
    readonly property real laneH: isPrint ? 13 : ok ? roles.u(18) : 18
    readonly property real laneGap: isPrint ? 3 : ok ? roles.u(4) : 4
    visible: ok && rows.length > 0
    spacing: isPrint ? 0 : ok ? roles.u(8) : 8

    PpSectionHead { visible: !ad.isPrint; width: ad.width; roles: ad.roles; title: ad.block ? (ad.block.title || "At address") : "At address"; sub: ad.block ? (ad.block.sub || "") : "" }
    // paper: the heading in the report's type, one atom kept with the first reading
    Column {
        visible: ad.isPrint
        property bool atom: true
        property bool keepNext: true
        width: ad.width
        Item { width: 1; height: 18 }
        Text { width: parent.width; text: ad.block ? (ad.block.title || "At address") : "At address"; font: ad.isPrint ? ad.ro.fH3 : Qt.font({}); color: ad.isPrint ? ad.ro.ink : "transparent" }
        Item { width: 1; height: 3 }
        Text { width: parent.width; visible: text !== ""; text: ad.block ? (ad.block.sub || "") : ""; font: ad.isPrint ? ad.ro.fSmall : Qt.font({}); color: ad.isPrint ? ad.ro.ink3 : "transparent"; wrapMode: Text.WordWrap }
        Item { width: 1; height: 6 }
    }

    Repeater {
        model: ad.ok ? ad.rows : []
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: ad.prefix + "Row"
            property bool atom: ad.isPrint
            readonly property var r: modelData
            readonly property string measureId: r.measureId || ""
            readonly property string text: r.text || "-"
            readonly property bool warned: r.warned === true
            readonly property bool hasHistory: r.hasHistory === true
            readonly property int historyN: r.historyN || 0
            readonly property bool banded: r.banded === true
            readonly property var ty: ad.roles
            readonly property bool pr: ad.isPrint
            readonly property string hueKey: r.hueKey || r.metricKey || ""
            readonly property color hue: pr ? ad.ro.hue(hueKey) : ty.hue(hueKey)
            readonly property real lo: ad.roles.fin(r.xLo) ? r.xLo : 0
            readonly property real hi: ad.roles.fin(r.xHi) ? r.xHi : 1
            function vx(v) { return ad.plotX + ad.plotW * (v - lo) / Math.max(1e-9, hi - lo) }
            readonly property real topY: pr ? 4 : ad.sideLabel ? 0 : labAbove.height + ty.u(4)
            readonly property real plotH: 2 * ad.laneH + ad.laneGap
            // the type: the report's on paper, the roles' on screen
            readonly property font fName: pr ? ad.ro.fBodyDM : ty.fBodyM
            readonly property font fNote: pr ? ad.ro.fMicro : ty.fSmall
            readonly property font fFigs: pr ? ad.ro.fSmall : ty.fSmall
            readonly property color ink: pr ? ad.ro.ink : ty.ink
            readonly property color ink2: pr ? ad.ro.ink2 : ty.ink2
            readonly property color ink3: pr ? ad.ro.ink3 : ty.ink3
            readonly property real laneNameX: ad.plotX + ad.plotW + (pr ? 6 : ty.u(8))
            width: ad.width
            // `ad` reads null while a printed report tears the block down: no height then, no TypeError
            height: !ad ? 0 : pr ? Math.max(labCol.height + 6, topY + plotH + ad.laneGap + figs.height + (warnT.visible ? warnT.height + 1 : 0) + 8)
                       : Math.max(ad.sideLabel ? labCol.height : 0, topY + plotH + ty.u(4) + figs.height + (warnT.visible ? warnT.height + ty.u(2) : 0) + ty.u(6))

            // ── the label: beside at medium and up (and on paper), above below that ──
            Row {
                id: labAbove
                visible: !ad.sideLabel
                spacing: rw.ty.u(6)
                Rectangle { width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                Text { id: nameA; text: rw.r.label || ""; font: rw.ty.fBodyM; color: rw.ty.ink }
                Text { anchors.baseline: nameA.baseline; text: (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : ""); font: rw.ty.fSmall; color: rw.ty.ink3 }
            }
            Column {
                id: labCol
                visible: ad.sideLabel
                y: rw.pr ? 4 : 0
                width: ad.labelW - (rw.pr ? 12 : rw.ty.u(12))
                spacing: rw.pr ? 2 : rw.ty.u(2)
                Row { spacing: rw.pr ? 6 : rw.ty.u(7)
                    Rectangle { width: rw.pr ? 7 : rw.ty.u(7); height: width; radius: rw.pr ? 3.5 : rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { width: rw.pr ? ad.labelW - 25 : labCol.width - rw.ty.u(14); wrapMode: Text.WordWrap; text: rw.r.label || ""; font: rw.fName; color: rw.ink } }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : "")
                       font: rw.fNote; color: rw.ink3 }
            }

            // ── the plot: the band across both lanes, this session's box (and this shot's dot), the earlier sessions' box ──
            Rectangle {
                objectName: ad.prefix + "Band"
                visible: rw.banded && ad.roles.fin(rw.r.bandLo) && ad.roles.fin(rw.r.bandHi)
                x: visible ? Math.max(ad.plotX, rw.vx(rw.r.bandLo)) : 0
                width: visible ? Math.max(1, Math.min(ad.plotX + ad.plotW, rw.vx(rw.r.bandHi)) - x) : 0
                y: rw.topY; height: rw.plotH
                color: rw.pr ? Qt.alpha(rw.hue, 0.16) : rw.ty.hueA(rw.hueKey, Math.max(0.10, rw.ty.envAlpha * 1.4))
            }
            Rectangle { visible: !rw.pr; x: ad.plotX; width: ad.plotW; y: rw.topY + rw.plotH; height: rw.ty.hw; color: rw.ty.hair }
            PpChBoxCell {
                objectName: ad.prefix + "SessionBox"
                x: ad.plotX; width: ad.plotW; y: rw.topY; height: ad.laneH
                lo: rw.lo; hi: rw.hi
                roles: rw.ty; brush: ad.brush; interactive: ad.interactive
                box: rw.r.box || null; values: rw.r.values || []; fired: []
                showPicked: !ad.shot && !rw.pr
                color: rw.hue
            }
            Item {
                objectName: ad.prefix + "Dot"
                visible: ad.shot && ad.roles.fin(rw.r.dot)
                x: visible ? rw.vx(rw.r.dot) : 0; y: rw.topY + ad.laneH / 2
                Rectangle { width: rw.ty.u(10); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2
                            color: rw.hue; border.width: 1.5; border.color: rw.ty.ink }
            }
            PpChBoxCell {
                objectName: ad.prefix + "History"
                visible: rw.hasHistory
                x: ad.plotX; width: ad.plotW; y: rw.topY + ad.laneH + ad.laneGap; height: ad.laneH
                lo: rw.lo; hi: rw.hi
                roles: rw.ty; brush: null; interactive: ad.interactive
                box: rw.hasHistory ? rw.r.history : null; values: []; fired: []
                showPicked: false; muted: true
            }
            // why there is no history: centred in its lane on screen, wrapped from the lane's top on paper
            Text {
                objectName: ad.prefix + "NoHistory"
                visible: !rw.hasHistory && text !== ""
                x: ad.plotX + (rw.pr ? 4 : rw.ty.u(4)); width: ad.plotW - (rw.pr ? 8 : rw.ty.u(8))
                elide: rw.pr ? Text.ElideNone : Text.ElideRight; wrapMode: rw.pr ? Text.WordWrap : Text.NoWrap
                y: rw.topY + ad.laneH + ad.laneGap + (rw.pr ? 0 : (ad.laneH - height) / 2)
                text: rw.r.historyWhy || ""; font: rw.fNote; color: rw.ink3
            }
            // the lanes' names at the right
            Text { x: rw.laneNameX; width: ad.laneW - (rw.pr ? 6 : rw.ty.u(8)); y: rw.topY + (rw.pr ? 0 : (ad.laneH - height) / 2); elide: rw.pr ? Text.ElideNone : Text.ElideRight
                   text: "this session"; font: rw.fNote; color: rw.ink3 }
            Text { x: rw.laneNameX; width: ad.laneW - (rw.pr ? 6 : rw.ty.u(8)); y: rw.topY + ad.laneH + ad.laneGap + (rw.pr ? 0 : (ad.laneH - height) / 2)
                   elide: rw.pr ? Text.ElideNone : Text.ElideRight; text: "earlier sessions"; font: rw.fNote; color: rw.ink3 }

            // ── the figures, then the ⚠ and its few words ──
            Text {
                id: figs
                objectName: ad.prefix + "Figures"
                x: ad.plotX; y: rw.topY + rw.plotH + ad.laneGap; width: rw.width - ad.plotX
                wrapMode: Text.WordWrap; textFormat: rw.pr ? Text.AutoText : Text.StyledText
                // the number first and strong, then the band and both boxes' medians, one line where it fits (paper: the
                // report's plain line, the parts apart)
                text: rw.pr ? [rw.text + (rw.r.valueWord ? " " + rw.r.valueWord : ""), rw.r.bandText || "", rw.r.boxText || "", rw.r.historyText || ""]
                                  .filter(function (x) { return x !== "" }).join("   ·   ")
                            : "<b>" + rw.text + "</b>" + (rw.r.valueWord ? " " + rw.r.valueWord : "")
                              + [rw.r.bandText || "", rw.r.boxText || "", rw.r.historyText || ""].filter(function (x) { return x !== "" })
                                    .map(function (x) { return "  ·  " + x }).join("")
                font: rw.fFigs; color: rw.ink2; lineHeight: rw.pr ? 1.2 : 1
            }
            Text {
                id: warnT
                objectName: ad.prefix + "Warn"
                visible: rw.warned
                x: ad.plotX; y: figs.y + figs.height + (rw.pr ? 1 : rw.ty.u(2)); width: rw.width - ad.plotX; wrapMode: Text.WordWrap
                text: rw.warned ? (rw.r.warn || "⚠") + " " + (rw.r.warnShort || "") : ""
                font: rw.fNote; color: rw.ink2
            }
            // paper: a faint rule under each reading
            Rectangle { visible: rw.pr; anchors.bottom: parent.bottom; anchors.bottomMargin: 3; width: parent.width; height: rw.pr ? ad.ro.hw : 0; color: rw.pr ? ad.ro.faint : "transparent" }
        }
    }
    Text { visible: !ad.isPrint && ad.ok && text !== ""; width: ad.ok ? ad.roles.measureW(ad.width) : 0; wrapMode: Text.WordWrap
           text: ad.block ? (ad.block.legend || "") : ""; font: ad.ok ? ad.roles.fSmall : Qt.font({}); color: ad.ok ? ad.roles.ink3 : "grey" }
    // paper: the legend, one atom kept with the last reading
    Item {
        property bool atom: true
        property bool keepPrev: true
        visible: ad.isPrint && !!ad.block && !!ad.block.legend
        width: ad.width; height: visible ? alg.height + 8 : 0
        Text { id: alg; y: 4; width: parent.width; text: ad.block ? (ad.block.legend || "") : ""; font: ad.isPrint ? ad.ro.fMicro : Qt.font({}); color: ad.isPrint ? ad.ro.ink3 : "grey"
               wrapMode: Text.WordWrap; lineHeight: 1.25 }
    }
}
