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
import QtTest
import "../../cameras/AutoAnnotations.js" as AA

// The coach's static marks (src/Gui/cameras/AutoAnnotations.js; auto_annotations_design.md)
// pressed with a hand-made replay payload in the exact shapes the tile reads: pose2d
// frames (flat kp [x,y,c]×17), club samples (normalised grip/head, flags), ball samples.
// What these assert: the marks are taken at ADDRESS and nowhere else; each sits on the
// keypoint it claims; the DTL plane line passes through the ball AND the grip and reaches
// the frame's edge; the face-on tile never gets the DTL marks and vice versa; missing
// data gives no mark rather than a guessed one.
TestCase {
    name: "AutoAnnotations"

    function kpFrame(t, pts) {
        // pts: { jointIndex: [x, y] } — everything else conf 0
        var kp = []
        for (var j = 0; j < 17; ++j) {
            var p = pts[j]
            kp.push(p ? p[0] : 0); kp.push(p ? p[1] : 0); kp.push(p ? 0.9 : 0)
        }
        return { t_us: t, kp: kp }
    }
    // A face-on address pose: head at (0.50, 0.20), ears 0.06 apart, hips at x 0.44 / 0.56;
    // the mask's edges (address_marks.h, through the payload) at 0.40 / 0.60.
    function faceOnDet(withSmoothed) {
        var addr = kpFrame(1000000, { 0: [0.50, 0.21], 1: [0.49, 0.20], 2: [0.51, 0.20],
                                      3: [0.47, 0.20], 4: [0.53, 0.20], 11: [0.44, 0.55], 12: [0.56, 0.55] })
        var later = kpFrame(1500000, { 0: [0.60, 0.21], 3: [0.57, 0.20], 4: [0.63, 0.20],
                                       11: [0.50, 0.55], 12: [0.62, 0.55] })
        var d = { pose2d: { frames: [addr, later] },
                  addressMarks: { found: true, rowY: 0.55, hipLeftX: 0.44, hipRightX: 0.56, leftX: 0.40, rightX: 0.60, buttX: NaN, side: 0, rows: 7 } }
        if (withSmoothed) d.pose2d.smoothed = [addr, later]
        return d
    }
    function dtlDet(withBall) {
        var addr = kpFrame(1000000, { 11: [0.40, 0.50], 12: [0.42, 0.50] })
        var d = { pose2d: { frames: [addr] },
                  addressMarks: { found: true, rowY: 0.50, hipLeftX: 0.40, hipRightX: 0.42, leftX: 0.33, rightX: 0.52, buttX: 0.33, side: -1, rows: 7 },
                  club: { valid: true, samples: [
                      { t_us: 990000, grip: [0.30, 0.45], head: [0.55, 0.85], flags: 0, conf: 0.9 },
                      { t_us: 1400000, grip: [0.10, 0.10], head: [0.20, 0.20], flags: 0, conf: 0.9 } ] } }
        if (withBall) d.ball = { samples: [ { t_us: 990000, x: 0.60, y: 0.90, r: 0.01, conf: 1, found: true } ] }
        return d
    }
    function ofKind(marks, k) { return marks.filter(function (m) { return m.kind === k }) }
    function onLine(l, p, tol) {
        var dx = l.x1 - l.x0, dy = l.y1 - l.y0
        var cross = Math.abs(dx * (p[1] - l.y0) - dy * (p[0] - l.x0)) / Math.hypot(dx, dy)
        return cross <= tol
    }

    function test_faceOn_head_and_hips_at_address() {
        var marks = AA.build(faceOnDet(true), false, 1000000)
        var circles = ofKind(marks, "circle"), vlines = ofKind(marks, "vline"), lines = ofKind(marks, "line")
        compare(circles.length, 1, "one head circle")
        fuzzyCompare(circles[0].cx, 0.50, 0.005)
        fuzzyCompare(circles[0].cy, 0.202, 0.005)
        fuzzyCompare(circles[0].r, 0.75 * 0.06, 1e-6, "radius 0.75 × the ear span")
        compare(vlines.length, 2, "one line per hip EDGE")
        fuzzyCompare(Math.min(vlines[0].x, vlines[1].x), 0.40, 1e-9, "the mask's left edge, not the joint")
        fuzzyCompare(Math.max(vlines[0].x, vlines[1].x), 0.60, 1e-9, "the mask's right edge, not the joint")
        compare(lines.length, 0, "no plane line on the face-on tile")
    }

    function test_marks_are_taken_at_address_not_later() {
        // The later frame has the head and hips 0.06–0.10 to the right; the marks must not move.
        var marks = AA.build(faceOnDet(false), false, 1000000)
        fuzzyCompare(ofKind(marks, "circle")[0].cx, 0.50, 0.005)
        // No pose frame within 60 ms of the asked instant ⇒ no head circle; the hip EDGES are
        // the analysis's own address reading and do not depend on the pose at that instant.
        var late = AA.build(faceOnDet(false), false, 1250000)
        compare(ofKind(late, "circle").length, 0, "no address pose frame ⇒ no head circle")
        compare(ofKind(late, "vline").length, 2, "the measured hip edges still stand")
        compare(AA.build(faceOnDet(false), false, -1).length, 0, "no address instant ⇒ no marks")
    }

    function test_dtl_hips_and_plane_through_ball_and_grip() {
        var marks = AA.build(dtlDet(true), true, 1000000)
        compare(ofKind(marks, "circle").length, 0, "no head circle on the DTL tile")
        var v = ofKind(marks, "vline")
        compare(v.length, 1, "one line on DTL: the butt")
        fuzzyCompare(v[0].x, 0.33, 1e-9, "the mask's rear edge")
        compare(v[0].label, "butt")
        var l = ofKind(marks, "line")
        compare(l.length, 1, "the address plane line")
        verify(onLine(l[0], [0.30, 0.45], 1e-6), "passes through the grip at address")
        verify(onLine(l[0], [0.60, 0.90], 1e-6), "passes through the ball")
        // Reaches the unit square's edges at both ends.
        function onEdge(x, y) { return Math.abs(x) < 1e-9 || Math.abs(x - 1) < 1e-9 || Math.abs(y) < 1e-9 || Math.abs(y - 1) < 1e-9 }
        verify(onEdge(l[0].x0, l[0].y0) && onEdge(l[0].x1, l[0].y1), "extended to the frame edges")
    }

    function test_dtl_plane_without_ball_is_the_shaft_line() {
        var marks = AA.build(dtlDet(false), true, 1000000)
        var l = ofKind(marks, "line")
        compare(l.length, 1)
        verify(onLine(l[0], [0.30, 0.45], 1e-6) && onLine(l[0], [0.55, 0.85], 1e-6), "grip → head of the address sample")
        verify(l[0].label.indexOf("shaft") >= 0)
    }

    function test_dtl_plane_needs_a_measured_address_sample() {
        var d = dtlDet(true)
        d.club.samples[0].flags = AA.kFlagCoasted
        compare(ofKind(AA.build(d, true, 1000000), "line").length, 0, "a coasted sample is not an address measurement")
        var e = dtlDet(true)
        e.club.valid = false
        compare(ofKind(AA.build(e, true, 1000000), "line").length, 0, "a refused track draws no plane")
    }

    function test_no_edges_means_no_hip_lines() {
        // A swing analysed before the marks existed, or whose mask missed the hips: the head
        // circle still draws (it is pose), the hip lines do not (they are edges, not joints).
        var d = faceOnDet(true); delete d.addressMarks
        var marks = AA.build(d, false, 1000000)
        compare(ofKind(marks, "circle").length, 1)
        compare(ofKind(marks, "vline").length, 0, "no edges ⇒ no hip lines, never the joints")
        var e = dtlDet(true); e.addressMarks = { found: false }
        var m2 = AA.build(e, true, 1000000)
        compare(ofKind(m2, "vline").length, 0)
        compare(ofKind(m2, "line").length, 1, "the plane line does not depend on the mask")
    }

    // A DTL address: the pose with the head, the trunk and both legs — ears 3 (0.66, 0.27) / 4
    // (0.68, 0.27), shoulders 5 (0.59, 0.38) / trail 6 (0.61, 0.39), hips (0.47, 0.52) / (0.49, 0.53),
    // the trail (right, 12/14/16 for a right-hander) leg nearer the camera — the analysis's
    // rear-outline points (tailbone (0.42, 0.50), back of the neck (0.58, 0.35); address_marks.h,
    // stage version 2; the coach lines no longer read them) and the ball at (0.80, 0.88). A later
    // frame with everything moved, which the lines must ignore.
    function dtlBodyDet(drop, noBack) {
        var pts = { 3: [0.66, 0.27], 4: [0.68, 0.27],
                    5: [0.59, 0.38], 6: [0.61, 0.39], 11: [0.47, 0.52], 12: [0.49, 0.53],
                    13: [0.52, 0.70], 14: [0.53, 0.71], 15: [0.50, 0.83], 16: [0.48, 0.88] }
        for (var i = 0; drop && i < drop.length; ++i) delete pts[drop[i]]
        var addr = kpFrame(1000000, pts)
        var later = kpFrame(1400000, { 5: [0.40, 0.30], 6: [0.42, 0.31], 11: [0.45, 0.50], 12: [0.47, 0.51],
                                       13: [0.60, 0.70], 14: [0.61, 0.71], 15: [0.60, 0.85], 16: [0.62, 0.86] })
        var am = { found: true, rowY: 0.525, hipLeftX: 0.47, hipRightX: 0.49, leftX: 0.41, rightX: 0.61, buttX: 0.41, side: -1, rows: 31,
                   tailboneX: 0.42, tailboneY: 0.50, neckX: 0.58, neckY: 0.35, backWalks: 11 }
        if (noBack) { am.tailboneX = NaN; am.tailboneY = NaN; am.neckX = NaN; am.neckY = NaN }
        return { pose2d: { frames: [addr, later] }, addressMarks: am,
                 ball: { samples: [ { t_us: 990000, x: 0.80, y: 0.88, r: 0.01, conf: 1, found: true } ] } }
    }
    function polys(marks, label) {
        return marks.filter(function (m) { return m.kind === "polyline" && (!label || m.label === label) })
    }
    function hogan(marks) { return marks.filter(function (m) { return m.kind === "polyline" && m.dash === "long" }) }
    function onSeg(a, b, p, tol) {
        var dx = b[0] - a[0], dy = b[1] - a[1]
        return Math.abs(dx * (p[1] - a[1]) - dy * (p[0] - a[0])) / Math.hypot(dx, dy) <= tol
    }

    // The joint neck of the fixture: halfway from the shoulder midpoint (0.60, 0.385) to the ear
    // midpoint (0.67, 0.27) — the point AddressMarksStage starts its back-of-the-neck walk from.
    function fixtureNeck(ears) {
        var e = ears || [[0.66, 0.27], [0.68, 0.27]], ex = 0, ey = 0
        for (var i = 0; i < e.length; ++i) { ex += e[i][0]; ey += e[i][1] }
        ex /= e.length; ey /= e.length
        var sx = 0.5 * (0.59 + 0.61), sy = 0.5 * (0.38 + 0.39)
        return [sx + 0.5 * (ex - sx), sy + 0.5 * (ey - sy)]
    }
    function near(p, q, tol) { return Math.abs(p[0] - q[0]) <= tol && Math.abs(p[1] - q[1]) <= tol }

    // ── the spine: the trail hip joint → the neck, joining the trail leg (Mark, 23 Oct 2026) ──
    function test_dtl_spine_is_trail_hip_to_neck() {
        var marks = AA.build(dtlBodyDet(), true, 1000000, true, 1)
        var sp = polys(marks, "spine")
        compare(sp.length, 1, "one spine line on the DTL tile")
        compare(sp[0].pts.length, 2)
        compare(sp[0].pts[0], polys(marks, "trail leg")[0].pts[0], "it starts EXACTLY at the trail leg's first point: the two lines join")
        compare(sp[0].pts[0], [0.49, 0.53], "right-handed: the trail hip is 12 — no tail below it")
        verify(near(sp[0].pts[1], fixtureNeck(), 1e-12),
               "it ends AT the joint neck, halfway from the shoulders to the ears — no extension through the head: " + sp[0].pts[1])
        // left-handed: the trail hip is the body's left (11), the neck the same
        var lm = AA.build(dtlBodyDet(), true, 1000000, false, 1), ls = polys(lm, "spine")
        compare(ls.length, 1)
        compare(ls[0].pts[0], [0.47, 0.52], "left-handed: from keypoint 11")
        compare(ls[0].pts[0], polys(lm, "trail leg")[0].pts[0], "left-handed: joined to the trail leg")
        verify(near(ls[0].pts[1], fixtureNeck(), 1e-12))
        // one ear seen: it stands for the ear point (the analysis's mean of the confident ears)
        verify(near(polys(AA.build(dtlBodyDet([3]), true, 1000000, true, 1), "spine")[0].pts[1], fixtureNeck([[0.68, 0.27]]), 1e-12),
               "one ear: the neck is halfway to it")
        // the rear-outline points are not this line: moving them does not move it
        var d = dtlBodyDet(); d.addressMarks.tailboneX = 0.30; d.addressMarks.neckX = 0.70
        compare(polys(AA.build(d, true, 1000000, true, 1), "spine")[0].pts, sp[0].pts, "the mask's back line does not move it")
        // the joints at the asked instant only: no pose frame at address ⇒ no spine, like the trail leg
        compare(polys(AA.build(dtlBodyDet(), true, 1250000, true, 1), "spine").length, 0, "no pose at address ⇒ no spine")
    }

    function test_dtl_spine_refusals_never_half_a_line() {
        function spineAndPlane(det, leadIsLeft) {
            var m = AA.build(det, true, 1000000, leadIsLeft === undefined ? true : leadIsLeft, 1)
            return polys(m, "spine").length + polys(m, "shoulder plane").length
        }
        var d = dtlBodyDet(); d.pose2d.frames[0].kp[12 * 3 + 2] = 0.2
        compare(spineAndPlane(d), 0, "trail hip conf 0.2 ⇒ no spine and no shoulder plane")
        var e = dtlBodyDet(); e.pose2d.frames[0].kp[11 * 3 + 2] = 0.2
        compare(spineAndPlane(e, false), 0, "left-handed: hip 11 conf 0.2 ⇒ none")
        compare(spineAndPlane(e, true), 2, "right-handed: the lead hip (11) is not needed")
        compare(spineAndPlane(dtlBodyDet([3, 4])), 0, "no ear seen ⇒ no neck ⇒ none")
        var f = dtlBodyDet(); f.pose2d.frames[0].kp[3 * 3 + 2] = 0.2; f.pose2d.frames[0].kp[4 * 3 + 2] = 0.3
        compare(spineAndPlane(f), 0, "ears at conf 0.2 / 0.3 (not above 0.3) ⇒ none")
        compare(spineAndPlane(dtlBodyDet([5])), 0, "the lead shoulder unseen ⇒ no shoulder midpoint ⇒ none")
        compare(spineAndPlane(dtlBodyDet([6])), 0, "the trail shoulder unseen ⇒ none")
    }

    function test_dtl_unreanalysed_swing_has_a_spine() {
        // Analysed before addressMarks version 2: no tailbone / neck. The spine comes from the
        // pose's joints now, so it is drawn all the same, and the shoulder plane with it.
        var marks = AA.build(dtlBodyDet(null, true), true, 1000000, true, 1)
        compare(polys(marks, "spine").length, 1, "no rear-outline points ⇒ the joint spine all the same")
        compare(polys(marks, "spine")[0].pts, polys(AA.build(dtlBodyDet(), true, 1000000, true, 1), "spine")[0].pts, "the same line")
        compare(polys(marks, "shoulder plane").length, 1, "and the shoulder plane parallel to it")
        compare(ofKind(marks, "vline").length, 1, "the butt line stands")
        compare(hogan(marks).length, 1, "the Hogan line stands (ball + pose)")
        compare(polys(marks, "trail leg").length, 1, "the trail leg stands (pose)")
        var e = dtlBodyDet(); e.addressMarks.found = false
        var em = AA.build(e, true, 1000000, true, 1)
        compare(polys(em, "spine").length, 1, "no address marks at all ⇒ the spine still drawn")
        compare(ofKind(em, "vline").length, 0, "and no butt line (that one is the mask's)")
        var g = dtlBodyDet(); delete g.addressMarks
        compare(polys(AA.build(g, true, 1000000, true, 1), "spine").length, 1, "no addressMarks object ⇒ the spine still drawn")
    }

    // ── the trail leg only ──
    function test_dtl_trail_leg_only() {
        var marks = AA.build(dtlBodyDet(), true, 1000000, true, 1)
        compare(polys(marks, "trail leg").length, 1, "the trail leg")
        compare(polys(marks, "trail leg")[0].pts, [[0.49, 0.53], [0.53, 0.71], [0.48, 0.88]], "right-handed: 12 → 14 → 16")
        compare(polys(marks, "lead leg").length, 0, "the lead leg is never drawn")
        var lefty = AA.build(dtlBodyDet(), true, 1000000, false, 1)
        compare(polys(lefty, "trail leg")[0].pts[0], [0.47, 0.52], "left-handed: the trail leg is the body's left")
        compare(polys(AA.build(dtlBodyDet([14]), true, 1000000, true, 1), "trail leg").length, 0, "trail knee unseen ⇒ no line, never half a leg")
        var d = dtlBodyDet(); d.pose2d.frames[0].kp[16 * 3 + 2] = 0.2
        compare(polys(AA.build(d, true, 1000000, true, 1), "trail leg").length, 0, "conf 0.2 ⇒ the trail ankle is not seen")
        compare(polys(AA.build(dtlBodyDet(), true, 1250000, true, 1), "trail leg").length, 0, "no pose at address ⇒ no leg")
    }

    // ── the shoulder plane (TPI): PARALLEL to the spine, just above the trail shoulder ──
    function shoulderAt(aspect, drop, leadIsLeft) {
        return polys(AA.build(dtlBodyDet(drop), true, 1000000, leadIsLeft === undefined ? true : leadIsLeft, aspect), "shoulder plane")
    }
    // Signed distance in PIXELS (unit height) of point p from the line through a with direction d
    // (normalised inputs, x stretched by the aspect); positive = above (smaller image y).
    function aboveBy(p, a, d, aspect) {
        var dx = d[0] * aspect, dy = d[1], len = Math.hypot(dx, dy)
        var nx = dy / len, ny = -dx / len
        if (ny > 0) { nx = -nx; ny = -ny }
        return ((p[0] - a[0]) * aspect * nx + (p[1] - a[1]) * ny)
    }
    function test_dtl_shoulder_plane_parallel_to_spine_above_trail_shoulder() {
        // Nick Henn's DTL frame (976 × 1024) and a wide 16:9 one: the offset is measured in pixels.
        var aspects = [976 / 1024, 16 / 9]
        for (var i = 0; i < aspects.length; ++i) {
            var a = aspects[i]
            var marks = AA.build(dtlBodyDet(), true, 1000000, true, a)
            var sh = polys(marks, "shoulder plane"), sp = polys(marks, "spine")
            compare(sh.length, 1, "one shoulder plane line (aspect " + a + ")")
            compare(sh[0].dash, "dot", "drawn dotted")
            var dS = [sp[0].pts[1][0] - sp[0].pts[0][0], sp[0].pts[1][1] - sp[0].pts[0][1]]
            var dP = [sh[0].pts[1][0] - sh[0].pts[0][0], sh[0].pts[1][1] - sh[0].pts[0][1]]
            var cross = (dS[0] * a) * dP[1] - dS[1] * (dP[0] * a)
            verify(Math.abs(cross) < 1e-12, "parallel to the spine line in the picture (cross " + cross + ")")
            verify(dP[1] < 0, "runs the same way as the spine (up toward the neck)")
            // the trail shoulder (6: [0.61, 0.39]) lies kShoulderPlaneMargin spine lengths BELOW the
            // line, measured at a right angle in pixels; the spine is trail hip (12) → the joint neck
            var nk = fixtureNeck()
            var spinePx = Math.hypot((nk[0] - 0.49) * a, nk[1] - 0.53)
            fuzzyCompare(-aboveBy([0.61, 0.39], sh[0].pts[0], dP, a), AA.kShoulderPlaneMargin * spinePx, 1e-9,
                         "just above the trail shoulder, by the margin")
            fuzzyCompare(Math.hypot(dP[0] * a, dP[1]), (AA.kShoulderPlaneDown + AA.kShoulderPlaneUp) * spinePx, 1e-9, "its length")
        }
        var lm = AA.build(dtlBodyDet(), true, 1000000, false, 1)
        var lefty = polys(lm, "shoulder plane")[0], lsp = polys(lm, "spine")[0]
        var dl = [lefty.pts[1][0] - lefty.pts[0][0], lefty.pts[1][1] - lefty.pts[0][1]]
        var dls = [lsp.pts[1][0] - lsp.pts[0][0], lsp.pts[1][1] - lsp.pts[0][1]]
        verify(Math.abs(dls[0] * dl[1] - dls[1] * dl[0]) < 1e-12, "left-handed: parallel to its own spine (hip 11 → neck)")
        var nk = fixtureNeck()
        fuzzyCompare(-aboveBy([0.59, 0.38], lefty.pts[0], dl, 1), AA.kShoulderPlaneMargin * Math.hypot(nk[0] - 0.47, nk[1] - 0.52), 1e-9,
                     "left-handed: the margin is above the left shoulder")
    }

    function test_dtl_shoulder_plane_refusals() {
        compare(shoulderAt(1, [3, 4]).length, 0, "no spine line (no ear, so no neck) ⇒ none")
        compare(shoulderAt(1, [12]).length, 0, "no spine line (no trail hip) ⇒ none")
        compare(shoulderAt(1, [6]).length, 0, "trail shoulder unseen ⇒ none")
        compare(shoulderAt(1, [5]).length, 0, "the lead shoulder unseen ⇒ no neck, no spine ⇒ none")
        compare(shoulderAt(undefined).length, 0, "no image aspect ⇒ no offset to measure")
        compare(shoulderAt(0).length, 0)
        compare(shoulderAt(NaN).length, 0)
        compare(polys(AA.build(dtlBodyDet(), true, 1250000, true, 1), "shoulder plane").length, 0, "no pose at address ⇒ none")
    }

    // ── the Hogan line: ball → trail shoulder, on to the picture's edge ──
    function test_dtl_hogan_line_ball_through_trail_shoulder() {
        var marks = AA.build(dtlBodyDet(), true, 1000000, true, 1)
        var h = hogan(marks)
        compare(h.length, 1, "one Hogan line")
        verify(h[0].label.indexOf("Hogan") === 0)
        compare(h[0].pts[0], [0.80, 0.88], "it starts at the ball")
        verify(onSeg(h[0].pts[0], h[0].pts[1], [0.61, 0.39], 1e-12), "through the trail shoulder at address")
        var e = h[0].pts[1]
        verify(Math.abs(e[0]) < 1e-9 || Math.abs(e[1]) < 1e-9, "on to the picture's edge (left or top here)")
        verify(e[1] < 0.39, "past the shoulder, above it, where the hands and club are at the top")
        // the dashed shaft plane is a different line, untouched
        compare(ofKind(marks, "line").length, 0, "this fixture has no club; the shaft plane line is not the Hogan line")
        var lefty = hogan(AA.build(dtlBodyDet(), true, 1000000, false, 1))[0]
        verify(onSeg(lefty.pts[0], lefty.pts[1], [0.59, 0.38], 1e-12), "left-handed: through the body's left shoulder")
    }

    function test_dtl_hogan_line_refusals() {
        compare(hogan(AA.build(dtlBodyDet([6]), true, 1000000, true, 1)).length, 0, "trail shoulder unseen ⇒ none")
        var d = dtlBodyDet(); d.pose2d.frames[0].kp[6 * 3 + 2] = 0.2
        compare(hogan(AA.build(d, true, 1000000, true, 1)).length, 0, "conf 0.2 ⇒ none")
        var e = dtlBodyDet(); delete e.ball
        compare(hogan(AA.build(e, true, 1000000, true, 1)).length, 0, "no ball ⇒ none")
        var f = dtlBodyDet(); f.ball.samples[0].found = false
        compare(hogan(AA.build(f, true, 1000000, true, 1)).length, 0, "a ball not found ⇒ none")
        compare(hogan(AA.build(dtlBodyDet(), true, 1250000, true, 1)).length, 0, "no pose at address ⇒ none")
    }

    function test_faceOn_never_gets_the_dtl_lines() {
        var marks = AA.build(dtlBodyDet(), false, 1000000, true, 1)
        compare(polys(marks).length, 0, "spine, shoulder plane, Hogan and leg lines are DTL only")
        compare(ofKind(marks, "vline").length, 2, "face-on keeps its hip edges")
    }

    // ── the View menu's sets: every mark carries one; the tile draws only the sets that are on ──
    function test_every_mark_has_its_set() {
        var want = { "head": "headHips", "hip edge": "headHips", "butt": "headHips",
                     "address shaft plane (ball → hands)": "plane", "address shaft plane (shaft)": "plane",
                     "Hogan plane (ball → trail shoulder)": "plane",
                     "spine": "posture", "shoulder plane": "posture", "trail leg": "posture" }
        var dtl = dtlBodyDet()
        dtl.club = dtlDet(true).club                     // the plane line too
        var all = AA.build(faceOnDet(true), false, 1000000, true, 1).concat(AA.build(dtl, true, 1000000, true, 1))
        compare(all.length, 3 + 6, "face-on: head + 2 hips; DTL: butt, plane, Hogan, spine, shoulder plane, trail leg")
        for (var i = 0; i < all.length; ++i) {
            verify(want[all[i].label] !== undefined, "a known mark: " + all[i].label)
            compare(all[i].set, want[all[i].label], all[i].label + " is in the " + want[all[i].label] + " set")
        }
    }

    function test_filter_by_sets() {
        var dtl = dtlBodyDet(); dtl.club = dtlDet(true).club
        var marks = AA.build(dtl, true, 1000000, true, 1)
        function labels(sets) { return AA.filterSets(marks, sets).map(function (m) { return m.label }).sort() }
        compare(labels({ posture: true, plane: false, headHips: false }), ["shoulder plane", "spine", "trail leg"], "Posture")
        compare(labels({ posture: false, plane: true, headHips: false }),
                ["Hogan plane (ball → trail shoulder)", "address shaft plane (ball → hands)"], "Plane")
        compare(labels({ posture: false, plane: false, headHips: true }), ["butt"], "Head & hips on DTL: the butt line")
        compare(AA.filterSets(marks, { posture: true, plane: true, headHips: true }).length, marks.length, "all on ⇒ everything")
        compare(AA.filterSets(marks, { posture: false, plane: false, headHips: false }).length, 0, "all off ⇒ nothing")
        compare(AA.filterSets(marks, null).length, 0, "no sets ⇒ nothing")
        compare(AA.filterSets([ { kind: "vline", x: 0.5, label: "untagged" } ], { posture: true, plane: true, headHips: true }).length, 0,
                "a mark with no set is never drawn")
        var fo = AA.build(faceOnDet(true), false, 1000000, true, 1)
        compare(AA.filterSets(fo, { posture: false, plane: false, headHips: true }).length, 3, "face-on Head & hips: the circle and both hip edges")
        compare(AA.filterSets(fo, { posture: true, plane: true, headHips: false }).length, 0, "face-on has nothing in Posture or Plane")
    }

    // The head circle from the person mask (address_marks.h measureHead, stage version 4): the
    // analysis's own circle — the crown and the head's width — wins over the face points, and like
    // the hip edges it does not depend on the pose at the asked instant. No outline (a hood, a cap,
    // a broken mask: `headFound` false with its reason, or a record from before version 4) ⇒ the
    // face points' circle, quietly.
    function test_head_circle_from_the_mask() {
        var d = faceOnDet(true)
        d.addressMarks.headFound = true
        d.addressMarks.headCx = 0.505; d.addressMarks.headCy = 0.17; d.addressMarks.headR = 0.032
        var c = ofKind(AA.build(d, false, 1000000), "circle")
        compare(c.length, 1, "one head circle")
        compare(c[0].source, "mask", "the mask's outline")
        fuzzyCompare(c[0].cx, 0.505, 1e-9); fuzzyCompare(c[0].cy, 0.17, 1e-9); fuzzyCompare(c[0].r, 0.032, 1e-9)
        compare(c[0].set, "headHips")
        var late = ofKind(AA.build(d, false, 1250000), "circle")
        compare(late.length, 1, "no address pose frame: the measured outline still stands")
        fuzzyCompare(late[0].cx, 0.505, 1e-9)
    }

    function test_head_circle_falls_back_to_the_face_points() {
        var d = faceOnDet(true)
        d.addressMarks.headFound = false
        d.addressMarks.headRefused = "the outline steps out before it is a head's width (a hood, a cap's brim or the shoulders join it)"
        d.addressMarks.headCx = NaN; d.addressMarks.headCy = NaN; d.addressMarks.headR = NaN
        var c = ofKind(AA.build(d, false, 1000000), "circle")
        compare(c.length, 1, "still a head circle")
        compare(c[0].source, "pose", "the face points'")
        fuzzyCompare(c[0].r, 0.75 * 0.06, 1e-6, "radius 0.75 × the ear span, as before")
        // found but no usable radius: never a circle of size NaN
        d.addressMarks.headFound = true
        compare(ofKind(AA.build(d, false, 1000000), "circle")[0].source, "pose", "a found flag without a circle is not a circle")
        // the old record (no head keys): the face points' circle
        compare(ofKind(AA.build(faceOnDet(true), false, 1000000), "circle")[0].source, "pose")
    }

    function test_head_radius_fallbacks() {
        // Ears missing, eyes present ⇒ 1.1 × the eye span; nothing ⇒ 4 % of the height; floor 2.5 %.
        var eyesOnly = { pose2d: { frames: [ kpFrame(0, { 1: [0.49, 0.2], 2: [0.51, 0.2] }) ] } }
        fuzzyCompare(ofKind(AA.build(eyesOnly, false, 0), "circle")[0].r, 0.025, 1e-9, "1.1 × 0.02 floors at 0.025")
        var noseOnly = { pose2d: { frames: [ kpFrame(0, { 0: [0.5, 0.2] }) ] } }
        fuzzyCompare(ofKind(AA.build(noseOnly, false, 0), "circle")[0].r, 0.04, 1e-9)
        var noHead = { pose2d: { frames: [ kpFrame(0, { 11: [0.4, 0.5] }) ] } }
        compare(ofKind(AA.build(noHead, false, 0), "circle").length, 0, "no head keypoint ⇒ no circle")
    }
}
