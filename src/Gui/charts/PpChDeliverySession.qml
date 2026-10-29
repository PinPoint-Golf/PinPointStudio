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

// PPCHDELIVERYSESSION — THE CLUB DELIVERY SECTION OF SESSION (docs/design/club_delivery_chart_design.md §4.7, 4.9; built
// the way PpChPostureSession is; PpChartsSessionView draws it under its own fold head "Club delivery", after the
// standard session content, in Analyse only). "Does my plane repeat, and does my ball flight follow it?"
//   · the head (the fit's finding, or the plane's when there is no monitor);
//   · the plane, position by position: one box per P-position over the session's shots on one shared scale (PpChBoxCell
//     → PpBoxPlot, the ONE box plot; the hover lays the values on the plot), this shot ringed, the median and how wide
//     the middle half is under each — drawn from 5 shots with the curve (band.shown); then where it varies most;
//   · the usual set-up: the median address line, shoulder plane and the hands' steepening (the plot's lines are these);
//   · side by side when the width allows: "Ball flight" (PpChDeliveryFlight, the golfer's day, this shot ringed) | "Shot
//     shape" (PpChShotShapeGrid, every shot; a dot opens its swing);
//   · "How far can I trust this?" under them, closed (said once).
// Every word and number is ChartsDeliveryPresenter.session's (body.dSession).
// objectNames: chDeliverySession, chDeliverySessionHead, chDeliveryBand (chDeliveryBandCol + tag), chDeliveryVaries,
// chDeliveryUsual, chDeliveryFlight, chDeliveryShape, chDeliveryTrust.
Column {
    id: ss
    objectName: "chDeliverySession"
    property QtObject roles: null
    property var brush: null
    property var body: null
    property bool interactive: true
    readonly property var se: body ? body.dSession : ({})
    readonly property bool ok: !!roles
    readonly property bool available: ok && !!se && se.available === true
    readonly property var band: available ? (se.band || ({})) : ({})
    readonly property var positions: band.positions || []
    readonly property var usual: available ? (se.usual || ({})) : ({})
    // the boxes' one scale: every position's whiskers and this shot's values
    readonly property var scale: {
        let lo = Infinity, hi = -Infinity
        for (const p of positions) {
            const b = p.box || {}
            for (const v of [b.min, b.max, p.picked]) if (v !== null && v !== undefined && isFinite(v)) { lo = Math.min(lo, v); hi = Math.max(hi, v) }
        }
        if (!isFinite(lo)) return { lo: 0, hi: 1 }
        const pad = Math.max(0.5, (hi - lo) * 0.06)
        return { lo: lo - pad, hi: hi + pad }
    }
    spacing: ok ? roles.u(18) : 18

    Column {
        objectName: "chDeliverySessionHead"
        width: ss.width; spacing: ss.ok ? ss.roles.u(6) : 6
        PpChText { roles: ss.roles; sentence: ss.se ? ss.se.head : null
                   font: ss.ok ? ss.roles.fStand : Qt.font({}); tone: ss.ok ? ss.roles.ink : "black" }
    }

    // ── the plane, position by position: a box per P-position, this shot ringed ──
    Column {
        objectName: "chDeliveryBand"
        visible: ss.available && ss.band.shown === true && ss.positions.length > 0
        width: ss.width; spacing: ss.ok ? ss.roles.u(8) : 8
        readonly property real colW: ss.ok ? Math.max(ss.roles.u(52), Math.min(ss.roles.u(80), (width - ss.roles.u(40)) / Math.max(1, ss.positions.length))) : 60
        PpSectionHead { width: parent.width; roles: ss.roles; title: "The plane at each position"
                        sub: ss.band.n ? ss.band.n + " shots with the swing plane curve" : "" }
        Row {
            Repeater {
                model: ss.available ? ss.positions : []
                delegate: Column {
                    id: bcol
                    required property var modelData
                    objectName: "chDeliveryBandCol"
                    readonly property string tag: modelData.tag
                    width: parent.parent.colW
                    spacing: ss.roles.u(2)
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.modelData.tag; font: ss.roles.fFigLabel; color: ss.roles.ink3 }
                    PpChBoxCell {
                        vertical: true
                        width: parent.width; height: ss.roles.u(110)
                        roles: ss.roles; brush: ss.brush; interactive: ss.interactive
                        lo: ss.scale.lo; hi: ss.scale.hi
                        box: bcol.modelData.box || null
                        values: bcol.modelData.values || []
                        color: ss.roles.hue("swingPlaneCurve")
                        onShotActivated: (o) => { if (ss.body) ss.body.pickOrdinal(o) }
                    }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.modelData.medianText || "-"; font: ss.roles.fFigS; color: ss.roles.ink }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: bcol.modelData.halfText || "-"; font: ss.roles.fFigLabel; color: ss.roles.ink3 }
                }
            }
        }
        Text { objectName: "chDeliveryVaries"; visible: text !== ""; width: ss.ok ? ss.roles.measureW(parent.width) : 0; wrapMode: Text.WordWrap
               text: ss.available && ss.se.varies ? (ss.se.varies.words || "") : ""
               font: ss.ok ? ss.roles.fBody : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
    }

    // ── the usual set-up: the lines on the plot, and the hands' steepening ──
    Flow {
        objectName: "chDeliveryUsual"
        visible: ss.available
        width: ss.width; spacing: ss.ok ? ss.roles.u(10) : 10
        Text { text: "Usually"; font: ss.ok ? ss.roles.fKicker : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
        Text { text: "address"; font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
        Text { text: ss.usual.addressText || "-"; font: ss.ok ? ss.roles.fFigS : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
        Text { text: "shoulders"; font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
        Text { text: ss.usual.shouldersText || "-"; font: ss.ok ? ss.roles.fFigS : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
        Text { text: "hands steeper coming down"; font: ss.ok ? ss.roles.fSmall : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
        Text { text: ss.usual.steepeningText || "-"; font: ss.ok ? ss.roles.fFigS : Qt.font({}); color: ss.ok ? ss.roles.ink : "black" }
    }

    // ── ball flight | shot shape ──
    Item {
        id: cols
        visible: ss.available
        width: ss.width
        readonly property real gap: ss.ok ? ss.roles.u(28) : 28
        readonly property bool two: ss.ok && ss.roles.columns(cols.width, 30, gap) >= 2
        readonly property real colW: two ? (cols.width - gap) / 2 : cols.width
        height: !visible ? 0 : two ? Math.max(fl.height, shp.height) : fl.height + ss.roles.u(18) + shp.height
        PpChDeliveryFlight {
            id: fl
            width: cols.colW
            roles: ss.roles; brush: ss.brush; interactive: ss.interactive
            ticker: ss.body ? ss.body.relationsP : null
            flight: ss.available ? (ss.se.flight || null) : null
            onShotActivated: (o) => { if (ss.body) ss.body.pickOrdinal(o) }
        }
        Column {
            id: shp
            objectName: "chDeliveryShape"
            x: cols.two ? cols.colW + cols.gap : 0; y: cols.two ? 0 : fl.height + ss.roles.u(18)
            width: cols.colW
            spacing: ss.ok ? ss.roles.u(6) : 6
            Text { text: "Shot shape"; font: ss.ok ? ss.roles.fKicker : Qt.font({}); color: ss.ok ? ss.roles.ink3 : "grey" }
            PpChShotShapeGrid {
                roles: ss.roles; interactive: ss.interactive
                // this shot ringed among the session's
                shot: { const m = (ss.se.shapes && ss.se.shapes.marks || []).filter(function (x) { return x.picked === true })[0]; return m || null }
                marks: ss.available && ss.se.shapes ? (ss.se.shapes.marks || []) : []
                line: ss.available && ss.se.shapes ? (ss.se.shapes.line || "-") : "-"
                onShotActivated: (o) => { if (ss.body) ss.body.pickOrdinal(o) }
            }
        }
    }

    PpChTrustFold {
        prefix: "chDelivery"
        visible: ss.available && (lines || []).length > 0
        width: ss.width; roles: ss.roles
        lines: ss.available && ss.se.trust ? (ss.se.trust.lines || []) : []
    }
}
