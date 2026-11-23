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
#pragma once

// The address marks' RESULT: the body's outer edges at hip height, TPI's back line and the head
// outline, per camera and per down-the-line P-frame (docs/design/auto_annotations_design.md §1).
// Plain value types the shot's analysis record holds (SwingAnalysis::addressMarks,
// swing_analysis.h), so they sit in the vocabulary, OpenCV-free and Qt-free; the measurement
// stays in Body/address_marks.h, which includes this. Moved out of address_marks.h unchanged
// (refactor 7.3, A-11).

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace pinpoint::analysis::addressmarks {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// One camera's marks. `leftX/rightX` are the outer edges of the body on the hip rows (face-on:
// the two hips; DTL: the body's front and rear); `buttX` is the rear edge on the DTL view, the
// side away from the hands (`side` −1 = image-left is the rear, +1 = image-right). The hip
// JOINTS the rows were read at are kept for the record (and for a tile to show what was read).
struct ViewMarks {
    bool    found    = false;
    int64_t frameTUs = 0;          // the frame the mask was read on
    double  rowY     = kNan;       // the hip row, normalised
    double  hipLeftX = kNan, hipRightX = kNan;   // the pose's hip joints, normalised
    double  leftX    = kNan, rightX = kNan;      // the mask's outer edges on the hip rows, normalised
    double  buttX    = kNan;       // DTL only: the rear edge
    int     side     = 0;          // DTL only: which side the rear is (−1 left, +1 right)
    int     rows     = 0;          // rows that contributed to the medians
    // Which rows were read: 0 = the hip joints' row (always, since 2026-10-01 10:30). The wrists'
    // row was tried first (the arms hang outside the hips at hip height, so the edge there can
    // be an elbow, ~1 % of the frame outside the pelvis) and REJECTED: the segmenter's mask
    // drops the dark lead thigh beside the bright glove at that height, and the "edge" became
    // the glove, 5 % of the frame INSIDE the thigh (library 4 July s8). The elbow is at worst a
    // small error on the outside; the glove is a large one on the inside. 1 is kept for the
    // record's sake and never produced.
    int     rowSource = 0;
    // DTL only: TPI's spine angle line, "from the base of the tailbone to the back of the neck"
    // (auto_annotations_design.md §1b), two points on the REAR outline of the silhouette, each
    // read by walking at a right angle to the trunk axis (hip-joint midpoint → shoulder-joint
    // midpoint) toward the rear (`side`) — not along an image row: with the trunk tilted ~40°
    // forward at address a row at neck height reads the upper back, 70 px behind the neck
    // (Nick Henn, 20 Oct). The tailbone: behind the hip-joint midpoint. The back of the neck:
    // behind the point halfway from the shoulder-joint midpoint to the ears. Normalised; NaN when
    // not measured (no rear side, a trunk or ear keypoint missing, too few walks).
    double  tailboneX = kNan, tailboneY = kNan;
    double  neckX     = kNan, neckY     = kNan;
    int     backWalks = 0;         // walks behind the lower of the two counts
    // THE HEAD OUTLINE (stage version 4, 22 Oct 2026; measureHead below): the head circle the coach
    // lines draw, off the SILHOUETTE instead of the face points — at address the golfer looks down at
    // the ball, the eyes sit low and forward, and a circle on the face keypoints lands on the face, not
    // round the skull (Mark, Nick Henn 20 Oct swing 2). The crown is the mask's top over the head; the
    // width the widest row of the head's outline between the crown and the ears; the circle touches the
    // crown with that width as its diameter. `headCx` ÷ W, `headCy` and `headR` ÷ H (the coach lines'
    // radius convention: a fraction of the frame HEIGHT), `crownY` and `headRowY` (the widest row) ÷ H,
    // `headLeftX/headRightX` (that row's ends) ÷ W. Not found ⇒ `headRefused` says why (a hood or a cap
    // merging with the body, the mask at the frame's edge, no head points); the readers then fall back
    // to the face-point circle.
    bool        headFound = false;
    double      headCx = kNan, headCy = kNan, headR = kNan;
    double      crownY = kNan, headRowY = kNan;
    double      headLeftX = kNan, headRightX = kNan;
    std::string headRefused;
};

// One DOWN-THE-LINE P-frame's marks (posture_chart_design.md §2.3, §10 stage 4a; stage version 3):
// the butt line and TPI's back line read on the frame nearest each position P1…P7, so the trunk
// bend and the hip depth can be read off the silhouette where the joint points turn out of the
// picture (P3–P5). The rear side is FROZEN from address (the hands go up at the top, and "away
// from the hands" would flip). P1 is the address frame, mask and butt line, copied from the coach
// lines' `dtl` view; its back line is re-read on that mask by the P-frame rule (behind the ears,
// AddressMarks::phaseNeckFrac), so the seven back lines are one construction.
struct DtlPhaseMarks {
    int         phase    = 0;      // the Phase (swing_analysis.h), as its persisted int
    int64_t     eventTUs = 0;      // the position's instant on the ladder
    ViewMarks   v;                 // found, the frame read, rowY, buttX, side (frozen), tailbone, neck
    std::string refused;           // why there is no reading, when there is none ("" when found)
};

struct AddressMarks {
    ViewMarks faceOn, dtl;
    std::vector<DtlPhaseMarks> dtlPhases;   // P1…P7 on the DTL camera, ladder order; empty before v3
    // Where the P-frames' back line reads its upper point (a fraction of the way from the shoulder
    // midpoint to the ears; AddressMarksStage phaseBackLine, 1.0 by default). The coach line in
    // `dtl` is read at 0.5, the collar; P1's entry re-reads the same address mask at this fraction.
    double phaseNeckFrac = kNan;
    bool any() const { return faceOn.found || dtl.found; }
};

} // namespace pinpoint::analysis::addressmarks
