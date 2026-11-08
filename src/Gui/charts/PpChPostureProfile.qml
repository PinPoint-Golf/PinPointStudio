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

// PPCHPOSTUREPROFILE — THE POSTURE PROFILE (docs/design/posture_chart_design.md §5.2, §5.5): one row per posture
// reading (head height, hip depth, trunk bend, lead knee, trail knee, pelvis height, side bend), P1…P8 as a discrete x axis,
// the change from address on y. It is the wrist profile's form (PpChPositionProfile, as PpChWristProfile), per cell:
//   · the chosen pattern's band as a wash in the row's metric colour (Theme.metricColor(hueKey): identity);
//   · the fault line (where the matching condition fires) as a hairline;
//   · This shot: this swing's dot in the hue, joined; outside the band a RING and its word under the number
//     ("in 7.6"), never colour alone; the Against ghost as hollow points on a dashed line;
//   · Session: the session's box per position (PpChBoxCell → PpBoxPlot, the ONE box plot), this shot ringed,
//     the median and how wide the middle half is under it, and under the row its consistency per club (ROW.grades: a
//     technical-layer Dispersion badge per graded position, against the pattern's band — Mark's rulings R2b, R7);
//   · the numbers under each column: the figure, or "-" where nothing was read; a reading under a ⚠ keeps its
//     figure and says the ⚠'s few words under it (the full reason is said once, in the trust fold);
//   · the pole word at the row's right ("in ▲"); the trail knee's row labelled with its style word;
//   · a SECOND READING under a cell where the presenter gives one (trunk bend at P3–P5: the back line, decision 16 of
//     session_posture.h): a smaller figure on its own line under the numbers, "back line −3", and its own ⚠ words —
//     not ringed, no band of its own; a row with one (ROW.hasAlt) is that line taller.
// A column head is a seek target (columnActivated); the playhead's position is the picked column through every row.
// Every word, number and scale is ChartsPosturePresenter's (profile.rows: ROW, positions: POS); QML maps to pixels.
// The form is PpChPositionProfile's (the one P1–P8 profile, shared with the wrist); this file maps the presenter's ROW
// and CELL onto it (rowOf) and sets the posture look: the band in the metric colour, a ring around a small dot.
// objectNames: chPostureProfile; each row chPostureRow (+ key); each column head chPostureCol (+ tag); each cell's
// numbers chPostureCell (+ tag, text, warned — the ⚠ drawn —, outside, rowKey); each second reading chPostureAltValue
// (+ tag, rowKey, text, warned) and its words chPostureAltWarn.
PpChPositionProfile {
    id: pp
    objectName: "chPostureProfile"
    prefix: "chPosture"
    // ChartsPosturePresenter's ROW → the profile's ROW (PpChPositionProfile's header)
    rowOf: function (r, m) {
        const ty = pp.roles, session = m === "session"
        const boxes = session && r.boxes ? r.boxes : []
        const camera = (r.camera || "") + (r.unit ? ", " + r.unit : "")
        return {
            key: r.key || "", hueKey: r.hueKey || r.metricKey || "",
            name: r.label || "", dot: true,
            aboveSub: [r.styleText || "", camera].filter(function (x) { return x !== "" }).join("   "),
            sideY: 4,
            sideLines: [{ text: r.styleText || "", font: "fSmallM", ink: "ink2", name: "chPostureStyle" },
                        { text: camera, font: "fSmall", ink: "ink3", keep: true }],
            lo: ty.fin(r.yLo) ? r.yLo : -1, hi: ty.fin(r.yHi) ? r.yHi : 1, unit: r.unit || "",
            hasAlt: r.hasAlt === true, poleTop: r.poleText || "", boxes: boxes,
            grades: session ? (r.grades || []) : [],
            cells: (r.cells || []).map(function (c, i) {
                const bc = session ? (boxes[i] || null) : null
                const text = session ? (bc ? (bc.medianText || "-") : "-") : (c.text || "-")
                const outside = c.outside === true
                return {
                    tag: c.tag || "", value: c.value,
                    mark: c.mark === "ring" || c.mark === "dot" ? c.mark : "",
                    markName: c.mark === "ring" ? "chPostureRing" : "chPostureDot",
                    banded: c.wash === "band" && ty.fin(c.bandLo) && ty.fin(c.bandHi), bandLo: c.bandLo, bandHi: c.bandHi,
                    faultHi: c.faultHi ? c.faultHi.edge : null, faultLo: c.faultLo ? c.faultLo.edge : null,
                    ghost: c.ghost,
                    text: text, dim: text === "-", outside: outside,
                    word: outside ? (c.ringWord || "") : (session && bc ? (bc.wideText || "") : ""),
                    warned: !!c.warn, warnText: (c.warn || "") + (c.warnShort ? " " + c.warnShort : ""),
                    alt: c.alt === true, altText: c.altText || "", altAvailable: !!c.altAvailable,
                    altWarnText: c.altWarn ? c.altWarn + (c.altWarnShort ? " " + c.altWarnShort : "") : ""
                }
            })
        }
    }
}
