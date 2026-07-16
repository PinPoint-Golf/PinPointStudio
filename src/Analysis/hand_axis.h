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

// Shared COCO-WholeBody hand geometry (WB4, wholebody_pose_design.md §2.2). The
// score-weighted per-hand centroid `handCentroid()` is factored out of
// pose_runner.cpp verbatim so the raw grip-anchor path and the smoothed-hands
// recompute (pose.gripFromSmoothedHands) share ONE definition — the outputs are
// byte-identical to the original pose_runner math. The grip-anchor resolver
// (`computeGripAnchors`) and the hand-axis direction (`handAxisDirection`, the
// ShaftTracker θ prior) build on it. Pure, Qt-only (QPointF via swing_analysis.h),
// cv-free — mirrors head_track/foot_metrics; unit-tested standalone.

#include <QPointF>
#include <algorithm>
#include <cmath>

#include "swing_analysis.h"   // PoseFrame2D, kLeftHandFirstKp, kRightHandFirstKp

namespace pinpoint::analysis {

// COCO-WholeBody per-hand layout (21 kp): index 0 = wrist root, then 4/finger
// thumb→pinky (CMC/MCP/IP/tip for thumb, MCP/PIP/DIP/tip for the others).
inline constexpr int kHandKpCount   = 21;
inline constexpr int kHandThumbCmc  = 1;
inline constexpr int kHandIndexMcp  = 5;
inline constexpr int kHandMiddleMcp = 9;
inline constexpr int kHandRingMcp   = 13;
inline constexpr int kHandPinkyMcp  = 17;

// COCO body wrist joints + the grip-fallback conf gate (pose_runner constants).
inline constexpr int   kLeftWristKp     = 9;    // PoseJoint::LeftWrist
inline constexpr int   kRightWristKp    = 10;   // PoseJoint::RightWrist
inline constexpr float kGripMinHandConf = 0.3f; // both hands must clear this for a knuckle centroid

struct HandCentroid {
    QPointF pt;
    float   conf = 0.f;
    bool    ok   = false;
};

// Score-weighted centroid of one hand's 21 keypoints (score > 0.2 only). conf =
// plain mean of the contributing scores; ok = false when no keypoint clears the
// floor. Fed pointers into the contiguous kp/conf arrays at kLeftHandFirstKp /
// kRightHandFirstKp. BYTE-IDENTICAL to pose_runner.cpp's original handCentroid.
inline HandCentroid handCentroid(const QPointF *pts, const float *scores)
{
    constexpr float kMinKpScore = 0.2f;
    double sx = 0.0, sy = 0.0, sw = 0.0, sconf = 0.0;
    int n = 0;
    for (int k = 0; k < kHandKpCount; ++k) {
        if (scores[k] <= kMinKpScore)
            continue;
        sx    += scores[k] * pts[k].x();
        sy    += scores[k] * pts[k].y();
        sw    += scores[k];
        sconf += scores[k];
        ++n;
    }
    HandCentroid c;
    if (n == 0 || sw <= 0.0)
        return c;
    c.pt   = QPointF(sx / sw, sy / sw);
    c.conf = static_cast<float>(sconf / n);
    c.ok   = true;
    return c;
}

// Resolve a frame's lead/trail grip anchors + handConf from ITS OWN hand
// keypoints, with the COCO-wrist fallback (handConf 0) when either hand is
// unconvincing. Same lead/trail resolution + conf gate pose_runner uses, so on
// the SMOOTHED track this recomputes the grip from the smoothed hands — the
// anchor inherits the smoother's honesty tiers (a low-conf/Off hand kp collapses
// the centroid and falls back to the wrist). `leftLeads` == (handedness != 2).
inline void computeGripAnchors(PoseFrame2D &f, bool leftLeads)
{
    const HandCentroid lc = handCentroid(&f.kp[kLeftHandFirstKp],  &f.conf[kLeftHandFirstKp]);
    const HandCentroid rc = handCentroid(&f.kp[kRightHandFirstKp], &f.conf[kRightHandFirstKp]);
    QPointF leftPt, rightPt;
    float   handConf = 0.f;
    if (lc.ok && rc.ok && std::min(lc.conf, rc.conf) >= kGripMinHandConf) {
        leftPt   = lc.pt;
        rightPt  = rc.pt;
        handConf = 0.5f * (lc.conf + rc.conf);
    } else {
        leftPt  = f.kp[kLeftWristKp];
        rightPt = f.kp[kRightWristKp];
    }
    f.leadHand  = leftLeads ? leftPt  : rightPt;
    f.trailHand = leftLeads ? rightPt : leftPt;
    f.handConf  = handConf;
}

// Image-plane direction (deg, atan2 convention, y-down PIXELS) of the grip's
// hand axis (wrist-root → middle-MCP), averaged over the hands whose BOTH
// endpoints clear confMin, weighted by each hand's endpoint confidence. Returns
// the averaged confidence (0 ⇒ no confident hand ⇒ outDeg left untouched). px
// space via (frameW, frameH) so the angle matches ShaftTracker's θ grid (also px).
inline float handAxisDirection(const PoseFrame2D &f, int frameW, int frameH,
                               double confMin, double &outDeg)
{
    double vx = 0.0, vy = 0.0, sconf = 0.0;
    int n = 0;
    const auto addHand = [&](int base) {
        const int root = base, mcp = base + kHandMiddleMcp;
        const double cr = f.conf[root], cm = f.conf[mcp];
        if (cr < confMin || cm < confMin)
            return;
        const double dx = (f.kp[mcp].x() - f.kp[root].x()) * frameW;
        const double dy = (f.kp[mcp].y() - f.kp[root].y()) * frameH;
        const double len = std::hypot(dx, dy);
        if (len <= 1e-9)
            return;
        const double c = std::min(cr, cm);
        vx += c * dx / len;                 // conf-weighted unit vectors
        vy += c * dy / len;
        sconf += c;
        ++n;
    };
    addHand(kLeftHandFirstKp);
    addHand(kRightHandFirstKp);
    if (n == 0 || (vx == 0.0 && vy == 0.0))
        return 0.f;
    outDeg = std::atan2(vy, vx) * 57.29577951308232;
    return static_cast<float>(sconf / n);
}

} // namespace pinpoint::analysis
