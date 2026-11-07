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

// PPCHWRISTPROFILE — THE CHECKPOINT PROFILE (docs/design/wrist_motion_into_charts.md §4 B, W1 W2 W10–W13): one
// row per wrist DOF, the eight checkpoints as a discrete x axis, Δ from address on y. The band is the chart
// wash per checkpoint; this swing is drawn in its metric's hue (Theme.metricColor, identity); a value outside
// its band is a warn mark (filled = fault, ring = watch) AND a word under its number ("fault · 19° open"), never
// colour alone; the playhead's checkpoint is the picked column through every row; the Against ghost is dashed
// with hollow points; in Session scope a box per checkpoint (PpChBoxCell, this shot ringed) and, under it, the
// median and how wide the middle half is, and under the row its consistency per club (ROW.grades: a technical-layer
// Dispersion badge per graded checkpoint, against the dominant style's band — Mark's rulings R2b, R7); the curve
// behind on demand. The numbers under every column ARE the
// old grid; the grid-only DOF (lead elbow) is a numbers-only row.
// Every word, number and scale is ChartsWristPresenter's (profile.rows: ROW, positions: POS); QML maps to pixels.
// The form is PpChPositionProfile's (the one P1–P8 profile, shared with Posture); this file maps the presenter's ROW and
// POINT onto it (rowOf) and sets the wrist look: taller plots, the chart wash, a hollow or filled warn mark.
// objectNames: chWristProfile; each row chWristRow (+ dof); each column head chWristCol (+ tag).
PpChPositionProfile {
    id: wp
    objectName: "chWristProfile"
    prefix: "chWrist"
    labelU: 200; poleU: 62; plotU: 96; numU: 36; gapU: 14; bandW: 0.64
    metricWash: false; elide: false; poleWrap: false
    // ChartsWristPresenter's ROW → the profile's ROW (PpChPositionProfile's header)
    rowOf: function (r, m) {
        const session = m === "session", no = r.numbersOnly === true
        const out = {
            key: r.key || "", dof: r.dof || "", hueKey: r.key || "",
            name: r.name || "", dot: !no,
            aboveSub: (r.sub || "") + (r.sourceText ? "   " + r.sourceText : ""),
            sideY: no ? 2 : 6,
            sideLines: [{ text: r.sub || "", font: "fSmall", ink: "ink2", keep: true },
                        { text: r.sourceText || "", font: "fSmall", ink: "ink3" }],
            lo: r.yLo !== undefined && r.yLo !== null ? r.yLo : -10, hi: r.yHi !== undefined && r.yHi !== null ? r.yHi : 10,
            unit: "°", numbersOnly: no, poleTop: r.poleTopText || "", poleBottom: r.poleBottomText || "",
            trace: r.trace || [], boxes: r.boxes || [],
            grades: session ? (r.grades || []) : []
        }
        // the grid-only DOF: numbers only (a gated value says the gate's words)
        if (no) {
            out.cells = (r.cells || []).map(function (c) {
                const why = !!c.trust
                return { text: why ? (c.valueText || "") : (c.valueText || "-"), trust: c.trust || "", dim: !c.available }
            })
            return out
        }
        const gh = r.ghost || null
        out.cells = (r.points || []).map(function (p, i) {
            const bc = session && r.boxes ? r.boxes[i] : null
            const o = !session && p.out === true, why = !session && !!p.trust
            const word = session ? (bc ? bc.wideText || "" : "") : o ? p.word || "" : ""
            return {
                value: p.available ? p.value : null,
                mark: p.available === true ? (p.out === true ? (p.fault === true ? "fault" : "out") : "dot") : "",
                markName: p.out ? "chWristOut" : "chWristIn",
                banded: p.banded === true && p.bandLo !== null && p.bandHi !== null, bandLo: p.bandLo, bandHi: p.bandHi,
                ghost: gh ? gh[i] : null,
                text: session ? (bc ? bc.medianText || "-" : "-") : why ? (p.valueText || "") : (p.valueText || "-"),
                dim: !(p.available || session), outside: o, trust: why ? p.trust : "",
                word: word, wordShort: session ? word : o ? p.wordShort || "" : ""
            }
        })
        return out
    }
}
