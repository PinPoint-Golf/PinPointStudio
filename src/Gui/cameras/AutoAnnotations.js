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
//   build(det, isDtl, addressUs, leadIsLeft, aspect) → [ mark, … ]   every mark also carries `set`
//   filterSets(marks, { posture, plane, headHips }) → the marks of the sets that are on
//     { kind: "circle",   cx, cy, r, label, source } face-on: the head at address (the mask's outline, else the face points)
//     { kind: "vline",    x, label }                 face-on: the outside of each hip; DTL: the rear of the butt
//     { kind: "line",     x0, y0, x1, y1, label }    DTL: the address shaft line through ball and grip (dashed)
//     { kind: "polyline", pts: [[x, y], …], label, dash }  DTL: the spine (trail hip joint → the
//                                                    neck, joining the trail leg) and the trail leg, solid;
//                                                    the shoulder plane, dash "dot"; the Hogan line, dash "long"
//
// `det` is the tile's own detail (face-on top level, or the nested `dtl` block — the same
// shapes, dtl_overlay_payload.h). The pose keypoints are COCO-17-first: nose 0, eyes 1–2,
// ears 3–4, shoulders 5–6, hips 11–12, knees 13–14, ankles 15–16 (odd = the body's left).
// `leadIsLeft` (default true, a right-handed golfer) picks the TRAIL side (shoulder, leg).
// `aspect` is the drawn image's width / height — only the shoulder plane needs it (its offset
// from the trail shoulder is a distance in pixels); without it that line is not drawn.
// Every number here is what the pose / mask / ball / club READ at address, in pixels; none is a
// metric and nothing persists.

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
// FROM THE PERSON MASK when the analysis read the head's outline on the address frame
// (`det.addressMarks.headFound`, address_marks.h measureHead, stage version 4): the circle
// touches the crown and is the head's width across — it fits the skull whether the golfer
// looks down at the ball or not (Mark, 22 Oct 2026: on the face points "when looking down it
// doesn't really work"). `source` says which: "mask" | "pose".
// ELSE (a swing analysed before version 4, or a mask that could not find the crown — a hood,
// a cap, a broken mask; quietly, the tile draws what it has) the face points' circle:
// centre = the mean of the visible head keypoints; radius from the ears (the head's
// width), else the eyes, else a share of the frame; never smaller than 2.5 % of the
// frame height. Normalised coordinates are not isotropic, so the radius is kept as a
// fraction of HEIGHT and the painter scales it by the content rect's height.
function maskHeadCircle(am) {
    if (!am || !am.found || am.headFound !== true) return null
    if (!(isFinite(am.headCx) && isFinite(am.headCy) && am.headR > 0)) return null
    return { kind: "circle", cx: am.headCx, cy: am.headCy, r: am.headR, label: "head", set: "headHips", source: "mask" }
}

function headCircle(frame, am) {
    var m = maskHeadCircle(am)
    if (m) return m
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
    return { kind: "circle", cx: cx, cy: cy, r: r, label: "head", set: "headHips", source: "pose" }
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
        if (isFinite(am.buttX)) out.push({ kind: "vline", x: am.buttX, label: "butt", set: "headHips" })
    } else {
        if (isFinite(am.leftX))  out.push({ kind: "vline", x: am.leftX,  label: "hip edge", set: "headHips" })
        if (isFinite(am.rightX)) out.push({ kind: "vline", x: am.rightX, label: "hip edge", set: "headHips" })
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
             label: ball ? "address shaft plane (ball → hands)" : "address shaft plane (shaft)", set: "plane" }
}

// ── DTL: the spine, shoulder-plane, Hogan and trail-leg lines, frozen at address ──
// Mark, 22 Oct 2026: "coaching lines on the dtl view that show the spine and leg angles frozen
// at address. then we can see how the golfer moves through the swing."

function mid(a, b) { return [0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1])] }

