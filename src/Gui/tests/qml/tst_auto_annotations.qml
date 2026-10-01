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
