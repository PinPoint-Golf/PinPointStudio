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
.pragma library

// The coach's static marks (docs/design/auto_annotations_design.md): measured ONCE on the
// frame nearest the swing's Address instant from the replay payload the skeleton is drawn
// from, returned in NORMALISED image coordinates, and drawn unchanged on every frame of
// the swing. Pure functions, no Qt: a QML test can press them with a hand-made payload.
//
//   build(det, isDtl, addressUs) → [ mark, … ]
//     { kind: "circle", cx, cy, r, label }           face-on: the head at address
//     { kind: "vline",  x, label }                   face-on: the outside of each hip; DTL: the rear of the butt
//     { kind: "line",   x0, y0, x1, y1, label }      DTL: the address shaft line through ball and grip
//
// `det` is the tile's own detail (face-on top level, or the nested `dtl` block — the same
// shapes, dtl_overlay_payload.h). The pose keypoints are COCO-17-first: nose 0, eyes 1–2,
// ears 3–4, hips 11–12. Every number here is what the pose / ball / club READ at address,
// in pixels; none is a metric and nothing persists.

var kConfMin = 0.3
var kAddressWindowUs = 60000     // a pose frame or club sample further than this from address is not "at address"
var kFlagHeadProjected = 0x10
var kFlagCoasted = 0x04
var kFlagPredicted = 0x20
var kFlagOffFrame = 0x80
var kFlagSynthesized = 0x100

// Greatest index with t_us <= t (−1 when empty) — the tile's own _indexFor.
function indexFor(arr, t) {
    var hi = arr.length - 1
    if (hi < 0 || t < arr[0].t_us) return hi < 0 ? -1 : 0
    if (t >= arr[hi].t_us) return hi
    var lo = 0
    while (hi - lo > 1) {
        var mid = (lo + hi) >> 1
        if (arr[mid].t_us <= t) lo = mid; else hi = mid
    }
    return lo
}

// The entry of `arr` nearest `t`, or null when none lies within `windowUs`.
function nearest(arr, t, windowUs) {
    if (!arr || arr.length === 0) return null
    var i = indexFor(arr, t)
    var best = null
    for (var k = i; k <= i + 1; ++k) {
        if (k < 0 || k >= arr.length) continue
        if (Math.abs(arr[k].t_us - t) > windowUs) continue
        if (!best || Math.abs(arr[k].t_us - t) < Math.abs(best.t_us - t)) best = arr[k]
    }
    return best
}

// The pose frame at address: the smoothed series when the payload carries one (it is what
// the skeleton draws), else the raw detections.
function addressPose(det, addressUs) {
    if (!det || !det.pose2d) return null
    var series = (det.pose2d.smoothed && det.pose2d.smoothed.length) ? det.pose2d.smoothed
               : (det.pose2d.frames && det.pose2d.frames.length) ? det.pose2d.frames : null
    if (!series) return null
    return nearest(series, addressUs, kAddressWindowUs)
}

function kpt(frame, j) {
    var kp = frame.kp
    if (!kp || kp.length < (j + 1) * 3) return null
    if (kp[j * 3 + 2] <= kConfMin) return null
    return [kp[j * 3], kp[j * 3 + 1]]
}

function dist(a, b) { return Math.hypot(a[0] - b[0], a[1] - b[1]) }

// ── face-on: the head circle ────────────────────────────────────────────────
// Centre = the mean of the visible head keypoints; radius from the ears (the head's
// width), else the eyes, else a share of the frame; never smaller than 2.5 % of the
// frame height. Normalised coordinates are not isotropic, so the radius is kept as a
// fraction of HEIGHT and the painter scales it by the content rect's height.
function headCircle(frame) {
    if (!frame) return null
    var pts = []
    for (var j = 0; j <= 4; ++j) { var p = kpt(frame, j); if (p) pts.push(p) }
    if (pts.length === 0) return null
    var cx = 0, cy = 0
    for (var i = 0; i < pts.length; ++i) { cx += pts[i][0]; cy += pts[i][1] }
    cx /= pts.length; cy /= pts.length
    var lEar = kpt(frame, 3), rEar = kpt(frame, 4), lEye = kpt(frame, 1), rEye = kpt(frame, 2)
    var r
    if (lEar && rEar)      r = 0.75 * dist(lEar, rEar)
    else if (lEye && rEye) r = 1.1 * dist(lEye, rEye)
    else                   r = 0.04
    r = Math.max(r, 0.025)
    return { kind: "circle", cx: cx, cy: cy, r: r, label: "head" }
}