// The SPINE line runs from the address TRAIL HIP JOINT (pose 12, or 11 for a left-hander — the
// trail leg's own first point, so the two lines join) to the NECK, through the trunk. Mark,
// 23 Oct 2026: "the solid green spine line is supposed to run from the hips (joining the line
// that is already there from the knee to the hips) to the neck." The neck is a JOINT-SPACE point:
// halfway from the shoulder midpoint to the ear point — the same point the analysis starts its
// back-of-the-neck walk from (analysis_pipeline.cpp, AddressMarksStage → measureBackLine's shX/earX
// and its default neckFrac 0.5): BOTH shoulders (5, 6) confident, and the mean of whichever ears
// (3, 4) are confident, at least one. Confident = kpt()'s conf > kConfMin, the stage's > 0.3.
// It ends AT the neck — no extension through the head — and starts AT the hip — no tail below it.
// WHY not the rear outline any more (the 22 Oct line, tailbone → back of the neck off the person
// mask, address_marks.h measureBackLine): on a golfer in a hoodie that line followed the hood and
// the back, not the trunk, and it did not meet the trail leg, which ends at the hip joint. From the
// pose alone, a swing analysed before addressMarks version 2 gets the line too. A hip or a neck
// that is not confident ⇒ no spine line (and so no shoulder plane), never half a line. Frozen at
// address: the same pose frame the trail leg is drawn from.

function jointNeck(frame) {
    if (!frame) return null
    var lsh = kpt(frame, 5), rsh = kpt(frame, 6)
    if (!lsh || !rsh) return null
    var ears = [kpt(frame, 3), kpt(frame, 4)].filter(function (p) { return p !== null })
    if (ears.length === 0) return null
    var ex = 0, ey = 0
    for (var i = 0; i < ears.length; ++i) { ex += ears[i][0]; ey += ears[i][1] }
    ex /= ears.length; ey /= ears.length
    var sh = mid(lsh, rsh)
    return [sh[0] + 0.5 * (ex - sh[0]), sh[1] + 0.5 * (ey - sh[1])]
}

function trailHip(frame, leadIsLeft) { return frame ? kpt(frame, leadIsLeft === false ? 11 : 12) : null }

// The spine's two ends and its direction (normalised), or null.
function spineAxis(frame, leadIsLeft) {
    var h = trailHip(frame, leadIsLeft), n = jointNeck(frame)
    if (!h || !n) return null
    var vx = n[0] - h[0], vy = n[1] - h[1]
    if (Math.abs(vx) < 1e-9 && Math.abs(vy) < 1e-9) return null
    return { h: h, n: n, vx: vx, vy: vy }
}

function spineLine(frame, leadIsLeft) {
    var b = spineAxis(frame, leadIsLeft)
    if (!b) return null
    return { kind: "polyline", label: "spine", set: "posture", pts: [ [b.h[0], b.h[1]], [b.n[0], b.n[1]] ] }
}

// ── the shoulder plane ──────────────────────────────────────────────────────
// TPI's flat-shoulder-plane check (mytpi.com, "Flat Shoulder Plane"): after the spine angle line,
// a second line "just above the trailing shoulder and parallel to the spine angle line"; at the
// top, "if the lead shoulder is above the line they have a flat shoulder plane." So: PARALLEL to
// the spine line above (trail hip → neck), offset from the address TRAIL shoulder joint (pose 6,
// or 5 for a left-hander) by kShoulderPlaneMargin of the spine line's length, at a right angle to
// it and upward (toward the golfer's back).
// The margin, from Nick Henn's 20 Oct swing 1 address frame (pose + the analysis's u2netp mask),
// measured against the 22 Oct tailbone→neck line: walking from the trail shoulder joint at a right
// angle to it, the body's outline was 0.22 of that line's length away (48 px of 222); 0.25 put the
// line just clear of the shoulder's outline — "just above" it. Since 23 Oct the margin, the extent
// and the direction are those of the hip→neck line (the same fractions of ITS length); the joint
// line runs through the trunk, so the trail shoulder joint now sits near it rather than 0.10 below
// it — worth a look on a real frame. The extent: kShoulderPlaneDown below the offset point and
// kShoulderPlaneUp above it, which spans the shoulders through the swing.
// The offset is a distance IN THE PICTURE: normalised coordinates are stretched by the frame's
// width and height separately, so the perpendicular is taken in pixels — `aspect` is the drawn
// image's width / height (the tile's content rect) — and mapped back. No spine line, no
// confident trail shoulder, or no usable aspect ⇒ no shoulder plane.
var kShoulderPlaneMargin = 0.25
var kShoulderPlaneUp = 0.5
var kShoulderPlaneDown = 0.7

function trailShoulder(frame, leadIsLeft) { return frame ? kpt(frame, leadIsLeft === false ? 5 : 6) : null }

