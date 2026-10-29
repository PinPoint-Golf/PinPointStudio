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

// PPCHDELIVERYFLIGHT — "Plane and your ball flight" (docs/design/club_delivery_chart_design.md §4.4), the Club delivery
// section's reason to exist, on This shot and Session alike: the fit's sentence (shown, refused or too few — a refusal
// is a finding and reads as one), the attack's when its own fit holds, this shot's place ("This one: 65° at P6, your
// steepest third → path −4.3°"; on Session none), and the scatter (PpChScatter): the golfer's shots of the day, this
// session's in ink and the day's other sessions faint, this shot ringed — or, with no monitor on it, a dashed hairline
// at its plane — and the fitted line only where the fit holds. Hover a dot: its shot and values ON the plot; a click
// opens a shot of this session. Every word is ChartsDeliveryPresenter's FLIGHT; QML maps values to pixels and spells
// the hovered values (the plane in whole degrees, the monitor's to one decimal: session_delivery.h decision 9).
// objectName: chDeliveryFlight (the scatter: chScatter, chDeliveryScatter on its holder).
Column {
    id: fb
    objectName: "chDeliveryFlight"
    property QtObject roles: null
    property var brush: null
    property QtObject ticker: null               // ChartsRelationsPresenter: ticks()
    property var flight: null                    // FLIGHT
    property bool interactive: true
    property bool showScatter: true
    signal shotActivated(int ordinal)
    readonly property bool ok: !!roles && !!flight
    spacing: ok ? roles.u(6) : 6

    function deg(v, dp, signed) {
        if (v === null || v === undefined || !isFinite(v)) return "-"
        const r = Number(Math.abs(v).toFixed(dp)), s = v < 0 && r !== 0 ? "−" : signed && r !== 0 ? "+" : ""
        return s + r.toFixed(dp) + "°"
    }
    // the session a dot of another session came from: its folder's trailing number ("…_Wrist_02" → "session 2")
    function sessionWord(name) { const m = String(name || "").match(/_(\d+)$/); return m ? "session " + Number(m[1]) : "another session" }
    readonly property var sc: ok ? (flight.scatter || null) : null
    readonly property bool xSigned: !!sc && String(sc.xLabel || "").toLowerCase().indexOf("steepening") >= 0
    readonly property var adapted: {
        if (!sc || sc.xLo === null || sc.xLo === undefined || sc.yLo === null || sc.yLo === undefined) return null
        const pts = (sc.points || []).filter(function (p) { return p.x !== null && p.y !== null && p.x !== undefined && p.y !== undefined })
            .map(function (p) {
                const who = p.thisSession ? "Shot " + p.ordinal : fb.sessionWord(p.session) + ", shot " + p.ordinal
                return { ordinal: p.thisSession ? p.ordinal : -1, x: p.x, y: p.y, faint: p.thisSession !== true, ring: p.picked === true,
                         label: who + "   " + fb.deg(p.x, 0, fb.xSigned) + " → " + fb.deg(p.y, 1, true) }
            })
        let xlo = sc.xLo, xhi = sc.xHi, ylo = sc.yLo, yhi = sc.yHi
        if (sc.picked && sc.picked.x !== null && sc.picked.x !== undefined) { xlo = Math.min(xlo, sc.picked.x); xhi = Math.max(xhi, sc.picked.x) }
        const px = Math.max(0.5, (xhi - xlo) * 0.06), py = Math.max(0.5, (yhi - ylo) * 0.08)
        return { xRange: { lo: xlo - px, hi: xhi + px }, yRange: { lo: ylo - py, hi: yhi + py }, points: pts,
                 line: sc.holds === true ? sc.line : null, clear: sc.holds === true, caption: "",
                 xLabel: sc.xLabel || "", yLabel: sc.yLabel || "", xUnit: sc.unit || "°", yUnit: sc.unit || "°" }
    }
    readonly property bool xOnly: !!sc && !!sc.picked && (sc.picked.y === null || sc.picked.y === undefined)
                                  && sc.picked.x !== null && sc.picked.x !== undefined

    Flow {
        width: parent.width; spacing: fb.ok ? fb.roles.u(10) : 10
        Text { text: "Ball flight"; font: fb.ok ? fb.roles.fKicker : Qt.font({}); color: fb.ok ? fb.roles.ink3 : "grey" }
        Text { visible: text !== ""; text: fb.ok && fb.flight.pool ? (fb.flight.pool.words || "") : ""
               font: fb.ok ? fb.roles.fSmall : Qt.font({}); color: fb.ok ? fb.roles.ink3 : "grey" }
    }
    Text { visible: text !== ""; width: fb.ok ? fb.roles.measureW(fb.width) : 0; wrapMode: Text.WordWrap
           text: fb.ok ? (fb.flight.text || "") : ""; font: fb.ok ? fb.roles.fBody : Qt.font({}); color: fb.ok ? fb.roles.ink : "black" }
    Text { visible: text !== ""; width: fb.ok ? fb.roles.measureW(fb.width) : 0; wrapMode: Text.WordWrap
           text: fb.ok ? (fb.flight.attackText || "") : ""; font: fb.ok ? fb.roles.fBody : Qt.font({}); color: fb.ok ? fb.roles.ink2 : "grey" }
    Text { visible: text !== ""; width: fb.ok ? fb.roles.measureW(fb.width) : 0; wrapMode: Text.WordWrap
           text: fb.ok ? (fb.flight.thisLine || "") : ""; font: fb.ok ? fb.roles.fBodyM : Qt.font({}); color: fb.ok ? fb.roles.ink : "black" }
    Item {
        objectName: "chDeliveryScatter"
        visible: fb.showScatter && !!fb.adapted && fb.adapted.points.length + (fb.xOnly ? 1 : 0) > 0
        width: Math.min(fb.width, fb.ok ? fb.roles.u(420) : 420); height: visible ? scat.height : 0
        PpChScatter {
            id: scat
            width: parent.width
            roles: fb.roles; brush: fb.brush; ticker: fb.ticker
            scatter: fb.adapted
            xKey: ""; yKey: ""
            interactive: fb.interactive
            hoverLabels: true
            showMarginals: false
            showCaption: false
            plotH: fb.ok ? fb.roles.u(150) : 150
            markX: fb.xOnly ? fb.sc.picked.x : NaN
            markXText: fb.xOnly ? "this shot " + fb.deg(fb.sc.picked.x, 0, fb.xSigned) : ""
            onShotActivated: (o) => fb.shotActivated(o)
        }
    }
}