// ── the hip verticals: the body's OUTER edges, from the person mask ─────────
// `det.addressMarks` (address_marks.h via the payload): face-on, the outside of each hip —
// the cue for sway; down the line, the rear of the butt — the cue for moving off the line.
// Measured by the analysis on the address frame; absent on a swing analysed before it
// existed or where the mask found no body on the hip rows ⇒ no line (never the joints
// instead: a line on a joint would read as an edge and lie by half a pelvis).
function hipLines(det, isDtl) {
    var am = det ? det.addressMarks : null
    if (!am || !am.found) return []
    var out = []
    if (isDtl) {
        if (isFinite(am.buttX)) out.push({ kind: "vline", x: am.buttX, label: "butt" })
    } else {
        if (isFinite(am.leftX))  out.push({ kind: "vline", x: am.leftX,  label: "hip edge" })
        if (isFinite(am.rightX)) out.push({ kind: "vline", x: am.rightX, label: "hip edge" })
    }
    return out
}

// ── DTL: the address shaft line ─────────────────────────────────────────────
// Through the ball and the grip of the measured club sample nearest address; without a
// ball, the sample's own grip → head. Extended to the unit square's edges both ways.
function restsOnMeasurement(sm) {
    return sm && (sm.flags & (kFlagCoasted | kFlagPredicted | kFlagOffFrame | kFlagSynthesized)) === 0
}

function addressClub(det, addressUs) {
    if (!det || !det.club || !det.club.valid || !det.club.samples) return null
    var s = nearest(det.club.samples, addressUs, kAddressWindowUs)
    return (s && restsOnMeasurement(s) && s.grip && s.head) ? s : null
}

function addressBall(det) {
    if (!det || !det.ball || !det.ball.samples || det.ball.samples.length === 0) return null
    var b = det.ball.samples[0]
    return (b && b.found && isFinite(b.x) && isFinite(b.y)) ? [b.x, b.y] : null
}

// Extend the line through p and q to the edges of the unit square. Returns null when
// the two points coincide.
function extendToSquare(p, q) {
    var dx = q[0] - p[0], dy = q[1] - p[1]
    if (Math.abs(dx) < 1e-9 && Math.abs(dy) < 1e-9) return null
    var tMin = -Infinity, tMax = Infinity
    function clip(d, lo, hi) {
        if (Math.abs(d) < 1e-12) return
        var t0 = (lo) / d, t1 = (hi) / d
        if (t0 > t1) { var tmp = t0; t0 = t1; t1 = tmp }
        tMin = Math.max(tMin, t0); tMax = Math.min(tMax, t1)
    }
    clip(dx, 0 - p[0], 1 - p[0])
    clip(dy, 0 - p[1], 1 - p[1])
    if (!(tMin < tMax)) return null
    return { x0: p[0] + tMin * dx, y0: p[1] + tMin * dy, x1: p[0] + tMax * dx, y1: p[1] + tMax * dy }
}

function planeLine(det, addressUs) {
    var s = addressClub(det, addressUs)
    if (!s) return null
    var grip = [s.grip[0], s.grip[1]]
    var ball = addressBall(det)
    var through = ball ? ball : [s.head[0], s.head[1]]
    var e = extendToSquare(grip, through)
    if (!e) return null
    return { kind: "line", x0: e.x0, y0: e.y0, x1: e.x1, y1: e.y1,
             label: ball ? "address shaft plane (ball → hands)" : "address shaft plane (shaft)" }
}

// ── everything, for one tile ────────────────────────────────────────────────
function build(det, isDtl, addressUs) {
    if (!det || !(addressUs >= 0)) return []
    var frame = addressPose(det, addressUs)
    var out = []
    if (!isDtl) {
        var c = headCircle(frame)
        if (c) out.push(c)
    }
    var hips = hipLines(det, isDtl)
    for (var i = 0; i < hips.length; ++i) out.push(hips[i])
    if (isDtl) {
        var pl = planeLine(det, addressUs)
        if (pl) out.push(pl)
    }
    return out
}