function shoulderPlane(frame, aspect, leadIsLeft) {
    var b = spineAxis(frame, leadIsLeft)
    if (!b || !(aspect > 0) || !isFinite(aspect)) return null
    var trail = trailShoulder(frame, leadIsLeft)
    if (!trail) return null
    // In pixels (unit height): d = (vx·a, vy); a normal n of the same length, the upward one.
    var nx = b.vy, ny = -b.vx * aspect
    if (ny > 0) { nx = -nx; ny = -ny }
    // back to normalised: (n.x / a, n.y); the line's direction is (vx, vy) in either space
    var o = [trail[0] + kShoulderPlaneMargin * nx / aspect, trail[1] + kShoulderPlaneMargin * ny]
    return { kind: "polyline", label: "shoulder plane", dash: "dot", set: "posture",
             pts: [ [o[0] - kShoulderPlaneDown * b.vx, o[1] - kShoulderPlaneDown * b.vy],
                    [o[0] + kShoulderPlaneUp * b.vx,   o[1] + kShoulderPlaneUp * b.vy] ] }
}

// ── the Hogan line ──────────────────────────────────────────────────────────
// Hogan's plane: from the ball through the address TRAIL shoulder joint. With the dashed shaft
// plane (ball up the address shaft) it bounds the "V zone" the club should swing within. The
// ball is the shaft plane's own (`det.ball.samples[0]`). It runs from the ball through the
// shoulder to the picture's edge: on Nick Henn's 20 Oct swing 1 the hands at the top sit 1.25 of
// the ball→shoulder length up it and the club beyond them, and the edge is at 1.78 — any fixed
// length short of the edge risks ending under the club. No ball or no confident trail shoulder ⇒
// no line. Long dashes, so it reads apart from the shaft plane's shorter ones.
function hoganLine(det, frame, leadIsLeft) {
    var ball = addressBall(det), trail = trailShoulder(frame, leadIsLeft)
    if (!ball || !trail) return null
    var e = extendToSquare(ball, trail)            // t: ball 0, shoulder 1; (x1, y1) is the shoulder side
    if (!e) return null
    return { kind: "polyline", label: "Hogan plane (ball → trail shoulder)", dash: "long", set: "plane",
             pts: [ [ball[0], ball[1]], [e.x1, e.y1] ] }
}

// ── the trail leg ───────────────────────────────────────────────────────────
// Hip → knee → ankle of the TRAIL leg (the near one down the line), drawn only when its three
// points are confident — never half a leg. The lead leg is not drawn: from down the line it is
// behind the trail leg and its line was a distraction (Mark, 22 Oct 2026).
function trailLeg(frame, leadIsLeft) {
    if (!frame) return null
    var j = leadIsLeft === false ? [11, 13, 15] : [12, 14, 16]
    var hip = kpt(frame, j[0]), knee = kpt(frame, j[1]), ankle = kpt(frame, j[2])
    return (hip && knee && ankle) ? { kind: "polyline", label: "trail leg", set: "posture", pts: [hip, knee, ankle] } : null
}

// ── the View menu's sets ────────────────────────────────────────────────────
// Every mark carries `set` (auto_annotations_design.md §1c), and the View menu turns each set on
// or off on its own (ViewLayout.coachSetsFor):
//   "posture"  — the spine (trail hip → neck), the shoulder plane, the trail leg
//   "plane"    — the address shaft plane and the Hogan line (the "V zone")
//   "headHips" — face-on: the head circle and the hip edges; DTL: the butt line
var kSets = ["posture", "plane", "headHips"]

// The marks whose set is on in `sets` ({ posture, plane, headHips }: bool). A mark with no set,
// or an unknown one, is dropped — never drawn by default.
function filterSets(marks, sets) {
    if (!marks || !sets) return []
    return marks.filter(function (m) { return kSets.indexOf(m.set) >= 0 && sets[m.set] === true })
}

// ── everything, for one tile ────────────────────────────────────────────────
function build(det, isDtl, addressUs, leadIsLeft, aspect) {
    if (!det || !(addressUs >= 0)) return []
    var frame = addressPose(det, addressUs)
    var out = []
    if (!isDtl) {
        var c = headCircle(frame, det.addressMarks)
        if (c) out.push(c)
    }
    var hips = hipLines(det, isDtl)
    for (var i = 0; i < hips.length; ++i) out.push(hips[i])
    if (isDtl) {
        var pl = planeLine(det, addressUs)
        if (pl) out.push(pl)
        var hg = hoganLine(det, frame, leadIsLeft)
        if (hg) out.push(hg)
        var sp = spineLine(frame, leadIsLeft)
        if (sp) out.push(sp)
        var sh = shoulderPlane(frame, aspect, leadIsLeft)
        if (sh) out.push(sh)
        var tl = trailLeg(frame, leadIsLeft)
        if (tl) out.push(tl)
    }
    return out
}
