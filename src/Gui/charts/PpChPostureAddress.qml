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
// objectNames: chPostureAddress; each row chPostureAddressRow (+ measureId, text, warned, hasHistory, historyN, banded);
// chPostureAddressBand, chPostureAddressDot, chPostureAddressSessionBox, chPostureAddressHistory (the history box),
// chPostureAddressNoHistory (its words), chPostureAddressFigures, chPostureAddressWarn.
Column {
    id: ad
    objectName: "chPostureAddress"
    property QtObject roles: null
    property var brush: null
    property var block: null
    property string mode: "shot"                 // "shot" | "session"
    property bool interactive: true
    readonly property bool ok: !!roles
    readonly property var rows: block && block.rows ? block.rows : []
    readonly property bool shot: mode === "shot"
    readonly property bool sideLabel: ok && roles.atLeast(width, "medium")
    readonly property real labelW: sideLabel ? roles.u(176) : 0
    readonly property real laneW: ok ? roles.u(96) : 96
    readonly property real plotX: labelW
    readonly property real plotW: Math.max(ok ? roles.u(140) : 140, width - plotX - laneW)
    readonly property real laneH: ok ? roles.u(18) : 18
    function fin(v) { return v !== null && v !== undefined && isFinite(v) }
    visible: ok && rows.length > 0
    spacing: ok ? roles.u(8) : 8

    PpSectionHead { width: ad.width; roles: ad.roles; title: ad.block ? (ad.block.title || "At address") : "At address"; sub: ad.block ? (ad.block.sub || "") : "" }

    Repeater {
        model: ad.ok ? ad.rows : []
        delegate: Item {
            id: rw
            required property var modelData
            required property int index
            objectName: "chPostureAddressRow"
            readonly property var r: modelData
            readonly property string measureId: r.measureId || ""
            readonly property string text: r.text || "-"
            readonly property bool warned: r.warned === true
            readonly property bool hasHistory: r.hasHistory === true
            readonly property int historyN: r.historyN || 0
            readonly property bool banded: r.banded === true
            readonly property var ty: ad.roles
            readonly property color hue: ty.hue(r.hueKey || r.metricKey || "")
            readonly property real lo: ad.fin(r.xLo) ? r.xLo : 0
            readonly property real hi: ad.fin(r.xHi) ? r.xHi : 1
            function vx(v) { return ad.plotX + ad.plotW * (v - lo) / Math.max(1e-9, hi - lo) }
            readonly property real topY: ad.sideLabel ? 0 : labAbove.height + ty.u(4)
            readonly property real plotH: 2 * ad.laneH + ty.u(4)
            width: ad.width
            height: Math.max(ad.sideLabel ? labCol.height : 0, topY + plotH + ty.u(4) + figs.height + (warnT.visible ? warnT.height + ty.u(2) : 0) + ty.u(6))

            // ── the label: beside at medium and up, above below that ──
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
                width: ad.labelW - rw.ty.u(12)
                spacing: rw.ty.u(2)
                Row { spacing: rw.ty.u(7)
                    Rectangle { width: rw.ty.u(7); height: width; radius: rw.ty.square ? 0 : width / 2; color: rw.hue; anchors.verticalCenter: parent.verticalCenter }
                    Text { width: labCol.width - rw.ty.u(14); wrapMode: Text.WordWrap; text: rw.r.label || ""; font: rw.ty.fBodyM; color: rw.ty.ink } }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: (rw.r.camera || "") + (rw.r.unit ? ", " + rw.r.unit : "")
                       font: rw.ty.fSmall; color: rw.ty.ink3 }
            }

            // ── the plot: the band across both lanes, this session's box (and this shot's dot), the earlier sessions' box ──
            Rectangle {
                objectName: "chPostureAddressBand"
                visible: rw.banded && ad.fin(rw.r.bandLo) && ad.fin(rw.r.bandHi)
                x: visible ? Math.max(ad.plotX, rw.vx(rw.r.bandLo)) : 0
                width: visible ? Math.max(1, Math.min(ad.plotX + ad.plotW, rw.vx(rw.r.bandHi)) - x) : 0
                y: rw.topY; height: rw.plotH
                color: rw.ty.hueA(rw.r.hueKey || rw.r.metricKey || "", Math.max(0.10, rw.ty.envAlpha * 1.4))
            }
            Rectangle { x: ad.plotX; width: ad.plotW; y: rw.topY + rw.plotH; height: rw.ty.hw; color: rw.ty.hair }
            PpChBoxCell {
                objectName: "chPostureAddressSessionBox"
                x: ad.plotX; width: ad.plotW; y: rw.topY; height: ad.laneH
                lo: rw.lo; hi: rw.hi
                roles: rw.ty; brush: ad.brush; interactive: ad.interactive
                box: rw.r.box || null; values: rw.r.values || []; fired: []
                showPicked: !ad.shot
                color: rw.hue
            }
            Item {
                objectName: "chPostureAddressDot"
                visible: ad.shot && ad.fin(rw.r.dot)
                x: visible ? rw.vx(rw.r.dot) : 0; y: rw.topY + ad.laneH / 2
                Rectangle { width: rw.ty.u(10); height: width; radius: rw.ty.square ? 0 : width / 2; x: -width / 2; y: -height / 2
                            color: rw.hue; border.width: 1.5; border.color: rw.ty.ink }
            }
            PpChBoxCell {
                objectName: "chPostureAddressHistory"
                visible: rw.hasHistory
                x: ad.plotX; width: ad.plotW; y: rw.topY + ad.laneH + rw.ty.u(4); height: ad.laneH
                lo: rw.lo; hi: rw.hi
                roles: rw.ty; brush: null; interactive: ad.interactive
                box: rw.hasHistory ? rw.r.history : null; values: []; fired: []
                showPicked: false; muted: true
            }
            Text {
                objectName: "chPostureAddressNoHistory"
                visible: !rw.hasHistory && text !== ""
                x: ad.plotX + rw.ty.u(4); width: ad.plotW - rw.ty.u(8); elide: Text.ElideRight
                y: rw.topY + ad.laneH + rw.ty.u(4) + (ad.laneH - height) / 2
                text: rw.r.historyWhy || ""; font: rw.ty.fSmall; color: rw.ty.ink3
            }
            // the lanes' names at the right
            Text { x: ad.plotX + ad.plotW + rw.ty.u(8); width: ad.laneW - rw.ty.u(8); y: rw.topY + (ad.laneH - height) / 2; elide: Text.ElideRight
                   text: "this session"; font: rw.ty.fSmall; color: rw.ty.ink3 }
            Text { x: ad.plotX + ad.plotW + rw.ty.u(8); width: ad.laneW - rw.ty.u(8); y: rw.topY + ad.laneH + rw.ty.u(4) + (ad.laneH - height) / 2
                   elide: Text.ElideRight; text: "earlier sessions"; font: rw.ty.fSmall; color: rw.ty.ink3 }

            // ── the figures, then the ⚠ and its few words ──
            Text {
                id: figs
                objectName: "chPostureAddressFigures"
                x: ad.plotX; y: rw.topY + rw.plotH + rw.ty.u(4); width: rw.width - ad.plotX
                wrapMode: Text.WordWrap; textFormat: Text.StyledText
                // the number first and strong, then the band and both boxes' medians, one line where it fits
                text: "<b>" + rw.text + "</b>" + (rw.r.valueWord ? " " + rw.r.valueWord : "")
                      + [rw.r.bandText || "", rw.r.boxText || "", rw.r.historyText || ""].filter(function (x) { return x !== "" })
                            .map(function (x) { return "  ·  " + x }).join("")
                font: rw.ty.fSmall; color: rw.ty.ink2
            }
            Text {
                id: warnT
                objectName: "chPostureAddressWarn"
                visible: rw.warned
                x: ad.plotX; y: figs.y + figs.height + rw.ty.u(2); width: rw.width - ad.plotX; wrapMode: Text.WordWrap
                text: rw.warned ? (rw.r.warn || "⚠") + " " + (rw.r.warnShort || "") : ""
                font: rw.ty.fSmall; color: rw.ty.ink2
            }
        }
    }
    Text { visible: ad.ok && text !== ""; width: ad.ok ? ad.roles.measureW(ad.width) : 0; wrapMode: Text.WordWrap
           text: ad.block ? (ad.block.legend || "") : ""; font: ad.ok ? ad.roles.fSmall : Qt.font({}); color: ad.ok ? ad.roles.ink3 : "grey" }
}
